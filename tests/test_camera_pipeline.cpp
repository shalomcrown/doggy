#include "camera_pipeline.h"
#include "config.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

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
    const CameraQueryResult usb_result = usb_pipeline.query(usb, "10.0.0.4");
    expect(usb_result.result == CameraResult::ok
                    && usb_process_ptr->last_spec.commands.size() == 1,
           "V4L2 camera uses one ffmpeg command");
    expect(has_arg(usb_process_ptr->last_spec, "/dev/video7")
                    && has_arg(usb_process_ptr->last_spec, "libx264")
                    && has_arg(usb_process_ptr->last_spec, "baseline"),
           "V4L2 camera uses configured device and H264 Baseline");
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

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
