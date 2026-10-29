#include "calibration.h"

#include <plog/Log.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

// ================================================================================

bool board_spec_ok(const BoardSpec &board) {
    if (board.inner_cols < 4 || board.inner_cols > 20) {
        return false;
    }
    if (board.inner_rows < 4 || board.inner_rows > 20) {
        return false;
    }
    if (std::isfinite(board.square_mm) == false) {
        return false;
    }
    if (board.square_mm < 5.0 || board.square_mm > 100.0) {
        return false;
    }
    return true;
}

// ================================================================================

static std::vector<cv::Point3f> object_points(int cols, int rows, double square_mm) {
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<std::size_t>(cols * rows));
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            points.emplace_back(
                    static_cast<float>(col * square_mm),
                    static_cast<float>(row * square_mm),
                    0.0f);
        }
    }
    return points;
}

// ================================================================================

static bool find_pattern(
        const cv::Mat &gray,
        int cols,
        int rows,
        std::vector<cv::Point2f> *corners) {
    if (gray.empty() || gray.type() != CV_8UC1 || cols < 4 || rows < 4) {
        return false;
    }
    const cv::Size pattern(cols, rows);
    const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE;
    if (cv::findChessboardCorners(gray, pattern, *corners, flags) == false) {
        return false;
    }
    const cv::TermCriteria criteria(
            cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001);
    cv::cornerSubPix(gray, *corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);
    return corners->size() == static_cast<std::size_t>(cols * rows);
}

// ================================================================================

static EyeDetection detect_eye(const cv::Mat &gray, const BoardSpec &board) {
    EyeDetection eye;
    std::vector<cv::Point2f> corners;
    int cols = board.inner_cols;
    int rows = board.inner_rows;
    if (find_pattern(gray, cols, rows, &corners) == false) {
        if (cols == rows || find_pattern(gray, rows, cols, &corners) == false) {
            return eye;
        }
        cols = board.inner_rows;
        rows = board.inner_cols;
    }

    eye.found = true;
    eye.pattern_cols = cols;
    eye.pattern_rows = rows;
    eye.corners = std::move(corners);
    eye.bounds = cv::boundingRect(eye.corners);
    if (eye.pattern_cols >= 2) {
        const cv::Point2f &first = eye.corners.front();
        const cv::Point2f &last = eye.corners[static_cast<std::size_t>(eye.pattern_cols - 1)];
        eye.angle_deg = std::atan2(last.y - first.y, last.x - first.x) * 180.0 / CV_PI;
    }
    return eye;
}

// ================================================================================

FrameDetection detect_frame(
        const cv::Mat &gray,
        const BoardSpec &board,
        bool stereo) {
    FrameDetection frame;
    frame.stereo = stereo;
    if (gray.empty() || gray.type() != CV_8UC1) {
        return frame;
    }

    if (stereo == false) {
        frame.left = detect_eye(gray, board);
        frame.usable = frame.left.found;
        return frame;
    }

    if (gray.cols < 2 || (gray.cols % 2) != 0) {
        return frame;
    }
    const int half = gray.cols / 2;
    const cv::Mat left = gray(cv::Rect(0, 0, half, gray.rows));
    const cv::Mat right = gray(cv::Rect(half, 0, half, gray.rows));
    frame.left = detect_eye(left, board);
    frame.right = detect_eye(right, board);
    frame.usable = frame.left.found && frame.right.found
            && frame.left.corners.size() == frame.right.corners.size();
    return frame;
}

// ================================================================================

bool corners_still(
        const std::vector<cv::Point2f> &previous,
        const std::vector<cv::Point2f> &current,
        float max_px) {
    if (previous.empty() || previous.size() != current.size()) {
        return false;
    }
    if (max_px < 0.0f) {
        return false;
    }
    for (std::size_t i = 0; i < previous.size(); ++i) {
        if (cv::norm(previous[i] - current[i]) > max_px) {
            return false;
        }
    }
    return true;
}

// ================================================================================

static double axis_aligned_angle(double angle_deg) {
    double wrapped = std::fmod(angle_deg, 90.0);
    if (wrapped < 0.0) {
        wrapped += 90.0;
    }
    if (wrapped > 45.0) {
        wrapped -= 90.0;
    }
    return wrapped;
}

// ================================================================================

static std::string cell_key(int col, int row, int distance, int tilt) {
    std::ostringstream text;
    text << "c" << col << "r" << row << "d" << distance << "t" << tilt;
    return text.str();
}

// ================================================================================

