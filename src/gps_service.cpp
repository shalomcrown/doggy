#include "gps.h"

#include <plog/Log.h>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

static constexpr const char *kGpsByIdDir = "/dev/serial/by-id";

// ================================================================================

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 4800:
            return B4800;
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 921600:
            return B921600;
        case 115200:
        default:
            return B115200;
    }
}

// ================================================================================

class GpsService {
public:
    void apply(const doggy::v1::GpsConfig &config);
    void attach(doggy::v1::Status *status) const;
    void stop();

private:
    mutable std::mutex mutex_;
    doggy::v1::GpsConfig config_;
    GpsDecoder decoder_;
    std::string error_;
    std::string last_log_;
    std::jthread thread_;

    void run(std::stop_token stop);
    bool resolve(
            std::string *path,
            GpsKind *kind,
            std::string *error,
            std::vector<std::string> *notes) const;
    void log_lines(const std::vector<std::string> &lines);
};

// ================================================================================

static GpsService &gps_service() {
    static GpsService service;
    return service;
}

// ================================================================================

bool GpsService::resolve(
        std::string *path,
        GpsKind *kind,
        std::string *error,
        std::vector<std::string> *notes) const {
    notes->clear();
    if (config_.device().empty() == false) {
        *path = config_.device();
        *kind = gps_kind_from_type(config_.type());
        notes->push_back(
                "GPS configured device " + config_.device()
                + " type " + config_.type()
                + " baud " + std::to_string(config_.baud()));
        return true;
    }
    if (config_.type() != "auto"
            && config_.type() != "ublox"
            && config_.type() != "septentrio") {
        *error = "GPS device is not set";
        notes->push_back("GPS discovery: GPS device is not set");
        return false;
    }
    std::vector<std::string> names;
    std::error_code failure;
    for (const fs::directory_entry &entry : fs::directory_iterator(kGpsByIdDir, failure)) {
        names.push_back(entry.path().filename().string());
    }
    if (failure && names.empty()) {
        *error = "No GPS receiver found";
        notes->push_back(
                std::string("GPS scan ") + kGpsByIdDir + " failed: " + failure.message());
        return false;
    }
    *notes = gps_discovery_messages(config_.type(), names);
    const GpsPortChoice choice = gps_choose_port(names);
    if (choice.ok == false) {
        *error = choice.error;
        return false;
    }
    if (config_.type() == "ublox" && choice.kind != GpsKind::ublox) {
        *error = "No u-blox GPS receiver found";
        return false;
    }
    if (config_.type() == "septentrio" && choice.kind != GpsKind::septentrio) {
        *error = "No Septentrio GPS receiver found";
        return false;
    }
    *path = (fs::path(kGpsByIdDir) / choice.device).string();
    *kind = config_.type() == "auto" ? choice.kind : gps_kind_from_type(config_.type());
    return true;
}

// ================================================================================

static bool gps_log_is_warning(const std::string &line) {
    return line.rfind("GPS discovery:", 0) == 0
            || line.rfind("GPS open failed", 0) == 0
            || line.rfind("GPS receiver closed", 0) == 0
            || (line.rfind("GPS scan ", 0) == 0 && line.find(" failed:") != std::string::npos);
}

// ================================================================================

void GpsService::log_lines(const std::vector<std::string> &lines) {
    std::string joined;
    for (const std::string &line : lines) {
        if (joined.empty() == false) {
            joined.push_back('\n');
        }
        joined += line;
    }
    if (joined.empty() || joined == last_log_) {
        return;
    }
    last_log_ = joined;
    for (const std::string &line : lines) {
        if (gps_log_is_warning(line)) {
            PLOG_WARNING << line;
        } else {
            PLOG_INFO << line;
        }
    }
}

// ================================================================================

