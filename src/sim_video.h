#ifndef SIM_VIDEO_H
#define SIM_VIDEO_H

#include <cstdint>
#include <string>
#include <vector>

// Loopback MediaMTX for the host simulator. The Pi keeps its own ports and TLS.

inline constexpr int kSimVideoWidth = 320;
inline constexpr int kSimVideoHeight = 240;
inline constexpr int kSimVideoFps = 10;
inline constexpr int kSimRtspPort = 18554;
inline constexpr int kSimWebRtcPort = 18889;

// ================================================================================

struct SimVideoStream {
    std::string id;
    std::string name;
    std::string webrtc_url;
};

// ================================================================================

std::vector<std::string> sim_ffmpeg_publish_command(
        const std::string &ffmpeg,
        int width,
        int height,
        int fps,
        const std::string &rtsp_url);

// ================================================================================

std::string sim_rtsp_url(const std::string &camera_id);

// ================================================================================

std::string sim_webrtc_url(const std::string &camera_id);

// ================================================================================

class SimVideo {
public:
    SimVideo();
    ~SimVideo();

    bool start(const std::string &mediamtx_bin, const std::string &config_path);
    void stop();
    bool running() const;
    void submit(
            int camera,
            std::vector<std::uint8_t> rgb,
            std::uint32_t width,
            std::uint32_t height);
    std::vector<SimVideoStream> offers() const;

private:
    struct Impl;
    Impl *impl_;
};

#endif
