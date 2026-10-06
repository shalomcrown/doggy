#include "sim_video.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static bool has_arg(const std::vector<std::string> &command, const std::string &arg) {
    for (const std::string &item : command) {
        if (item == arg) {
            return true;
        }
    }
    return false;
}

// ================================================================================

int main() {
    const std::string left = sim_rtsp_url("usb_left");
    const std::vector<std::string> command = sim_ffmpeg_publish_command(
            "/usr/bin/ffmpeg", kSimVideoWidth, kSimVideoHeight, kSimVideoFps, left);
    expect(command.empty() == false
                    && command.front() == "/usr/bin/ffmpeg"
                    && has_arg(command, "pipe:0")
                    && has_arg(command, "libx264")
                    && has_arg(command, "rgb24")
                    && command.back() == left
                    && left == "rtsp://127.0.0.1:18554/usb_left",
           "ffmpeg publishes loopback RTSP from a raw RGB pipe");

    expect(sim_ffmpeg_publish_command(
                   "/usr/bin/ffmpeg", 320, 240, 10, "rtsp://10.0.0.8/usb_left")
                    .empty(),
           "a non-loopback RTSP url is rejected");
    expect(sim_ffmpeg_publish_command(
                   "/usr/bin/ffmpeg", 320, 240, 10, "rtsp://127.0.0.1:18554/usb left")
                    .empty(),
           "an RTSP url with a space is rejected");
    expect(sim_webrtc_url("usb_right") == "http://127.0.0.1:18889/usb_right/whep",
           "the right camera uses loopback HTTP WHEP");
    expect(sim_webrtc_url("cam0").empty() && sim_rtsp_url("../usb_left").empty(),
           "unknown camera ids do not build urls");

#ifdef DOGGY_SIM_SOURCE_DIR
    const std::string path = std::string(DOGGY_SIM_SOURCE_DIR) + "/sim/mediamtx.yml";
    std::ifstream file(path);
    const std::string text(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    expect(text.find("webrtcEncryption: false") != std::string::npos
                    && text.find("127.0.0.1:18554") != std::string::npos
                    && text.find("127.0.0.1:18889") != std::string::npos
                    && text.find("127.0.0.1:18000") != std::string::npos
                    && text.find("127.0.0.1:18001") != std::string::npos
                    && text.find("usb_left") != std::string::npos
                    && text.find("usb_right") != std::string::npos
                    && text.find("record: false") != std::string::npos,
           "simulator MediaMTX is loopback HTTP and does not record");
#else
    expect(false, "simulator MediaMTX config path is compiled in");
#endif

    return failures == 0 ? 0 : 1;
}
