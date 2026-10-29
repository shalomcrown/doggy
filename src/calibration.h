#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <opencv2/core.hpp>

#include <string>
#include <vector>

inline constexpr float kCalibrationStillPx = 12.0f;
inline constexpr int kCalibrationMinimumSamples = 10;
inline constexpr int kCalibrationTargetSamples = 20;
inline constexpr int kCalibrationMaximumSamples = 40;

// ================================================================================

struct BoardSpec {
    int inner_cols = 9;
    int inner_rows = 6;
    double square_mm = 25.0;
};

// ================================================================================

struct NormalizedBox {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

// ================================================================================

struct EyeDetection {
    bool found = false;
    int pattern_cols = 0;
    int pattern_rows = 0;
    std::vector<cv::Point2f> corners;
    cv::Rect bounds;
    double angle_deg = 0;
};

// ================================================================================

struct FrameDetection {
    EyeDetection left;
    EyeDetection right;
    bool stereo = false;
    bool usable = false;
};

// ================================================================================

struct CalibrationSample {
    std::vector<cv::Point2f> left;
    std::vector<cv::Point2f> right;
};

// ================================================================================

struct CalibrationModel {
    bool ok = false;
    bool stereo = false;
    int image_width = 0;
    int image_height = 0;
    int inner_cols = 0;
    int inner_rows = 0;
    double square_mm = 0;
    std::string stable_id;
    bool swap_halves = false;
    int rotation_deg = 0;
    double rms_left_px = 0;
    double rms_right_px = 0;
    double rms_stereo_px = 0;
    double focal_px = 0;
    double baseline_mm = 0;
    bool coverage_complete = false;
    cv::Mat camera_matrix_left;
    cv::Mat distortion_left;
    cv::Mat camera_matrix_right;
    cv::Mat distortion_right;
    cv::Mat rotation;
    cv::Mat translation;
    cv::Mat essential;
    cv::Mat fundamental;
    cv::Mat rectify_left;
    cv::Mat rectify_right;
    cv::Mat projection_left;
    cv::Mat projection_right;
    cv::Mat disparity_to_depth;
};

// ================================================================================

struct CalibrationIdentity {
    bool present = false;
    bool stereo = false;
    int image_width = 0;
    int image_height = 0;
    std::string stable_id;
    bool swap_halves = false;
    int rotation_deg = 0;
    double rms_left_px = 0;
    double rms_right_px = 0;
    double rms_stereo_px = 0;
    double focal_px = 0;
    double baseline_mm = 0;
};

// ================================================================================

bool board_spec_ok(const BoardSpec &board);

// ================================================================================

FrameDetection detect_frame(
        const cv::Mat &gray,
        const BoardSpec &board,
        bool stereo);

// ================================================================================

bool corners_still(
        const std::vector<cv::Point2f> &previous,
        const std::vector<cv::Point2f> &current,
        float max_px);

// ================================================================================

std::string coverage_key(const cv::Size &frame, const EyeDetection &eye);

// ================================================================================

std::vector<std::string> required_coverage_keys();

// ================================================================================

bool coverage_complete(const std::vector<std::string> &keys);

// ================================================================================

std::string next_coverage_key(const std::vector<std::string> &keys);

// ================================================================================

NormalizedBox target_box(const cv::Size &frame, const std::string &key);

// ================================================================================

CalibrationModel calibrate_samples(
        const std::vector<CalibrationSample> &samples,
        const cv::Size &frame,
        const BoardSpec &board,
        bool stereo);

// ================================================================================

bool write_calibration_file(const std::string &path, const CalibrationModel &model);

// ================================================================================

CalibrationIdentity read_calibration_identity(const std::string &path);

// ================================================================================

bool calibration_identity_matches(
        const CalibrationIdentity &saved,
        bool stereo,
        int width,
        int height,
        const std::string &stable_id,
        bool swap_halves,
        int rotation_deg);

#endif
