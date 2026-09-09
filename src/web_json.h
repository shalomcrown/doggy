#ifndef WEB_JSON_H
#define WEB_JSON_H

#include "robot_api.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

// ================================================================================

inline nlohmann::json servos_to_json_value(const std::vector<ServoSnapshot> &items) {
    nlohmann::json list = nlohmann::json::array();
    for (const ServoSnapshot &item : items) {
        nlohmann::json row = {
            {"id", item.id},
            {"name", item.name},
            {"pwm", item.pwm}
        };
        if (item.pwm == 0) {
            row["angle"] = nullptr;
        } else {
            row["angle"] = item.angle;
        }

        list.push_back(std::move(row));
    }

    return {{"items", std::move(list)}};
}

// ================================================================================

inline std::string servos_to_json(const std::vector<ServoSnapshot> &items) {
    return servos_to_json_value(items).dump();
}

// ================================================================================

inline const char *dog_error_code_json(DogErrorCode code) {
    switch (code) {
        case DogErrorCode::i2c: return "i2c";
    }

    return "unknown";
}

// ================================================================================

inline nlohmann::json vec3_to_json(const Vec3 &v) {
    return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

// ================================================================================

inline const char *robot_type_json(RobotType type) {
    switch (type) {
        case RobotType::dog: return "DOG";
        case RobotType::rover: return "ROVER";
    }

    return "DOG";
}

// ================================================================================

inline const char *motor_direction_json(MotorDirection direction) {
    switch (direction) {
        case MotorDirection::forward: return "forward";
        case MotorDirection::reverse: return "reverse";
    }

    return "forward";
}

// ================================================================================

inline nlohmann::json motors_to_json_value(const std::vector<MotorSnapshot> &items) {
    nlohmann::json list = nlohmann::json::array();
    for (const MotorSnapshot &item : items) {
        list.push_back({
            {"id", item.id},
            {"name", item.name},
            {"pwm", item.pwm},
            {"enabled", item.enabled},
            {"direction", motor_direction_json(item.direction)}
        });
    }

    return {{"items", std::move(list)}};
}

// ================================================================================

inline std::string status_to_json(const DogStatus &status, const std::string &version) {
    nlohmann::json errors = nlohmann::json::array();
    for (const DogError &err : status.errors) {
        errors.push_back({
            {"code", dog_error_code_json(err.code)},
            {"message", err.message}
        });
    }

    nlohmann::json body = {
        {"type", robot_type_json(status.type)},
        {"version", version},
        {"errors", std::move(errors)},
        {"imu",
         {{"ok", status.imu.ok},
          {"temperature_c", status.imu.temperature_c},
          {"accel", vec3_to_json(status.imu.accel)},
          {"gyro", vec3_to_json(status.imu.gyro)}}},
        {"battery",
         {{"ok", status.battery.ok},
          {"voltage_v", status.battery.voltage_v}}}
    };
    if (status.type == RobotType::dog) {
        body["servos"] = servos_to_json_value(status.servos);
    } else {
        body["speed"] = status.speed;
        body["turn"] = status.turn;
        body["motors"] = motors_to_json_value(status.motors);
    }
    return body.dump();
}

// ================================================================================

inline bool parse_servo_post(const std::string &body, bool &disable, double &angle) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false) {
        return false;
    }

    if (json.contains("enabled") && json["enabled"].is_boolean()
            && json["enabled"].get<bool>() == false) {
        disable = true;
        return true;
    }

    if (json.contains("angle") && json["angle"].is_number()) {
        disable = false;
        angle = json["angle"].get<double>();
        return true;
    }

    return false;
}

// ================================================================================

inline bool parse_system_post(const std::string &body, SystemAction &action, std::string &pin) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false) {
        return false;
    }

    if (json.contains("pin") == false || json["pin"].is_string() == false) {
        return false;
    }

    if (json.contains("action") == false || json["action"].is_string() == false) {
        return false;
    }

    const std::string name = json["action"].get<std::string>();
    if (name == "restart") {
        action = SystemAction::restart;
    } else if (name == "reboot") {
        action = SystemAction::reboot;
    } else if (name == "shutdown") {
        action = SystemAction::shutdown;
    } else {
        return false;
    }

    pin = json["pin"].get<std::string>();
    return true;
}

// ================================================================================

inline bool parse_pin_post(const std::string &body, std::string &pin, std::string &current_pin) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false) {
        return false;
    }

    if (json.contains("pin") == false || json["pin"].is_string() == false) {
        return false;
    }

    pin = json["pin"].get<std::string>();
    if (json.contains("current_pin")) {
        if (json["current_pin"].is_string() == false) {
            return false;
        }

        current_pin = json["current_pin"].get<std::string>();
    } else {
        current_pin.clear();
    }

    return true;
}

// ================================================================================

inline bool parse_config_pin(const std::string &body, std::string &pin) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false) {
        return false;
    }

    if (json.contains("pin")) {
        if (json["pin"].is_string() == false) {
            return false;
        }
        pin = json["pin"].get<std::string>();
    } else {
        pin.clear();
    }
    return true;
}

// ================================================================================

inline bool parse_drive_post(const std::string &body, double &speed, double &turn) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false
            || json.contains("speed") == false || json["speed"].is_number() == false
            || json.contains("turn") == false || json["turn"].is_number() == false) {
        return false;
    }

    speed = json["speed"].get<double>();
    turn = json["turn"].get<double>();
    return std::isfinite(speed) && std::isfinite(turn);
}

// ================================================================================

inline const char *system_action_json(SystemAction action) {
    switch (action) {
        case SystemAction::restart: return "restart";
        case SystemAction::reboot: return "reboot";
        case SystemAction::shutdown: return "shutdown";
    }

    return "unknown";
}

// ================================================================================

inline std::string system_accepted_json(SystemAction action) {
    return nlohmann::json{
        {"accepted", true},
        {"action", system_action_json(action)}
    }.dump();
}

// ================================================================================

inline bool parse_angle_json(const std::string &body, double &angle) {
    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || json.is_object() == false) {
        return false;
    }

    if (json.contains("angle") == false || json["angle"].is_number() == false) {
        return false;
    }

    angle = json["angle"].get<double>();
    return true;
}

#endif