std::string coverage_key(const cv::Size &frame, const EyeDetection &eye) {
    if (eye.found == false || frame.width <= 0 || frame.height <= 0) {
        return {};
    }
    const float cx = static_cast<float>(eye.bounds.x) + eye.bounds.width * 0.5f;
    const float cy = static_cast<float>(eye.bounds.y) + eye.bounds.height * 0.5f;
    int col = static_cast<int>(cx / static_cast<float>(frame.width) * 3.0f);
    int row = static_cast<int>(cy / static_cast<float>(frame.height) * 3.0f);
    if (col < 0) {
        col = 0;
    }
    if (row < 0) {
        row = 0;
    }
    if (col > 2) {
        col = 2;
    }
    if (row > 2) {
        row = 2;
    }
    const int distance = eye.bounds.width > frame.width * 0.34 ? 1 : 0;
    const int tilt = std::fabs(axis_aligned_angle(eye.angle_deg)) > 12.0 ? 1 : 0;
    return cell_key(col, row, distance, tilt);
}

// ================================================================================

static void append_center_out(std::vector<std::string> *keys, int distance) {
    const int cols[] = {1, 1, 0, 2, 1, 0, 2, 0, 2};
    const int rows[] = {1, 0, 1, 1, 2, 0, 0, 2, 2};
    for (int index = 0; index < 9; ++index) {
        keys->push_back(cell_key(cols[index], rows[index], distance, 0));
    }
}

// ================================================================================

std::vector<std::string> required_coverage_keys() {
    std::vector<std::string> keys;
    append_center_out(&keys, 0);
    append_center_out(&keys, 1);
    keys.emplace_back("tilt");
    return keys;
}

// ================================================================================

static int tilt_samples(const std::vector<std::string> &keys) {
    int count = 0;
    for (const std::string &key : keys) {
        if (key.size() >= 2 && key[key.size() - 2] == 't' && key.back() == '1') {
            count += 1;
        }
    }
    return count;
}

// ================================================================================

static bool has_key(const std::vector<std::string> &keys, const std::string &want) {
    for (const std::string &key : keys) {
        if (key == want) {
            return true;
        }
    }
    return false;
}

// ================================================================================

bool coverage_complete(const std::vector<std::string> &keys) {
    for (const std::string &required : required_coverage_keys()) {
        if (required == "tilt") {
            if (tilt_samples(keys) < 2) {
                return false;
            }
            continue;
        }
        if (has_key(keys, required) == false) {
            return false;
        }
    }
    return true;
}

// ================================================================================

std::string next_coverage_key(const std::vector<std::string> &keys) {
    for (const std::string &required : required_coverage_keys()) {
        if (required == "tilt") {
            if (tilt_samples(keys) < 2) {
                return "tilt";
            }
            continue;
        }
        if (has_key(keys, required) == false) {
            return required;
        }
    }
    return {};
}

// ================================================================================

static NormalizedBox box_from_pixels(const cv::Size &frame, cv::Rect rect) {
    NormalizedBox box;
    if (frame.width <= 0 || frame.height <= 0) {
        return box;
    }
    if (rect.x < 0) {
        rect.width += rect.x;
        rect.x = 0;
    }
    if (rect.y < 0) {
        rect.height += rect.y;
        rect.y = 0;
    }
    if (rect.x + rect.width > frame.width) {
        rect.width = frame.width - rect.x;
    }
    if (rect.y + rect.height > frame.height) {
        rect.height = frame.height - rect.y;
    }
    if (rect.width < 1) {
        rect.width = 1;
    }
    if (rect.height < 1) {
        rect.height = 1;
    }
    box.x = static_cast<double>(rect.x) / frame.width;
    box.y = static_cast<double>(rect.y) / frame.height;
    box.width = static_cast<double>(rect.width) / frame.width;
    box.height = static_cast<double>(rect.height) / frame.height;
    return box;
}

// ================================================================================

