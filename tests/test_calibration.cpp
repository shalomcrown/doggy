#include "calibration.h"
#include "calibration_session.h"
#include "camera_pipeline.h"
#include "config.h"
#include "doggy_log.h"
#include "image_ops.h"
#include "robot_api.h"
#include "web_server.h"

#include "httplib.h"
#include "nlohmann/json.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <thread>
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

class FakeRobot : public RobotApi {
public:
    FakeRobot() {
        config_ = default_config();
    }

    // ================================================================================

    RobotType robotType() const override {
        return doggy::v1::DOG;
    }

    // ================================================================================

    DogStatus getStatus() const override {
        return {};
    }

    // ================================================================================

    CommandResult replaceConfig(const Config &config, const std::string &) override {
        config_ = config;
        return CommandResult::ok;
    }
};

// ================================================================================

class FakeCameraProcess final : public CameraProcess {
public:
    bool alive = false;

    // ================================================================================

    bool start(const CameraProcessSpec &) override {
        alive = true;
        return true;
    }

    // ================================================================================

    bool running() override {
        return alive;
    }

    // ================================================================================

    void stop() override {
        alive = false;
    }
};

// ================================================================================

class FakeDiscovery final : public CameraDiscovery {
public:
    bool rpiCameraAvailable() override {
        return false;
    }

    // ================================================================================

    std::optional<std::string> firstV4l2CaptureDevice() override {
        return std::string("/dev/video0");
    }

    // ================================================================================

    std::vector<V4l2CaptureDevice> listV4l2CaptureDevices() override {
        V4l2CaptureDevice device;
        device.device = "/dev/video0";
        device.name = "USB camera";
        device.stable_id = "usb-test";
        return {device};
    }
};

// ================================================================================

struct FrameQueue {
    std::mutex mutex;
    std::condition_variable waiting;
    std::deque<cv::Mat> frames;
    bool closed = false;
};

// ================================================================================

class QueueSource final : public CalibrationSource {
public:
    explicit QueueSource(std::shared_ptr<FrameQueue> queue) : queue_(std::move(queue)) {
    }

    // ================================================================================

    int next(cv::Mat *gray, int timeout_ms) override {
        std::unique_lock<std::mutex> lock(queue_->mutex);
        const bool ready = queue_->waiting.wait_for(
                lock,
                std::chrono::milliseconds(timeout_ms),
                [this]() {
                    return queue_->closed || queue_->frames.empty() == false;
                });
        if (ready == false) {
            return 0;
        }
        if (queue_->frames.empty()) {
            return -1;
        }
        *gray = queue_->frames.front();
        queue_->frames.pop_front();
        return 1;
    }

private:
    std::shared_ptr<FrameQueue> queue_;
};

// ================================================================================

static cv::Mat camera_matrix(double focal, double cx, double cy) {
    return (cv::Mat_<double>(3, 3) << focal, 0, cx, 0, focal, cy, 0, 0, 1);
}

// ================================================================================

static cv::Mat render_view(
        const cv::Size &image,
        const cv::Mat &camera,
        const cv::Mat &distortion,
        const cv::Vec3d &rotation,
        double x_shift,
        double y_shift,
        double distance_mm) {
    const double square_mm = 25.0;
    const int squares_x = 10;
    const int squares_y = 7;
    const double board_w = squares_x * square_mm;
    const double board_h = squares_y * square_mm;
    cv::Mat rotation_matrix;
    cv::Rodrigues(rotation, rotation_matrix);
    cv::Mat center = (cv::Mat_<double>(3, 1) << board_w / 2.0, board_h / 2.0, 0.0);
    cv::Mat translation = -rotation_matrix * center;
    translation.at<double>(0) += x_shift;
    translation.at<double>(1) += y_shift;
    translation.at<double>(2) += distance_mm;

    const int scale = 4;
    cv::Mat camera_hi = camera.clone();
    camera_hi.at<double>(0, 0) *= scale;
    camera_hi.at<double>(1, 1) *= scale;
    camera_hi.at<double>(0, 2) *= scale;
    camera_hi.at<double>(1, 2) *= scale;
    const cv::Size canvas(image.width * scale, image.height * scale);

    std::vector<cv::Point3f> corners;
    corners.reserve(static_cast<std::size_t>((squares_x + 1) * (squares_y + 1)));
    for (int row = 0; row <= squares_y; ++row) {
        for (int col = 0; col <= squares_x; ++col) {
            corners.emplace_back(
                    static_cast<float>(col * square_mm),
                    static_cast<float>(row * square_mm),
                    0.0f);
        }
    }
    std::vector<cv::Point2f> projected;
    cv::projectPoints(corners, rotation, translation, camera_hi, distortion, projected);

    cv::Mat hi(canvas, CV_8UC1, cv::Scalar(255));
    const int stride = squares_x + 1;
    for (int row = 0; row < squares_y; ++row) {
        for (int col = 0; col < squares_x; ++col) {
            if (((col + row) % 2) != 0) {
                continue;
            }
            const int origin = row * stride + col;
            const std::vector<cv::Point> polygon = {
                    cv::Point(std::lround(projected[origin].x),
                            std::lround(projected[origin].y)),
                    cv::Point(std::lround(projected[origin + 1].x),
                            std::lround(projected[origin + 1].y)),
                    cv::Point(std::lround(projected[origin + stride + 1].x),
                            std::lround(projected[origin + stride + 1].y)),
                    cv::Point(std::lround(projected[origin + stride].x),
                            std::lround(projected[origin + stride].y)),
            };
            cv::fillConvexPoly(hi, polygon, cv::Scalar(0), cv::LINE_8);
        }
    }
    cv::Mat view;
    cv::resize(hi, view, image, 0, 0, cv::INTER_AREA);
    return view;
}

