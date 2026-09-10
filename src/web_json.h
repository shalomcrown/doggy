#ifndef WEB_JSON_H
#define WEB_JSON_H

#include "config.h"
#include "robot_api.h"

#include <cmath>
#include <string>
#include <vector>

// ================================================================================

inline std::string status_to_json(DogStatus status, const char *version) {
    status.set_version(version);
    if (status.has_type() == false) {
        status.set_type(doggy::v1::DOG);
    }
    if (status.has_imu() == false) {
        status.mutable_imu()->set_ok(false);
    }
    if (status.has_battery() == false) {
        status.mutable_battery()->set_ok(false);
    }
    if (status.type() == doggy::v1::ROVER) {
        status.clear_servos();
    } else {
        status.mutable_servos();
    }
    return proto_to_json(status);
}

// ================================================================================

inline std::string servos_to_json(const std::vector<ServoSnapshot> &items) {
    doggy::v1::ServoList list;
    for (const ServoSnapshot &item : items) {
        *list.add_items() = item;
    }
    return proto_to_json(list);
}

// ================================================================================

inline const char *robot_type_json(RobotType type) {
    switch (type) {
        case doggy::v1::ROVER: return "ROVER";
        case doggy::v1::DOG:
        default: return "DOG";
    }
}

// ================================================================================

inline bool parse_servo_post(const std::string &body, bool &disable, double &angle) {
    doggy::v1::ServoCommand cmd;
    if (proto_from_json(body, cmd) == false) {
        return false;
    }
    if (cmd.has_enabled() && cmd.enabled() == false) {
        disable = true;
        return true;
    }
    if (cmd.has_angle()) {
        disable = false;
        angle = cmd.angle();
        return true;
    }
    return false;
}

// ================================================================================

inline bool parse_system_post(const std::string &body, SystemAction &action, std::string &pin) {
    doggy::v1::SystemCommand cmd;
    if (proto_from_json(body, cmd) == false) {
        return false;
    }
    if (cmd.pin().empty() || cmd.has_action() == false) {
        return false;
    }
    action = static_cast<SystemAction>(cmd.action());
    pin = cmd.pin();
    return true;
}

// ================================================================================

inline bool parse_pin_post(const std::string &body, std::string &pin, std::string &current_pin) {
    doggy::v1::PinCommand cmd;
    if (proto_from_json(body, cmd) == false) {
        return false;
    }
    if (cmd.pin().empty()) {
        return false;
    }
    pin = cmd.pin();
    current_pin = cmd.current_pin();
    return true;
}

// ================================================================================

inline bool parse_config_pin(const std::string &body, std::string &pin) {
    Config cmd;
    if (proto_from_json(body, cmd) == false) {
        return false;
    }
    pin = cmd.pin();
    return true;
}

// ================================================================================

inline bool parse_drive_post(const std::string &body, double &speed, double &turn) {
    doggy::v1::Drive drive;
    if (proto_from_json(body, drive) == false) {
        return false;
    }
    if (drive.has_speed() == false || drive.has_turn() == false) {
        return false;
    }
    speed = drive.speed();
    turn = drive.turn();
    return std::isfinite(speed) && std::isfinite(turn);
}

// ================================================================================

inline std::string system_accepted_json(SystemAction action) {
    doggy::v1::SystemAccepted accepted;
    accepted.set_accepted(true);
    accepted.set_action(static_cast<doggy::v1::SystemAction>(action));
    return proto_to_json(accepted);
}

#endif
