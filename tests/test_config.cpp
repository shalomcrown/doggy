#include "config.h"
#include "i2c_interface.hpp"

#include <mbedtls/sha256.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

static int failures = 0;

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

int main() {
    Config defaults = default_config();
    expect(defaults.robot().type() == doggy::v1::DOG, "missing robot type defaults to DOG");
    expect(defaults.motors().front_left().channel() == 0, "default front_left motor channel is 0");
    expect(defaults.motors().front_right().channel() == 1, "default front_right motor channel is 1");
    expect(defaults.motors().rear_left().channel() == 2, "default rear_left motor channel is 2");
    expect(defaults.motors().rear_right().channel() == 3, "default rear_right motor channel is 3");
    expect(defaults.motors().front_left().enabled(), "motors default enabled");
    expect(defaults.motors().front_left().direction() == doggy::v1::forward,
           "motors default forward");
    expect(defaults.lora().enabled() == false, "lora defaults disabled");
    expect(defaults.lora().band() == doggy::v1::HF, "lora band defaults to HF");
    expect(defaults.lora().txch() == 18 && defaults.lora().rxch() == 18,
           "lora HF default channels are 18 (868 MHz)");
    expect(defaults.lora().baud() == 115200, "lora baud defaults to 115200");
    expect(defaults.lora().lbt() == 0,
           "lora LBT defaults to factory value 0");
    expect(defaults.servos().front_right_waist() == 11, "default front_right_waist is 11");
    expect(defaults.servos().front_right_hip() == 12, "default front_right_hip is 12");
    expect(defaults.servos().front_right_knee() == 13, "default front_right_knee is 13");
    expect(defaults.servos().front_left_waist() == 4, "default front_left_waist is 4");
    expect(defaults.servos().front_left_hip() == 3, "default front_left_hip is 3");
    expect(defaults.servos().front_left_knee() == 2, "default front_left_knee is 2");
    expect(defaults.servos().rear_left_waist() == 7, "default rear_left_waist is 7");
    expect(defaults.servos().rear_left_hip() == 6, "default rear_left_hip is 6");
    expect(defaults.servos().rear_left_knee() == 5, "default rear_left_knee is 5");
    expect(defaults.servos().rear_right_waist() == 8, "default rear_right_waist is 8");
    expect(defaults.servos().rear_right_hip() == 9, "default rear_right_hip is 9");
    expect(defaults.servos().rear_right_knee() == 10, "default rear_right_knee is 10");
    expect(defaults.servos().head_neck() == 15, "default head_neck is 15");
    expect(defaults.i2c().servo_board().bus() == 1 && defaults.i2c().servo_board().address() == "0x40",
           "default servo_board is bus 1 address 0x40");
    expect(defaults.i2c().imu().bus() == 1 && defaults.i2c().imu().address() == "0x68",
           "default imu is bus 1 address 0x68");
    expect(defaults.i2c().ads().bus() == 1 && defaults.i2c().ads().address() == "0x48",
           "default ads is bus 1 address 0x48");
    expect(i2c_device_path(1) == "/dev/i2c-1", "i2c_device_path 1 is /dev/i2c-1");

    const std::string dumped = config_to_json(defaults);
    expect(dumped.find("\"0x40\"") != std::string::npos,
           "to_json_string writes hex servo_board address");
    expect(dumped.find("\"front_right_waist\"") != std::string::npos,
           "to_json_string writes default channels");
    expect(dumped.find("\"type\"") != std::string::npos && dumped.find("DOG") != std::string::npos,
           "to_json_string writes DOG type");
    expect(dumped.find("\"front_left\"") != std::string::npos,
           "to_json_string writes motor configuration");
    expect(dumped.find("\"lora\"") != std::string::npos,
           "to_json_string writes lora configuration");
    expect(dumped.find("HF") != std::string::npos,
           "to_json_string writes lora HF band");
    expect(dumped.find("\"txch\"") != std::string::npos,
           "to_json_string writes lora TXCH");
    expect(dumped.find("115200") != std::string::npos,
           "to_json_string writes lora baud");
    expect(dumped.find("\"lbt\"") != std::string::npos,
           "to_json_string writes numeric lora LBT");

    const Config from_text = config_from_json(
            R"({"robot":{"type":"ROVER"},"motors":{"front_left":{"channel":4,"enabled":false,"direction":"reverse"}},"lora":{"enabled":true,"band":"LF","txch":23,"rxch":23,"lbt":255},"servos":{"head_neck":14},"i2c":{"imu":{"address":"0x69"}}})");
    expect(from_text.robot().type() == doggy::v1::ROVER, "from_json_string reads ROVER");
    expect(from_text.motors().front_left().channel() == 4, "from_json_string reads motor channel");
    expect(from_text.motors().front_left().enabled() == false, "from_json_string reads motor enabled");
    expect(from_text.motors().front_left().direction() == doggy::v1::reverse,
           "from_json_string reads reverse direction");
    expect(from_text.servos().head_neck() == 14, "from_json_string overlays head_neck");
    expect(from_text.servos().front_right_waist() == 11,
           "from_json_string keeps default waist");
    expect(from_text.i2c().imu().address() == "0x69", "from_json_string overlays imu address");
    expect(from_text.lora().enabled(), "from_json_string reads lora.enabled");
    expect(from_text.lora().band() == doggy::v1::LF, "from_json_string reads lora.band LF");
    expect(from_text.lora().txch() == 23 && from_text.lora().rxch() == 23,
           "from_json_string reads lora TXCH/RXCH");
    expect(from_text.lora().lbt() == 255,
           "from_json_string reads maximum numeric lora LBT");

    Config rover_base = default_config();
    rover_base.mutable_robot()->set_type(doggy::v1::ROVER);
    rover_base.mutable_motors()->mutable_front_left()->set_channel(7);
    const Config overlaid_rover = config_overlay_json(
            rover_base, R"({"motors":{"front_right":{"channel":8}}})");
    expect(overlaid_rover.robot().type() == doggy::v1::ROVER,
           "overlay without type keeps ROVER");
    expect(overlaid_rover.motors().front_left().channel() == 7,
           "overlay keeps existing motor channel");
    expect(overlaid_rover.motors().front_right().channel() == 8,
           "overlay updates named motor channel");

    bool from_text_threw = false;
    try {
        config_from_json("{ not json");
    } catch (const ConfigError &) {
        from_text_threw = true;
    }

    expect(from_text_threw, "from_json_string invalid JSON throws ConfigError");

    bool bad_type_threw = false;
    try {
        config_from_json(R"({"robot":{"type":"BOAT"}})");
    } catch (const ConfigError &) {
        bad_type_threw = true;
    }
    expect(bad_type_threw, "unknown robot type throws ConfigError");

    bool bad_direction_threw = false;
    try {
        config_from_json(
                R"({"motors":{"front_left":{"direction":"sideways"}}})");
    } catch (const ConfigError &) {
        bad_direction_threw = true;
    }
    expect(bad_direction_threw, "unknown motor direction throws ConfigError");

    bool bad_txch_threw = false;
    try {
        config_from_json(R"({"lora":{"txch":81}})");
    } catch (const ConfigError &) {
        bad_txch_threw = true;
    }
    expect(bad_txch_threw, "lora TXCH above 80 throws ConfigError");

    bool bad_lbt_threw = false;
    try {
        config_from_json(R"({"lora":{"lbt":256}})");
    } catch (const ConfigError &) {
        bad_lbt_threw = true;
    }
    expect(bad_lbt_threw, "lora LBT above 255 throws ConfigError");

    std::string legacy_lbt_error;
    try {
        config_from_json(R"({"lora":{"listen_before_talk":true}})");
    } catch (const ConfigError &ex) {
        legacy_lbt_error = ex.what();
    }
    expect(legacy_lbt_error.empty() == false, "legacy boolean LBT config fails clearly");
    expect(legacy_lbt_error.find("listen_before_talk") != std::string::npos,
           "invalid config JSON error names the rejected field");

    bool bad_baud_threw = false;
    try {
        config_from_json(R"({"lora":{"baud":1200}})");
    } catch (const ConfigError &) {
        bad_baud_threw = true;
    }
    expect(bad_baud_threw, "unsupported lora baud throws ConfigError");

    unsigned char empty_digest[32] = {};
    static const unsigned char kEmptySha256[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
        0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    expect(mbedtls_sha256(nullptr, 0, empty_digest, 0) == 0,
           "mbedtls_sha256 empty input succeeds");
    expect(std::memcmp(empty_digest, kEmptySha256, sizeof(kEmptySha256)) == 0,
           "mbedtls_sha256 empty string matches FIPS vector");

    const std::string hashed = hash_pin("1234");
    expect(hashed.find("sha256$") == 0, "hash_pin uses salted SHA-256");
    expect(pin_matches("1234", hashed), "pin_matches accepts the PIN");
    expect(pin_matches("0000", hashed) == false, "pin_matches rejects a wrong PIN");
    expect(pin_length_ok("123") == false, "PIN shorter than 4 is rejected");

    Config with_pin = default_config();
    with_pin.mutable_system()->set_pin_hash(hashed);
    const std::string public_json = config_to_public_json(with_pin);
    expect(public_json.find("\"pin_set\":true") != std::string::npos,
           "public JSON reports pin_set");
    expect(public_json.find("pin_hash") == std::string::npos,
           "public JSON omits pin_hash");
    expect(public_json.find("\"type\":\"DOG\"") != std::string::npos,
           "public JSON includes robot type");
    expect(public_json.find("\"lora\"") != std::string::npos,
           "public JSON includes lora");
    expect(public_json.find("\"pin\"") == std::string::npos,
           "public JSON omits plaintext pin");
    expect(config_to_json(with_pin).find("pin_hash") != std::string::npos,
           "file JSON keeps pin_hash");

    Config with_air = default_config();
    const std::string air_passphrase = "correct horse battery staple";
    with_air.mutable_lora()->set_air_key(air_passphrase);
    const std::string public_air = config_to_public_json(with_air);
    expect(public_air.find("air_key_set") != std::string::npos,
           "public JSON reports air_key_set");
    expect(public_air.find(air_passphrase) == std::string::npos,
           "public JSON omits air passphrase");
    expect(config_to_json(with_air).find("air_key") != std::string::npos,
           "file JSON keeps air passphrase");
    const Config overlay_keep = config_overlay_json(
            with_air, R"({"lora":{"enabled":true,"air_key":""}})");
    expect(overlay_keep.lora().air_key() == with_air.lora().air_key(),
           "empty overlay air_key keeps existing");
    bool bad_air_threw = false;
    try {
        config_from_json(R"({"lora":{"air_key":"short"}})");
    } catch (const ConfigError &) {
        bad_air_threw = true;
    }
    expect(bad_air_threw, "air passphrase shorter than 8 bytes throws ConfigError");
    bad_air_threw = false;
    try {
        config_from_json(R"({"lora":{"air_key":"        "}})");
    } catch (const ConfigError &) {
        bad_air_threw = true;
    }
    expect(bad_air_threw, "whitespace-only air passphrase throws ConfigError");
    bad_air_threw = false;
    try {
        config_from_json(
                R"({"lora":{"air_key":"valid passphrase\nwith newline"}})");
    } catch (const ConfigError &) {
        bad_air_threw = true;
    }
    expect(bad_air_threw, "air passphrase with newline throws ConfigError");
    bad_air_threw = false;
    try {
        Config too_long = default_config();
        too_long.mutable_lora()->set_air_key(std::string(129, 'a'));
        config_from_json(config_to_json(too_long));
    } catch (const ConfigError &) {
        bad_air_threw = true;
    }
    expect(bad_air_threw, "air passphrase longer than 128 bytes throws ConfigError");

    const std::filesystem::path dir =
            std::filesystem::temp_directory_path()
            / ("doggy-config-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string created = (dir / "doggy.json").string();

    std::string create_error;
    const Config written = config_load_or_create(created, &create_error);
    expect(create_error.empty(), "load_or_create writes defaults without error");
    expect(std::filesystem::is_regular_file(created), "load_or_create creates the file");
    expect(written.servos().front_right_waist() == 11, "created config keeps default channel");

    const std::string air_path = (dir / "with-air.json").string();
    config_save_file(with_air, air_path);
    const auto air_perms = std::filesystem::status(air_path).permissions();
    expect((air_perms & std::filesystem::perms::group_read) == std::filesystem::perms::none
                    && (air_perms & std::filesystem::perms::others_read) == std::filesystem::perms::none,
            "doggy.json with air_key is not group/world readable");

    const Config reloaded = config_load_file(created);
    expect(reloaded.i2c().servo_board().address() == "0x40", "reloaded servo_board address is 0x40");
    expect(reloaded.i2c().imu().address() == "0x68", "reloaded imu address is 0x68");
    expect(reloaded.i2c().ads().address() == "0x48", "reloaded ads address is 0x48");
    expect(reloaded.servos().head_neck() == 15, "reloaded head_neck is 15");

    {
        std::ifstream in(created);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        expect(text.find("\"0x40\"") != std::string::npos, "saved address is hex string 0x40");
        expect(text.find("\"ads\"") != std::string::npos, "saved JSON includes ads");
    }

    const std::string overlay = (dir / "overlay.json").string();
    {
        std::ofstream out(overlay);
        out << R"({"servos":{"head_neck":14},"i2c":{"imu":{"address":"0x69"}}})" << '\n';
    }

    const Config overlaid = config_load_file(overlay);
    expect(overlaid.servos().head_neck() == 14, "overlay changes head_neck");
    expect(overlaid.servos().front_right_waist() == 11, "overlay keeps default waist");
    expect(overlaid.i2c().imu().address() == "0x69", "overlay hex string address");
    expect(overlaid.i2c().servo_board().address() == "0x40", "overlay keeps default servo address");

    const std::string numeric = (dir / "numeric.json").string();
    {
        std::ofstream out(numeric);
        out << R"({"i2c":{"servo_board":{"address":"0x40"}}})" << '\n';
    }

    const Config from_int = config_load_file(numeric);
    expect(from_int.i2c().servo_board().address() == "0x40", "hex string address 0x40");

    const std::string bad = (dir / "bad.json").string();
    std::string invalid_error;
    try {
        {
            std::ofstream out(bad);
            out << "{ not json" << '\n';
        }
        config_load_file(bad);
    } catch (const ConfigError &ex) {
        invalid_error = ex.what();
    }

    expect(invalid_error.empty() == false, "invalid JSON throws ConfigError");
    expect(invalid_error.find(bad) != std::string::npos,
           "config file load error names the offending file");

    bool range_threw = false;
    try {
        const std::string range = (dir / "range.json").string();
        {
            std::ofstream out(range);
            out << R"({"servos":{"head_neck":16}})" << '\n';
        }
        config_load_file(range);
    } catch (const ConfigError &) {
        range_threw = true;
    }

    expect(range_threw, "channel 16 throws ConfigError");

    unsetenv("DOGGY_CONFIG");

    // getStatus() returns Status by value. Ranging over temporary.errors() in
    // C++20 dangles (Pi SEGV). Copy the message first, same as main.cpp.
    auto status_with_i2c_errors = []() {
        DogStatus status;
        doggy::v1::Error *err = status.add_errors();
        err->set_code(doggy::v1::i2c);
        err->set_message("Could not open IMU on /dev/i2c-1 address 0x68");
        err = status.add_errors();
        err->set_code(doggy::v1::i2c);
        err->set_message("Could not open ADC on /dev/i2c-1 address 0x48");
        return status;
    };
    const std::vector<std::string> startup_errors =
            status_error_messages(status_with_i2c_errors());
    expect(startup_errors.size() == 2,
           "status_error_messages copies errors from a temporary Status");
    expect(startup_errors[0] == "Could not open IMU on /dev/i2c-1 address 0x68"
                    && startup_errors[1]
                            == "Could not open ADC on /dev/i2c-1 address 0x48",
           "status_error_messages preserves i2c error text");
    expect(status_error_messages(DogStatus{}).empty(),
           "status_error_messages is empty when Status has no errors");

    expect(config_default_path() == kDefaultConfigPath,
           "default path is /etc/doggy/doggy.json");
    setenv("DOGGY_CONFIG", created.c_str(), 1);
    expect(config_default_path() == created, "DOGGY_CONFIG overrides default path");
    unsetenv("DOGGY_CONFIG");

    std::filesystem::remove_all(dir);

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
