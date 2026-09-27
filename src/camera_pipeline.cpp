#include "camera_pipeline.h"

#include "camera_ids.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <linux/videodev2.h>
#include <memory>
#include <unordered_map>
#include <poll.h>
#include <cstring>
#include <string>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;

static constexpr const char *kFfmpegPath = "/usr/bin/ffmpeg";
static constexpr const char *kRpiCameraPath = "/usr/bin/rpicam-vid";
static constexpr const char *kRtspBase = "rtsp://127.0.0.1:8554/";
static constexpr int kWebRtcPort = 8889;
static constexpr int kCameraProbeTimeoutMs = 2000;
static constexpr int kChildStopTimeoutMs = 2500;
static constexpr int kRpiBringUpDelayUs = 800000;
static constexpr int kUsbBringUpDelayUs = 400000;

// Target ~250 ms max IDR spacing at the configured frame rate.
static constexpr int kLowLatencyGopDivisor = 4;

// ================================================================================

static int low_latency_gop_frames(int fps) {
    if (fps <= 0) {
        return 1;
    }

    const int quarter_second = (fps + kLowLatencyGopDivisor - 1) / kLowLatencyGopDivisor;
    return std::max(1, quarter_second);
}

// USB MJPEG is encoded in software only. h264_v4l2m2m (MMAL) can kernel-oops when
// multiple ffmpeg encoders run alongside the Pi camera on bcm2835 (see dmesg).

// ================================================================================

static void append_ffmpeg_low_latency_input(std::vector<std::string> *command) {
    command->push_back("-fflags");
    command->push_back("nobuffer+flush_packets");
    command->push_back("-flags");
    command->push_back("low_delay");
    command->push_back("-probesize");
    command->push_back("32");
    command->push_back("-analyzeduration");
    command->push_back("0");
}

// ================================================================================

static void append_ffmpeg_low_latency_output(std::vector<std::string> *command) {
    command->push_back("-max_delay");
    command->push_back("0");
    command->push_back("-muxdelay");
    command->push_back("0");
    command->push_back("-muxpreload");
    command->push_back("0");
    command->push_back("-flush_packets");
    command->push_back("1");
}

// ================================================================================

static std::vector<char *> argv_for(std::vector<std::string> &command) {
    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (std::string &item : command) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    return argv;
}

// ================================================================================

static void close_inherited_descriptors() {
#if defined(__linux__) && defined(SYS_close_range)
    if (syscall(SYS_close_range, STDERR_FILENO + 1, ~0U, 0) == 0) {
        return;
    }
#endif
    const long limit = sysconf(_SC_OPEN_MAX);
    const int last = limit > 0 && limit < 4096 ? static_cast<int>(limit) : 4096;
    for (int fd = STDERR_FILENO + 1; fd < last; ++fd) {
        close(fd);
    }
}

// ================================================================================

class PosixCameraProcess final : public CameraProcess {
public:
    ~PosixCameraProcess() override {
        stop();
    }

    // ================================================================================

    bool start(const CameraProcessSpec &spec) override {
        stop();
        if (spec.commands.empty() || spec.commands.size() > 2) {
            return false;
        }
        for (const std::vector<std::string> &command : spec.commands) {
            if (command.empty() || access(command.front().c_str(), X_OK) != 0) {
                return false;
            }
        }

        int pipe_fds[2] = {-1, -1};
        if (spec.commands.size() == 2 && pipe(pipe_fds) != 0) {
            return false;
        }

        std::vector<std::vector<std::string>> commands = spec.commands;
        std::vector<std::vector<char *>> argvs;
        argvs.reserve(commands.size());
        for (std::vector<std::string> &command : commands) {
            argvs.push_back(argv_for(command));
        }

        for (std::size_t i = 0; i < commands.size(); ++i) {
            const pid_t pid = fork();
            if (pid == 0) {
                const pid_t group = process_group_ > 0 ? process_group_ : 0;
                setpgid(0, group);
                if (commands.size() == 2) {
                    if (i == 0) {
                        dup2(pipe_fds[1], STDOUT_FILENO);
                    } else {
                        dup2(pipe_fds[0], STDIN_FILENO);
                    }
                }
                close_inherited_descriptors();
                execv(argvs[i][0], argvs[i].data());
                _exit(127);
            }
            if (pid < 0) {
                if (pipe_fds[0] >= 0) {
                    close(pipe_fds[0]);
                    close(pipe_fds[1]);
                }
                stop();
                return false;
            }
            if (process_group_ <= 0) {
                process_group_ = pid;
            }
            setpgid(pid, process_group_);
            children_.push_back(pid);
        }

        if (pipe_fds[0] >= 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
        }
        return true;
    }

