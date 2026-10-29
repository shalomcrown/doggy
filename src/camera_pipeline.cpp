#include "camera_pipeline.h"

#include "camera_ids.h"
#include "utils.h"

#include <plog/Log.h>

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
#include <sstream>
#include <string>
#include <sys/ioctl.h>
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
static constexpr int kLivePublishFps = 15;
static constexpr int kUsbFallbackWidth = 1280;
static constexpr int kUsbFallbackHeight = 720;

// ================================================================================

static int low_latency_gop_frames(int fps) {
    if (fps <= 0) {
        return 1;
    }

    const int quarter_second = fps / kLowLatencyGopDivisor;
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

static void append_mjpeg_discrete_sizes(
        int fd,
        uint32_t buffer_type,
        std::vector<V4l2FrameSize> *sizes) {
    v4l2_fmtdesc format{};
    format.type = buffer_type;
    for (format.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &format) == 0; format.index += 1) {
        if (format.pixelformat != V4L2_PIX_FMT_MJPEG) {
            continue;
        }

        v4l2_frmsizeenum frame{};
        frame.pixel_format = V4L2_PIX_FMT_MJPEG;
        for (frame.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame) == 0;
                frame.index += 1) {
            if (frame.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                continue;
            }

            V4l2FrameSize size;
            size.width = static_cast<int>(frame.discrete.width);
            size.height = static_cast<int>(frame.discrete.height);
            sizes->push_back(size);
        }
    }
}

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

    const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
            ? capability.device_caps
            : capability.capabilities;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0
            && (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
        close(fd);
        return std::nullopt;
    }

    const std::string bus_info(
            reinterpret_cast<const char *>(capability.bus_info),
            strnlen(reinterpret_cast<const char *>(capability.bus_info),
                    sizeof(capability.bus_info)));
    if (bus_info.rfind("usb-", 0) != 0) {
        close(fd);
        return std::nullopt;
    }

    V4l2CaptureDevice item;
    item.device = path.string();
    item.name = std::string(
            reinterpret_cast<const char *>(capability.card),
            strnlen(reinterpret_cast<const char *>(capability.card),
                    sizeof(capability.card)));
    item.stable_id = bus_info;
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) != 0) {
        append_mjpeg_discrete_sizes(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, &item.mjpeg_sizes);
    }
    if (item.mjpeg_sizes.empty() && (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0) {
        append_mjpeg_discrete_sizes(
                fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, &item.mjpeg_sizes);
    }
    close(fd);
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

static bool mjpeg_size_is_stereo_pair(
        const V4l2FrameSize &size,
        const std::vector<V4l2FrameSize> &sizes) {
    if (size.width < 2 || (size.width % 2) != 0 || size.height <= 0) {
        return false;
    }

    const int half_width = size.width / 2;
    for (const V4l2FrameSize &other : sizes) {
        if (other.width == half_width && other.height == size.height) {
            return true;
        }
    }

    return false;
}

// ================================================================================

static bool mjpeg_sizes_contain(
        const std::vector<V4l2FrameSize> &sizes,
        int width,
        int height) {
    for (const V4l2FrameSize &size : sizes) {
        if (size.width == width && size.height == height) {
            return true;
        }
    }

    return false;
}

// ================================================================================

std::optional<V4l2FrameSize> select_stereo_mjpeg_size(
        const std::vector<V4l2FrameSize> &sizes) {
    const V4l2FrameSize preferred[] = {
        {1280, 480},
        {2560, 720},
        {1280, 360},
    };
    for (const V4l2FrameSize &want : preferred) {
        if (mjpeg_sizes_contain(sizes, want.width, want.height)
                && mjpeg_size_is_stereo_pair(want, sizes)) {
            return want;
        }
    }

    std::optional<V4l2FrameSize> best;
    for (const V4l2FrameSize &size : sizes) {
        if (mjpeg_size_is_stereo_pair(size, sizes) == false) {
            continue;
        }
        if (best.has_value() == false
                || size.width > best->width
                || (size.width == best->width && size.height > best->height)) {
            best = size;
        }
    }

    return best;
}

// ================================================================================

static void usb_fallback_frame_size(
        int configured_width,
        int configured_height,
        int *width,
        int *height) {
    *width = kUsbFallbackWidth;
    *height = kUsbFallbackHeight;
    if (configured_width > 0 && configured_height > 0
            && configured_width <= kUsbFallbackWidth
            && configured_height <= kUsbFallbackHeight) {
        *width = configured_width;
        *height = configured_height;
    }
}

// ================================================================================

std::vector<LiveCameraChoice> rank_live_cameras(
        const std::vector<V4l2CaptureDevice> &devices,
        bool rpi_available,
        int configured_width,
        int configured_height,
        int configured_fps,
        int configured_rotation,
        bool swap_halves) {
    std::vector<V4l2CaptureDevice> ordered = devices;
    std::sort(ordered.begin(), ordered.end(),
            [](const V4l2CaptureDevice &left, const V4l2CaptureDevice &right) {
                if (left.stable_id != right.stable_id) {
                    return left.stable_id < right.stable_id;
                }
                return left.device < right.device;
            });

    std::vector<LiveCameraChoice> ranked;
    for (const V4l2CaptureDevice &device : ordered) {
        const std::optional<V4l2FrameSize> mode =
                select_stereo_mjpeg_size(device.mjpeg_sizes);
        if (mode.has_value() == false) {
            continue;
        }

        LiveCameraChoice choice;
        choice.kind = LiveCameraKind::stereo;
        choice.device = device.device;
        choice.name = "Stereo camera";
        choice.width = mode->width;
        choice.height = mode->height;
        choice.fps = kLivePublishFps;
        choice.rotation_deg = configured_rotation;
        choice.swap_halves = swap_halves;
        choice.stable_id = device.stable_id;
        ranked.push_back(choice);
    }

    if (rpi_available) {
        LiveCameraChoice choice;
        choice.kind = LiveCameraKind::rpi;
        choice.name = "Pi camera";
        choice.width = configured_width > 0 ? configured_width : kUsbFallbackWidth;
        choice.height = configured_height > 0 ? configured_height : kUsbFallbackHeight;
        choice.fps = configured_fps > 0 ? configured_fps : kLivePublishFps;
        choice.rotation_deg = configured_rotation;
        choice.stable_id = "rpi";
        ranked.push_back(choice);
    }

    for (const V4l2CaptureDevice &device : ordered) {
        if (select_stereo_mjpeg_size(device.mjpeg_sizes).has_value()) {
            continue;
        }

        LiveCameraChoice choice;
        choice.kind = LiveCameraKind::usb;
        choice.device = device.device;
        choice.name = "USB camera";
        usb_fallback_frame_size(
                configured_width, configured_height, &choice.width, &choice.height);
        choice.fps = kLivePublishFps;
        choice.rotation_deg = configured_rotation;
        choice.stable_id = device.stable_id;
        ranked.push_back(choice);
        break;
    }

    return ranked;
}

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
            "--buffer-count", "2",
            "--denoise", "off",
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
    };
    append_ffmpeg_low_latency_input(&relay);
    relay.insert(relay.end(), {
            "-f", "h264",
            "-framerate", std::to_string(camera.fps()),
            "-i", "pipe:0",
            "-an",
            "-c:v", "copy",
    });
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
    if (camera.swap_halves()) {
        std::string graph =
                "[0:v]split[a][b];"
                "[b]crop=iw/2:ih:iw/2:0[right];"
                "[a]crop=iw/2:ih:0:0[left];"
                "[right][left]hstack";
        if (camera.rotation_deg() == 180) {
            graph += ",hflip,vflip";
        }
        graph += "[v]";
        command.insert(command.end(), {"-filter_complex", graph, "-map", "[v]"});
    } else if (camera.rotation_deg() == 180) {
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

void CameraPipeline::stopFeedersLocked() {
    std::vector<std::string> ids;
    ids.reserve(feeders_.size());
    for (const auto &entry : feeders_) {
        ids.push_back(entry.first);
    }
    for (const std::string &id : ids) {
        stopFeeder(id);
    }
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

// ================================================================================

static bool any_camera_enabled(const Config &config) {
    for (const doggy::v1::Camera &camera : config.cameras().items()) {
        if (camera.enabled()) {
            return true;
        }
    }

    return false;
}

// ================================================================================

static const doggy::v1::Camera *primary_camera(const Config &config) {
    for (const doggy::v1::Camera &camera : config.cameras().items()) {
        if (camera.id() == kCameraPrimaryId) {
            return &camera;
        }
    }

    return nullptr;
}

// ================================================================================

static std::string live_selection_key(const Config &config) {
    std::string key = "enabled:";
    for (const doggy::v1::Camera &camera : config.cameras().items()) {
        if (camera.enabled()) {
            key += camera.id();
            key += ",";
        }
    }

    const doggy::v1::Camera *primary = primary_camera(config);
    key += "|w=";
    key += primary == nullptr ? "0" : std::to_string(primary->width());
    key += "|h=";
    key += primary == nullptr ? "0" : std::to_string(primary->height());
    key += "|fps=";
    key += primary == nullptr ? "0" : std::to_string(primary->fps());
    key += "|rot=";
    key += primary == nullptr ? "0" : std::to_string(primary->rotation_deg());
    key += "|swap=";
    key += primary != nullptr && primary->swap_halves() ? "1" : "0";
    return key;
}

// ================================================================================

static void append_live_stream(
        doggy::v1::CameraStreamList *streams,
        const std::string &host,
        const std::string &name,
        bool ready) {
    doggy::v1::CameraStream *stream = streams->add_items();
    stream->set_id(kCameraPrimaryId);
    stream->set_name(name);
    stream->set_ready(ready);
    if (ready) {
        stream->set_webrtc_url(
                "https://" + host + ":" + std::to_string(kWebRtcPort) + "/"
                + std::string(kCameraPrimaryId) + "/whep");
    }
}

// ================================================================================

static const char *live_camera_kind_label(LiveCameraKind kind) {
    if (kind == LiveCameraKind::stereo) {
        return "stereo";
    }
    if (kind == LiveCameraKind::rpi) {
        return "rpi";
    }
    if (kind == LiveCameraKind::usb) {
        return "usb";
    }

    return "none";
}

// ================================================================================

static std::string mjpeg_size_list(const std::vector<V4l2FrameSize> &sizes) {
    std::string text;
    for (const V4l2FrameSize &size : sizes) {
        if (text.empty() == false) {
            text += ",";
        }
        text += std::to_string(size.width);
        text += "x";
        text += std::to_string(size.height);
    }
    if (text.empty()) {
        return "none";
    }

    return text;
}

// ================================================================================

static void log_discovered_cameras(
        const std::vector<V4l2CaptureDevice> &devices,
        bool rpi_available) {
    PLOG_INFO << "camera probe raspberry pi " << (rpi_available ? "yes" : "no")
            << " usb " << devices.size();
    for (const V4l2CaptureDevice &device : devices) {
        const std::optional<V4l2FrameSize> mode =
                select_stereo_mjpeg_size(device.mjpeg_sizes);
        std::string line = "camera found ";
        line += device.device;
        line += " name ";
        line += device.name;
        line += " id ";
        line += device.stable_id;
        line += " stereo ";
        line += mode.has_value() ? "yes" : "no";
        line += " mjpeg ";
        line += mjpeg_size_list(device.mjpeg_sizes);
        if (mode.has_value()) {
            line += " selected ";
            line += std::to_string(mode->width);
            line += "x";
            line += std::to_string(mode->height);
        }
        PLOG_INFO << line;
    }
}

// ================================================================================

static void log_live_camera(
        const char *event,
        const LiveCameraChoice &choice,
        bool warning) {
    const std::string device = choice.device.empty() ? "-" : choice.device;
    std::ostringstream line;
    line << "camera " << event << " " << live_camera_kind_label(choice.kind)
            << " " << device << " " << choice.width << "x" << choice.height
            << " " << choice.fps << " fps stereo "
            << (choice.kind == LiveCameraKind::stereo ? "yes" : "no")
            << " invert " << (choice.rotation_deg == 180 ? "yes" : "no")
            << " swap " << (choice.swap_halves ? "yes" : "no");
    if (warning) {
        PLOG_WARNING << line.str();
        return;
    }

    PLOG_INFO << line.str();
}

// ================================================================================

static doggy::v1::Camera capture_camera(const LiveCameraChoice &choice) {
    doggy::v1::Camera camera;
    camera.set_id(kCameraPrimaryId);
    camera.set_name(choice.name);
    camera.set_source(choice.kind == LiveCameraKind::rpi ? "rpi" : "v4l2");
    camera.set_device(choice.device);
    camera.set_width(choice.width);
    camera.set_height(choice.height);
    camera.set_fps(choice.fps);
    camera.set_rotation_deg(choice.rotation_deg);
    camera.set_swap_halves(choice.swap_halves);
    camera.set_enabled(true);
    return camera;
}

// ================================================================================

CameraQueryResult CameraPipeline::query(
        const Config &config,
        const std::string &host) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (any_camera_enabled(config) == false) {
        stopFeedersLocked();
        return {};
    }

    const std::string selection_key = live_selection_key(config);
    const auto healthy = feeders_.find(kCameraPrimaryId);
    if (feeders_.size() == 1
            && healthy != feeders_.end()
            && healthy->second.selection_key == selection_key
            && healthy->second.process
            && healthy->second.process->running()) {
        CameraQueryResult result;
        result.result = CameraResult::ok;
        append_live_stream(
                &result.streams, host, healthy->second.stream_name, true);
        return result;
    }

    const std::vector<V4l2CaptureDevice> devices = discoverV4l2Locked();
    const bool rpi_available = discovery_->rpiCameraAvailable();
    const doggy::v1::Camera *primary = primary_camera(config);
    const int configured_width = primary == nullptr ? 0 : primary->width();
    const int configured_height = primary == nullptr ? 0 : primary->height();
    const int configured_fps = primary == nullptr ? 0 : primary->fps();
    const int configured_rotation = primary == nullptr ? 180 : primary->rotation_deg();
    const bool swap_halves = primary != nullptr && primary->swap_halves();
    const std::vector<LiveCameraChoice> choices = rank_live_cameras(
            devices,
            rpi_available,
            configured_width,
            configured_height,
            configured_fps,
            configured_rotation,
            swap_halves);
    log_discovered_cameras(devices, rpi_available);

    stopFeedersLocked();

    CameraQueryResult result;
    if (choices.empty()) {
        PLOG_WARNING << "camera: no capture source";
        result.result = CameraResult::not_found;
        return result;
    }

    bool any_failed = false;
    for (const LiveCameraChoice &choice : choices) {
        const doggy::v1::Camera camera = capture_camera(choice);
        const CameraProcessSpec spec = choice.kind == LiveCameraKind::rpi
                ? rpi_process_spec(camera)
                : v4l2_process_spec(camera, choice.device);
        ActiveFeeder next;
        next.selection_key = selection_key;
        next.stream_name = choice.name;
        next.device = choice.device;
        next.choice = choice;
        next.config_key = selection_key + "|" + choice.device + "|"
                + std::to_string(choice.width) + "x" + std::to_string(choice.height)
                + "|" + std::to_string(choice.fps);
        next.process = makeProcess();
        if (next.process->start(spec) == false || next.process->running() == false) {
            log_live_camera("start failed", choice, true);
            any_failed = true;
            continue;
        }
        log_live_camera("live", choice, false);

        feeders_.emplace(kCameraPrimaryId, std::move(next));
        if (choice.kind == LiveCameraKind::rpi) {
            usleep(kRpiBringUpDelayUs);
        } else {
            usleep(kUsbBringUpDelayUs);
        }

        result.result = CameraResult::ok;
        append_live_stream(&result.streams, host, choice.name, true);
        return result;
    }

    result.result = any_failed ? CameraResult::failed : CameraResult::not_found;
    return result;
}

// ================================================================================

std::optional<LiveCameraChoice> CameraPipeline::liveCamera() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto healthy = feeders_.find(kCameraPrimaryId);
    if (healthy == feeders_.end() || healthy->second.process == nullptr
            || healthy->second.process->running() == false) {
        return std::nullopt;
    }
    return healthy->second.choice;
}
