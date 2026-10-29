#include "calibration_session.h"

#include "camera_ids.h"
#include "config.h"
#include "utils.h"

#include <plog/Log.h>

#include <google/protobuf/util/json_util.h>
#include <opencv2/imgcodecs.hpp>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;

// ================================================================================

std::vector<std::string> calibration_grab_command(
        const std::string &ffmpeg_path,
        const std::string &url,
        int width,
        int height) {
    return {
            ffmpeg_path,
            "-hide_banner",
            "-loglevel", "warning",
            "-nostdin",
            "-rtsp_transport", "tcp",
            "-skip_frame", "nokey",
            "-i", url,
            "-an",
            "-vf", "scale=" + std::to_string(width) + ":"
                    + std::to_string(height) + ":flags=neighbor,format=gray",
            "-fps_mode", "passthrough",
            "-f", "rawvideo",
            "-pix_fmt", "gray",
            "pipe:1",
    };
}

namespace {

constexpr const char *kFfmpegPath = "/usr/bin/ffmpeg";
const std::string kRtspUrl = std::string("rtsp://127.0.0.1:8554/") + kCameraPrimaryId;

// ================================================================================

std::string child_status_text(int status) {
    if (WIFEXITED(status)) {
        return "exit " + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return "signal " + std::to_string(WTERMSIG(status));
    }
    return "status " + std::to_string(status);
}

// ================================================================================

std::string command_line(const std::vector<std::string> &command) {
    std::ostringstream line;
    for (std::size_t index = 0; index < command.size(); ++index) {
        if (index > 0) {
            line << ' ';
        }
        line << command[index];
    }
    return line.str();
}

// ================================================================================

class RtspGraySource final : public CalibrationSource {
public:
    ~RtspGraySource() override {
        close_process();
    }

    // ================================================================================

    bool open(int width, int height) {
        close_process();
        if (width < 16 || height < 16 || access(kFfmpegPath, X_OK) != 0) {
            PLOG_WARNING << "calibration ffmpeg cannot start";
            return false;
        }
        width_ = width;
        height_ = height;
        command_ = calibration_grab_command(kFfmpegPath, kRtspUrl, width, height);

        int pipes[2] = {-1, -1};
        if (pipe(pipes) != 0) {
            return false;
        }
        const pid_t pid = fork();
        if (pid < 0) {
            ::close(pipes[0]);
            ::close(pipes[1]);
            return false;
        }
        if (pid == 0) {
            setpgid(0, 0);
            dup2(pipes[1], STDOUT_FILENO);
            close_inherited_descriptors();
            std::vector<char *> argv;
            argv.reserve(command_.size() + 1);
            for (std::string &item : command_) {
                argv.push_back(item.data());
            }
            argv.push_back(nullptr);
            execv(argv[0], argv.data());
            _exit(127);
        }

        setpgid(pid, pid);
        ::close(pipes[1]);
        pid_ = pid;
        fd_ = pipes[0];
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            fresh_ = false;
            ended_ = false;
            latest_.release();
        }
        reader_ = std::thread([this, fd = fd_]() { read_frames(fd); });
        PLOG_INFO << "calibration ffmpeg start pid " << pid_
                  << " " << command_line(command_);
        return true;
    }

    // ================================================================================

    int next(cv::Mat *gray, int timeout_ms) override {
        if (gray == nullptr || reader_.joinable() == false) {
            return -1;
        }
        std::unique_lock<std::mutex> lock(frame_mutex_);
        const bool ready = frame_ready_.wait_for(
                lock,
                std::chrono::milliseconds(timeout_ms),
                [this]() { return fresh_ || ended_; });
        if (ready == false) {
            return 0;
        }
        if (fresh_ == false) {
            return -1;
        }
        latest_.copyTo(*gray);
        fresh_ = false;
        return 1;
    }

private:
    // ================================================================================

    void read_frames(int fd) {
        const std::size_t need = static_cast<std::size_t>(width_)
                * static_cast<std::size_t>(height_);
        std::string pending;
        while (true) {
            while (pending.size() < need) {
                char buffer[65536];
                const ssize_t count = ::read(fd, buffer, sizeof(buffer));
                if (count < 0 && errno == EINTR) {
                    continue;
                }
                if (count < 0 && errno == EBADF) {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    ended_ = true;
                    frame_ready_.notify_one();
                    return;
                }
                if (count <= 0) {
                    PLOG_WARNING << "calibration ffmpeg read ended count " << count
                                 << " errno " << errno;
                    note_source_ended();
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    ended_ = true;
                    frame_ready_.notify_one();
                    return;
                }
                pending.append(buffer, static_cast<std::size_t>(count));
            }

            cv::Mat frame(height_, width_, CV_8UC1);
            std::memcpy(frame.data, pending.data(), need);
            pending.erase(0, need);
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_ = std::move(frame);
            fresh_ = true;
            frame_ready_.notify_one();
        }
    }