    // ================================================================================

    bool running() override {
        if (children_.empty()) {
            return false;
        }
        for (pid_t pid : children_) {
            int status = 0;
            const pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) {
                stop();
                return false;
            }
        }
        return true;
    }

    // ================================================================================

    void stop() override {
        const std::vector<pid_t> targets = children_;
        if (process_group_ > 0) {
            kill(-process_group_, SIGTERM);
        }
        for (pid_t pid : targets) {
            kill(pid, SIGTERM);
        }

        const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kChildStopTimeoutMs);
        for (pid_t pid : targets) {
            while (true) {
                int status = 0;
                const pid_t done = waitpid(pid, &status, WNOHANG);
                if (done == pid || (done < 0 && errno == ECHILD)) {
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, WNOHANG);
                    break;
                }
                usleep(20000);
            }
        }

        children_.clear();
        process_group_ = -1;
    }

private:
    pid_t process_group_ = -1;
    std::vector<pid_t> children_;
};

// ================================================================================

static std::optional<V4l2CaptureDevice> probe_usb_capture_node(const fs::path &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return std::nullopt;
    }
    v4l2_capability capability{};
    if (ioctl(fd, VIDIOC_QUERYCAP, &capability) != 0) {
        close(fd);
        return std::nullopt;
    }
    close(fd);

    const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
            ? capability.device_caps
            : capability.capabilities;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0
            && (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
        return std::nullopt;
    }

    const std::string bus_info(
            reinterpret_cast<const char *>(capability.bus_info),
            strnlen(reinterpret_cast<const char *>(capability.bus_info),
                    sizeof(capability.bus_info)));
    if (bus_info.rfind("usb-", 0) != 0) {
        return std::nullopt;
    }

    V4l2CaptureDevice item;
    item.device = path.string();
    item.name = std::string(
            reinterpret_cast<const char *>(capability.card),
            strnlen(reinterpret_cast<const char *>(capability.card),
                    sizeof(capability.card)));
    item.stable_id = bus_info;
    return item;
}

// ================================================================================

