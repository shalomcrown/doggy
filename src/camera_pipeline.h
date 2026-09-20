#ifndef CAMERA_PIPELINE_H
#define CAMERA_PIPELINE_H

#include "config.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// ================================================================================

struct CameraProcessSpec {
    std::vector<std::vector<std::string>> commands;
};

// ================================================================================

class CameraProcess {
public:
    virtual ~CameraProcess() = default;
    virtual bool start(const CameraProcessSpec &spec) = 0;
    virtual bool running() = 0;
    virtual void stop() = 0;
};

// ================================================================================

class CameraDiscovery {
public:
    virtual ~CameraDiscovery() = default;
    virtual bool rpiCameraAvailable() = 0;
    virtual std::optional<std::string> firstV4l2CaptureDevice() = 0;
};

// ================================================================================

enum class CameraResult {
    ok,
    not_found,
    failed
};

// ================================================================================

struct CameraQueryResult {
    CameraResult result = CameraResult::not_found;
    doggy::v1::CameraStreamList streams;
};

// ================================================================================

class CameraPipeline {
public:
    CameraPipeline();
    explicit CameraPipeline(std::unique_ptr<CameraProcess> process);
    CameraPipeline(
            std::unique_ptr<CameraProcess> process,
            std::unique_ptr<CameraDiscovery> discovery);
    ~CameraPipeline();

    CameraQueryResult query(const Config &config, const std::string &host);

private:
    CameraQueryResult streamsFor(
            const doggy::v1::Camera &camera, const std::string &host) const;

    std::unique_ptr<CameraProcess> process_;
    std::unique_ptr<CameraDiscovery> discovery_;
    std::string active_id_;
    std::string active_config_;
    std::mutex mutex_;
};

#endif
