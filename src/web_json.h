#ifndef WEB_JSON_H
#define WEB_JSON_H

#include "dog_api.h"

#include <cctype>
#include <sstream>
#include <string>

// ================================================================================

inline std::string json_escape(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (const char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }

    return out;
}

// ================================================================================

inline std::string servos_to_json(const std::vector<ServoSnapshot> &items) {
    std::ostringstream os;
    os << "{\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            os << ',';
        }

        os << "{\"id\":" << items[i].id
           << ",\"name\":\"" << json_escape(items[i].name) << '"'
           << ",\"angle\":" << items[i].angle
           << ",\"pwm\":" << items[i].pwm
           << '}';
    }

    os << "]}";
    return os.str();
}

// ================================================================================

inline const char *dog_error_code_json(DogErrorCode code) {
    switch (code) {
        case DogErrorCode::i2c: return "i2c";
    }

    return "unknown";
}

// ================================================================================

inline void append_xyz_json(std::ostringstream &os, const char *key, const Vec3 &v) {
    os << '"' << key << "\":{\"x\":" << v.x << ",\"y\":" << v.y << ",\"z\":" << v.z << '}';
}

// ================================================================================

inline std::string status_to_json(const DogStatus &status) {
    std::ostringstream os;
    os << "{\"errors\":[";
    for (size_t i = 0; i < status.errors.size(); ++i) {
        if (i > 0) {
            os << ',';
        }

        os << "{\"code\":\"" << dog_error_code_json(status.errors[i].code)
           << "\",\"message\":\"" << json_escape(status.errors[i].message) << "\"}";
    }

    os << "],\"imu\":{\"ok\":";
    if (status.imu.ok) {
        os << "true";
    } else {
        os << "false";
    }

    os << ",\"temperature_c\":" << status.imu.temperature_c << ',';
    append_xyz_json(os, "accel", status.imu.accel);
    os << ',';
    append_xyz_json(os, "gyro", status.imu.gyro);
    os << "}}";
    return os.str();
}

// ================================================================================

inline bool parse_angle_json(const std::string &body, double &angle) {
    const auto key = body.find("\"angle\"");
    if (key == std::string::npos) {
        return false;
    }

    auto pos = body.find(':', key + 7);
    if (pos == std::string::npos) {
        return false;
    }

    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
        ++pos;
    }

    if (pos >= body.size()) {
        return false;
    }

    try {
        size_t consumed = 0;
        angle = std::stod(body.substr(pos), &consumed);
        return consumed > 0;
    } catch (const std::exception &) {
        return false;
    }
}

#endif

