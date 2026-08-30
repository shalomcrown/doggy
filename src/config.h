#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <stdexcept>
#include <string>

inline constexpr const char *kDefaultConfigPath = "/etc/doggy/doggy.json";

// ================================================================================

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ================================================================================

class I2cDeviceConfig {
public:
    int bus = 1;
    uint8_t address = 0x00;
};

// ================================================================================

class I2cConfig {
public:
    I2cDeviceConfig servo_board{1, 0x40};
    I2cDeviceConfig imu{1, 0x68};
    I2cDeviceConfig ads{1, 0x48};
};

// ================================================================================

class ServoChannelsConfig {
public:
    int front_right_waist = 11;
    int front_right_hip = 12;
    int front_right_knee = 13;
    int front_left_waist = 4;
    int front_left_hip = 3;
    int front_left_knee = 2;
    int rear_left_waist = 7;
    int rear_left_hip = 6;
    int rear_left_knee = 5;
    int rear_right_waist = 8;
    int rear_right_hip = 9;
    int rear_right_knee = 10;
    int head_neck = 15;
};

// ================================================================================

class SystemConfig {
public:
    std::string pin_hash;

    bool pin_is_set() const;
};

// ================================================================================

class Config {
public:
    I2cConfig i2c;
    ServoChannelsConfig servos;
    SystemConfig system;

    static constexpr int kPinMinLength = 4;
    static constexpr int kPinMaxLength = 64;

    static std::string default_path();
    static Config load_file(const std::string &path);
    static Config from_json_string(const std::string &text);
    static void save_file(const Config &config, const std::string &path);
    static Config load_or_create(const std::string &path, std::string *create_error = nullptr);
    static bool pin_length_ok(const std::string &pin);
    static std::string hash_pin(const std::string &pin);
    static bool pin_matches(const std::string &pin, const std::string &hash);
    std::string to_json_string() const;
    std::string to_public_json_string() const;
};

#endif