void GpsService::run(std::stop_token stop) {
    int fd = -1;
    while (stop.stop_requested() == false) {
        if (fd < 0) {
            std::string path;
            GpsKind kind = GpsKind::automatic;
            std::string error;
            std::vector<std::string> notes;
            if (resolve(&path, &kind, &error, &notes) == false) {
                std::lock_guard<std::mutex> lock(mutex_);
                error_ = error;
                log_lines(notes);
            } else {
                fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
                if (fd < 0) {
                    const int open_error = errno;
                    notes.push_back(
                            "GPS open failed " + path + ": " + std::strerror(open_error));
                    std::lock_guard<std::mutex> lock(mutex_);
                    error_ = "Could not open GPS receiver " + path;
                    log_lines(notes);
                } else {
                    termios tty{};
                    if (tcgetattr(fd, &tty) != 0) {
                        const int tty_error = errno;
                        close(fd);
                        fd = -1;
                        notes.push_back(
                                "GPS open failed " + path + ": " + std::strerror(tty_error));
                        std::lock_guard<std::mutex> lock(mutex_);
                        error_ = "Could not open GPS receiver " + path;
                        log_lines(notes);
                    } else {
                        cfmakeraw(&tty);
                        const speed_t speed = baud_to_speed(config_.baud());
                        cfsetispeed(&tty, speed);
                        cfsetospeed(&tty, speed);
                        tty.c_cflag |= CLOCAL | CREAD;
                        tcsetattr(fd, TCSANOW, &tty);
                        notes.push_back(
                                "GPS open " + path + " baud " + std::to_string(config_.baud())
                                + " kind " + gps_kind_label(kind));
                        std::lock_guard<std::mutex> lock(mutex_);
                        decoder_.set_kind(kind);
                        error_.clear();
                        log_lines(notes);
                        last_log_.clear();
                    }
                }
            }
        }
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        pollfd poll_fd{};
        poll_fd.fd = fd;
        poll_fd.events = POLLIN;
        const int ready = poll(&poll_fd, 1, 200);
        if (ready < 0) {
            continue;
        }
        if (ready == 0) {
            continue;
        }
        uint8_t bytes[512];
        const ssize_t count = read(fd, bytes, sizeof(bytes));
        if (count < 0) {
            continue;
        }
        if (count == 0) {
            close(fd);
            fd = -1;
            log_lines({"GPS receiver closed"});
            continue;
        }
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(mutex_);
        decoder_.feed(bytes, static_cast<std::size_t>(count), now);
    }
    if (fd >= 0) {
        close(fd);
    }
}

// ================================================================================

void GpsService::apply(const doggy::v1::GpsConfig &config) {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    decoder_ = GpsDecoder();
    error_.clear();
    last_log_.clear();
    if (config.enabled() == false) {
        PLOG_INFO << "GPS disabled";
        return;
    }
    const std::string device = config.device().empty() ? "(scan)" : config.device();
    PLOG_INFO << "GPS enabled type " << config.type()
              << " device " << device
              << " baud " << config.baud();
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

// ================================================================================

void GpsService::attach(doggy::v1::Status *status) const {
    if (status == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    if (config_.enabled() == false) {
        doggy::v1::Gps *gps = status->mutable_gps();
        gps->set_ok(false);
        gps->set_fix_type(doggy::v1::GPS_FIX_NO_GPS);
        gps->set_spoofing(doggy::v1::GPS_SPOOF_UNKNOWN);
        gps->set_jamming(doggy::v1::GPS_JAM_UNKNOWN);
        return;
    }
    *status->mutable_gps() = decoder_.publish(now);
    if (error_.empty() == false) {
        doggy::v1::Error *error = status->add_errors();
        error->set_code(doggy::v1::gps);
        error->set_message(error_);
    }
}

// ================================================================================

void GpsService::stop() {
    thread_ = std::jthread();
}

// ================================================================================

void gps_service_apply(const doggy::v1::GpsConfig &config) {
    gps_service().apply(config);
}

// ================================================================================

void gps_service_attach(doggy::v1::Status *status) {
    gps_service().attach(status);
}

// ================================================================================

void gps_service_stop() {
    gps_service().stop();
}
