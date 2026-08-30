#include "config.h"
#include "utils.h"

#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ================================================================================

static std::string hex_address(uint8_t address) {
    const unsigned char byte = static_cast<unsigned char>(address);
    return "0x" + to_hex(&byte, 1);
}

// ================================================================================

static uint8_t parse_address(const nlohmann::json &value, const char *field) {
    if (value.is_number_integer()) {
        const int n = value.get<int>();
        if (n < 0 || n > 0x7F) {
            throw ConfigError(std::string("config ") + field + " address out of range");
        }

        return static_cast<uint8_t>(n);
    }

    if (value.is_string() == false) {
        throw ConfigError(std::string("config ") + field + " address must be a hex string or integer");
    }

    const std::string text = value.get<std::string>();
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

static int parse_bus(const nlohmann::json &value, const char *field) {
    if (value.is_number_integer() == false) {
        throw ConfigError(std::string("config ") + field + " bus must be an integer");
    }

    const int bus = value.get<int>();
    if (bus < 0 || bus > 255) {
        throw ConfigError(std::string("config ") + field + " bus out of range");
    }

    return bus;
}

// ================================================================================

static int parse_channel(const nlohmann::json &value, const char *field) {
    if (value.is_number_integer() == false) {
        throw ConfigError(std::string("config ") + field + " must be an integer");
    }

    const int channel = value.get<int>();
    if (channel < 0 || channel > 15) {
        throw ConfigError(std::string("config ") + field + " channel out of range");
    }

    return channel;
}

// ================================================================================

static void apply_i2c_device(I2cDeviceConfig &device, const nlohmann::json &obj, const char *field) {
    if (obj.is_object() == false) {
        throw ConfigError(std::string("config i2c.") + field + " must be an object");
    }

    if (obj.contains("bus")) {
        device.bus = parse_bus(obj["bus"], field);
    }

    if (obj.contains("address")) {
        device.address = parse_address(obj["address"], field);
    }
}

// ================================================================================

static void apply_json(Config &config, const nlohmann::json &root) {
    if (root.is_object() == false) {
        throw ConfigError("config root must be a JSON object");
    }

    if (root.contains("i2c")) {
        const auto &i2c = root["i2c"];
        if (i2c.is_object() == false) {
            throw ConfigError("config i2c must be an object");
        }

        if (i2c.contains("servo_board")) {
            apply_i2c_device(config.i2c.servo_board, i2c["servo_board"], "servo_board");
        }

        if (i2c.contains("imu")) {
            apply_i2c_device(config.i2c.imu, i2c["imu"], "imu");
        }

        if (i2c.contains("ads")) {
            apply_i2c_device(config.i2c.ads, i2c["ads"], "ads");
        }
    }

    if (root.contains("servos")) {
        const auto &servos = root["servos"];
        if (servos.is_object() == false) {
            throw ConfigError("config servos must be an object");
        }

        const auto set_ch = [&](const char *key, int &dest) {
            if (servos.contains(key)) {
                dest = parse_channel(servos[key], key);
            }
        };

        set_ch("front_right_waist", config.servos.front_right_waist);
        set_ch("front_right_hip", config.servos.front_right_hip);
        set_ch("front_right_knee", config.servos.front_right_knee);
        set_ch("front_left_waist", config.servos.front_left_waist);
        set_ch("front_left_hip", config.servos.front_left_hip);
        set_ch("front_left_knee", config.servos.front_left_knee);
        set_ch("rear_left_waist", config.servos.rear_left_waist);
        set_ch("rear_left_hip", config.servos.rear_left_hip);
        set_ch("rear_left_knee", config.servos.rear_left_knee);
        set_ch("rear_right_waist", config.servos.rear_right_waist);
        set_ch("rear_right_hip", config.servos.rear_right_hip);
        set_ch("rear_right_knee", config.servos.rear_right_knee);
        set_ch("head_neck", config.servos.head_neck);
    }

    if (root.contains("system")) {
        const auto &system = root["system"];
        if (system.is_object() == false) {
            throw ConfigError("config system must be an object");
        }

        if (system.contains("pin_hash")) {
            if (system["pin_hash"].is_string() == false) {
                throw ConfigError("config system.pin_hash must be a string");
            }

            config.system.pin_hash = system["pin_hash"].get<std::string>();
        }
    }
}

// ================================================================================

static nlohmann::json i2c_device_json(const I2cDeviceConfig &device) {
    return nlohmann::json{
        {"bus", device.bus},
        {"address", hex_address(device.address)}
    };
}

// ================================================================================

static nlohmann::json to_json(const Config &config) {
    nlohmann::json root = {
        {"i2c",
         {{"servo_board", i2c_device_json(config.i2c.servo_board)},
          {"imu", i2c_device_json(config.i2c.imu)},
          {"ads", i2c_device_json(config.i2c.ads)}}},
        {"servos",
         {{"front_right_waist", config.servos.front_right_waist},
          {"front_right_hip", config.servos.front_right_hip},
          {"front_right_knee", config.servos.front_right_knee},
          {"front_left_waist", config.servos.front_left_waist},
          {"front_left_hip", config.servos.front_left_hip},
          {"front_left_knee", config.servos.front_left_knee},
          {"rear_left_waist", config.servos.rear_left_waist},
          {"rear_left_hip", config.servos.rear_left_hip},
          {"rear_left_knee", config.servos.rear_left_knee},
          {"rear_right_waist", config.servos.rear_right_waist},
          {"rear_right_hip", config.servos.rear_right_hip},
          {"rear_right_knee", config.servos.rear_right_knee},
          {"head_neck", config.servos.head_neck}}}
    };
    if (config.system.pin_hash.empty() == false) {
        root["system"] = {{"pin_hash", config.system.pin_hash}};
    }

    return root;
}

// ================================================================================

std::string Config::default_path() {
    if (const char *env = std::getenv("DOGGY_CONFIG")) {
        if (env[0] != '\0') {
            return env;
        }
    }

    return kDefaultConfigPath;
}

// ================================================================================

Config Config::from_json_string(const std::string &text) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception &ex) {
        throw ConfigError(std::string("invalid config JSON: ") + ex.what());
    }

    Config config;
    apply_json(config, root);
    return config;
}

