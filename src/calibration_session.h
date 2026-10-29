#ifndef CALIBRATION_SESSION_H
#define CALIBRATION_SESSION_H

#include "calibration.h"
#include "camera_pipeline.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ================================================================================

class CalibrationSource {
public:
    virtual ~CalibrationSource() = default;
    // 1 a frame was written, 0 the wait timed out, -1 the source ended.
    virtual int next(cv::Mat *gray, int timeout_ms) = 0;
};

// ================================================================================

using CalibrationSourceFactory =
        std::function<std::unique_ptr<CalibrationSource>(const LiveCameraChoice &)>;

// ================================================================================

struct CalibrationReply {
    int status = 200;
    std::string body;
    std::string content_type = "application/json";
    std::string file_path;
};

// ================================================================================

struct CalibrationPaths {
    std::string snapshots_root;
    std::string result_path;
};

// ================================================================================

std::string calibration_snapshots_root();

// ================================================================================

std::string calibration_result_path();

// ================================================================================

std::vector<std::string> calibration_grab_command(
        const std::string &ffmpeg_path,
        const std::string &url,
        int width,
        int height);

// ================================================================================

class CalibrationService {
public:
    explicit CalibrationService(CalibrationPaths paths);
    ~CalibrationService();

    void set_source_factory(CalibrationSourceFactory factory);

    CalibrationReply begin(const std::string &body, const LiveCameraChoice &camera);
    CalibrationReply status(const LiveCameraChoice *camera) const;
    CalibrationReply capture();
    CalibrationReply finish();
    CalibrationReply cancel();
    CalibrationReply save();
    CalibrationReply download() const;

private:
    struct State;

    void run(std::unique_ptr<CalibrationSource> source);
    void join_worker();
    std::string status_json(const LiveCameraChoice *camera) const;
    void clear_session_dir();

    CalibrationPaths paths_;
    CalibrationSourceFactory factory_;
    std::unique_ptr<State> state_;
};

#endif