    // ================================================================================

    void note_source_ended() {
        if (pid_ <= 0) {
            PLOG_WARNING << "calibration ffmpeg ended without a pid";
            return;
        }
        int status = 0;
        const pid_t done = waitpid(pid_, &status, WNOHANG);
        if (done == pid_ || (done < 0 && errno == ECHILD)) {
            if (done == pid_) {
                PLOG_WARNING << "calibration ffmpeg ended " << child_status_text(status);
            } else {
                PLOG_WARNING << "calibration ffmpeg ended already reaped";
            }
            pid_ = -1;
            return;
        }
        PLOG_WARNING << "calibration ffmpeg pipe closed while pid " << pid_
                     << " is still running";
    }

    // ================================================================================

    void close_process() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (reader_.joinable()) {
            reader_.join();
        }
        if (pid_ > 0) {
            kill(-pid_, SIGTERM);
            const auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(500);
            while (std::chrono::steady_clock::now() < deadline) {
                int status = 0;
                const pid_t done = waitpid(pid_, &status, WNOHANG);
                if (done == pid_ || (done < 0 && errno == ECHILD)) {
                    pid_ = -1;
                    break;
                }
                usleep(20000);
            }
            if (pid_ > 0) {
                kill(-pid_, SIGKILL);
                int status = 0;
                const pid_t done = waitpid(pid_, &status, 0);
                if (done == pid_) {
                    PLOG_INFO << "calibration ffmpeg stopped " << child_status_text(status);
                }
                pid_ = -1;
            }
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int width_ = 0;
    int height_ = 0;
    pid_t pid_ = -1;
    int fd_ = -1;
    std::thread reader_;
    std::mutex frame_mutex_;
    std::condition_variable frame_ready_;
    cv::Mat latest_;
    bool fresh_ = false;
    bool ended_ = false;
    std::vector<std::string> command_;
};

// ================================================================================

CalibrationReply error_reply(int status, const char *code) {
    CalibrationReply reply;
    reply.status = status;
    reply.body = std::string("{\"error\":\"") + code + "\"}";
    return reply;
}

// ================================================================================

std::string message_json(const doggy::v1::CalibrationStatus &status) {
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.always_print_primitive_fields = true;
    std::string out;
    const auto result = google::protobuf::util::MessageToJsonString(status, &out, options);
    if (result.ok() == false) {
        return "{\"error\":\"calibration\"}";
    }
    return out;
}

// ================================================================================

void set_box(doggy::v1::CalibrationBox *message, const NormalizedBox &box) {
    message->set_x(box.x);
    message->set_y(box.y);
    message->set_width(box.width);
    message->set_height(box.height);
}

// ================================================================================

bool directory_is_inside(const fs::path &root, const fs::path &child) {
    std::error_code error;
    const fs::path root_real = fs::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const fs::path child_real = fs::weakly_canonical(child, error);
    if (error) {
        return false;
    }
    const fs::path relative = fs::relative(child_real, root_real, error);
    if (error || relative.empty() || relative == ".") {
        return false;
    }
    for (const fs::path &part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

// ================================================================================

cv::Size eye_size_of(const LiveCameraChoice &camera) {
    cv::Size eye(camera.width, camera.height);
    if (camera.kind == LiveCameraKind::stereo && eye.width > 0) {
        eye.width /= 2;
    }
    return eye;
}

// ================================================================================

bool already_covered(const std::vector<std::string> &keys, const std::string &key) {
    for (const std::string &have : keys) {
        if (have == key) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ================================================================================

struct CalibrationService::State {
    mutable std::mutex data;
    std::mutex control;
    std::thread worker;
    doggy::v1::CalibrationState phase = doggy::v1::CALIBRATION_IDLE;
    bool stop = false;
    bool manual = false;
    bool finish_requested = false;
    bool saved = false;
    BoardSpec board;
    LiveCameraChoice camera;
    std::vector<CalibrationSample> samples;
    std::vector<std::string> covered;
    std::vector<cv::Point2f> previous_left;
    std::vector<cv::Point2f> previous_right;
    FrameDetection last;
    bool have_last = false;
    std::string message;
    std::string session_dir;
    CalibrationModel model;
};

// ================================================================================

std::string calibration_snapshots_root() {
    const char *override_dir = std::getenv("DOGGY_CALIBRATION_DIR");
    if (override_dir != nullptr && override_dir[0] != '\0') {
        return override_dir;
    }
    return "/var/lib/doggy/calibration";
}

// ================================================================================

std::string calibration_result_path() {
    const char *override_path = std::getenv("DOGGY_CALIBRATION_RESULT");
    if (override_path != nullptr && override_path[0] != '\0') {
        return override_path;
    }
    return (fs::path(config_default_path()).parent_path() / "camera_calibration.yml").string();
}

// ================================================================================

CalibrationService::CalibrationService(CalibrationPaths paths) :
    paths_(std::move(paths)),
    state_(std::make_unique<State>()) {
    factory_ = [](const LiveCameraChoice &camera) {
        auto source = std::make_unique<RtspGraySource>();
        if (source->open(camera.width, camera.height) == false) {
            return std::unique_ptr<CalibrationSource>();
        }
        return std::unique_ptr<CalibrationSource>(std::move(source));
    };
}

// ================================================================================

CalibrationService::~CalibrationService() {
    cancel();
}

// ================================================================================

void CalibrationService::set_source_factory(CalibrationSourceFactory factory) {
    std::lock_guard<std::mutex> control(state_->control);
    if (factory) {
        factory_ = std::move(factory);
    }
}

// ================================================================================

void CalibrationService::join_worker() {
    if (state_->worker.joinable()) {
        state_->worker.join();
    }
}

// ================================================================================

void CalibrationService::clear_session_dir() {
    std::string dir;
    {
        std::lock_guard<std::mutex> data(state_->data);
        dir = state_->session_dir;
        state_->session_dir.clear();
    }
    if (dir.empty()) {
        return;
    }
    if (directory_is_inside(paths_.snapshots_root, dir) == false) {
        PLOG_WARNING << "calibration snapshots left in place";
        return;
    }
    std::error_code error;
    fs::remove_all(dir, error);
}

// ================================================================================

std::string CalibrationService::status_json(const LiveCameraChoice *camera) const {
    doggy::v1::CalibrationStatus message;
    std::lock_guard<std::mutex> data(state_->data);
    message.set_state(state_->phase);
    message.set_stereo(state_->camera.kind == LiveCameraKind::stereo);
    message.set_accepted(static_cast<int>(state_->samples.size()));
    message.set_target_count(kCalibrationTargetSamples);
    message.set_message(state_->message);
    const bool capturing = state_->phase == doggy::v1::CALIBRATION_CAPTURING;
    message.set_can_finish(
            capturing
            && static_cast<int>(state_->samples.size()) >= kCalibrationMinimumSamples);

    if (capturing) {
        const std::string key = next_coverage_key(state_->covered);
        if (key.empty() == false) {
            set_box(message.mutable_next_target(),
                    target_box(eye_size_of(state_->camera), key));
        }
    }
    if (state_->have_last) {
        const cv::Size eye = eye_size_of(state_->camera);
        doggy::v1::CalibrationEye *left = message.mutable_left();
        left->set_found(state_->last.left.found);
        if (state_->last.left.found) {
            NormalizedBox bounds;
            bounds.x = static_cast<double>(state_->last.left.bounds.x) / eye.width;
            bounds.y = static_cast<double>(state_->last.left.bounds.y) / eye.height;
            bounds.width = static_cast<double>(state_->last.left.bounds.width) / eye.width;
            bounds.height = static_cast<double>(state_->last.left.bounds.height) / eye.height;
            set_box(left->mutable_bounds(), bounds);
        }
        if (state_->camera.kind == LiveCameraKind::stereo) {
            doggy::v1::CalibrationEye *right = message.mutable_right();
            right->set_found(state_->last.right.found);
            if (state_->last.right.found) {
                NormalizedBox bounds;
                bounds.x = static_cast<double>(state_->last.right.bounds.x) / eye.width;
                bounds.y = static_cast<double>(state_->last.right.bounds.y) / eye.height;
                bounds.width = static_cast<double>(state_->last.right.bounds.width) / eye.width;
                bounds.height = static_cast<double>(state_->last.right.bounds.height) / eye.height;
                set_box(right->mutable_bounds(), bounds);
            }
        }
    }

    if (state_->phase == doggy::v1::CALIBRATION_DONE && state_->model.ok) {
        doggy::v1::CalibrationSummary *summary = message.mutable_result();
        summary->set_rms_left_px(state_->model.rms_left_px);
        summary->set_rms_right_px(state_->model.rms_right_px);
        summary->set_rms_stereo_px(state_->model.rms_stereo_px);
        summary->set_focal_px(state_->model.focal_px);
        summary->set_baseline_mm(state_->model.baseline_mm);
        summary->set_coverage_complete(state_->model.coverage_complete);
        summary->set_saved(state_->saved);
        summary->set_stale(false);
    } else if (state_->phase == doggy::v1::CALIBRATION_IDLE) {
        const CalibrationIdentity stored = read_calibration_identity(paths_.result_path);
        if (stored.present) {
            doggy::v1::CalibrationSummary *summary = message.mutable_result();
            summary->set_rms_left_px(stored.rms_left_px);
            summary->set_rms_right_px(stored.rms_right_px);
            summary->set_rms_stereo_px(stored.rms_stereo_px);
            summary->set_focal_px(stored.focal_px);
            summary->set_baseline_mm(stored.baseline_mm);
            summary->set_saved(true);
            bool stale = false;
            if (camera != nullptr) {
                stale = calibration_identity_matches(
                                stored,
                                camera->kind == LiveCameraKind::stereo,
                                camera->width,
                                camera->height,
                                camera->stable_id,
                                camera->swap_halves,
                                camera->rotation_deg) == false;
            }
            summary->set_stale(stale);
            message.set_stereo(stored.stereo);
        }
    }
    return message_json(message);
}

// ================================================================================

void CalibrationService::run(std::unique_ptr<CalibrationSource> source) {
    auto compute = [this]() {
        std::vector<CalibrationSample> samples;
        BoardSpec board;
        cv::Size frame;
        bool stereo = false;
        {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            state_->phase = doggy::v1::CALIBRATION_COMPUTING;
            state_->message = "Computing";
            samples = state_->samples;
            board = state_->board;
            frame = cv::Size(state_->camera.width, state_->camera.height);
            stereo = state_->camera.kind == LiveCameraKind::stereo;
        }
        CalibrationModel model = calibrate_samples(samples, frame, board, stereo);
        std::lock_guard<std::mutex> data(state_->data);
        if (state_->stop) {
            return;
        }
        model.stable_id = state_->camera.stable_id;
        model.swap_halves = state_->camera.swap_halves;
        model.rotation_deg = state_->camera.rotation_deg;
        model.coverage_complete = coverage_complete(state_->covered);
        state_->model = std::move(model);
        if (state_->model.ok == false) {
            state_->phase = doggy::v1::CALIBRATION_FAILED;
            state_->message = "calibration_failed";
            PLOG_WARNING << "calibration failed";
            return;
        }
        state_->phase = doggy::v1::CALIBRATION_DONE;
        state_->message = "Ready to save";
        PLOG_INFO << "calibration done focal_px " << state_->model.focal_px
                  << " baseline_mm " << state_->model.baseline_mm
                  << " rms_left " << state_->model.rms_left_px
                  << " rms_stereo " << state_->model.rms_stereo_px;
    };

    while (true) {
        bool finish_now = false;
        int accepted_count = 0;
        {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            accepted_count = static_cast<int>(state_->samples.size());
            finish_now = (state_->finish_requested && accepted_count >= kCalibrationMinimumSamples)
                    || accepted_count >= kCalibrationMaximumSamples
                    || (accepted_count >= kCalibrationTargetSamples
                            && coverage_complete(state_->covered));
        }
        if (finish_now) {
            PLOG_INFO << "calibration solve starting samples " << accepted_count;
            compute();
            return;
        }

        cv::Mat gray;
        const int got = source->next(&gray, 200);
        if (got < 0) {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            state_->phase = doggy::v1::CALIBRATION_FAILED;
            state_->message = "Camera frames stopped";
            PLOG_WARNING << "calibration frame source ended";
            return;
        }
        if (got == 0) {
            continue;
        }

        LiveCameraChoice camera;
        BoardSpec board;
        bool manual = false;
        std::vector<cv::Point2f> previous_left;
        std::vector<cv::Point2f> previous_right;
        {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            camera = state_->camera;
            board = state_->board;
            manual = state_->manual;
            previous_left = state_->previous_left;
            previous_right = state_->previous_right;
        }
        if (gray.cols != camera.width || gray.rows != camera.height || gray.type() != CV_8UC1) {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            state_->phase = doggy::v1::CALIBRATION_FAILED;
            state_->message = "frame_size";
            PLOG_WARNING << "calibration frame size " << gray.cols << "x" << gray.rows
                         << " expected " << camera.width << "x" << camera.height;
            return;
        }

        const bool stereo = camera.kind == LiveCameraKind::stereo;
        const FrameDetection detected = detect_frame(gray, board, stereo);
        const cv::Size eye = eye_size_of(camera);
        const std::string key = coverage_key(eye, detected.left);
        const bool still = detected.usable
                && corners_still(previous_left, detected.left.corners, kCalibrationStillPx)
                && (stereo == false
                        || corners_still(
                                previous_right,
                                detected.right.corners,
                                kCalibrationStillPx));

        bool accepted = false;
        std::string snapshot_path;
        std::string decision = "rejected";
        {
            std::lock_guard<std::mutex> data(state_->data);
            if (state_->stop) {
                return;
            }
            state_->last = detected;
            state_->have_last = true;
            if (detected.usable) {
                state_->previous_left = detected.left.corners;
                state_->previous_right = detected.right.corners;
            } else {
                state_->previous_left.clear();
                state_->previous_right.clear();
            }

            const bool known = key.empty() || already_covered(state_->covered, key);
            const bool take = still
                    && static_cast<int>(state_->samples.size()) < kCalibrationMaximumSamples
                    && (manual || known == false);
            if (detected.usable == false) {
                state_->message = stereo ? "Board not seen in both eyes" : "Board not seen";
            } else if (still == false) {
                state_->message = "Hold still";
            } else if (take == false) {
                state_->message = next_coverage_key(state_->covered) == "tilt"
                        ? "Tilt the board"
                        : "Move the board into the outline";
            }

            if (take) {
                CalibrationSample sample;
                sample.left = detected.left.corners;
                sample.right = detected.right.corners;
                state_->samples.push_back(std::move(sample));
                if (key.empty() == false) {
                    state_->covered.push_back(key);
                }
                state_->manual = false;
                state_->message = "Saved a snapshot";
                accepted = true;
                decision = "accepted";
                accepted_count = static_cast<int>(state_->samples.size());
                const int index = static_cast<int>(state_->samples.size());
                const std::string dir = state_->session_dir;
                if (dir.empty() == false) {
                    std::ostringstream name;
                    name << dir << "/frame_" << index << ".jpg";
                    snapshot_path = name.str();
                }
                PLOG_INFO << "calibration snapshot " << index;
            }
            accepted_count = static_cast<int>(state_->samples.size());
        }
        PLOG_INFO << "calibration frame " << gray.cols << "x" << gray.rows
                  << " detected " << (detected.usable ? "yes" : "no")
                  << " still " << (still ? "yes" : "no")
                  << " key " << (key.empty() ? "-" : key)
                  << " manual " << (manual ? "yes" : "no")
                  << " decision " << decision
                  << " accepted " << accepted_count;
        if (snapshot_path.empty() == false && cv::imwrite(snapshot_path, gray) == false) {
            PLOG_WARNING << "calibration snapshot was not written " << snapshot_path;
        } else if (snapshot_path.empty() == false) {
            PLOG_INFO << "calibration snapshot written " << snapshot_path;
        }
        if (accepted == false) {
            continue;
        }
    }
}

// ================================================================================

CalibrationReply CalibrationService::begin(
        const std::string &body,
        const LiveCameraChoice &camera) {
    doggy::v1::CalibrationStart request;
    std::string parse_error;
    if (proto_from_json(body, request, &parse_error) == false) {
        PLOG_INFO << "calibration start rejected";
        return error_reply(400, "bad_request");
    }
    BoardSpec board;
    board.inner_cols = request.inner_cols();
    board.inner_rows = request.inner_rows();
    board.square_mm = request.square_mm();
    if (board_spec_ok(board) == false) {
        return error_reply(400, "bad_request");
    }
    if (camera.width < 16 || camera.height < 16
            || camera.kind == LiveCameraKind::none) {
        return error_reply(404, "not_found");
    }

    std::lock_guard<std::mutex> control(state_->control);
    {
        std::lock_guard<std::mutex> data(state_->data);
        if (state_->phase == doggy::v1::CALIBRATION_CAPTURING
                || state_->phase == doggy::v1::CALIBRATION_COMPUTING) {
            return error_reply(409, "busy");
        }
    }
    join_worker();
    clear_session_dir();

    std::unique_ptr<CalibrationSource> source = factory_(camera);
    if (source == nullptr) {
        return error_reply(500, "camera_io");
    }

    std::ostringstream folder;
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
    folder << stamp << "-" << getpid();
    const fs::path session = fs::path(paths_.snapshots_root) / folder.str();
    std::error_code error;
    fs::create_directories(session, error);
    if (error) {
        PLOG_WARNING << "calibration directory: " << error.message();
        return error_reply(500, "camera_io");
    }

    {
        std::lock_guard<std::mutex> data(state_->data);
        state_->stop = false;
        state_->manual = false;
        state_->finish_requested = false;
        state_->saved = false;
        state_->board = board;
        state_->camera = camera;
        state_->samples.clear();
        state_->covered.clear();
        state_->previous_left.clear();
        state_->previous_right.clear();
        state_->last = {};
        state_->have_last = false;
        state_->model = {};
        state_->session_dir = session.string();
        state_->message = "Show the board";
        state_->phase = doggy::v1::CALIBRATION_CAPTURING;
    }

    PLOG_INFO << "calibration start " << camera.width << "x" << camera.height
              << (camera.kind == LiveCameraKind::stereo ? " stereo" : " mono")
              << " board " << board.inner_cols << "x" << board.inner_rows
              << " square_mm " << board.square_mm;
    state_->worker = std::thread([this, source = std::move(source)]() mutable {
        run(std::move(source));
    });

    CalibrationReply reply;
    reply.status = 202;
    reply.body = status_json(nullptr);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::status(const LiveCameraChoice *camera) const {
    CalibrationReply reply;
    reply.body = status_json(camera);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::capture() {
    std::lock_guard<std::mutex> control(state_->control);
    {
        std::lock_guard<std::mutex> data(state_->data);
        if (state_->phase != doggy::v1::CALIBRATION_CAPTURING) {
            return error_reply(409, "busy");
        }
        state_->manual = true;
    }
    CalibrationReply reply;
    reply.status = 202;
    reply.body = status_json(nullptr);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::finish() {
    std::lock_guard<std::mutex> control(state_->control);
    {
        std::lock_guard<std::mutex> data(state_->data);
        if (state_->phase != doggy::v1::CALIBRATION_CAPTURING) {
            return error_reply(409, "busy");
        }
        if (static_cast<int>(state_->samples.size()) < kCalibrationMinimumSamples) {
            return error_reply(400, "bad_request");
        }
        state_->finish_requested = true;
    }
    CalibrationReply reply;
    reply.status = 202;
    reply.body = status_json(nullptr);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::cancel() {
    std::lock_guard<std::mutex> control(state_->control);
    {
        std::lock_guard<std::mutex> data(state_->data);
        state_->stop = true;
    }
    join_worker();
    clear_session_dir();
    {
        std::lock_guard<std::mutex> data(state_->data);
        state_->stop = false;
        state_->manual = false;
        state_->finish_requested = false;
        state_->phase = doggy::v1::CALIBRATION_IDLE;
        state_->samples.clear();
        state_->covered.clear();
        state_->previous_left.clear();
        state_->previous_right.clear();
        state_->have_last = false;
        state_->message.clear();
        state_->model = {};
        state_->camera = {};
    }
    CalibrationReply reply;
    reply.body = status_json(nullptr);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::save() {
    std::lock_guard<std::mutex> control(state_->control);
    CalibrationModel model;
    {
        std::lock_guard<std::mutex> data(state_->data);
        if (state_->phase != doggy::v1::CALIBRATION_DONE || state_->model.ok == false) {
            return error_reply(409, "busy");
        }
        model = state_->model;
    }
    if (write_calibration_file(paths_.result_path, model) == false) {
        return error_reply(500, "camera_io");
    }
    {
        std::lock_guard<std::mutex> data(state_->data);
        state_->saved = true;
    }
    PLOG_INFO << "calibration saved";
    CalibrationReply reply;
    reply.body = status_json(nullptr);
    return reply;
}

// ================================================================================

CalibrationReply CalibrationService::download() const {
    std::error_code error;
    if (fs::is_regular_file(paths_.result_path, error) == false) {
        return error_reply(404, "not_found");
    }
    CalibrationReply reply;
    reply.file_path = paths_.result_path;
    reply.content_type = "application/x-yaml";
    return reply;
}