NormalizedBox target_box(const cv::Size &frame, const std::string &key) {
    if (key == "tilt") {
        const int width = frame.width / 2;
        const int height = frame.height / 2;
        return box_from_pixels(
                frame,
                cv::Rect((frame.width - width) / 2, (frame.height - height) / 2, width, height));
    }

    int col = 1;
    int row = 1;
    int distance = 0;
    if (key.size() >= 8 && key[0] == 'c' && key[2] == 'r' && key[4] == 'd') {
        col = key[1] - '0';
        row = key[3] - '0';
        distance = key[5] - '0';
    }
    if (col < 0 || col > 2) {
        col = 1;
    }
    if (row < 0 || row > 2) {
        row = 1;
    }

    const int cell_w = frame.width / 3 > 0 ? frame.width / 3 : 1;
    const int cell_h = frame.height / 3 > 0 ? frame.height / 3 : 1;
    if (distance == 0) {
        const int inset_x = cell_w / 10;
        const int inset_y = cell_h / 10;
        const int width = cell_w - 2 * inset_x > 0 ? cell_w - 2 * inset_x : 1;
        const int height = cell_h - 2 * inset_y > 0 ? cell_h - 2 * inset_y : 1;
        return box_from_pixels(
                frame,
                cv::Rect(col * cell_w + inset_x, row * cell_h + inset_y, width, height));
    }

    const int width = frame.width * 55 / 100 > 0 ? frame.width * 55 / 100 : 1;
    const int height = frame.height * 55 / 100 > 0 ? frame.height * 55 / 100 : 1;
    const int cx = col * cell_w + cell_w / 2;
    const int cy = row * cell_h + cell_h / 2;
    return box_from_pixels(frame, cv::Rect(cx - width / 2, cy - height / 2, width, height));
}

// ================================================================================

static bool calibrate_eye(
        const std::vector<std::vector<cv::Point3f>> &objects,
        const std::vector<std::vector<cv::Point2f>> &images,
        const cv::Size &eye,
        cv::Mat *camera,
        cv::Mat *distortion,
        double *rms) {
    cv::Mat rvecs;
    cv::Mat tvecs;
    const double error = cv::calibrateCamera(
            objects, images, eye, *camera, *distortion, rvecs, tvecs);
    if (std::isfinite(error) == false || camera->empty() || distortion->empty()) {
        return false;
    }
    *rms = error;
    return true;
}

// ================================================================================

CalibrationModel calibrate_samples(
        const std::vector<CalibrationSample> &samples,
        const cv::Size &frame,
        const BoardSpec &board,
        bool stereo) {
    CalibrationModel model;
    model.stereo = stereo;
    model.image_width = frame.width;
    model.image_height = frame.height;
    model.inner_cols = board.inner_cols;
    model.inner_rows = board.inner_rows;
    model.square_mm = board.square_mm;
    if (board_spec_ok(board) == false || frame.width < 16 || frame.height < 16) {
        return model;
    }
    if (stereo && (frame.width % 2) != 0) {
        return model;
    }

    const cv::Size eye = stereo
            ? cv::Size(frame.width / 2, frame.height)
            : frame;
    std::vector<std::vector<cv::Point3f>> objects;
    std::vector<std::vector<cv::Point2f>> left_points;
    std::vector<std::vector<cv::Point2f>> right_points;
    for (const CalibrationSample &sample : samples) {
        if (sample.left.size() < 16) {
            continue;
        }
        const int count = static_cast<int>(sample.left.size());
        int cols = board.inner_cols;
        int rows = board.inner_rows;
        if (count != cols * rows) {
            if (count != rows * cols) {
                continue;
            }
            cols = board.inner_rows;
            rows = board.inner_cols;
        }
        if (stereo && sample.right.size() != sample.left.size()) {
            continue;
        }
        objects.push_back(object_points(cols, rows, board.square_mm));
        left_points.push_back(sample.left);
        if (stereo) {
            right_points.push_back(sample.right);
        }
    }
    if (objects.size() < 3) {
        return model;
    }

    try {
        if (calibrate_eye(
                    objects,
                    left_points,
                    eye,
                    &model.camera_matrix_left,
                    &model.distortion_left,
                    &model.rms_left_px) == false) {
            return model;
        }
        model.focal_px = model.camera_matrix_left.at<double>(0, 0);
        if (stereo == false) {
            model.ok = true;
            return model;
        }

        if (calibrate_eye(
                    objects,
                    right_points,
                    eye,
                    &model.camera_matrix_right,
                    &model.distortion_right,
                    &model.rms_right_px) == false) {
            return model;
        }

        const cv::TermCriteria criteria(
                cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 50, 1e-6);
        model.rms_stereo_px = cv::stereoCalibrate(
                objects,
                left_points,
                right_points,
                model.camera_matrix_left,
                model.distortion_left,
                model.camera_matrix_right,
                model.distortion_right,
                eye,
                model.rotation,
                model.translation,
                model.essential,
                model.fundamental,
                cv::CALIB_FIX_INTRINSIC,
                criteria);
        cv::stereoRectify(
                model.camera_matrix_left,
                model.distortion_left,
                model.camera_matrix_right,
                model.distortion_right,
                eye,
                model.rotation,
                model.translation,
                model.rectify_left,
                model.rectify_right,
                model.projection_left,
                model.projection_right,
                model.disparity_to_depth,
                cv::CALIB_ZERO_DISPARITY,
                0,
                eye);
        model.baseline_mm = cv::norm(model.translation);
        model.focal_px = model.camera_matrix_left.at<double>(0, 0);
        model.ok = std::isfinite(model.rms_stereo_px);
    } catch (const cv::Exception &ex) {
        PLOG_WARNING << "calibration solve failed: " << ex.what();
        model.ok = false;
    }
    return model;
}