class SystemCameraDiscovery final : public CameraDiscovery {
public:
    bool rpiCameraAvailable() override {
        if (access(kRpiCameraPath, X_OK) != 0) {
            return false;
        }
        int output[2] = {-1, -1};
        if (pipe(output) != 0) {
            return false;
        }
        char *const probe_argv[] = {
            const_cast<char *>(kRpiCameraPath),
            const_cast<char *>("--list-cameras"),
            nullptr
        };
        const pid_t pid = fork();
        if (pid == 0) {
            dup2(output[1], STDOUT_FILENO);
            dup2(output[1], STDERR_FILENO);
            close_inherited_descriptors();
            execv(probe_argv[0], probe_argv);
            _exit(127);
        }
        close(output[1]);
        if (pid < 0) {
            close(output[0]);
            return false;
        }

        std::string text;
        const auto deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(kCameraProbeTimeoutMs);
        bool timed_out = false;
        while (true) {
            const auto remaining = std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                timed_out = true;
                break;
            }
            pollfd waiting{output[0], POLLIN, 0};
            const int ready = poll(&waiting, 1, static_cast<int>(remaining.count()));
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                timed_out = true;
                break;
            }
            if (ready == 0) {
                timed_out = true;
                break;
            }
            char buffer[512];
            const ssize_t count = read(output[0], buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            text.append(buffer, static_cast<std::size_t>(count));
        }
        close(output[0]);
        if (timed_out) {
            kill(pid, SIGKILL);
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (timed_out) {
            return false;
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0
                && (text.find("0 :") != std::string::npos
                    || text.find("0:") != std::string::npos);
    }

    // ================================================================================

    std::vector<V4l2CaptureDevice> listV4l2CaptureDevices() override {
        std::vector<fs::path> candidates;
        std::error_code error;
        for (const fs::directory_entry &entry :
                fs::directory_iterator("/dev", error)) {
            if (entry.path().filename().string().rfind("video", 0) == 0) {
                candidates.push_back(entry.path());
            }
        }
        std::sort(candidates.begin(), candidates.end());

        std::unordered_map<std::string, V4l2CaptureDevice> by_stable;
        for (const fs::path &candidate : candidates) {
            const std::optional<V4l2CaptureDevice> probed = probe_usb_capture_node(candidate);
            if (probed.has_value() == false) {
                continue;
            }

            const V4l2CaptureDevice item = *probed;
            const auto existing = by_stable.find(item.stable_id);
            if (existing == by_stable.end()) {
                by_stable.emplace(item.stable_id, item);
                continue;
            }

            if (existing->second.device > item.device) {
                existing->second = item;
            }
        }

        std::vector<V4l2CaptureDevice> devices;
        devices.reserve(by_stable.size());
        for (auto &entry : by_stable) {
            devices.push_back(std::move(entry.second));
        }
        std::sort(devices.begin(), devices.end(), [](const V4l2CaptureDevice &a,
                       const V4l2CaptureDevice &b) {
            return a.device < b.device;
        });
        return devices;
    }

    // ================================================================================

    std::optional<std::string> firstV4l2CaptureDevice() override {
        const std::vector<V4l2CaptureDevice> devices = listV4l2CaptureDevices();
        if (devices.empty()) {
            return std::nullopt;
        }

        return devices.front().device;
    }
};

// ================================================================================

static CameraProcessSpec rpi_process_spec(const doggy::v1::Camera &camera) {
    CameraProcessSpec spec;
    spec.commands.push_back({
            kRpiCameraPath,
            "--nopreview",
            "--timeout", "0",
            "--width", std::to_string(camera.width()),
            "--height", std::to_string(camera.height()),
            "--framerate", std::to_string(camera.fps()),
            "--rotation", std::to_string(camera.rotation_deg()),
            "--codec", "h264",
            "--profile", "baseline",
            "--inline",
            "--flush",
            "--low-latency",
            "-g", std::to_string(low_latency_gop_frames(camera.fps())),
            "--output", "-"});

    std::vector<std::string> relay = {
            kFfmpegPath,
            "-nostdin",
            "-hide_banner",
            "-loglevel", "warning",
            "-f", "h264",
            "-framerate", std::to_string(camera.fps()),
            "-i", "pipe:0",
            "-an",
            "-c:v", "copy",
    };
    append_ffmpeg_low_latency_output(&relay);
    relay.push_back("-f");
    relay.push_back("rtsp");
    relay.push_back("-rtsp_transport");
    relay.push_back("tcp");
    relay.push_back(std::string(kRtspBase) + camera.id());
    spec.commands.push_back(std::move(relay));
    return spec;
}

// ================================================================================

static CameraProcessSpec v4l2_process_spec(
        const doggy::v1::Camera &camera,
        const std::string &device) {
    std::vector<std::string> command = {
        kFfmpegPath,
        "-nostdin",
        "-hide_banner",
        "-loglevel", "warning",
    };
    append_ffmpeg_low_latency_input(&command);
    command.insert(command.end(), {
        "-f", "v4l2",
        "-input_format", "mjpeg",
        "-framerate", std::to_string(camera.fps()),
        "-video_size",
        std::to_string(camera.width()) + "x" + std::to_string(camera.height()),
        "-i", device
    });
    if (camera.rotation_deg() == 180) {
        command.insert(command.end(), {"-vf", "hflip,vflip"});
    }
    const int gop = low_latency_gop_frames(camera.fps());
    const std::string gop_text = std::to_string(gop);
    command.insert(command.end(), {
        "-an",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-pix_fmt", "yuv420p",
        "-g", gop_text,
        "-keyint_min", gop_text,
        "-sc_threshold", "0",
        "-x264-params",
        "nal-hrd=none:force-cfr=1:sync-lookahead=0:rc-lookahead=0:"
        "refs=1:bframes=0",
    });
    append_ffmpeg_low_latency_output(&command);
    command.push_back("-f");
    command.push_back("rtsp");
    command.push_back("-rtsp_transport");
    command.push_back("tcp");
    command.push_back(std::string(kRtspBase) + camera.id());
    CameraProcessSpec spec;
    spec.commands.push_back(std::move(command));
    return spec;
}

// ================================================================================

class DelegatingCameraProcess final : public CameraProcess {
public:
    explicit DelegatingCameraProcess(std::shared_ptr<CameraProcess> inner) :
        inner_(std::move(inner)) {
    }

    // ================================================================================

    bool start(const CameraProcessSpec &spec) override {
        return inner_->start(spec);
    }

    // ================================================================================

    bool running() override {
        return inner_->running();
    }

    // ================================================================================

