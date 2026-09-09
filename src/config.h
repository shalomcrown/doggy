#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <stdexcept>
#include <string>

inline constexpr const char *kDefaultConfigPath = "/etc/doggy/doggy.json";

enum class RobotType {
    dog,
    rover
};

enum class MotorDirection {
    forward,
    reverse
};

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

class MotorConfig {
public:
    int channel = 0;
    bool enabled = true;
    MotorDirection direction = MotorDirection::forward;
};

// ================================================================================

class MotorChannelsConfig {
public:
    MotorConfig front_left{0, true, MotorDirection::forward};
    MotorConfig front_right{1, true, MotorDirection::forward};
    MotorConfig rear_left{2, true, MotorDirection::forward};
    MotorConfig rear_right{3, true, MotorDirection::forward};
};

// ================================================================================

class LoraConfig {
public:
    bool enabled = false;
    std::string device;
    std::string country;
    int frequency_hz = 0;
};

// ================================================================================

class RobotConfig {
public:
    RobotType type = RobotType::dog;
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
    RobotConfig robot;
    I2cConfig i2c;
    ServoChannelsConfig servos;
    MotorChannelsConfig motors;
    LoraConfig lora;
    SystemConfig system;

    static constexpr int kPinMinLength = 4;
    static constexpr int kPinMaxLength = 64;

    static std::string default_path();
    static Config load_file(const std::string &path);
    static Config from_json_string(const std::string &text);
    static Config overlay_json_string(const Config &base, const std::string &text);
    static void save_file(const Config &config, const std::string &path);
    static Config load_or_create(const std::string &path, std::string *create_error = nullptr);
    static bool pin_length_ok(const std::string &pin);
    static std::string hash_pin(const std::string &pin);
    static bool pin_matches(const std::string &pin, const std::string &hash);
    std::string to_json_string() const;
    std::string to_public_json_string() const;
};

#endif