// ================================================================================

std::string Config::to_json_string() const {
    return to_json(*this).dump();
}

// ================================================================================

std::string Config::to_public_json_string() const {
    nlohmann::json root = to_json(*this);
    root.erase("system");
    root["system"] = {{"pin_set", system.pin_is_set()}};
    return root.dump();
}

// ================================================================================

bool SystemConfig::pin_is_set() const {
    return pin_hash.empty() == false;
}

// ================================================================================

bool Config::pin_length_ok(const std::string &pin) {
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

std::string Config::hash_pin(const std::string &pin) {
    if (pin_length_ok(pin) == false) {
        throw ConfigError("PIN length out of range");
    }

    const std::string salt = random_pin_salt_hex();
    return std::string("sha256$") + salt + "$" + hash_pin_with_salt(pin, salt);
}

// ================================================================================

bool Config::pin_matches(const std::string &pin, const std::string &hash) {
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

Config Config::load_file(const std::string &path) {
    std::ifstream in(path);
    if (in.is_open() == false) {
        throw ConfigError("could not read config file: " + path);
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const nlohmann::json::exception &ex) {
        throw ConfigError(std::string("invalid config JSON: ") + ex.what());
    }

    Config config;
    apply_json(config, root);
    return config;
}

// ================================================================================

void Config::save_file(const Config &config, const std::string &path) {
    const fs::path file(path);
    if (file.has_parent_path()) {
        fs::create_directories(file.parent_path());
    }

    std::ofstream out(path);
    if (out.is_open() == false) {
        throw ConfigError("could not write config file: " + path);
    }

    out << to_json(config).dump(2) << '\n';
    if (out.fail()) {
        throw ConfigError("could not write config file: " + path);
    }
}

// ================================================================================

Config Config::load_or_create(const std::string &path, std::string *create_error) {
    if (fs::is_regular_file(path)) {
        return load_file(path);
    }

    Config config;
    try {
        save_file(config, path);
    } catch (const std::exception &ex) {
        if (create_error != nullptr) {
            *create_error = ex.what();
        }
    }

    return config;
}
