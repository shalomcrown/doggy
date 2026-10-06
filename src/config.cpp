#include "config.h"
#include "gps.h"
#include "camera_ids.h"
#include "stereo_bindings.h"
#include "utils.h"

#include <nlohmann/json.hpp>
#include <google/protobuf/util/json_util.h>
#include <mbedtls/sha256.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static constexpr int kDefaultCameraFps = 15;
static constexpr int kDefaultUsbCameraFps = 5;

// ================================================================================

static std::string hex_address(uint8_t address) {
    const unsigned char byte = static_cast<unsigned char>(address);
    return "0x" + to_hex(&byte, 1);
}

// ================================================================================

static uint8_t parse_address_text(const std::string &text, const char *field) {
    std::size_t consumed = 0;
    int n = 0;
    try {
        n = std::stoi(text, &consumed, 0);
    } catch (const std::exception &) {
        throw ConfigError(std::string("config ") + field + " address is not a number");
    }
    if (consumed != text.size() || n < 0 || n > 0x7F) {
        throw ConfigError(std::string("config ") + field + " address out of range");
    }
    return static_cast<uint8_t>(n);
}

// ================================================================================

static void validate_i2c_device(const doggy::v1::I2cDevice &device, const char *name) {
    if (device.bus() < 0 || device.bus() > 255) {
        throw ConfigError(std::string("config ") + name + " bus out of range");
    }
    if (device.address().empty() == false) {
        parse_address_text(device.address(), name);
    }
}

// ================================================================================

static void validate_channel(int channel, const char *field, int max_value) {
    if (channel < 0 || channel > max_value) {
        throw ConfigError(std::string("config ") + field + " channel out of range");
    }
}

// ================================================================================

static void validate_lora(const doggy::v1::Lora &lora) {
    switch (lora.baud()) {
        case 0:
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
            break;
        default:
            throw ConfigError("config lora.baud must be 9600, 19200, 38400, 57600, or 115200");
    }
    if (lora.txch() < 0 || lora.txch() > 80) {
        throw ConfigError("config lora.txch out of range");
    }
    if (lora.rxch() < 0 || lora.rxch() > 80) {
        throw ConfigError("config lora.rxch out of range");
    }
    if (lora.lbt() > 255) {
        throw ConfigError("config lora.lbt out of range");
    }
    if (lora.device().find('\n') != std::string::npos
            || lora.device().find('\r') != std::string::npos) {
        throw ConfigError("config lora.device must not contain a newline");
    }
    if (lora.air_key().empty() == false) {
        if (lora.air_key().find('\n') != std::string::npos
                || lora.air_key().find('\r') != std::string::npos) {
            throw ConfigError("config lora.air_key passphrase must not contain a newline");
        }
        const std::size_t first = lora.air_key().find_first_not_of(" \t\f\v");
        const std::size_t last = lora.air_key().find_last_not_of(" \t\f\v");
        const std::size_t normalized_size =
                first == std::string::npos ? 0 : last - first + 1;
        if (normalized_size < 8 || normalized_size > 128) {
            throw ConfigError("config lora.air_key passphrase must be 8 to 128 bytes");
        }
    }
}

// ================================================================================

static void validate_motor_channel(
        int channel,
        const char *field,
        std::array<bool, 16> &used) {
    validate_channel(channel, field, 15);
    if (used[static_cast<std::size_t>(channel)]) {
        throw ConfigError(std::string("config duplicate motor channel: ") + field);
    }
    used[static_cast<std::size_t>(channel)] = true;
}

// ================================================================================

static void validate_motor(
        const doggy::v1::Motor &motor,
        const char *name,
        std::array<bool, 16> &used) {
    const std::string prefix = std::string("motors.") + name;
    validate_motor_channel(motor.pwm(), (prefix + ".pwm").c_str(), used);
    validate_motor_channel(motor.in2(), (prefix + ".in2").c_str(), used);
    validate_motor_channel(motor.in1(), (prefix + ".in1").c_str(), used);
}

// ================================================================================

