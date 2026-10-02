#include "gps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>

static constexpr const char *kGpsByIdDir = "/dev/serial/by-id";
static constexpr int64_t kGpsStaleMs = 5000;
static constexpr int64_t kBinaryHoldsNmeaMs = 2000;
static constexpr std::size_t kGpsBufferLimit = 4096;

// ================================================================================

static uint16_t read_u16(const uint8_t *data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

// ================================================================================

static uint32_t read_u32(const uint8_t *data) {
    return static_cast<uint32_t>(data[0])
            | (static_cast<uint32_t>(data[1]) << 8)
            | (static_cast<uint32_t>(data[2]) << 16)
            | (static_cast<uint32_t>(data[3]) << 24);
}

// ================================================================================

static int32_t read_i32(const uint8_t *data) {
    return static_cast<int32_t>(read_u32(data));
}

// ================================================================================

static float read_f32(const uint8_t *data) {
    float value = 0.0f;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

// ================================================================================

static double read_f64(const uint8_t *data) {
    double value = 0.0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

// ================================================================================

static bool is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

// ================================================================================

static int64_t unix_ms_from_civil(
        int year,
        int month,
        int day,
        int hour,
        int minute,
        int second,
        int millisecond) {
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned month_index = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153 * month_index + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097
            + static_cast<int64_t>(doe) - 719468;
    return days * 86400000LL
            + static_cast<int64_t>(hour) * 3600000LL
            + static_cast<int64_t>(minute) * 60000LL
            + static_cast<int64_t>(second) * 1000LL
            + millisecond;
}

// ================================================================================

static doggy::v1::GpsFixType nmea_quality(int quality) {
    switch (quality) {
        case 1:
        case 3:
        case 6:
        case 7:
        case 8:
            return doggy::v1::GPS_FIX_3D;
        case 2:
        case 9:
            return doggy::v1::GPS_FIX_DGPS;
        case 4:
            return doggy::v1::GPS_FIX_RTK_FIXED;
        case 5:
            return doggy::v1::GPS_FIX_RTK_FLOAT;
        default:
            return doggy::v1::GPS_FIX_NO_FIX;
    }
}

// ================================================================================

static doggy::v1::GpsFixType ubx_fix(uint8_t fix_type, uint8_t flags) {
    const unsigned carrier = (flags >> 6) & 0x3u;
    if (carrier == 2) {
        return doggy::v1::GPS_FIX_RTK_FIXED;
    }
    if (carrier == 1) {
        return doggy::v1::GPS_FIX_RTK_FLOAT;
    }
    switch (fix_type) {
        case 2:
            return doggy::v1::GPS_FIX_2D;
        case 3:
        case 4:
            return doggy::v1::GPS_FIX_3D;
        default:
            return doggy::v1::GPS_FIX_NO_FIX;
    }
}

// ================================================================================

static doggy::v1::GpsFixType sbf_mode(uint8_t mode) {
    switch (mode & 0x0f) {
        case 1:
            return doggy::v1::GPS_FIX_3D;
        case 2:
        case 6:
            return doggy::v1::GPS_FIX_DGPS;
        case 3:
            return doggy::v1::GPS_FIX_STATIC;
        case 4:
        case 7:
            return doggy::v1::GPS_FIX_RTK_FIXED;
        case 5:
        case 8:
            return doggy::v1::GPS_FIX_RTK_FLOAT;
        case 10:
            return doggy::v1::GPS_FIX_PPP;
        default:
            return doggy::v1::GPS_FIX_NO_FIX;
    }
}

// ================================================================================

static doggy::v1::GpsFixType novatel_type(uint32_t pos_type) {
    switch (pos_type) {
        case 1:
            return doggy::v1::GPS_FIX_STATIC;
        case 16:
            return doggy::v1::GPS_FIX_3D;
        case 17:
        case 18:
            return doggy::v1::GPS_FIX_DGPS;
        case 32:
        case 33:
        case 34:
        case 68:
        case 77:
            return doggy::v1::GPS_FIX_RTK_FLOAT;
        case 48:
        case 49:
        case 50:
            return doggy::v1::GPS_FIX_RTK_FIXED;
        case 69:
        case 78:
            return doggy::v1::GPS_FIX_PPP;
        default:
            return doggy::v1::GPS_FIX_NO_FIX;
    }
}

// ================================================================================

static doggy::v1::GpsSpoofing ubx_spoof(uint8_t flags2) {
    switch ((flags2 >> 3) & 0x3u) {
        case 1:
            return doggy::v1::GPS_SPOOF_NONE;
        case 2:
            return doggy::v1::GPS_SPOOF_INDICATED;
        case 3:
            return doggy::v1::GPS_SPOOF_MULTIPLE;
        default:
            return doggy::v1::GPS_SPOOF_UNKNOWN;
    }
}

// ================================================================================

static doggy::v1::GpsJamming ubx_jam(uint8_t flags) {
    switch (flags & 0x3u) {
        case 1:
            return doggy::v1::GPS_JAM_OK;
        case 2:
            return doggy::v1::GPS_JAM_WARNING;
        case 3:
            return doggy::v1::GPS_JAM_CRITICAL;
        default:
            return doggy::v1::GPS_JAM_UNKNOWN;
    }
}

// ================================================================================

static bool nmea_checksum_ok(const std::string &sentence) {
    if (sentence.size() < 4 || sentence.front() != '$') {
        return false;
    }
    const std::size_t star = sentence.find('*');
    if (star == std::string::npos || star + 3 > sentence.size()) {
        return false;
    }
    unsigned expected = 0;
    if (std::sscanf(sentence.c_str() + star + 1, "%2x", &expected) != 1) {
        return false;
    }
    unsigned value = 0;
    for (std::size_t i = 1; i < star; ++i) {
        value ^= static_cast<unsigned char>(sentence[i]);
    }
    return value == expected;
}

// ================================================================================

static std::vector<std::string> nmea_fields(const std::string &sentence) {
    const std::size_t star = sentence.find('*');
    const std::string body = sentence.substr(1, star - 1);
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= body.size()) {
        const std::size_t comma = body.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(body.substr(start));
            break;
        }
        fields.push_back(body.substr(start, comma - start));
        start = comma + 1;
    }
    return fields;
}

// ================================================================================

static bool nmea_angle(
        const std::string &field,
        const std::string &hemisphere,
        double *degrees) {
    const std::size_t dot = field.find('.');
    if (dot == std::string::npos || dot < 2) {
        return false;
    }
    try {
        const double whole = std::stod(field.substr(0, dot - 2));
        const double minutes = std::stod(field.substr(dot - 2));
        double value = whole + minutes / 60.0;
        if (hemisphere == "S" || hemisphere == "W") {
            value = -value;
        }
        *degrees = value;
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// ================================================================================

static bool nmea_time(const std::string &field, int *hour, int *minute, int *second, int *millisecond) {
    if (field.size() < 6) {
        return false;
    }
    try {
        *hour = std::stoi(field.substr(0, 2));
        *minute = std::stoi(field.substr(2, 2));
        const double seconds = std::stod(field.substr(4));
        *second = static_cast<int>(seconds);
        *millisecond = static_cast<int>(std::llround((seconds - *second) * 1000.0));
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// ================================================================================

static bool nmea_date(const std::string &field, int *year, int *month, int *day) {
    if (field.size() != 6) {
        return false;
    }
    for (char ch : field) {
        if (is_digit(ch) == false) {
            return false;
        }
    }
    *day = std::stoi(field.substr(0, 2));
    *month = std::stoi(field.substr(2, 2));
    const int year_part = std::stoi(field.substr(4, 2));
    *year = year_part >= 80 ? 1900 + year_part : 2000 + year_part;
    return true;
}

// ================================================================================

static bool ends_with_gga(const std::string &name) {
    return name.size() >= 3 && name.compare(name.size() - 3, 3, "GGA") == 0;
}

// ================================================================================

static bool ends_with_rmc(const std::string &name) {
    return name.size() >= 3 && name.compare(name.size() - 3, 3, "RMC") == 0;
}

// ================================================================================

static bool ends_with_gst(const std::string &name) {
    return name.size() >= 3 && name.compare(name.size() - 3, 3, "GST") == 0;
}

// ================================================================================

static uint16_t sbf_crc(const uint8_t *data, std::size_t length) {
    uint16_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

// ================================================================================

static uint32_t novatel_crc(const uint8_t *data, std::size_t length) {
    uint32_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1u) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ================================================================================

static bool ubx_checksum_ok(const uint8_t *frame, std::size_t length) {
    if (length < 8) {
        return false;
    }
    unsigned ck_a = 0;
    unsigned ck_b = 0;
    for (std::size_t i = 2; i + 2 < length; ++i) {
        ck_a = (ck_a + frame[i]) & 0xffu;
        ck_b = (ck_b + ck_a) & 0xffu;
    }
    return frame[length - 2] == ck_a && frame[length - 1] == ck_b;
}

// ================================================================================

static int by_id_interface(const std::string &name) {
    const std::size_t pos = name.rfind("-if");
    if (pos == std::string::npos || pos + 3 >= name.size()) {
        return -1;
    }
    int value = 0;
    for (std::size_t i = pos + 3; i < name.size(); ++i) {
        if (is_digit(name[i]) == false) {
            return -1;
        }
        value = value * 10 + (name[i] - '0');
    }
    return value;
}

// ================================================================================

static std::string by_id_group(const std::string &name) {
    if (by_id_interface(name) < 0) {
        return name;
    }
    return name.substr(0, name.rfind("-if"));
}

// ================================================================================

bool gps_device_path_allowed(const std::string &path) {
    if (path.empty()) {
        return true;
    }
    const char *prefix = nullptr;
    if (path.rfind("/dev/ttyACM", 0) == 0) {
        prefix = "/dev/ttyACM";
    } else if (path.rfind("/dev/ttyUSB", 0) == 0) {
        prefix = "/dev/ttyUSB";
    } else if (path.rfind("/dev/ttyS", 0) == 0) {
        prefix = "/dev/ttyS";
    } else if (path.rfind("/dev/serial/by-id/", 0) == 0) {
        prefix = "/dev/serial/by-id/";
    }
    if (prefix == nullptr || path.find("..") != std::string::npos) {
        return false;
    }
    const std::string rest = path.substr(std::strlen(prefix));
    if (rest.empty() || rest.find('/') != std::string::npos) {
        return false;
    }
    for (char ch : path) {
        const unsigned char value = static_cast<unsigned char>(ch);
        const bool ok = std::isalnum(value) != 0 || ch == '.' || ch == '_'
                || ch == ':' || ch == '/' || ch == '-';
        if (ok == false) {
            return false;
        }
    }
    return true;
}

// ================================================================================

GpsKind gps_kind_from_type(const std::string &type) {
    if (type == "nmea") {
        return GpsKind::nmea;
    }
    if (type == "ublox") {
        return GpsKind::ublox;
    }
    if (type == "septentrio") {
        return GpsKind::septentrio;
    }
    if (type == "novatel") {
        return GpsKind::novatel;
    }
    return GpsKind::automatic;
}

// ================================================================================

GpsPortChoice gps_choose_port(const std::vector<std::string> &by_id_names) {
    struct Candidate {
        std::string name;
        GpsKind kind;
        int interface_number;
        std::string group;
    };
    std::vector<Candidate> found;
    for (const std::string &name : by_id_names) {
        GpsKind kind = GpsKind::automatic;
        if (name.find("Septentrio") != std::string::npos) {
            kind = GpsKind::septentrio;
        } else if (name.find("u-blox") != std::string::npos) {
            kind = GpsKind::ublox;
        } else {
            continue;
        }
        found.push_back(Candidate{name, kind, by_id_interface(name), by_id_group(name)});
    }
    GpsPortChoice choice;
    if (found.empty()) {
        choice.error = "No GPS receiver found";
        return choice;
    }
    const std::string group = found.front().group;
    const GpsKind kind = found.front().kind;
    for (const Candidate &candidate : found) {
        if (candidate.group != group || candidate.kind != kind) {
            choice.error = "More than one GPS receiver is attached";
            return choice;
        }
    }
    const int preferred = kind == GpsKind::septentrio ? 2 : 0;
    const Candidate *selected = &found.front();
    for (const Candidate &candidate : found) {
        if (candidate.interface_number == preferred) {
            selected = &candidate;
            break;
        }
        if (selected->interface_number < 0
                || (candidate.interface_number >= 0
                        && candidate.interface_number < selected->interface_number)) {
            selected = &candidate;
        }
    }
    choice.ok = true;
    choice.device = selected->name;
    choice.kind = kind;
    return choice;
}

// ================================================================================

const char *gps_kind_label(GpsKind kind) {
    switch (kind) {
        case GpsKind::nmea:
            return "nmea";
        case GpsKind::ublox:
            return "ublox";
        case GpsKind::septentrio:
            return "septentrio";
        case GpsKind::novatel:
            return "novatel";
        case GpsKind::automatic:
            return "auto";
    }
    return "auto";
}

// ================================================================================

std::vector<std::string> gps_discovery_messages(
        const std::string &type,
        const std::vector<std::string> &by_id_names) {
    std::vector<std::string> lines;
    lines.push_back(std::string("GPS scan ") + kGpsByIdDir);
    if (by_id_names.empty()) {
        lines.push_back("GPS discovery: No GPS receiver found");
        return lines;
    }
    for (const std::string &name : by_id_names) {
        lines.push_back("GPS by-id " + name);
    }
    const GpsPortChoice choice = gps_choose_port(by_id_names);
    if (choice.ok == false) {
        lines.push_back("GPS discovery: " + choice.error);
        return lines;
    }
    if (type == "ublox" && choice.kind != GpsKind::ublox) {
        lines.push_back("GPS discovery: No u-blox GPS receiver found");
        return lines;
    }
    if (type == "septentrio" && choice.kind != GpsKind::septentrio) {
        lines.push_back("GPS discovery: No Septentrio GPS receiver found");
        return lines;
    }
    const GpsKind kind = type == "auto" ? choice.kind : gps_kind_from_type(type);
    lines.push_back(
            std::string("GPS selected ") + kGpsByIdDir + "/" + choice.device
            + " kind " + gps_kind_label(kind));
    return lines;
}

// ================================================================================

void GpsDecoder::set_kind(GpsKind kind) {
    kind_ = kind;
}

// ================================================================================

bool GpsDecoder::accept_nmea() const {
    return true;
}

// ================================================================================

bool GpsDecoder::accept_ubx() const {
    return kind_ == GpsKind::automatic || kind_ == GpsKind::ublox;
}

// ================================================================================

bool GpsDecoder::accept_sbf() const {
    return kind_ == GpsKind::automatic || kind_ == GpsKind::septentrio;
}

// ================================================================================

bool GpsDecoder::accept_novatel() const {
    return kind_ == GpsKind::automatic || kind_ == GpsKind::novatel;
}

// ================================================================================

void GpsDecoder::note_sentence(int64_t host_unix_ms, bool binary) {
    received_unix_ms_ = host_unix_ms;
    fix_.set_ok(true);
    if (binary) {
        binary_unix_ms_ = host_unix_ms;
    }
}

// ================================================================================

void GpsDecoder::parse_nmea(const std::string &sentence, int64_t host_unix_ms) {
    if (nmea_checksum_ok(sentence) == false) {
        return;
    }
    const std::vector<std::string> fields = nmea_fields(sentence);
    if (fields.empty()) {
        return;
    }
    if (ends_with_rmc(fields[0]) && fields.size() > 9) {
        int year = 0;
        int month = 0;
        int day = 0;
        if (nmea_date(fields[9], &year, &month, &day)) {
            nmea_year_ = year;
            nmea_month_ = month;
            nmea_day_ = day;
        }
        return;
    }
    if (ends_with_gst(fields[0]) && fields.size() > 7) {
        try {
            const double lat_std = std::stod(fields[6]);
            const double lon_std = std::stod(fields[7]);
            fix_.set_accuracy_m(std::hypot(lat_std, lon_std));
            note_sentence(host_unix_ms, false);
        } catch (const std::exception &) {
        }
        return;
    }
    if (ends_with_gga(fields[0]) == false || fields.size() < 10) {
        return;
    }
    if (binary_unix_ms_ != 0 && host_unix_ms - binary_unix_ms_ < kBinaryHoldsNmeaMs) {
        return;
    }
    int quality = 0;
    try {
        quality = std::stoi(fields[6]);
    } catch (const std::exception &) {
        return;
    }
    const doggy::v1::GpsFixType fix_type = nmea_quality(quality);
    note_sentence(host_unix_ms, false);
    fix_.set_fix_type(fix_type);
    if (fix_type == doggy::v1::GPS_FIX_NO_FIX) {
        fix_.clear_latitude_deg();
        fix_.clear_longitude_deg();
        fix_.clear_altitude_amsl_m();
        fix_.clear_satellites();
        fix_.set_fix_unix_ms(host_unix_ms);
        return;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    if (nmea_angle(fields[2], fields[3], &latitude) == false
            || nmea_angle(fields[4], fields[5], &longitude) == false) {
        return;
    }
    try {
        fix_.set_latitude_deg(latitude);
        fix_.set_longitude_deg(longitude);
        fix_.set_altitude_amsl_m(std::stod(fields[9]));
        fix_.set_satellites(std::stoi(fields[7]));
    } catch (const std::exception &) {
        return;
    }
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    if (nmea_year_ != 0 && nmea_time(fields[1], &hour, &minute, &second, &millisecond)) {
        fix_.set_fix_unix_ms(unix_ms_from_civil(
                nmea_year_, nmea_month_, nmea_day_, hour, minute, second, millisecond));
    } else {
        fix_.set_fix_unix_ms(host_unix_ms);
    }
}

// ================================================================================

void GpsDecoder::parse_ubx(const uint8_t *frame, std::size_t length, int64_t host_unix_ms) {
    if (ubx_checksum_ok(frame, length) == false || length < 8) {
        return;
    }
    const uint8_t message_class = frame[2];
    const uint8_t message_id = frame[3];
    const uint8_t *payload = frame + 6;
    const std::size_t payload_length = length - 8;
    if (message_class == 0x01 && message_id == 0x07 && payload_length >= 44) {
        const doggy::v1::GpsFixType fix_type = ubx_fix(payload[20], payload[21]);
        note_sentence(host_unix_ms, true);
        fix_.set_fix_type(fix_type);
        fix_.set_satellites(payload[23]);
        fix_.set_accuracy_m(static_cast<double>(read_u32(payload + 40)) / 1000.0);
        if (fix_type == doggy::v1::GPS_FIX_NO_FIX) {
            fix_.clear_latitude_deg();
            fix_.clear_longitude_deg();
            fix_.clear_altitude_amsl_m();
        } else {
            fix_.set_longitude_deg(static_cast<double>(read_i32(payload + 24)) / 1.0e7);
            fix_.set_latitude_deg(static_cast<double>(read_i32(payload + 28)) / 1.0e7);
            fix_.set_altitude_amsl_m(static_cast<double>(read_i32(payload + 36)) / 1000.0);
        }
        const uint8_t valid = payload[11];
        if ((valid & 0x03) == 0x03 && payload_length >= 20) {
            const int year = read_u16(payload + 4);
            const int month = payload[6];
            const int day = payload[7];
            const int hour = payload[8];
            const int minute = payload[9];
            const int second = payload[10];
            int millisecond = read_i32(payload + 16) / 1000000;
            int adjusted_second = second;
            if (millisecond < 0) {
                adjusted_second -= 1;
                millisecond += 1000;
            }
            fix_.set_fix_unix_ms(unix_ms_from_civil(
                    year, month, day, hour, minute, adjusted_second, millisecond));
        } else {
            fix_.set_fix_unix_ms(host_unix_ms);
        }
        return;
    }
    if (message_class == 0x01 && message_id == 0x03 && payload_length >= 8) {
        fix_.set_spoofing(ubx_spoof(payload[7]));
        note_sentence(host_unix_ms, false);
        return;
    }
    if (message_class == 0x0a && message_id == 0x38 && payload_length >= 8) {
        const unsigned blocks = payload[1];
        doggy::v1::GpsJamming worst = doggy::v1::GPS_JAM_UNKNOWN;
        for (unsigned block = 0; block < blocks; ++block) {
            const std::size_t offset = 4 + static_cast<std::size_t>(block) * 24;
            if (offset + 2 > payload_length) {
                break;
            }
            const doggy::v1::GpsJamming state = ubx_jam(payload[offset + 1]);
            if (state > worst) {
                worst = state;
            }
        }
        fix_.set_jamming(worst);
        note_sentence(host_unix_ms, false);
    }
}

// ================================================================================

void GpsDecoder::parse_sbf(const uint8_t *frame, std::size_t length, int64_t host_unix_ms) {
    if (length < 8 || sbf_crc(frame + 4, length - 4) != read_u16(frame + 2)) {
        return;
    }
    const unsigned block = read_u16(frame + 4) & 0x1fffu;
    if (block == 4007 && length >= 92) {
        const doggy::v1::GpsFixType fix_type = frame[15] == 0
                ? sbf_mode(frame[14])
                : doggy::v1::GPS_FIX_NO_FIX;
        note_sentence(host_unix_ms, true);
        fix_.set_fix_type(fix_type);
        fix_.set_fix_unix_ms(host_unix_ms);
        fix_.set_satellites(frame[74]);
        fix_.set_accuracy_m(static_cast<double>(read_u16(frame + 90)) / 100.0);
        if (fix_type == doggy::v1::GPS_FIX_NO_FIX) {
            fix_.clear_latitude_deg();
            fix_.clear_longitude_deg();
            fix_.clear_altitude_amsl_m();
            return;
        }
        constexpr double kRadToDeg = 57.29577951308232;
        fix_.set_latitude_deg(read_f64(frame + 16) * kRadToDeg);
        fix_.set_longitude_deg(read_f64(frame + 24) * kRadToDeg);
        fix_.set_altitude_amsl_m(read_f64(frame + 32) - read_f32(frame + 40));
    }
}

// ================================================================================

void GpsDecoder::parse_novatel(
        const uint8_t *frame,
        std::size_t length,
        int64_t host_unix_ms) {
    if (length < 32 || novatel_crc(frame, length - 4) != read_u32(frame + length - 4)) {
        return;
    }
    if (read_u16(frame + 4) != 42) {
        return;
    }
    const unsigned header_length = frame[3];
    if (header_length + 72 > length) {
        return;
    }
    const uint8_t *body = frame + header_length;
    const doggy::v1::GpsFixType fix_type = read_u32(body) == 0
            ? novatel_type(read_u32(body + 4))
            : doggy::v1::GPS_FIX_NO_FIX;
    note_sentence(host_unix_ms, true);
    fix_.set_fix_type(fix_type);
    fix_.set_fix_unix_ms(host_unix_ms);
    fix_.set_satellites(body[65]);
    fix_.set_accuracy_m(std::hypot(read_f32(body + 40), read_f32(body + 44)));
    if (fix_type == doggy::v1::GPS_FIX_NO_FIX) {
        fix_.clear_latitude_deg();
        fix_.clear_longitude_deg();
        fix_.clear_altitude_amsl_m();
        return;
    }
    fix_.set_latitude_deg(read_f64(body + 8));
    fix_.set_longitude_deg(read_f64(body + 16));
    fix_.set_altitude_amsl_m(read_f64(body + 24));
}

// ================================================================================

bool GpsDecoder::consume() {
    if (buffer_.empty()) {
        return false;
    }
    if (buffer_[0] == '$' && (buffer_.size() < 2 || buffer_[1] != '@')) {
        const auto end = std::find(buffer_.begin(), buffer_.end(), static_cast<uint8_t>('\n'));
        if (end == buffer_.end()) {
            if (buffer_.size() > 512) {
                buffer_.erase(buffer_.begin());
                return true;
            }
            return false;
        }
        const std::string sentence(buffer_.begin(), end);
        const int64_t host_unix_ms = feed_unix_ms_;
        buffer_.erase(buffer_.begin(), end + 1);
        if (accept_nmea()) {
            parse_nmea(sentence, host_unix_ms);
        }
        return true;
    }
    if (buffer_.size() >= 2 && buffer_[0] == '$' && buffer_[1] == '@') {
        if (buffer_.size() < 8) {
            return false;
        }
        const std::size_t length = read_u16(buffer_.data() + 6);
        if (length < 8 || length > 1024) {
            buffer_.erase(buffer_.begin());
            return true;
        }
        if (buffer_.size() < length) {
            return false;
        }
        if (accept_sbf()) {
            parse_sbf(buffer_.data(), length, feed_unix_ms_);
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
        return true;
    }
    if (buffer_.size() >= 2 && buffer_[0] == 0xb5 && buffer_[1] == 0x62) {
        if (buffer_.size() < 6) {
            return false;
        }
        const std::size_t length = 8u + read_u16(buffer_.data() + 4);
        if (length > 1024) {
            buffer_.erase(buffer_.begin());
            return true;
        }
        if (buffer_.size() < length) {
            return false;
        }
        if (accept_ubx()) {
            parse_ubx(buffer_.data(), length, feed_unix_ms_);
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
        return true;
    }
    if (buffer_.size() >= 3 && buffer_[0] == 0xaa && buffer_[1] == 0x44 && buffer_[2] == 0x12) {
        if (buffer_.size() < 10) {
            return false;
        }
        const unsigned header_length = buffer_[3];
        const std::size_t message_length = read_u16(buffer_.data() + 8);
        if (header_length < 10 || header_length > 64 || message_length > 2048) {
            buffer_.erase(buffer_.begin());
            return true;
        }
        const std::size_t length = header_length + message_length + 4;
        if (buffer_.size() < length) {
            return false;
        }
        if (accept_novatel()) {
            parse_novatel(buffer_.data(), length, feed_unix_ms_);
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(length));
        return true;
    }
    buffer_.erase(buffer_.begin());
    return true;
}

// ================================================================================

void GpsDecoder::feed(const uint8_t *data, std::size_t length, int64_t host_unix_ms) {
    feed_unix_ms_ = host_unix_ms;
    buffer_.insert(buffer_.end(), data, data + length);
    if (buffer_.size() > kGpsBufferLimit) {
        buffer_.erase(buffer_.begin(), buffer_.end() - 16);
    }
    while (consume()) {
    }
}

// ================================================================================

doggy::v1::Gps GpsDecoder::publish(int64_t now_unix_ms) const {
    doggy::v1::Gps published;
    published.set_spoofing(doggy::v1::GPS_SPOOF_UNKNOWN);
    published.set_jamming(doggy::v1::GPS_JAM_UNKNOWN);
    if (fix_.ok() == false || now_unix_ms - received_unix_ms_ > kGpsStaleMs) {
        published.set_ok(false);
        published.set_fix_type(fix_.ok()
                ? doggy::v1::GPS_FIX_NO_FIX
                : doggy::v1::GPS_FIX_NO_GPS);
        return published;
    }
    return fix_;
}