// ================================================================================

struct Pose {
    double rx;
    double ry;
    double rz;
    double x;
    double y;
    double z;
};

// ================================================================================

static std::vector<Pose> sample_poses() {
    return {
            {0.0, 0.0, 0.0, 0, 0, 620},
            {0.15, -0.1, 0.05, 20, -10, 700},
            {-0.12, 0.18, -0.04, -25, 15, 560},
            {0.05, 0.2, 0.1, 10, 20, 800},
            {-0.2, -0.05, 0.08, -15, -20, 640},
            {0.18, 0.08, -0.12, 30, 5, 900},
            {-0.08, 0.22, 0.0, -30, 10, 580},
            {0.1, -0.18, 0.15, 5, -25, 760},
            {-0.16, 0.04, -0.08, 15, 25, 680},
            {0.22, -0.12, 0.02, -10, 0, 840},
            {0.0, 0.25, 0.2, 0, -15, 600},
            {-0.05, -0.2, -0.1, 25, 12, 720},
    };
}

// ================================================================================

static bool near(double value, double want, double fraction) {
    return std::fabs(value - want) <= std::fabs(want) * fraction;
}

// ================================================================================

static bool command_has(
        const std::vector<std::string> &command,
        const std::string &item) {
    for (const std::string &part : command) {
        if (part == item) {
            return true;
        }
    }
    return false;
}

// ================================================================================