static void validate_gps(const doggy::v1::GpsConfig &gps) {
    if (gps.type() != "auto"
            && gps.type() != "nmea"
            && gps.type() != "ublox"
            && gps.type() != "septentrio"
            && gps.type() != "novatel") {
        throw ConfigError("config gps.type must be auto, nmea, ublox, septentrio, or novatel");
    }
    switch (gps.baud()) {
        case 4800:
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
        case 230400:
        case 460800:
        case 921600:
            break;
        default:
            throw ConfigError("config gps.baud is not a supported serial rate");
    }
    if (gps_device_path_allowed(gps.device()) == false) {
        throw ConfigError("config gps.device must be a serial path");
    }
}

// ================================================================================

static void validate_cameras(const doggy::v1::Cameras &cameras) {
    std::set<std::string> ids;
    for (const doggy::v1::Camera &camera : cameras.items()) {
        if (camera.id().empty() || camera.id().size() > 64) {
            throw ConfigError("config camera id must be 1 to 64 characters");
        }
        for (char ch : camera.id()) {
            const unsigned char c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) == 0 && ch != '.' && ch != '-' && ch != '_') {
                throw ConfigError("config camera id contains an invalid character");
            }
        }
        if (ids.insert(camera.id()).second == false) {
            throw ConfigError("config duplicate camera id: " + camera.id());
        }
        if (camera.source() != "auto"
                && camera.source() != "rpi"
                && camera.source() != "v4l2") {
            throw ConfigError("config camera source must be auto, rpi, or v4l2");
        }
        if (camera.device().find('\n') != std::string::npos
                || camera.device().find('\r') != std::string::npos) {
            throw ConfigError("config camera device must not contain a newline");
        }
        if (camera.width() < 160 || camera.width() > 3840
                || camera.height() < 120 || camera.height() > 2160
                || camera.fps() < 1 || camera.fps() > 60) {
            throw ConfigError("config camera dimensions or fps out of range");
        }
        if (camera.rotation_deg() != 0 && camera.rotation_deg() != 180) {
            throw ConfigError("config camera rotation_deg must be 0 or 180");
        }
    }
}

// ================================================================================

static void validate_camera_pair(const doggy::v1::CameraPair &pair) {
    if (pair.left_stable_id().empty() == false
            && stereo_bindings_stable_id_ok(pair.left_stable_id()) == false) {
        throw ConfigError("config camera_pair.left_stable_id is invalid");
    }
    if (pair.right_stable_id().empty() == false
            && stereo_bindings_stable_id_ok(pair.right_stable_id()) == false) {
        throw ConfigError("config camera_pair.right_stable_id is invalid");
    }
    if (pair.left_stable_id().empty() == false
            && pair.right_stable_id().empty() == false
            && pair.left_stable_id() == pair.right_stable_id()) {
        throw ConfigError("config camera_pair left and right stable_id must differ");
    }
}

// ================================================================================

static void validate_telemetry(const doggy::v1::Telemetry &telemetry) {
    if (telemetry.retain_hours() < 1 || telemetry.retain_hours() > 8760) {
        throw ConfigError("config telemetry.retain_hours out of range");
    }
}

// ================================================================================

static void validate_media(const doggy::v1::Media &media) {
    if (media.retain_hours() < 1 || media.retain_hours() > 168) {
        throw ConfigError("config media.retain_hours out of range");
    }
    if (media.min_free_mb() < 64 || media.min_free_mb() > 1048576) {
        throw ConfigError("config media.min_free_mb out of range");
    }
}

// ================================================================================