// ================================================================================

static void write_mat(cv::FileStorage &file, const char *name, const cv::Mat &value) {
    if (value.empty() == false) {
        file.write(name, value);
    }
}

// ================================================================================

bool write_calibration_file(const std::string &path, const CalibrationModel &model) {
    if (model.ok == false || path.empty()) {
        return false;
    }
    const fs::path parent = fs::path(path).parent_path();
    std::error_code error;
    if (parent.empty() == false) {
        fs::create_directories(parent, error);
    }
    if (error) {
        PLOG_WARNING << "calibration directory: " << error.message();
        return false;
    }

    const std::string temporary = path + ".tmp";
    cv::FileStorage file(temporary, cv::FileStorage::WRITE);
    if (file.isOpened() == false) {
        return false;
    }
    file << "stereo" << (model.stereo ? 1 : 0);
    file << "image_width" << model.image_width;
    file << "image_height" << model.image_height;
    file << "stable_id" << model.stable_id;
    file << "swap_halves" << (model.swap_halves ? 1 : 0);
    file << "rotation_deg" << model.rotation_deg;
    file << "inner_cols" << model.inner_cols;
    file << "inner_rows" << model.inner_rows;
    file << "square_mm" << model.square_mm;
    file << "rms_left_px" << model.rms_left_px;
    file << "rms_right_px" << model.rms_right_px;
    file << "rms_stereo_px" << model.rms_stereo_px;
    file << "focal_px" << model.focal_px;
    file << "baseline_mm" << model.baseline_mm;
    write_mat(file, "camera_matrix_left", model.camera_matrix_left);
    write_mat(file, "distortion_left", model.distortion_left);
    write_mat(file, "camera_matrix_right", model.camera_matrix_right);
    write_mat(file, "distortion_right", model.distortion_right);
    write_mat(file, "rotation", model.rotation);
    write_mat(file, "translation", model.translation);
    write_mat(file, "rectify_left", model.rectify_left);
    write_mat(file, "rectify_right", model.rectify_right);
    write_mat(file, "projection_left", model.projection_left);
    write_mat(file, "projection_right", model.projection_right);
    write_mat(file, "disparity_to_depth", model.disparity_to_depth);
    file.release();

    std::error_code rename_error;
    fs::rename(temporary, path, rename_error);
    if (rename_error) {
        fs::remove(path, rename_error);
        fs::rename(temporary, path, rename_error);
    }
    return rename_error.operator bool() == false && fs::is_regular_file(path);
}

// ================================================================================

CalibrationIdentity read_calibration_identity(const std::string &path) {
    CalibrationIdentity identity;
    std::error_code error;
    if (path.empty() || fs::is_regular_file(path, error) == false) {
        return identity;
    }
    cv::FileStorage file(path, cv::FileStorage::READ);
    if (file.isOpened() == false) {
        return identity;
    }
    identity.present = true;
    identity.stereo = static_cast<int>(file["stereo"]) != 0;
    identity.image_width = static_cast<int>(file["image_width"]);
    identity.image_height = static_cast<int>(file["image_height"]);
    identity.stable_id = static_cast<std::string>(file["stable_id"]);
    identity.swap_halves = static_cast<int>(file["swap_halves"]) != 0;
    identity.rotation_deg = static_cast<int>(file["rotation_deg"]);
    identity.rms_left_px = static_cast<double>(file["rms_left_px"]);
    identity.rms_right_px = static_cast<double>(file["rms_right_px"]);
    identity.rms_stereo_px = static_cast<double>(file["rms_stereo_px"]);
    identity.focal_px = static_cast<double>(file["focal_px"]);
    identity.baseline_mm = static_cast<double>(file["baseline_mm"]);
    return identity;
}

// ================================================================================

bool calibration_identity_matches(
        const CalibrationIdentity &saved,
        bool stereo,
        int width,
        int height,
        const std::string &stable_id,
        bool swap_halves,
        int rotation_deg) {
    if (saved.present == false) {
        return false;
    }
    return saved.stereo == stereo
            && saved.image_width == width
            && saved.image_height == height
            && saved.stable_id == stable_id
            && saved.swap_halves == swap_halves
            && saved.rotation_deg == rotation_deg;
}
