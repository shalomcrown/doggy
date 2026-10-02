#include "camera_pipeline.h"
#include "config.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

// ================================================================================

static void expect(bool condition, const char *name) {
    if (condition) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

class FakeCameraProcess final : public CameraProcess {
public:
    int starts = 0;
    int stops = 0;
    bool start_ok = true;
    bool is_running = false;
    CameraProcessSpec last_spec;

    // ================================================================================

    bool start(const CameraProcessSpec &spec) override {
        starts += 1;
        last_spec = spec;
        is_running = start_ok;
        return start_ok;
    }

    // ================================================================================

    bool running() override {
        return is_running;
    }

    // ================================================================================

    void stop() override {
        stops += 1;
        is_running = false;
    }
};

// ================================================================================

class LiveCounter {
public:
    int live = 0;
};

// ================================================================================

class CountingFake final : public CameraProcess {
public:
    explicit CountingFake(LiveCounter *counter) : counter_(counter) {
    }

    // ================================================================================

    bool start(const CameraProcessSpec &) override {
        if (active_ == false) {
            counter_->live += 1;
            active_ = true;
        }
        return true;
    }

    // ================================================================================

    bool running() override {
        return active_;
    }

    // ================================================================================

    void stop() override {
        if (active_) {
            counter_->live -= 1;
            active_ = false;
        }
    }

private:
    LiveCounter *counter_ = nullptr;
    bool active_ = false;
};

// ================================================================================

class FakeCameraDiscovery final : public CameraDiscovery {
public:
    bool has_rpi = false;
    int probes = 0;
    std::optional<std::string> v4l2_device;

    // ================================================================================

    bool rpiCameraAvailable() override {
        probes += 1;
        return has_rpi;
    }

    // ================================================================================

    std::optional<std::string> firstV4l2CaptureDevice() override {
        probes += 1;
        return v4l2_device;
    }

    // ================================================================================

    std::vector<V4l2CaptureDevice> listV4l2CaptureDevices() override {
        probes += 1;
        std::vector<V4l2CaptureDevice> devices;
        for (const std::optional<std::string> &entry : v4l2_devices) {
            if (entry.has_value() == false) {
                continue;
            }

            V4l2CaptureDevice device;
            device.device = *entry;
            device.name = "USB camera";
            device.stable_id = "usb-" + *entry;
            devices.push_back(device);
        }

        if (devices.empty() && v4l2_device.has_value()) {
            V4l2CaptureDevice device;
            device.device = *v4l2_device;
            device.name = "USB camera";
            device.stable_id = "usb-" + *v4l2_device;
            devices.push_back(device);
        }

        return devices;
    }

    std::vector<std::optional<std::string>> v4l2_devices;
};

// ================================================================================

static bool has_arg(const CameraProcessSpec &spec, const std::string &arg) {
    for (const std::vector<std::string> &command : spec.commands) {
        for (const std::string &item : command) {
            if (item == arg) {
                return true;
            }
        }
    }
    return false;
}

// ================================================================================

static bool has_arg_pair(
        const CameraProcessSpec &spec,
        const std::string &first,
        const std::string &second) {
    for (const std::vector<std::string> &command : spec.commands) {
        for (std::size_t i = 1; i < command.size(); ++i) {
            if (command[i - 1] == first && command[i] == second) {
                return true;
            }
        }
    }
    return false;
}

// ================================================================================

int main() {
    auto rpi_process = std::make_unique<FakeCameraProcess>();
    FakeCameraProcess *rpi_process_ptr = rpi_process.get();
    auto rpi_discovery = std::make_unique<FakeCameraDiscovery>();
    rpi_discovery->has_rpi = true;
    FakeCameraDiscovery *rpi_discovery_ptr = rpi_discovery.get();
    CameraPipeline rpi_pipeline(
            std::move(rpi_process), std::move(rpi_discovery));

    const Config defaults = default_config();
    const CameraQueryResult first =
            rpi_pipeline.query(defaults, "rover.local");
    expect(first.result == CameraResult::ok,
           "auto Raspberry Pi camera starts");
    expect(rpi_process_ptr->starts == 1
                    && rpi_process_ptr->last_spec.commands.size() == 2,
           "Raspberry Pi camera uses producer and publisher commands");
    expect(has_arg(rpi_process_ptr->last_spec, "/usr/bin/rpicam-vid")
                    && has_arg(rpi_process_ptr->last_spec, "-c:v")
                    && has_arg(rpi_process_ptr->last_spec, "copy"),
           "Raspberry Pi H264 is copied through ffmpeg");
    expect(has_arg(rpi_process_ptr->last_spec, "libx264") == false
                    && has_arg(rpi_process_ptr->last_spec, "h264_v4l2m2m") == false,
           "Raspberry Pi capture is never re-encoded by ffmpeg");
    expect(has_arg(rpi_process_ptr->last_spec, "-framerate"),
           "Raspberry Pi elementary stream is timestamped at the configured rate");
    expect(has_arg_pair(
                    rpi_process_ptr->last_spec, "--rotation", "180"),
           "Raspberry Pi camera applies default rotation in rpicam-vid");
    expect(has_arg_pair(
                    rpi_process_ptr->last_spec, "--buffer-count", "2")
                    && has_arg_pair(
                            rpi_process_ptr->last_spec, "--denoise", "off"),
           "Raspberry Pi capture minimizes buffering and denoise latency");
    expect(has_arg(rpi_process_ptr->last_spec, "--low-latency"),
           "Raspberry Pi camera enables low-latency encode");
    expect(has_arg(rpi_process_ptr->last_spec, "nobuffer+flush_packets")
                    && has_arg_pair(
                            rpi_process_ptr->last_spec, "-analyzeduration", "0"),
           "Raspberry Pi relay minimizes ffmpeg input buffering");
    expect(has_arg_pair(rpi_process_ptr->last_spec, "-g", "3"),
           "Raspberry Pi camera keeps GOP at or below 250ms");
    expect(first.streams.items_size() == 1
                    && first.streams.items(0).webrtc_url()
                            == "https://rover.local:8889/cam0/whep",
           "camera response contains Host-derived WHEP URL");

    const CameraQueryResult second =
            rpi_pipeline.query(defaults, "rover.local");
    expect(second.result == CameraResult::ok && rpi_process_ptr->starts == 1,
           "second camera query does not duplicate a running feeder");
    expect(rpi_discovery_ptr->probes == 1,
           "camera query does not re-probe while the feeder is running");

    Config changed_rotation = defaults;
    changed_rotation.mutable_cameras()->mutable_items(0)->set_rotation_deg(0);
    const CameraQueryResult reconfigured =
            rpi_pipeline.query(changed_rotation, "rover.local");
    expect(reconfigured.result == CameraResult::ok
                    && rpi_process_ptr->starts == 2,
           "camera query restarts feeder after configuration change");
    expect(has_arg_pair(rpi_process_ptr->last_spec, "--rotation", "0"),
           "restarted Raspberry Pi feeder uses changed rotation");

    rpi_process_ptr->is_running = false;
    const CameraQueryResult restarted =
            rpi_pipeline.query(changed_rotation, "rover.local");
    expect(restarted.result == CameraResult::ok && rpi_process_ptr->starts == 3,
           "camera query restarts an exited feeder");

    auto usb_process = std::make_unique<FakeCameraProcess>();
    FakeCameraProcess *usb_process_ptr = usb_process.get();
    auto usb_discovery = std::make_unique<FakeCameraDiscovery>();
    CameraPipeline usb_pipeline(
            std::move(usb_process), std::move(usb_discovery));
    Config usb = defaults;
    doggy::v1::Camera *camera = usb.mutable_cameras()->mutable_items(0);
    camera->set_source("v4l2");
    camera->set_device("/dev/video7");
    camera->set_fps(30);
    const CameraQueryResult usb_result = usb_pipeline.query(usb, "10.0.0.4");
    expect(usb_result.result == CameraResult::ok
                    && usb_process_ptr->last_spec.commands.size() == 1,
           "V4L2 camera uses one ffmpeg command");
    expect(has_arg(usb_process_ptr->last_spec, "/dev/video7")
                    && has_arg(usb_process_ptr->last_spec, "mjpeg")
                    && has_arg(
                            usb_process_ptr->last_spec,
                            "nobuffer+flush_packets"),
           "V4L2 camera uses low-latency MJPEG capture");
    expect(has_arg_pair(usb_process_ptr->last_spec, "-framerate", "30")
                    && has_arg_pair(usb_process_ptr->last_spec, "-g", "7"),
           "V4L2 camera honors an explicit fps with a short GOP");
    expect(has_arg(usb_process_ptr->last_spec, "libx264")
                    && has_arg(usb_process_ptr->last_spec, "baseline")
                    && has_arg(
                            usb_process_ptr->last_spec,
                            "h264_v4l2m2m") == false,
           "V4L2 camera uses software H.264 baseline encode");
    expect(has_arg(usb_process_ptr->last_spec, "-vf")
                    && has_arg(usb_process_ptr->last_spec, "hflip,vflip"),
           "V4L2 camera applies default 180 degree rotation");

    camera->set_rotation_deg(0);
    const CameraQueryResult usb_unrotated =
            usb_pipeline.query(usb, "10.0.0.4");
    expect(usb_unrotated.result == CameraResult::ok
                    && usb_process_ptr->starts == 2
                    && has_arg(usb_process_ptr->last_spec, "-vf") == false,
           "V4L2 zero rotation restarts without a rotation filter");

    auto missing_process = std::make_unique<FakeCameraProcess>();
    auto missing_discovery = std::make_unique<FakeCameraDiscovery>();
    CameraPipeline missing_pipeline(
            std::move(missing_process), std::move(missing_discovery));
    expect(missing_pipeline.query(defaults, "rover.local").result
                    == CameraResult::not_found,
           "auto camera reports not found when discovery finds no input");

    Config disabled = defaults;
    disabled.mutable_cameras()->mutable_items(0)->set_enabled(false);
    expect(missing_pipeline.query(disabled, "rover.local").result
                    == CameraResult::not_found,
           "disabled camera reports not found");

    auto failed_process = std::make_unique<FakeCameraProcess>();
    failed_process->start_ok = false;
    auto failed_discovery = std::make_unique<FakeCameraDiscovery>();
    failed_discovery->has_rpi = true;
    CameraPipeline failed_pipeline(
            std::move(failed_process), std::move(failed_discovery));
    expect(failed_pipeline.query(defaults, "rover.local").result
                    == CameraResult::failed,
           "camera launch failure is reported");

    auto stereo_process = std::make_unique<FakeCameraProcess>();
    FakeCameraProcess *stereo_process_ptr = stereo_process.get();
    auto stereo_discovery = std::make_unique<FakeCameraDiscovery>();
    stereo_discovery->has_rpi = true;
    stereo_discovery->v4l2_devices = {std::optional<std::string>{"/dev/video2"},
            std::optional<std::string>{"/dev/video4"}};
    CameraPipeline stereo_pipeline(
            std::move(stereo_process), std::move(stereo_discovery));

    Config stereo_config = defaults;
    for (doggy::v1::Camera &camera : *stereo_config.mutable_cameras()->mutable_items()) {
        if (camera.id() == "usb_left" || camera.id() == "usb_right") {
            camera.set_enabled(true);
        }
    }

    const CameraQueryResult stereo_result =
            stereo_pipeline.query(stereo_config, "rover.local");
    expect(stereo_result.result == CameraResult::ok
                    && stereo_result.streams.items_size() == 3,
           "enabled primary and stereo cameras return three streams");
    expect(stereo_process_ptr->starts >= 3,
           "stereo query starts feeders for each enabled camera");
    expect(stereo_result.persist_stereo.has_value(),
           "first stereo query persists default bindings in config");

    StereoBindings bindings = *stereo_result.persist_stereo;
    stereo_bindings_swap(&bindings);
    stereo_bindings_apply(&stereo_config, bindings);
    const CameraQueryResult swapped_query =
            stereo_pipeline.query(stereo_config, "rover.local");
    expect(swapped_query.result == CameraResult::ok,
           "stereo query works after bindings are swapped in config");

    LiveCounter independent_counter;
    auto independent_discovery = std::make_unique<FakeCameraDiscovery>();
    independent_discovery->has_rpi = true;
    independent_discovery->v4l2_devices = {
            std::optional<std::string>{"/dev/video0"},
            std::optional<std::string>{"/dev/video2"}};
    CameraPipeline independent_pipeline(
            [&independent_counter] {
                return std::make_unique<CountingFake>(&independent_counter);
            },
            std::move(independent_discovery));
    Config independent_config = default_config();
    for (doggy::v1::Camera &camera :
            *independent_config.mutable_cameras()->mutable_items()) {
        if (camera.id() == "usb_left" || camera.id() == "usb_right") {
            camera.set_enabled(true);
        }
    }
    const CameraQueryResult independent_first =
            independent_pipeline.query(independent_config, "rover.local");
    expect(independent_first.result == CameraResult::ok
                    && independent_counter.live == 3,
           "each enabled camera uses an independent feeder process");
    const CameraQueryResult independent_second =
            independent_pipeline.query(independent_config, "rover.local");
    expect(independent_second.streams.items(0).ready()
                    && independent_second.streams.items(1).ready()
                    && independent_second.streams.items(2).ready(),
           "all streams stay ready on a healthy multi-camera query");
    expect(independent_counter.live == 3,
           "healthy multi-camera query keeps all feeders running");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
