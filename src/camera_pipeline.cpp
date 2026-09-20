#include "camera_pipeline.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <linux/videodev2.h>
#include <poll.h>
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

// Runs between fork and exec, so it must stay async-signal-safe: no allocation.
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

        // argv is built before forking: the child may not allocate, because
        // another thread can hold the allocator lock at the moment of the fork.
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
        if (process_group_ > 0) {
            kill(-process_group_, SIGTERM);
            usleep(50000);
            kill(-process_group_, SIGKILL);
        }
        for (pid_t pid : children_) {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
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

static bool is_v4l2_capture_device(const fs::path &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    v4l2_capability capability{};
    const bool queried = ioctl(fd, VIDIOC_QUERYCAP, &capability) == 0;
    close(fd);
    if (queried == false) {
        return false;
    }
    const uint32_t caps = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
            ? capability.device_caps
            : capability.capabilities;
    return (caps & V4L2_CAP_VIDEO_CAPTURE) != 0
            || (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) != 0;
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

        // The camera may already be held by a running feeder, so the probe is
        // bounded: an HTTP worker must never block here.
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

    std::optional<std::string> firstV4l2CaptureDevice() override {
        std::vector<fs::path> candidates;
        std::error_code error;
        for (const fs::directory_entry &entry :
                fs::directory_iterator("/dev", error)) {
            if (entry.path().filename().string().rfind("video", 0) == 0) {
                candidates.push_back(entry.path());
            }
        }
        std::sort(candidates.begin(), candidates.end());
        for (const fs::path &candidate : candidates) {
            if (is_v4l2_capture_device(candidate)) {
                return candidate.string();
            }
        }
        return std::nullopt;
    }
};

// ================================================================================

static CameraProcessSpec rpi_process_spec(const doggy::v1::Camera &camera) {
    CameraProcessSpec spec;
    spec.commands = {
        {
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
            "--output", "-"
        },
        {
            kFfmpegPath,
            "-nostdin",
            "-hide_banner",
            "-loglevel", "warning",
            "-f", "h264",
            "-framerate", std::to_string(camera.fps()),
            "-i", "pipe:0",
            "-an",
            "-c:v", "copy",
            "-f", "rtsp",
            "-rtsp_transport", "tcp",
            std::string(kRtspBase) + camera.id()
        }
    };
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
        "-f", "v4l2",
        "-framerate", std::to_string(camera.fps()),
        "-video_size",
        std::to_string(camera.width()) + "x" + std::to_string(camera.height()),
        "-i", device
    };
    if (camera.rotation_deg() == 180) {
        command.insert(command.end(), {"-vf", "hflip,vflip"});
    }
    command.insert(command.end(), {
        "-an",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-pix_fmt", "yuv420p",
        "-g", std::to_string(camera.fps() * 2),
        "-f", "rtsp",
        "-rtsp_transport", "tcp",
        std::string(kRtspBase) + camera.id()
    });
    CameraProcessSpec spec;
    spec.commands.push_back(std::move(command));
    return spec;
}

// ================================================================================

CameraPipeline::CameraPipeline() :
    CameraPipeline(
            std::make_unique<PosixCameraProcess>(),
            std::make_unique<SystemCameraDiscovery>()) {
}

// ================================================================================

CameraPipeline::CameraPipeline(std::unique_ptr<CameraProcess> process) :
    CameraPipeline(std::move(process), std::make_unique<SystemCameraDiscovery>()) {
}

// ================================================================================

CameraPipeline::CameraPipeline(
        std::unique_ptr<CameraProcess> process,
        std::unique_ptr<CameraDiscovery> discovery) :
    process_(std::move(process)),
    discovery_(std::move(discovery)) {
}

// ================================================================================

CameraPipeline::~CameraPipeline() {
    std::lock_guard<std::mutex> lock(mutex_);
    process_->stop();
}

// ================================================================================

CameraQueryResult CameraPipeline::query(
        const Config &config,
        const std::string &host) {
    std::lock_guard<std::mutex> lock(mutex_);
    const doggy::v1::Camera *selected = nullptr;
    for (const doggy::v1::Camera &camera : config.cameras().items()) {
        if (camera.enabled()) {
            selected = &camera;
            break;
        }
    }
    if (selected == nullptr) {
        return {};
    }

    // A page reload must not re-probe or respawn while the feeder is healthy.
    const std::string selected_config = selected->SerializeAsString();
    if (active_id_ == selected->id()
            && active_config_ == selected_config
            && process_->running()) {
        return streamsFor(*selected, host);
    }

    std::string source = selected->source();
    std::string device = selected->device();
    if (source == "auto") {
        if (discovery_->rpiCameraAvailable()) {
            source = "rpi";
        } else {
            const std::optional<std::string> found =
                    discovery_->firstV4l2CaptureDevice();
            if (found.has_value() == false) {
                return {};
            }
            source = "v4l2";
            device = *found;
        }
    } else if (source == "v4l2" && device.empty()) {
        const std::optional<std::string> found =
                discovery_->firstV4l2CaptureDevice();
        if (found.has_value() == false) {
            return {};
        }
        device = *found;
    }

    process_->stop();
    const CameraProcessSpec spec = source == "rpi"
            ? rpi_process_spec(*selected)
            : v4l2_process_spec(*selected, device);
    if (process_->start(spec) == false) {
        active_id_.clear();
        active_config_.clear();
        return {CameraResult::failed, {}};
    }
    active_id_ = selected->id();
    active_config_ = selected_config;
    return streamsFor(*selected, host);
}

// ================================================================================

CameraQueryResult CameraPipeline::streamsFor(
        const doggy::v1::Camera &camera,
        const std::string &host) const {
    CameraQueryResult result;
    result.result = CameraResult::ok;
    doggy::v1::CameraStream *stream = result.streams.add_items();
    stream->set_id(camera.id());
    stream->set_name(camera.name());
    stream->set_webrtc_url(
            "https://" + host + ":" + std::to_string(kWebRtcPort)
            + "/" + camera.id() + "/whep");
    stream->set_ready(true);
    return result;
}