    void stop() override {
        inner_->stop();
    }

private:
    std::shared_ptr<CameraProcess> inner_;
};

// ================================================================================

CameraPipeline::CameraPipeline() :
    CameraPipeline(
            [] { return std::make_unique<PosixCameraProcess>(); },
            std::make_unique<SystemCameraDiscovery>()) {
}

// ================================================================================

CameraPipeline::CameraPipeline(std::unique_ptr<CameraProcess> process) :
    CameraPipeline(
            std::move(process),
            std::make_unique<SystemCameraDiscovery>()) {
}

// ================================================================================

CameraPipeline::CameraPipeline(
        CameraProcessFactory factory,
        std::unique_ptr<CameraDiscovery> discovery) :
    process_factory_(std::move(factory)),
    discovery_(std::move(discovery)) {
}

// ================================================================================

CameraPipeline::CameraPipeline(
        std::unique_ptr<CameraProcess> process,
        std::unique_ptr<CameraDiscovery> discovery) :
    discovery_(std::move(discovery)) {
    std::shared_ptr<CameraProcess> shared(std::move(process));
    process_factory_ = [shared] {
        return std::make_unique<DelegatingCameraProcess>(shared);
    };
}

// ================================================================================

CameraPipeline::~CameraPipeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &entry : feeders_) {
        if (entry.second.process) {
            entry.second.process->stop();
        }
    }
    feeders_.clear();
}

// ================================================================================

std::unique_ptr<CameraProcess> CameraPipeline::makeProcess() {
    return process_factory_();
}

// ================================================================================

std::vector<V4l2CaptureDevice> CameraPipeline::discoverV4l2Locked() {
    return discovery_->listV4l2CaptureDevices();
}

// ================================================================================

StereoBindings CameraPipeline::prepareBindingsLocked(
        const Config &config,
        const std::vector<V4l2CaptureDevice> &devices,
        bool *changed) {
    StereoBindings bindings = stereo_bindings_from_config(config);
    std::vector<std::string> stable_ids;
    for (const V4l2CaptureDevice &device : devices) {
        stable_ids.push_back(device.stable_id);
    }

    const bool updated = stereo_bindings_ensure_defaults(&bindings, stable_ids);
    if (changed != nullptr) {
        *changed = updated;
    }

    return bindings;
}

// ================================================================================

std::string CameraPipeline::roleForStableIdLocked(
        const std::string &stable_id,
        const StereoBindings &bindings) const {
    if (stable_id == bindings.left_stable_id) {
        return kCameraUsbPairLeftId;
    }
    if (stable_id == bindings.right_stable_id) {
        return kCameraUsbPairRightId;
    }

    return "none";
}

// ================================================================================

std::optional<std::string> CameraPipeline::deviceForStableId(
        const std::string &stable_id,
        const std::vector<V4l2CaptureDevice> &devices) const {
    for (const V4l2CaptureDevice &device : devices) {
        if (device.stable_id == stable_id) {
            return device.device;
        }
    }

    return std::nullopt;
}

// ================================================================================

bool CameraPipeline::resolveSourceForCamera(
        const doggy::v1::Camera &camera,
        const std::vector<V4l2CaptureDevice> &devices,
        const StereoBindings &bindings,
        bool rpi_reserved,
        bool rpi_available,
        std::string &source,
        std::string &device) const {
    source = camera.source();
    device = camera.device();

    if (camera_id_is_usb_pair(camera.id())) {
        const std::string stable = camera_id_is_usb_pair_left(camera.id())
                ? bindings.left_stable_id
                : bindings.right_stable_id;
        if (stable.empty()) {
            return false;
        }

        const std::optional<std::string> resolved = deviceForStableId(stable, devices);
        if (resolved.has_value() == false) {
            return false;
        }

        source = "v4l2";
        device = *resolved;
        return true;
    }

    if (source == "auto") {
        if (rpi_reserved && rpi_available) {
            source = "rpi";
            device.clear();
            return true;
        }

        for (const V4l2CaptureDevice &item : devices) {
            if (item.stable_id == bindings.left_stable_id
                    || item.stable_id == bindings.right_stable_id) {
                continue;
            }

            source = "v4l2";
            device = item.device;
            return true;
        }

        return false;
    }

    if (source == "rpi") {
        return rpi_reserved && rpi_available;
    }

    if (source == "v4l2") {
        if (device.empty()) {
            const std::optional<std::string> found =
                    discovery_->firstV4l2CaptureDevice();
            if (found.has_value() == false) {
                return false;
            }

            device = *found;
        }

        return true;
    }

    return false;
}

