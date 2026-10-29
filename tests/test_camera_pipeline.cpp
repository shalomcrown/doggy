#include "camera_pipeline.h"
#include "config.h"
#include "doggy_log.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>

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
    int fail_remaining = 0;
    bool is_running = false;
    CameraProcessSpec last_spec;

    // ================================================================================

    bool start(const CameraProcessSpec &spec) override {
        starts += 1;
        last_spec = spec;
        if (fail_remaining > 0) {
            fail_remaining -= 1;
            is_running = false;
            return false;
        }
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
            if (device.device == stereo_device) {
                device.mjpeg_sizes = stereo_sizes;
            }
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
    std::string stereo_device;
    std::vector<V4l2FrameSize> stereo_sizes;
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

static std::vector<V4l2FrameSize> sample_stereo_sizes() {
    return {
        {1280, 720},
        {640, 480},
        {640, 360},
        {1280, 712},
        {640, 472},
        {640, 352},
        {2560, 720},
        {1280, 480},
        {1280, 360},
    };
}

// ================================================================================

static std::string read_log_file(const fs::path &directory) {
    std::ifstream in(doggy_log_path(directory.string()));
    return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
}

// ================================================================================

int main() {
    const fs::path log_dir = fs::temp_directory_path()
            / ("doggy-camera-log-" + std::to_string(getpid()));
    fs::remove_all(log_dir);
    DoggyLogOptions log_options;
    log_options.directory = log_dir.string();
    log_options.roll_on_start = false;
    expect(init_doggy_log(log_options), "camera test log opens");

    const std::optional<V4l2FrameSize> stereo_mode =
            select_stereo_mjpeg_size(sample_stereo_sizes());
    expect(stereo_mode.has_value()
                    && stereo_mode->width == 1280
                    && stereo_mode->height == 480,
           "side-by-side MJPEG list prefers 1280x480");
    std::vector<V4l2FrameSize> without_vga = sample_stereo_sizes();
    without_vga.erase(
            std::remove_if(without_vga.begin(), without_vga.end(),
                    [](const V4l2FrameSize &size) {
                        return size.width == 640 && size.height == 480;
                    }),
            without_vga.end());
    const std::optional<V4l2FrameSize> wide_mode =
            select_stereo_mjpeg_size(without_vga);
    expect(wide_mode.has_value()
                    && wide_mode->width == 2560
                    && wide_mode->height == 720,
           "stereo mode falls through to 2560x720 when 640x480 is absent");
    expect(select_stereo_mjpeg_size({{1280, 712}, {640, 472}}).has_value() == false,
           "short single-eye sizes are not a stereo mode");
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
    expect(first.streams.items(0).name() == "Pi camera"
                    && rpi_discovery_ptr->probes == 2,
           "Pi camera is selected after USB discovery finds no stereo module");

    const CameraQueryResult second =
            rpi_pipeline.query(defaults, "rover.local");
    expect(second.result == CameraResult::ok && rpi_process_ptr->starts == 1,
           "second camera query does not duplicate a running feeder");
    expect(rpi_discovery_ptr->probes == 2,
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
    usb_discovery->v4l2_devices = {std::optional<std::string>{"/dev/video7"}};
    CameraPipeline usb_pipeline(
            std::move(usb_process), std::move(usb_discovery));
    Config usb = defaults;
    doggy::v1::Camera *camera = usb.mutable_cameras()->mutable_items(0);
    camera->set_width(640);
    camera->set_height(360);
    camera->set_fps(30);
    const CameraQueryResult usb_result = usb_pipeline.query(usb, "10.0.0.4");
    expect(usb_result.result == CameraResult::ok
                    && usb_result.streams.items_size() == 1
                    && usb_result.streams.items(0).name() == "USB camera"
                    && usb_process_ptr->last_spec.commands.size() == 1,
           "one plain USB camera is the live feed when stereo and Pi are absent");
    expect(has_arg(usb_process_ptr->last_spec, "/dev/video7")
                    && has_arg(usb_process_ptr->last_spec, "mjpeg")
                    && has_arg(usb_process_ptr->last_spec, "640x360")
                    && has_arg(
                            usb_process_ptr->last_spec,
                            "nobuffer+flush_packets"),
           "V4L2 camera uses low-latency MJPEG capture at the smaller configured size");
    expect(has_arg_pair(usb_process_ptr->last_spec, "-framerate", "15")
                    && has_arg_pair(usb_process_ptr->last_spec, "-g", "3"),
           "plain USB camera publishes at 15 fps with a short GOP");
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
    stereo_discovery->stereo_device = "/dev/video4";
    stereo_discovery->stereo_sizes = sample_stereo_sizes();
    CameraPipeline stereo_pipeline(
            std::move(stereo_process), std::move(stereo_discovery));

    Config stereo_config = defaults;
    for (doggy::v1::Camera &camera : *stereo_config.mutable_cameras()->mutable_items()) {
        if (camera.id() == "usb_left" || camera.id() == "usb_right") {
            camera.set_enabled(true);
        }
        if (camera.id() == "cam0") {
            camera.set_rotation_deg(0);
            camera.set_swap_halves(false);
        }
    }

    const CameraQueryResult stereo_result =
            stereo_pipeline.query(stereo_config, "rover.local");
    expect(stereo_result.result == CameraResult::ok
                    && stereo_result.streams.items_size() == 1
                    && stereo_result.streams.items(0).id() == "cam0"
                    && stereo_result.streams.items(0).name() == "Stereo camera",
           "a stereo module wins over the Pi camera and a second USB camera");
    const std::optional<LiveCameraChoice> live = stereo_pipeline.liveCamera();
    expect(live.has_value()
                    && live->kind == LiveCameraKind::stereo
                    && live->stable_id == "usb-/dev/video4"
                    && live->width == 1280
                    && live->height == 480,
           "the live camera records the stereo module");
    expect(stereo_process_ptr->starts == 1
                    && has_arg(stereo_process_ptr->last_spec, "/dev/video4")
                    && has_arg(stereo_process_ptr->last_spec, "1280x480")
                    && has_arg_pair(stereo_process_ptr->last_spec, "-framerate", "15")
                    && has_arg(stereo_process_ptr->last_spec, "/usr/bin/rpicam-vid") == false
                    && has_arg(stereo_process_ptr->last_spec, "-vf") == false,
           "stereo MJPEG is published once, unrotated, at 1280x480");
    const std::string stereo_log = read_log_file(log_dir);
    expect(stereo_log.find("camera probe raspberry pi yes usb 2") != std::string::npos
                    && stereo_log.find(
                            "camera found /dev/video4 name USB camera id usb-/dev/video4 stereo yes")
                            != std::string::npos
                    && stereo_log.find("selected 1280x480") != std::string::npos
                    && stereo_log.find(
                            "camera found /dev/video2 name USB camera id usb-/dev/video2 stereo no")
                            != std::string::npos
                    && stereo_log.find("camera live stereo /dev/video4 1280x480 15 fps stereo yes")
                            != std::string::npos,
           "camera selection is written to the log");
    stereo_config.mutable_cameras()->mutable_items(0)->set_rotation_deg(180);
    stereo_config.mutable_cameras()->mutable_items(0)->set_swap_halves(true);
    const CameraQueryResult swapped_halves =
            stereo_pipeline.query(stereo_config, "rover.local");
    expect(swapped_halves.result == CameraResult::ok
                    && stereo_process_ptr->starts == 2
                    && has_arg(stereo_process_ptr->last_spec, "-filter_complex")
                    && has_arg(stereo_process_ptr->last_spec, "hflip,vflip") == false,
           "stereo swap and invert restart the feeder");
    bool swap_graph = false;
    for (const std::vector<std::string> &command : stereo_process_ptr->last_spec.commands) {
        for (const std::string &arg : command) {
            if (arg.find("hstack,hflip,vflip") != std::string::npos
                    && arg.find("crop=iw/2:ih:iw/2:0") != std::string::npos) {
                swap_graph = true;
            }
        }
    }
    expect(swap_graph, "stereo swap exchanges halves and then inverts");

    auto fallback_process = std::make_unique<FakeCameraProcess>();
    fallback_process->fail_remaining = 1;
    FakeCameraProcess *fallback_process_ptr = fallback_process.get();
    auto fallback_discovery = std::make_unique<FakeCameraDiscovery>();
    fallback_discovery->has_rpi = true;
    fallback_discovery->v4l2_devices = {std::optional<std::string>{"/dev/video4"}};
    fallback_discovery->stereo_device = "/dev/video4";
    fallback_discovery->stereo_sizes = sample_stereo_sizes();
    CameraPipeline fallback_pipeline(
            std::move(fallback_process), std::move(fallback_discovery));
    const CameraQueryResult fallback_result =
            fallback_pipeline.query(defaults, "rover.local");
    expect(fallback_result.result == CameraResult::ok
                    && fallback_result.streams.items(0).name() == "Pi camera"
                    && fallback_process_ptr->starts == 2
                    && has_arg(fallback_process_ptr->last_spec, "/usr/bin/rpicam-vid"),
           "a failed stereo feeder falls back to the Pi camera");
    const std::string fallback_log = read_log_file(log_dir);
    expect(fallback_log.find(
                    "camera start failed stereo /dev/video4 1280x480 15 fps stereo yes")
                    != std::string::npos
                    && fallback_log.find("camera live rpi - 1280x720 15 fps stereo no")
                            != std::string::npos,
           "a failed stereo start and the Pi fallback are logged");

    auto pair_process = std::make_unique<FakeCameraProcess>();
    FakeCameraProcess *pair_process_ptr = pair_process.get();
    auto pair_discovery = std::make_unique<FakeCameraDiscovery>();
    pair_discovery->v4l2_devices = {std::optional<std::string>{"/dev/video2"},
            std::optional<std::string>{"/dev/video0"}};
    CameraPipeline pair_pipeline(
            std::move(pair_process), std::move(pair_discovery));
    Config pair_config = defaults;
    pair_config.mutable_cameras()->mutable_items(0)->set_width(1920);
    pair_config.mutable_cameras()->mutable_items(0)->set_height(1080);
    const CameraQueryResult pair_result =
            pair_pipeline.query(pair_config, "rover.local");
    expect(pair_result.result == CameraResult::ok
                    && pair_result.streams.items_size() == 1
                    && pair_process_ptr->starts == 1
                    && has_arg(pair_process_ptr->last_spec, "/dev/video0")
                    && has_arg(pair_process_ptr->last_spec, "1280x720")
                    && has_arg(pair_process_ptr->last_spec, "/dev/video2") == false,
           "two plain USB cameras publish only the lowest stable id at 1280x720");

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
                    && independent_first.streams.items_size() == 1
                    && independent_counter.live == 1,
           "enabled extra camera entries still start one feeder");
    const CameraQueryResult independent_second =
            independent_pipeline.query(independent_config, "rover.local");
    expect(independent_second.streams.items_size() == 1
                    && independent_second.streams.items(0).ready(),
           "the single live stream stays ready on a healthy query");
    expect(independent_counter.live == 1,
           "a healthy query keeps the one feeder running");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