static void validate_config(const Config &config) {
    if (config.robot().gcs_timeout_s() < 2
            || config.robot().gcs_timeout_s() > 60) {
        throw ConfigError("config robot.gcs_timeout_s out of range");
    }
    if (std::isfinite(config.robot().turn_gain_min()) == false
            || config.robot().turn_gain_min() < 0.0
            || config.robot().turn_gain_min() > 1.0) {
        throw ConfigError("config robot.turn_gain_min out of range");
    }
    validate_i2c_device(config.i2c().servo_board(), "servo_board");
    validate_i2c_device(config.i2c().imu(), "imu");
    validate_i2c_device(config.i2c().ads(), "ads");
    validate_channel(config.servos().front_right_waist(), "front_right_waist", 15);
    validate_channel(config.servos().front_right_hip(), "front_right_hip", 15);
    validate_channel(config.servos().front_right_knee(), "front_right_knee", 15);
    validate_channel(config.servos().front_left_waist(), "front_left_waist", 15);
    validate_channel(config.servos().front_left_hip(), "front_left_hip", 15);
    validate_channel(config.servos().front_left_knee(), "front_left_knee", 15);
    validate_channel(config.servos().rear_left_waist(), "rear_left_waist", 15);
    validate_channel(config.servos().rear_left_hip(), "rear_left_hip", 15);
    validate_channel(config.servos().rear_left_knee(), "rear_left_knee", 15);
    validate_channel(config.servos().rear_right_waist(), "rear_right_waist", 15);
    validate_channel(config.servos().rear_right_hip(), "rear_right_hip", 15);
    validate_channel(config.servos().rear_right_knee(), "rear_right_knee", 15);
    validate_channel(config.servos().head_neck(), "head_neck", 15);
    std::array<bool, 16> used_motor_channels{};
    validate_motor(config.motors().front_left(), "front_left", used_motor_channels);
    validate_motor(config.motors().front_right(), "front_right", used_motor_channels);
    validate_motor(config.motors().rear_right(), "rear_right", used_motor_channels);
    validate_motor(config.motors().rear_left(), "rear_left", used_motor_channels);
    validate_lora(config.lora());
    validate_cameras(config.cameras());
    if (config.has_camera_pair()) {
        validate_camera_pair(config.camera_pair());
    }
    validate_media(config.media());
    validate_telemetry(config.telemetry());
    validate_gps(config.gps());
}

// ================================================================================

static void set_motor_defaults(
        doggy::v1::Motor *motor,
        int pwm,
        int in2,
        int in1) {
    if (motor->has_pwm() == false) {
        motor->set_pwm(pwm);
    }
    if (motor->has_in2() == false) {
        motor->set_in2(in2);
    }
    if (motor->has_in1() == false) {
        motor->set_in1(in1);
    }
    if (motor->has_enabled() == false) {
        motor->set_enabled(true);
    }
    if (motor->has_direction() == false) {
        motor->set_direction(doggy::v1::forward);
    }
}

// ================================================================================

