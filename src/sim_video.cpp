#include "sim_video.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr std::size_t kFrameBytes =
        static_cast<std::size_t>(kSimVideoWidth) * kSimVideoHeight * 3u;

// ================================================================================

bool known_camera(const std::string &camera_id) {
    return camera_id == "usb_left" || camera_id == "usb_right";
}

// ================================================================================

bool loopback_rtsp(const std::string &url) {
    const std::string prefix = "rtsp://127.0.0.1:";
    if (url.rfind(prefix, 0) != 0) {
        return false;
    }
    if (url.find(' ') != std::string::npos || url.find('\n') != std::string::npos) {
        return false;
    }
    return true;
}

// ================================================================================

int gop_frames(int fps) {
    int gop = fps / 4;
    if (gop < 1) {
        gop = 1;
    }
    return gop;
}

// ================================================================================

void block_sigpipe() {
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
}

// ================================================================================

bool write_all(int fd, const std::uint8_t *data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t wrote = ::write(fd, data + sent, size - sent);
        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (wrote == 0) {
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }
    return true;
}

// ================================================================================

std::string ffmpeg_binary() {
    if (::access("/usr/bin/ffmpeg", X_OK) == 0) {
        return "/usr/bin/ffmpeg";
    }
    return "ffmpeg";
}

// ================================================================================

pid_t spawn_group(const std::vector<std::string> &args, int *stdin_fd) {
    int pipe_fd[2] = {-1, -1};
    if (stdin_fd != nullptr) {
        if (::pipe(pipe_fd) != 0) {
            return -1;
        }
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (pipe_fd[0] >= 0) {
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);
        }
        return -1;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        if (stdin_fd != nullptr) {
            if (::dup2(pipe_fd[0], STDIN_FILENO) < 0) {
                _exit(127);
            }
            ::close(pipe_fd[0]);
            ::close(pipe_fd[1]);
        }
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    ::setpgid(pid, pid);
    if (stdin_fd != nullptr) {
        ::close(pipe_fd[0]);
        *stdin_fd = pipe_fd[1];
    }
    return pid;
}

// ================================================================================

void reap_group(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    ::kill(-pid, SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        const pid_t got = ::waitpid(pid, nullptr, WNOHANG);
        if (got == pid || (got < 0 && errno != EINTR)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
}

// ================================================================================

bool wait_loopback_port(int port) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1
                && ::connect(
                        fd,
                        reinterpret_cast<sockaddr *>(&address),
                        sizeof(address)) == 0) {
            ::close(fd);
            return true;
        }
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

}  // namespace

// ================================================================================

std::vector<std::string> sim_ffmpeg_publish_command(
        const std::string &ffmpeg,
        int width,
        int height,
        int fps,
        const std::string &rtsp_url) {
    if (ffmpeg.empty() || width < 2 || height < 2 || width > 1920 || height > 1080
            || fps < 1 || fps > 60 || loopback_rtsp(rtsp_url) == false) {
        return {};
    }
    const std::string gop = std::to_string(gop_frames(fps));
    return {
        ffmpeg,
        "-nostdin",
        "-hide_banner",
        "-loglevel", "warning",
        "-fflags", "nobuffer",
        "-flags", "low_delay",
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-video_size", std::to_string(width) + "x" + std::to_string(height),
        "-framerate", std::to_string(fps),
        "-i", "pipe:0",
        "-an",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-pix_fmt", "yuv420p",
        "-g", gop,
        "-keyint_min", gop,
        "-sc_threshold", "0",
        "-f", "rtsp",
        "-rtsp_transport", "tcp",
        rtsp_url
    };
}

// ================================================================================

std::string sim_rtsp_url(const std::string &camera_id) {
    if (known_camera(camera_id) == false) {
        return {};
    }
    return "rtsp://127.0.0.1:" + std::to_string(kSimRtspPort) + "/" + camera_id;
}

// ================================================================================

std::string sim_webrtc_url(const std::string &camera_id) {
    if (known_camera(camera_id) == false) {
        return {};
    }
    return "http://127.0.0.1:" + std::to_string(kSimWebRtcPort) + "/"
            + camera_id + "/whep";
}

// ================================================================================

struct SimVideo::Impl {
    std::atomic<bool> running{false};
    std::atomic<bool> stop{false};
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    std::vector<std::uint8_t> frames[2];
    bool have_frame[2] = {false, false};
    int feed_fd[2] = {-1, -1};
    std::thread feeders[2];
    pid_t mediamtx_pid = -1;
    pid_t ffmpeg_pid[2] = {-1, -1};