static bool command_contains(
        const std::vector<std::string> &command,
        const std::string &item) {
    for (const std::string &part : command) {
        if (part.find(item) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ================================================================================

static void test_grab_command() {
    const std::vector<std::string> command = calibration_grab_command(
            "/usr/bin/ffmpeg", "rtsp://127.0.0.1:8554/cam0", 1280, 480);
    expect(command_has(command, "-fps_mode") && command_has(command, "passthrough"),
           "calibration grab passes timestamps through");
    expect(command_has(command, "-skip_frame") && command_has(command, "nokey"),
           "calibration grab decodes keyframes only");
    expect(command_contains(command, "select=") == false
                   && command_contains(command, "scale=1280:480")
                   && command_contains(command, "fps=") == false,
           "calibration grab keeps every keyframe");
}

// ================================================================================

static void test_geometry() {
    expect(board_spec_ok(BoardSpec{9, 6, 25}) , "default board is accepted");
    expect(board_spec_ok(BoardSpec{3, 6, 25}) == false, "too few corners are rejected");
    expect(corners_still({{0, 0}, {10, 0}}, {{1, 0}, {11, 0}}, 2.0f), "small motion is still");
    expect(corners_still({{0, 0}}, {{5, 0}}, 2.0f) == false, "a jump is not still");
    expect(corners_still({{0, 0}}, {{8, 0}}, kCalibrationStillPx),
           "a held board may move several pixels");
    expect(corners_still({{0, 0}}, {{kCalibrationStillPx + 1.0f, 0}}, kCalibrationStillPx) == false,
           "a clear move is not still");

    EyeDetection eye;
    eye.found = true;
    eye.bounds = cv::Rect(10, 10, 40, 30);
    eye.angle_deg = 1;
    const std::string key = coverage_key(cv::Size(300, 300), eye);
    expect(key == "c0r0d0t0", "a small board in the corner is a far cell");
    eye.bounds = cv::Rect(20, 20, 200, 150);
    eye.angle_deg = 20;
    expect(coverage_key(cv::Size(300, 300), eye) == "c1r0d1t1", "a large tilted board is near");
    eye.bounds = cv::Rect(110, 110, 80, 60);
    eye.angle_deg = 180;
    expect(coverage_key(cv::Size(300, 300), eye) == "c1r1d0t0",
           "a reversed chessboard is straight");

    std::vector<std::string> keys = required_coverage_keys();
    keys.pop_back();
    expect(coverage_complete(keys) == false, "tilt views are still required");
    keys.push_back("c0r0d0t1");
    keys.push_back("c1r1d1t1");
    expect(coverage_complete(keys), "eighteen flat cells and two tilts finish the set");
    expect(next_coverage_key({}) == "c1r1d0t0", "the guide starts in the middle");
    expect(next_coverage_key({"c1r1d0t0"}) == "c1r0d0t0", "the guide moves out from the middle");
    const NormalizedBox middle = target_box(cv::Size(640, 480), "c1r1d0t0");
    expect(middle.x > 0.2 && middle.x + middle.width < 0.8,
           "the guide sits inside the middle of one eye");
    expect(middle.x >= 0.0 && middle.x + middle.width <= 1.0,
           "the guide stays inside one stereo half");

    const NormalizedBox box = target_box(cv::Size(300, 300), "tilt");
    expect(box.width > 0.4 && box.width < 0.6, "the tilt guide sits in the middle");
}

// ================================================================================

static std::vector<CalibrationSample> collect(
        const std::vector<cv::Mat> &frames,
        const BoardSpec &board,
        bool stereo) {
    std::vector<CalibrationSample> samples;
    for (const cv::Mat &frame : frames) {
        const FrameDetection detected = detect_frame(frame, board, stereo);
        if (detected.usable == false) {
            continue;
        }
        CalibrationSample sample;
        sample.left = detected.left.corners;
        sample.right = detected.right.corners;
        samples.push_back(std::move(sample));
    }
    return samples;
}

// ================================================================================

static void test_recovery() {
    const BoardSpec board{9, 6, 25};
    const cv::Mat distortion = (cv::Mat_<double>(1, 5) << -0.05, 0.01, 0, 0, 0);
    const cv::Mat left_camera = camera_matrix(800, 320, 240);
    std::vector<cv::Mat> mono;
    for (const Pose &pose : sample_poses()) {
        mono.push_back(render_view(
                cv::Size(640, 480),
                left_camera,
                distortion,
                cv::Vec3d(pose.rx, pose.ry, pose.rz),
                pose.x,
                pose.y,
                pose.z));
    }
    const std::vector<CalibrationSample> mono_samples = collect(mono, board, false);
    expect(mono_samples.size() >= 8, "the mono board is found in most views");
    const CalibrationModel mono_model = calibrate_samples(
            mono_samples, cv::Size(640, 480), board, false);
    if (mono_model.ok == false || near(mono_model.focal_px, 800, 0.01) == false) {
        std::cout << "mono focal " << mono_model.focal_px
                  << " rms " << mono_model.rms_left_px
                  << " samples " << mono_samples.size() << std::endl;
    }
    expect(mono_model.ok && near(mono_model.focal_px, 800, 0.01),
           "mono calibration recovers the focal length within 1 percent");

    const cv::Mat right_camera = camera_matrix(790, 330, 235);
    const cv::Mat right_distortion = (cv::Mat_<double>(1, 5) << -0.04, 0.0, 0, 0, 0);
    std::vector<cv::Mat> stereo;
    for (const Pose &pose : sample_poses()) {
        const cv::Vec3d rotation(pose.rx, pose.ry, pose.rz);
        const cv::Mat left = render_view(
                cv::Size(640, 480), left_camera, distortion,
                rotation, pose.x, pose.y, pose.z);
        const cv::Mat right = render_view(
                cv::Size(640, 480), right_camera, right_distortion,
                rotation, pose.x - 60, pose.y, pose.z);
        cv::Mat side;
        cv::hconcat(left, right, side);
        stereo.push_back(side);
    }
    const std::vector<CalibrationSample> stereo_samples = collect(stereo, board, true);
    expect(stereo_samples.size() >= 8, "both eyes see the board in most views");
    const CalibrationModel stereo_model = calibrate_samples(
            stereo_samples, cv::Size(1280, 480), board, true);
    if (stereo_model.ok == false
            || near(stereo_model.focal_px, 800, 0.01) == false
            || near(stereo_model.baseline_mm, 60, 2.0 / 60.0) == false) {
        std::cout << "stereo focal " << stereo_model.focal_px
                  << " baseline " << stereo_model.baseline_mm
                  << " rms " << stereo_model.rms_stereo_px
                  << " samples " << stereo_samples.size() << std::endl;
    }
    expect(stereo_model.ok
                    && near(stereo_model.focal_px, 800, 0.01)
                    && near(stereo_model.baseline_mm, 60, 2.0 / 60.0),
           "stereo calibration recovers focal length and a 60 mm baseline");

    const fs::path file = fs::temp_directory_path()
            / ("doggy-cal-" + std::to_string(getpid()) + ".yml");
    fs::remove(file);
    CalibrationModel stored = stereo_model;
    stored.stable_id = "usb-test";
    stored.rotation_deg = 180;
    expect(write_calibration_file(file.string(), stored), "calibration file is written");
    const CalibrationIdentity identity = read_calibration_identity(file.string());
    expect(calibration_identity_matches(
                   identity, true, 1280, 480, "usb-test", false, 180),
           "the saved identity matches the camera that produced it");
    expect(calibration_identity_matches(
                   identity, true, 1280, 480, "usb-other", false, 180) == false,
           "a different camera is stale");
    fs::remove(file);
}

// ================================================================================

static void test_image_ops() {
    cv::Mat disparity(32, 32, CV_16S, cv::Scalar(0));
    disparity.at<short>(16, 16) = 200;
    const cv::Mat cleaned = despeckle_disparity(disparity, 8, 20);
    expect(cleaned.at<short>(16, 16) == 0, "a one-pixel disparity speck is removed");

    cv::Mat salt(16, 16, CV_8UC1, cv::Scalar(0));
    salt.at<unsigned char>(8, 8) = 255;
    const cv::Mat smoothed = median_smooth(salt, 3);
    expect(smoothed.at<unsigned char>(8, 8) == 0, "median smooth removes salt");

    const cv::Mat denoised = denoise_gray(salt);
    expect(denoised.size() == salt.size() && denoised.type() == CV_8UC1,
           "gray denoise keeps the image shape");

    cv::Mat left(120, 160, CV_8UC1, cv::Scalar(180));
    cv::Mat right = left.clone();
    cv::rectangle(left, cv::Rect(40, 20, 8, 80), cv::Scalar(20), cv::FILLED);
    cv::rectangle(right, cv::Rect(48, 20, 8, 80), cv::Scalar(20), cv::FILLED);
    const cv::Mat depth = stereo_disparity(left, right);
    expect(depth.empty() == false && depth.rows == left.rows && depth.cols == left.cols,
           "stereo disparity and the smoother return a full map");
}

// ================================================================================

static nlohmann::json body_of(const httplib::Result &result) {
    if (result == nullptr) {
        return {};
    }
    return nlohmann::json::parse(result->body, nullptr, false);
}

// ================================================================================

static bool wait_for_count(httplib::Client &client, int count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline) {
        const nlohmann::json body = body_of(client.Get("/api/calibration"));
        if (body.is_object() && body.value("accepted", 0) >= count) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return false;
}

// ================================================================================

static void test_http() {
    const fs::path root = fs::temp_directory_path()
            / ("doggy-cal-http-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path result = root / "camera_calibration.yml";
    const fs::path index = root / "index.html";
    {
        std::ofstream page(index);
        page << "<!DOCTYPE html><title>cal</title>";
    }

    FakeRobot robot;
    Config config = robot.getConfig();
    for (doggy::v1::Camera &camera : *config.mutable_cameras()->mutable_items()) {
        camera.set_enabled(camera.id() == "cam0");
        if (camera.id() == "cam0") {
            camera.set_width(640);
            camera.set_height(480);
        }
    }
    robot.replaceConfig(config, "");
    CameraPipeline pipeline(
            std::make_unique<FakeCameraProcess>(),
            std::make_unique<FakeDiscovery>());
    CalibrationService service(CalibrationPaths{root.string(), result.string()});
    auto queue = std::make_shared<FrameQueue>();
    service.set_source_factory([queue](const LiveCameraChoice &) {
        return std::unique_ptr<CalibrationSource>(new QueueSource(queue));
    });

    WebServer server(robot, index.string(), "127.0.0.1", 0, &pipeline, nullptr, &service);
    expect(server.start(), "calibration server starts");
    httplib::Client client("127.0.0.1", server.port());
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(5, 0);

    config = robot.getConfig();
    for (doggy::v1::Camera &camera : *config.mutable_cameras()->mutable_items()) {
        camera.set_enabled(false);
    }
    robot.replaceConfig(config, "");
    auto missing = client.Post("/api/calibration", "{\"inner_cols\":9,\"inner_rows\":6,\"square_mm\":25}", "application/json");
    expect(missing && missing->status == 404, "calibration without a camera is 404");
    config = robot.getConfig();
    for (doggy::v1::Camera &camera : *config.mutable_cameras()->mutable_items()) {
        if (camera.id() == "cam0") {
            camera.set_enabled(true);
            camera.set_width(640);
            camera.set_height(480);
        }
    }
    robot.replaceConfig(config, "");

    auto bad = client.Post("/api/calibration", "{\"inner_cols\":2,\"inner_rows\":6,\"square_mm\":25}", "application/json");
    expect(bad && bad->status == 400, "a board that is too small is 400");

    auto started = client.Post(
            "/api/calibration",
            "{\"inner_cols\":9,\"inner_rows\":6,\"square_mm\":25}",
            "application/json");
    expect(started && started->status == 202, "calibration start is 202");
    auto again = client.Post(
            "/api/calibration",
            "{\"inner_cols\":9,\"inner_rows\":6,\"square_mm\":25}",
            "application/json");
    expect(again && again->status == 409, "a second calibration is 409");
    auto early = client.Post("/api/calibration/finish", "", "application/json");
    expect(early && early->status == 400, "finishing before 10 snapshots is 400");

    const cv::Mat camera = camera_matrix(800, 320, 240);
    const cv::Mat distortion = (cv::Mat_<double>(1, 5) << -0.05, 0.01, 0, 0, 0);
    int accepted = 0;
    for (const Pose &pose : sample_poses()) {
        if (accepted >= 10) {
            break;
        }
        cv::Mat view = render_view(
                cv::Size(640, 480), camera, distortion,
                cv::Vec3d(pose.rx, pose.ry, pose.rz), pose.x, pose.y, pose.z);
        if (detect_frame(view, BoardSpec{9, 6, 25}, false).usable == false) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            queue->frames.push_back(view);
            queue->waiting.notify_one();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto capture = client.Post("/api/calibration/capture", "", "application/json");
        expect(capture && capture->status == 202, "capture request is accepted");
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            queue->frames.push_back(view);
            queue->waiting.notify_one();
        }
        if (wait_for_count(client, accepted + 1) == false) {
            break;
        }
        accepted += 1;
    }
    expect(accepted >= 10, "ten still views are accepted");
    auto finish = client.Post("/api/calibration/finish", "", "application/json");
    expect(finish && finish->status == 202, "finish starts the solve");

    bool done = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        const nlohmann::json body = body_of(client.Get("/api/calibration"));
        const std::string state = body.value("state", "");
        if (state == "CALIBRATION_DONE") {
            done = true;
            break;
        }
        if (state == "CALIBRATION_FAILED") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    expect(done, "the solve finishes");
    auto saved = client.Post("/api/calibration/save", "", "application/json");
    expect(saved && saved->status == 200, "save is 200");
    auto download = client.Get("/api/calibration/result");
    expect(download && download->status == 200
                   && download->body.find("camera_matrix_left") != std::string::npos,
           "the calibration file downloads");
    auto cleared = client.Delete("/api/calibration");
    expect(cleared && cleared->status == 200, "discard returns to idle");
    auto still_there = client.Get("/api/calibration/result");
    expect(still_there && still_there->status == 200,
           "discard does not delete the saved file");

    {
        std::lock_guard<std::mutex> lock(queue->mutex);
        queue->closed = true;
        queue->waiting.notify_all();
    }
    server.stop();
    fs::remove_all(root);
}

// ================================================================================

int main() {
    const fs::path log_dir = fs::temp_directory_path()
            / ("doggy-cal-log-" + std::to_string(getpid()));
    fs::create_directories(log_dir);
    DoggyLogOptions options;
    options.directory = log_dir.string();
    options.roll_on_start = false;
    init_doggy_log(options);

    test_geometry();
    test_image_ops();
    test_grab_command();
    test_recovery();
    test_http();
    fs::remove_all(log_dir);
    return failures == 0 ? 0 : 1;
}