void fill_config_defaults(Config &config) {
    if (config.robot().has_type() == false) {
        config.mutable_robot()->set_type(doggy::v1::DOG);
    }
    if (config.robot().has_gcs_timeout_s() == false) {
        config.mutable_robot()->set_gcs_timeout_s(3);
    }
    if (config.robot().has_turn_gain_min() == false) {
        config.mutable_robot()->set_turn_gain_min(0.25);
    }
    if (config.media().has_retain_hours() == false) {
        config.mutable_media()->set_retain_hours(24);
    }
    if (config.media().has_min_free_mb() == false) {
        config.mutable_media()->set_min_free_mb(512);
    }
    if (config.telemetry().has_retain_hours() == false) {
        config.mutable_telemetry()->set_retain_hours(168);
    }
    if (config.gps().has_enabled() == false) {
        config.mutable_gps()->set_enabled(false);
    }
    if (config.gps().has_type() == false || config.gps().type().empty()) {
        config.mutable_gps()->set_type("auto");
    }
    if (config.gps().has_baud() == false) {
        config.mutable_gps()->set_baud(115200);
    }
    if (config.cameras().items_size() == 0) {
        config.mutable_cameras()->add_items();
    }
    for (int i = 0; i < config.mutable_cameras()->items_size(); ++i) {
        doggy::v1::Camera *camera = config.mutable_cameras()->mutable_items(i);
        if (camera->has_id() == false) {
            camera->set_id("cam" + std::to_string(i));
        }
        if (camera->has_name() == false || camera->name().empty()) {
            if (camera->id() == kCameraUsbPairLeftId
                    || camera->id() == kCameraLegacyPairLeftId) {
                camera->set_name("USB camera (left)");
            } else if (camera->id() == kCameraUsbPairRightId
                    || camera->id() == kCameraLegacyPairRightId) {
                camera->set_name("USB camera (right)");
            } else if (camera->id() == kCameraPrimaryId) {
                camera->set_name("Pi camera");
            } else {
                camera->set_name("Camera " + std::to_string(i + 1));
            }
        }
        if (camera->has_source() == false) {
            camera->set_source("auto");
        }
        if (camera->has_width() == false) {
            camera->set_width(1280);
        }
        if (camera->has_height() == false) {
            camera->set_height(720);
        }
        if (camera->has_fps() == false) {
            camera->set_fps(camera->source() == "v4l2"
                            ? kDefaultUsbCameraFps
                            : kDefaultCameraFps);
        }
        if (camera->has_rotation_deg() == false) {
            camera->set_rotation_deg(180);
        }
        if (camera->has_enabled() == false) {
            camera->set_enabled(true);
        }
    }

    auto camera_id_exists = [&](const char *id) {
        for (const doggy::v1::Camera &camera : config.cameras().items()) {
            if (camera.id() == id) {
                return true;
            }
        }

        return false;
    };

    auto add_usb_pair_camera = [&](const char *id, const char *name) {
        doggy::v1::Camera *camera = config.mutable_cameras()->add_items();
        camera->set_id(id);
        camera->set_name(name);
        camera->set_source("v4l2");
        camera->set_width(1280);
        camera->set_height(720);
        camera->set_fps(kDefaultUsbCameraFps);
        camera->set_rotation_deg(180);
        camera->set_enabled(false);
    };

    if (camera_id_exists(kCameraPrimaryId)) {
        if (camera_id_exists(kCameraUsbPairLeftId) == false
                && camera_id_exists(kCameraLegacyPairLeftId) == false) {
            add_usb_pair_camera(kCameraUsbPairLeftId, "USB camera (left)");
        }
        if (camera_id_exists(kCameraUsbPairRightId) == false
                && camera_id_exists(kCameraLegacyPairRightId) == false) {
            add_usb_pair_camera(kCameraUsbPairRightId, "USB camera (right)");
        }
    }

    doggy::v1::I2cConfig *i2c = config.mutable_i2c();
    if (i2c->servo_board().has_bus() == false) {
        i2c->mutable_servo_board()->set_bus(1);
    }
    if (i2c->servo_board().has_address() == false) {
        const uint8_t address =
                config.robot().type() == doggy::v1::ROVER ? 0x60 : 0x40;
        i2c->mutable_servo_board()->set_address(hex_address(address));
    }
    if (i2c->imu().has_bus() == false) {
        i2c->mutable_imu()->set_bus(1);
    }
    if (i2c->imu().has_address() == false) {
        i2c->mutable_imu()->set_address(hex_address(0x68));
    }
    if (i2c->ads().has_bus() == false) {
        i2c->mutable_ads()->set_bus(1);
    }
    if (i2c->ads().has_address() == false) {
        i2c->mutable_ads()->set_address(hex_address(0x48));
    }
    doggy::v1::ServoChannels *servos = config.mutable_servos();
    if (servos->has_front_right_waist() == false) {
        servos->set_front_right_waist(11);
    }
    if (servos->has_front_right_hip() == false) {
        servos->set_front_right_hip(12);
    }
    if (servos->has_front_right_knee() == false) {
        servos->set_front_right_knee(13);
    }
    if (servos->has_front_left_waist() == false) {
        servos->set_front_left_waist(4);
    }
    if (servos->has_front_left_hip() == false) {
        servos->set_front_left_hip(3);
    }
    if (servos->has_front_left_knee() == false) {
        servos->set_front_left_knee(2);
    }
    if (servos->has_rear_left_waist() == false) {
        servos->set_rear_left_waist(7);
    }
    if (servos->has_rear_left_hip() == false) {
        servos->set_rear_left_hip(6);
    }
    if (servos->has_rear_left_knee() == false) {
        servos->set_rear_left_knee(5);
    }
    if (servos->has_rear_right_waist() == false) {
        servos->set_rear_right_waist(8);
    }
    if (servos->has_rear_right_hip() == false) {
        servos->set_rear_right_hip(9);
    }
    if (servos->has_rear_right_knee() == false) {
        servos->set_rear_right_knee(10);
    }
    if (servos->has_head_neck() == false) {
        servos->set_head_neck(15);
    }
    doggy::v1::Motors *motors = config.mutable_motors();
    set_motor_defaults(motors->mutable_front_left(), 2, 3, 4);
    set_motor_defaults(motors->mutable_front_right(), 5, 6, 7);
    set_motor_defaults(motors->mutable_rear_right(), 8, 9, 10);
    set_motor_defaults(motors->mutable_rear_left(), 11, 12, 13);
    doggy::v1::Lora *lora = config.mutable_lora();
    if (lora->has_enabled() == false) {
        lora->set_enabled(false);
    }
    if (lora->has_baud() == false || lora->baud() == 0) {
        lora->set_baud(115200);
    }
    if (lora->has_band() == false) {
        lora->set_band(doggy::v1::HF);
    }
    if (lora->has_txch() == false) {
        lora->set_txch(18);
    }
    if (lora->has_rxch() == false) {
        lora->set_rxch(18);
    }
    if (lora->has_lbt() == false) {
        lora->set_lbt(0);
    }
}

