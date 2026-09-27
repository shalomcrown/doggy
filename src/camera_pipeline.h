#ifndef CAMERA_PIPELINE_H
#define CAMERA_PIPELINE_H

#include "config.h"
#include "stereo_bindings.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// ================================================================================

struct CameraProcessSpec {
    std::vector<std::vector<std::string>> commands;
};

// ================================================================================

struct V4l2CaptureDevice {
    std::string device;
    std::string name;
    std::string stable_id;
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
    virtual std::vector<V4l2CaptureDevice> listV4l2CaptureDevices() = 0;
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
    std::optional<StereoBindings> persist_stereo;
};

// ================================================================================

using CameraProcessFactory = std::function<std::unique_ptr<CameraProcess>()>;

// ================================================================================

class CameraPipeline {
public:
    CameraPipeline();
    explicit CameraPipeline(std::unique_ptr<CameraProcess> process);
    CameraPipeline(
            CameraProcessFactory factory,
            std::unique_ptr<CameraDiscovery> discovery);
    CameraPipeline(
            std::unique_ptr<CameraProcess> process,
            std::unique_ptr<CameraDiscovery> discovery);
    ~CameraPipeline();

    CameraQueryResult query(const Config &config, const std::string &host);

    doggy::v1::CameraDeviceList listDevices(const Config &config);

    void invalidateStereoFeeders();

    void stopAllFeeders();

private:
    struct ActiveFeeder {
        std::string config_key;
        std::string device;
        std::unique_ptr<CameraProcess> process;
    };

    std::vector<V4l2CaptureDevice> discoverV4l2Locked();

    StereoBindings prepareBindingsLocked(
            const Config &config,
            const std::vector<V4l2CaptureDevice> &devices,
            bool *changed);

    std::string roleForStableIdLocked(
            const std::string &stable_id,
            const StereoBindings &bindings) const;

    std::optional<std::string> deviceForStableId(
            const std::string &stable_id,
            const std::vector<V4l2CaptureDevice> &devices) const;

    bool resolveSourceForCamera(
            const doggy::v1::Camera &camera,
            const std::vector<V4l2CaptureDevice> &devices,
            const StereoBindings &bindings,
            bool rpi_reserved,
            bool rpi_available,
            std::string &source,
            std::string &device) const;

    CameraQueryResult streamsFor(
            const std::vector<const doggy::v1::Camera *> &cameras,
            const std::string &host,
            const std::vector<V4l2CaptureDevice> &devices,
            const StereoBindings &bindings,
            bool rpi_available);

    void stopFeeder(const std::string &id);

    void stopStereoFeedersLocked();

    std::unique_ptr<CameraProcess> makeProcess();

    std::function<std::unique_ptr<CameraProcess>()> process_factory_;
    std::unique_ptr<CameraDiscovery> discovery_;
    std::unordered_map<std::string, ActiveFeeder> feeders_;
    std::mutex mutex_;
};

#endif