// ================================================================================

void CameraPipeline::stopFeeder(const std::string &id) {
    auto found = feeders_.find(id);
    if (found == feeders_.end()) {
        return;
    }

    if (found->second.process) {
        found->second.process->stop();
    }

    feeders_.erase(found);
}

// ================================================================================

void CameraPipeline::stopStereoFeedersLocked() {
    stopFeeder(kCameraUsbPairLeftId);
    stopFeeder(kCameraUsbPairRightId);
    stopFeeder(kCameraLegacyPairLeftId);
    stopFeeder(kCameraLegacyPairRightId);
}

// ================================================================================

void CameraPipeline::invalidateStereoFeeders() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopStereoFeedersLocked();
}

// ================================================================================

static int camera_feeder_start_priority(const std::string &id) {
    if (id == kCameraPrimaryId) {
        return 0;
    }
    if (camera_id_is_usb_pair_left(id)) {
        return 1;
    }
    if (camera_id_is_usb_pair_right(id)) {
        return 2;
    }

    return 3;
}

// ================================================================================

void CameraPipeline::stopAllFeeders() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(feeders_.size());
    for (const auto &entry : feeders_) {
        ids.push_back(entry.first);
    }
    std::sort(ids.begin(), ids.end(), [](const std::string &a, const std::string &b) {
        return camera_feeder_start_priority(a) > camera_feeder_start_priority(b);
    });
    for (const std::string &id : ids) {
        stopFeeder(id);
    }
}

// ================================================================================

doggy::v1::CameraDeviceList CameraPipeline::listDevices(const Config &config) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<V4l2CaptureDevice> devices = discoverV4l2Locked();
    StereoBindings bindings = stereo_bindings_from_config(config);
    std::vector<std::string> stable_ids;
    for (const V4l2CaptureDevice &device : devices) {
        stable_ids.push_back(device.stable_id);
    }
    stereo_bindings_ensure_defaults(&bindings, stable_ids);

    doggy::v1::CameraDeviceList list;
    for (const V4l2CaptureDevice &device : devices) {
        doggy::v1::CameraDevice *item = list.add_items();
        item->set_stable_id(device.stable_id);
        item->set_device(device.device);
        item->set_name(device.name);
        item->set_role(roleForStableIdLocked(device.stable_id, bindings));
    }

    return list;
}

CameraQueryResult CameraPipeline::streamsFor(
        const std::vector<const doggy::v1::Camera *> &cameras,
        const std::string &host,
        const std::vector<V4l2CaptureDevice> &devices,
        const StereoBindings &bindings,
        bool rpi_available) {
    CameraQueryResult result;
    result.result = CameraResult::ok;

    for (const doggy::v1::Camera *camera : cameras) {
        doggy::v1::CameraStream *stream = result.streams.add_items();
        stream->set_id(camera->id());
        stream->set_name(camera->name());

        std::string source;
        std::string device;
        const bool rpi_reserved = camera->id() == kCameraPrimaryId
                || camera->source() == "rpi"
                || camera->source() == "auto";
        const bool resolved = resolveSourceForCamera(
                *camera,
                devices,
                bindings,
                rpi_reserved,
                rpi_available,
                source,
                device);
        const auto feeder = feeders_.find(camera->id());
        const bool running = feeder != feeders_.end()
                && feeder->second.process
                && feeder->second.process->running();
        stream->set_ready(resolved && running);
        if (resolved == false || running == false) {
            continue;
        }

        stream->set_webrtc_url(
                "https://" + host + ":" + std::to_string(kWebRtcPort) + "/"
                + camera->id() + "/whep");
    }

    return result;
}

// ================================================================================

