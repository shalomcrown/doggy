#ifndef GPS_H
#define GPS_H

#include "doggy.pb.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ================================================================================

enum class GpsKind {
    automatic,
    nmea,
    ublox,
    septentrio,
    novatel,
};

// ================================================================================

struct GpsPortChoice {
    bool ok = false;
    std::string device;
    GpsKind kind = GpsKind::automatic;
    std::string error;
};

// ================================================================================

bool gps_device_path_allowed(const std::string &path);

// ================================================================================

GpsKind gps_kind_from_type(const std::string &type);

// ================================================================================

GpsPortChoice gps_choose_port(const std::vector<std::string> &by_id_names);

// ================================================================================

const char *gps_kind_label(GpsKind kind);

// ================================================================================

std::vector<std::string> gps_discovery_messages(
        const std::string &type,
        const std::vector<std::string> &by_id_names);

// ================================================================================

class GpsDecoder {
public:
    void set_kind(GpsKind kind);
    void feed(const uint8_t *data, std::size_t length, int64_t host_unix_ms);
    doggy::v1::Gps publish(int64_t now_unix_ms) const;

private:
    GpsKind kind_ = GpsKind::automatic;
    std::vector<uint8_t> buffer_;
    doggy::v1::Gps fix_;
    int64_t feed_unix_ms_ = 0;
    int64_t received_unix_ms_ = 0;
    int64_t binary_unix_ms_ = 0;
    int nmea_year_ = 0;
    int nmea_month_ = 0;
    int nmea_day_ = 0;

    bool accept_nmea() const;
    bool accept_ubx() const;
    bool accept_sbf() const;
    bool accept_novatel() const;
    bool consume();
    void note_sentence(int64_t host_unix_ms, bool binary);
    void parse_nmea(const std::string &sentence, int64_t host_unix_ms);
    void parse_ubx(const uint8_t *frame, std::size_t length, int64_t host_unix_ms);
    void parse_sbf(const uint8_t *frame, std::size_t length, int64_t host_unix_ms);
    void parse_novatel(const uint8_t *frame, std::size_t length, int64_t host_unix_ms);
};

// ================================================================================

void gps_service_apply(const doggy::v1::GpsConfig &config);

// ================================================================================

void gps_service_attach(doggy::v1::Status *status);

// ================================================================================

void gps_service_stop();

#endif