// ================================================================================

Config default_config() {
    Config config;
    fill_config_defaults(config);
    return config;
}

// ================================================================================

uint8_t i2c_address_byte(const doggy::v1::I2cDevice &device) {
    if (device.address().empty()) {
        return 0;
    }
    return parse_address_text(device.address(), "i2c");
}

// ================================================================================

std::vector<std::string> status_error_messages(DogStatus status) {
    std::vector<std::string> messages;
    messages.reserve(static_cast<std::size_t>(status.errors_size()));
    for (const doggy::v1::Error &err : status.errors()) {
        messages.push_back(err.message());
    }
    return messages;
}

// ================================================================================

bool pin_is_set(const Config &config) {
    return config.system().pin_hash().empty() == false;
}

// ================================================================================

bool pin_length_ok(const std::string &pin) {
    return pin.size() >= static_cast<std::size_t>(kPinMinLength)
            && pin.size() <= static_cast<std::size_t>(kPinMaxLength);
}

// ================================================================================

static std::string hash_pin_with_salt(const std::string &pin, const std::string &salt_hex) {
    std::vector<uint8_t> material;
    material.reserve(salt_hex.size() + pin.size());
    material.insert(material.end(), salt_hex.begin(), salt_hex.end());
    material.insert(material.end(), pin.begin(), pin.end());
    unsigned char digest[32] = {};
    if (mbedtls_sha256(material.data(), material.size(), digest, 0) != 0) {
        throw ConfigError("could not hash PIN");
    }
    return to_hex(digest, sizeof(digest));
}

// ================================================================================

static std::string random_pin_salt_hex() {
    unsigned char raw[16] = {};
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        throw ConfigError("could not open /dev/urandom for PIN salt");
    }
    const ssize_t got = read(fd, raw, sizeof(raw));
    close(fd);
    if (got != static_cast<ssize_t>(sizeof(raw))) {
        throw ConfigError("could not read random bytes for PIN salt");
    }
    return to_hex(raw, sizeof(raw));
}

// ================================================================================

std::string hash_pin(const std::string &pin) {
    if (pin_length_ok(pin) == false) {
        throw ConfigError("PIN length out of range");
    }
    const std::string salt = random_pin_salt_hex();
    return std::string("sha256$") + salt + "$" + hash_pin_with_salt(pin, salt);
}

// ================================================================================

