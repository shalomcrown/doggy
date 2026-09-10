#ifndef CONFIG_H
#define CONFIG_H

#include "doggy.pb.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline constexpr const char *kDefaultConfigPath = "/etc/doggy/doggy.json";
inline constexpr int kPinMinLength = 4;
inline constexpr int kPinMaxLength = 64;

using Config = doggy::v1::Config;
using RobotType = doggy::v1::RobotType;
using MotorDirection = doggy::v1::MotorDirection;
using LoraBand = doggy::v1::LoraBand;
using DogStatus = doggy::v1::Status;
using ServoSnapshot = doggy::v1::ServoSnapshot;
using MotorSnapshot = doggy::v1::MotorSnapshot;

// ================================================================================

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Config default_config();
void fill_config_defaults(Config &config);
uint8_t i2c_address_byte(const doggy::v1::I2cDevice &device);
std::vector<std::string> status_error_messages(DogStatus status);
bool pin_is_set(const Config &config);
bool pin_length_ok(const std::string &pin);
std::string hash_pin(const std::string &pin);
bool pin_matches(const std::string &pin, const std::string &hash);
std::string config_to_json(const Config &config);
std::string config_to_public_json(const Config &config);
Config config_from_json(const std::string &text);
Config config_overlay_json(const Config &base, const std::string &text);
void config_save_file(const Config &config, const std::string &path);
Config config_load_file(const std::string &path);
Config config_load_or_create(const std::string &path, std::string *create_error = nullptr);
std::string config_default_path();
std::string proto_to_json(const google::protobuf::Message &message);
bool proto_from_json(const std::string &text, google::protobuf::Message &message);

#endif