CameraQueryResult CameraPipeline::query(
        const Config &config,
        const std::string &host) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<const doggy::v1::Camera *> enabled;
    for (const doggy::v1::Camera &camera : config.cameras().items()) {
        if (camera.enabled()) {
            enabled.push_back(&camera);
        }
    }

    if (enabled.empty()) {
        for (auto &entry : feeders_) {
            if (entry.second.process) {
                entry.second.process->stop();
            }
        }
        feeders_.clear();
        return {};
    }

    bool feeders_healthy = true;
    for (const doggy::v1::Camera *camera : enabled) {
        const std::string config_key = camera->SerializeAsString();
        const auto found = feeders_.find(camera->id());
        if (found == feeders_.end() || found->second.process == nullptr
                || found->second.process->running() == false) {
            feeders_healthy = false;
            break;
        }

        if (found->second.config_key.size() < config_key.size()
                || found->second.config_key.compare(0, config_key.size(), config_key) != 0) {
            feeders_healthy = false;
            break;
        }
    }

    if (feeders_healthy) {
        bool usb_pair_enabled = false;
        for (const doggy::v1::Camera *camera : enabled) {
            if (camera_id_is_usb_pair(camera->id())) {
                usb_pair_enabled = true;
                break;
            }
        }

        std::vector<V4l2CaptureDevice> devices;
        StereoBindings bindings = stereo_bindings_from_config(config);
        if (usb_pair_enabled) {
            devices = discoverV4l2Locked();
            std::vector<std::string> stable_ids;
            for (const V4l2CaptureDevice &device : devices) {
                stable_ids.push_back(device.stable_id);
            }
            stereo_bindings_ensure_defaults(&bindings, stable_ids);
        }

        return streamsFor(enabled, host, devices, bindings, true);
    }

    bool usb_pair_enabled = false;
    for (const doggy::v1::Camera *camera : enabled) {
        if (camera_id_is_usb_pair(camera->id())) {
            usb_pair_enabled = true;
            break;
        }
    }

    std::vector<V4l2CaptureDevice> devices;
    if (usb_pair_enabled) {
        devices = discoverV4l2Locked();
    }

    bool bindings_changed = false;
    StereoBindings bindings = usb_pair_enabled
            ? prepareBindingsLocked(config, devices, &bindings_changed)
            : stereo_bindings_from_config(config);

    const bool rpi_available = discovery_->rpiCameraAvailable();

    std::unordered_map<std::string, bool> want;
    for (const doggy::v1::Camera *camera : enabled) {
        want[camera->id()] = true;
    }

    for (auto it = feeders_.begin(); it != feeders_.end();) {
        if (want.find(it->first) == want.end()) {
            if (it->second.process) {
                it->second.process->stop();
            }
            it = feeders_.erase(it);
            continue;
        }

        ++it;
    }

    bool any_failed = false;
    bool any_ready = false;

    std::vector<const doggy::v1::Camera *> startup_order = enabled;
    std::sort(startup_order.begin(), startup_order.end(),
            [](const doggy::v1::Camera *a, const doggy::v1::Camera *b) {
                return camera_feeder_start_priority(a->id())
                        < camera_feeder_start_priority(b->id());
            });

    for (const doggy::v1::Camera *camera : startup_order) {
        std::string source;
        std::string device;
        const bool rpi_reserved = camera->id() == kCameraPrimaryId
                || camera->source() == "rpi"
                || camera->source() == "auto";
        if (resolveSourceForCamera(
                    *camera,
                    devices,
                    bindings,
                    rpi_reserved,
                    rpi_available,
                    source,
                    device)
                == false) {
            stopFeeder(camera->id());
            continue;
        }

        const std::string config_key = camera->SerializeAsString();
        const std::string fingerprint = config_key + "|" + source + "|" + device;
        ActiveFeeder *feeder = nullptr;
        const auto found = feeders_.find(camera->id());
        if (found != feeders_.end()) {
            feeder = &found->second;
        }

        if (feeder != nullptr
                && feeder->config_key == fingerprint
                && feeder->process
                && feeder->process->running()) {
            any_ready = true;
            continue;
        }

        stopFeeder(camera->id());
        ActiveFeeder next;
        next.config_key = fingerprint;
        next.device = device;
        next.process = makeProcess();
        const CameraProcessSpec spec = source == "rpi"
                ? rpi_process_spec(*camera)
                : v4l2_process_spec(*camera, device);
        if (next.process->start(spec) == false) {
            any_failed = true;
            continue;
        }

        feeders_.emplace(camera->id(), std::move(next));
        any_ready = true;
        if (source == "rpi") {
            usleep(kRpiBringUpDelayUs);
        } else if (source == "v4l2") {
            usleep(kUsbBringUpDelayUs);
        }
    }

    CameraQueryResult result = streamsFor(enabled, host, devices, bindings, rpi_available);
    if (any_ready == false && any_failed) {
        result.result = CameraResult::failed;
    } else if (any_ready == false && enabled.empty() == false) {
        result.result = CameraResult::not_found;
    } else {
        result.result = CameraResult::ok;
    }

    if (bindings_changed) {
        result.persist_stereo = bindings;
    }

    return result;
}