bool pin_matches(const std::string &pin, const std::string &hash) {
    if (hash.empty() || pin_length_ok(pin) == false) {
        return false;
    }
    if (hash.compare(0, 7, "sha256$") != 0) {
        return false;
    }
    const std::size_t salt_end = hash.find('$', 7);
    if (salt_end == std::string::npos || salt_end + 1 >= hash.size()) {
        return false;
    }
    const std::string salt = hash.substr(7, salt_end - 7);
    const std::string expected = std::string("sha256$") + salt + "$"
            + hash_pin_with_salt(pin, salt);
    return hashes_equal(expected.c_str(), hash.c_str());
}

// ================================================================================

std::string proto_to_json(const google::protobuf::Message &message) {
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    std::string out;
    const auto status = google::protobuf::util::MessageToJsonString(message, &out, options);
    if (status.ok() == false) {
        throw ConfigError(std::string("protobuf json: ") + status.ToString());
    }
    return out;
}

// ================================================================================

bool proto_from_json(const std::string &text, google::protobuf::Message &message,
        std::string *error) {
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = false;
    const auto status = google::protobuf::util::JsonStringToMessage(text, &message, options);
    if (status.ok() == false && error != nullptr) {
        *error = status.ToString();
    }
    return status.ok();
}

// ================================================================================

std::string config_to_json(const Config &config) {
    Config file = config;
    file.clear_pin();
    file.mutable_system()->clear_pin_set();
    file.mutable_lora()->clear_air_key_set();
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    options.add_whitespace = true;
    std::string out;
    const auto status = google::protobuf::util::MessageToJsonString(file, &out, options);
    if (status.ok() == false) {
        throw ConfigError(std::string("protobuf json: ") + status.ToString());
    }
    return out;
}

// ================================================================================

std::string config_to_public_json(const Config &config) {
    Config pub = config;
    pub.clear_pin();
    const bool key_set = pub.lora().air_key().empty() == false;
    pub.mutable_lora()->clear_air_key();
    pub.mutable_lora()->set_air_key_set(key_set);
    pub.mutable_system()->clear_pin_hash();
    pub.mutable_system()->set_pin_set(pin_is_set(config));
    return proto_to_json(pub);
}

// ================================================================================

static std::string config_migrate_legacy_json(const std::string &raw) {
    if (raw.find("stereo") == std::string::npos) {
        return raw;
    }

    nlohmann::json document = nlohmann::json::parse(raw, nullptr, false);
    if (document.is_discarded()) {
        return raw;
    }

    if (document.contains("stereo") && document.contains("camera_pair") == false) {
        document["camera_pair"] = document["stereo"];
        document.erase("stereo");
    }

    if (document.contains("cameras") && document["cameras"].contains("items")
            && document["cameras"]["items"].is_array()) {
        for (nlohmann::json &item : document["cameras"]["items"]) {
            if (item.contains("id") == false) {
                continue;
            }
            if (item["id"] == kCameraLegacyPairLeftId) {
                item["id"] = kCameraUsbPairLeftId;
                item["name"] = "USB camera (left)";
            } else if (item["id"] == kCameraLegacyPairRightId) {
                item["id"] = kCameraUsbPairRightId;
                item["name"] = "USB camera (right)";
            }
        }
    }

    return document.dump();
}

// ================================================================================

Config config_from_json(const std::string &text) {
    const std::string migrated = config_migrate_legacy_json(text);
    Config config;
    std::string parse_error;
    if (proto_from_json(migrated, config, &parse_error) == false) {
        throw ConfigError("invalid config JSON: " + parse_error);
    }
    fill_config_defaults(config);
    validate_config(config);
    return config;
}

// ================================================================================