    // ================================================================================

    void feed_loop(int camera) {
        block_sigpipe();
        std::vector<std::uint8_t> frame(kFrameBytes, 0);
        while (stop.load() == false) {
            {
                std::unique_lock<std::mutex> lock(frame_mutex);
                frame_cv.wait_for(lock, std::chrono::milliseconds(100));
                if (have_frame[camera]) {
                    frame = frames[camera];
                }
            }
            if (stop.load()) {
                break;
            }
            if (feed_fd[camera] < 0
                    || write_all(feed_fd[camera], frame.data(), frame.size()) == false) {
                break;
            }
        }
    }

    // ================================================================================

    void close_feeds() {
        for (int camera = 0; camera < 2; ++camera) {
            if (feed_fd[camera] >= 0) {
                ::close(feed_fd[camera]);
                feed_fd[camera] = -1;
            }
        }
    }

    // ================================================================================

    void reap_children() {
        for (int camera = 0; camera < 2; ++camera) {
            reap_group(ffmpeg_pid[camera]);
            ffmpeg_pid[camera] = -1;
        }
        reap_group(mediamtx_pid);
        mediamtx_pid = -1;
    }
};

// ================================================================================

SimVideo::SimVideo() : impl_(new Impl) {
}

// ================================================================================

SimVideo::~SimVideo() {
    stop();
    delete impl_;
    impl_ = nullptr;
}

// ================================================================================

bool SimVideo::start(const std::string &mediamtx_bin, const std::string &config_path) {
    if (impl_->running.load()) {
        return true;
    }
    if (mediamtx_bin.empty() || config_path.empty()
            || ::access(mediamtx_bin.c_str(), X_OK) != 0
            || ::access(config_path.c_str(), R_OK) != 0) {
        return false;
    }
    impl_->stop.store(false);
    impl_->mediamtx_pid = spawn_group({mediamtx_bin, config_path}, nullptr);
    if (impl_->mediamtx_pid < 0 || wait_loopback_port(kSimRtspPort) == false) {
        stop();
        return false;
    }
    const char *ids[2] = {"usb_left", "usb_right"};
    for (int camera = 0; camera < 2; ++camera) {
        const std::vector<std::string> command = sim_ffmpeg_publish_command(
                ffmpeg_binary(),
                kSimVideoWidth,
                kSimVideoHeight,
                kSimVideoFps,
                sim_rtsp_url(ids[camera]));
        if (command.empty()) {
            stop();
            return false;
        }
        impl_->ffmpeg_pid[camera] = spawn_group(command, &impl_->feed_fd[camera]);
        if (impl_->ffmpeg_pid[camera] < 0) {
            stop();
            return false;
        }
        impl_->feeders[camera] = std::thread(&Impl::feed_loop, impl_, camera);
    }
    impl_->running.store(true);
    return true;
}

// ================================================================================

void SimVideo::stop() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->running.store(false);
    impl_->stop.store(true);
    impl_->frame_cv.notify_all();
    for (int camera = 0; camera < 2; ++camera) {
        if (impl_->feeders[camera].joinable()) {
            impl_->feeders[camera].join();
        }
    }
    impl_->close_feeds();
    impl_->reap_children();
    impl_->stop.store(false);
}

// ================================================================================

bool SimVideo::running() const {
    return impl_ != nullptr && impl_->running.load();
}

// ================================================================================

void SimVideo::submit(
        int camera,
        std::vector<std::uint8_t> rgb,
        std::uint32_t width,
        std::uint32_t height) {
    if (impl_ == nullptr || impl_->running.load() == false) {
        return;
    }
    if ((camera != 0 && camera != 1) || width != kSimVideoWidth
            || height != kSimVideoHeight || rgb.size() != kFrameBytes) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        impl_->frames[camera] = std::move(rgb);
        impl_->have_frame[camera] = true;
    }
    impl_->frame_cv.notify_all();
}

// ================================================================================

std::vector<SimVideoStream> SimVideo::offers() const {
    std::vector<SimVideoStream> streams;
    if (running() == false) {
        return streams;
    }
    const char *ids[2] = {"usb_left", "usb_right"};
    const char *names[2] = {"Left camera", "Right camera"};
    for (int camera = 0; camera < 2; ++camera) {
        SimVideoStream stream;
        stream.id = ids[camera];
        stream.name = names[camera];
        stream.webrtc_url = sim_webrtc_url(ids[camera]);
        streams.push_back(std::move(stream));
    }
    return streams;
}
