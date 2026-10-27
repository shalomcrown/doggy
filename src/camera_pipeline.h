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

struct V4l2FrameSize {
    int width = 0;
    int height = 0;
};

// ================================================================================

struct V4l2CaptureDevice {
    std::string device;
    std::string name;
    std::string stable_id;
    std::vector<V4l2FrameSize> mjpeg_sizes;
};

// ================================================================================

enum class LiveCameraKind {
    none = 0,
    stereo,
    rpi,
    usb
};

// ================================================================================

struct LiveCameraChoice {
    LiveCameraKind kind = LiveCameraKind::none;
    std::string device;
    std::string name;
    int width = 0;
    int height = 0;
    int fps = 0;
    int rotation_deg = 0;
    bool swap_halves = false;
};

// ================================================================================

std::optional<V4l2FrameSize> select_stereo_mjpeg_size(
        const std::vector<V4l2FrameSize> &sizes);

// ================================================================================

std::vector<LiveCameraChoice> rank_live_cameras(
        const std::vector<V4l2CaptureDevice> &devices,
        bool rpi_available,
        int configured_width,
        int configured_height,
        int configured_fps,
        int configured_rotation,
        bool swap_halves);

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
        std::string selection_key;
        std::string stream_name;
        std::string device;
        std::unique_ptr<CameraProcess> process;
    };

    std::vector<V4l2CaptureDevice> discoverV4l2Locked();

    std::string roleForStableIdLocked(
            const std::string &stable_id,
            const StereoBindings &bindings) const;

    void stopFeeder(const std::string &id);

    void stopFeedersLocked();

    void stopStereoFeedersLocked();

    std::unique_ptr<CameraProcess> makeProcess();

    std::function<std::unique_ptr<CameraProcess>()> process_factory_;
    std::unique_ptr<CameraDiscovery> discovery_;
    std::unordered_map<std::string, ActiveFeeder> feeders_;
    std::mutex mutex_;
};

#endif