Config config_overlay_json(const Config &base, const std::string &text) {
    const std::string migrated = config_migrate_legacy_json(text);
    Config overlay;
    if (proto_from_json(migrated, overlay) == false) {
        throw ConfigError("invalid config JSON");
    }
    Config next = base;
    const std::string kept_key = next.lora().air_key();
    if (overlay.has_cameras()) {
        next.clear_cameras();
    }
    next.MergeFrom(overlay);
    if (overlay.lora().air_key().empty()) {
        next.mutable_lora()->set_air_key(kept_key);
    }
    next.clear_pin();
    fill_config_defaults(next);
    validate_config(next);
    return next;
}

// ================================================================================

std::string config_default_path() {
    if (const char *env = std::getenv("DOGGY_CONFIG")) {
        if (env[0] != '\0') {
            return env;
        }
    }
    return kDefaultConfigPath;
}

// ================================================================================

Config config_load_file(const std::string &path) {
    std::ifstream in(path);
    if (in.is_open() == false) {
        throw ConfigError("could not read config file: " + path);
    }
    const std::string text((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
    try {
        return config_from_json(text);
    } catch (const ConfigError &ex) {
        throw ConfigError(path + ": " + ex.what());
    }
}

// ================================================================================

static void remove_quietly(const fs::path &path) {
    std::error_code error;
    fs::remove(path, error);
}

// ================================================================================

static void throw_config_write(const std::string &path) {
    throw ConfigError("could not write config file: " + path);
}

// ================================================================================

static void write_config_temp(
        const fs::path &temp,
        const std::string &json,
        bool secret,
        const std::string &path) {
    const mode_t mode = secret ? static_cast<mode_t>(0600) : static_cast<mode_t>(0666);
    const int fd = open(
            temp.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
            mode);
    if (fd < 0) {
        throw_config_write(path);
    }

    if (secret && fchmod(fd, 0600) != 0) {
        close(fd);
        remove_quietly(temp);
        throw_config_write(path);
    }

    const char *data = json.data();
    std::size_t remaining = json.size();
    while (remaining > 0) {
        const ssize_t wrote = write(fd, data, remaining);
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            close(fd);
            remove_quietly(temp);
            throw_config_write(path);
        }
        data += wrote;
        remaining -= static_cast<std::size_t>(wrote);
    }

    const int sync_result = fsync(fd);
    const int close_result = close(fd);
    if (sync_result != 0 || close_result != 0) {
        remove_quietly(temp);
        throw_config_write(path);
    }
}

// ================================================================================

void config_save_file(const Config &config, const std::string &path) {
    const fs::path file(path);
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path());
    }

    const std::string json = config_to_json(config);
    if (json.empty()) {
        throw_config_write(path);
    }

    const fs::path temp(file.string() + ".tmp");
    const bool secret = config.lora().air_key().empty() == false;
    write_config_temp(temp, json, secret, path);

    std::error_code rename_error;
    fs::rename(temp, file, rename_error);
    if (rename_error) {
        remove_quietly(temp);
        throw_config_write(path);
    }

    if (file.has_parent_path() == false) {
        return;
    }

    const int dir_fd = open(
            file.parent_path().c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
}

// ================================================================================

Config config_load_or_create(const std::string &path, std::string *create_error) {
    if (fs::is_regular_file(path)) {
        std::ifstream in(path);
        if (in.is_open() == false) {
            throw ConfigError("could not read config file: " + path);
        }
        const std::string raw((std::istreambuf_iterator<char>(in)),
                std::istreambuf_iterator<char>());
        Config config = config_from_json(raw);
        const bool needs_usb_pair_schema =
                raw.find("\"usb_left\"") == std::string::npos
                || raw.find("\"camera_pair\"") == std::string::npos
                || raw.find("\"stereo\"") != std::string::npos
                || raw.find("\"stereo_left\"") != std::string::npos;
        if (needs_usb_pair_schema) {
            try {
                config_save_file(config, path);
            } catch (const ConfigError &ex) {
                if (create_error != nullptr) {
                    *create_error = ex.what();
                }
            }
        }
        return config;
    }
    Config config = default_config();
    try {
        config_save_file(config, path);
    } catch (const std::exception &ex) {
        if (create_error != nullptr) {
            *create_error = ex.what();
        }
    }
    return config;
}
