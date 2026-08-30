#include "config.h"
#include "i2c_interface.hpp"

#include <mbedtls/sha256.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

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
    Config defaults;
    expect(defaults.servos.front_right_waist == 11, "default front_right_waist is 11");
    expect(defaults.servos.front_right_hip == 12, "default front_right_hip is 12");
    expect(defaults.servos.front_right_knee == 13, "default front_right_knee is 13");
    expect(defaults.servos.front_left_waist == 4, "default front_left_waist is 4");
    expect(defaults.servos.front_left_hip == 3, "default front_left_hip is 3");
    expect(defaults.servos.front_left_knee == 2, "default front_left_knee is 2");
    expect(defaults.servos.rear_left_waist == 7, "default rear_left_waist is 7");
    expect(defaults.servos.rear_left_hip == 6, "default rear_left_hip is 6");
    expect(defaults.servos.rear_left_knee == 5, "default rear_left_knee is 5");
    expect(defaults.servos.rear_right_waist == 8, "default rear_right_waist is 8");
    expect(defaults.servos.rear_right_hip == 9, "default rear_right_hip is 9");
    expect(defaults.servos.rear_right_knee == 10, "default rear_right_knee is 10");
    expect(defaults.servos.head_neck == 15, "default head_neck is 15");
    expect(defaults.i2c.servo_board.bus == 1 && defaults.i2c.servo_board.address == 0x40,
           "default servo_board is bus 1 address 0x40");
    expect(defaults.i2c.imu.bus == 1 && defaults.i2c.imu.address == 0x68,
           "default imu is bus 1 address 0x68");
    expect(defaults.i2c.ads.bus == 1 && defaults.i2c.ads.address == 0x48,
           "default ads is bus 1 address 0x48");
    expect(i2c_device_path(1) == "/dev/i2c-1", "i2c_device_path 1 is /dev/i2c-1");

    const std::string dumped = defaults.to_json_string();
    expect(dumped.find("\"0x40\"") != std::string::npos,
           "to_json_string writes hex servo_board address");
    expect(dumped.find("\"front_right_waist\":11") != std::string::npos,
           "to_json_string writes default channels");

    const Config from_text = Config::from_json_string(
            R"({"servos":{"head_neck":14},"i2c":{"imu":{"address":"0x69"}}})");
    expect(from_text.servos.head_neck == 14, "from_json_string overlays head_neck");
    expect(from_text.servos.front_right_waist == 11,
           "from_json_string keeps default waist");
    expect(from_text.i2c.imu.address == 0x69, "from_json_string overlays imu address");

    bool from_text_threw = false;
    try {
        Config::from_json_string("{ not json");
    } catch (const ConfigError &) {
        from_text_threw = true;
    }

    expect(from_text_threw, "from_json_string invalid JSON throws ConfigError");

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

    const std::string hashed = Config::hash_pin("1234");
    expect(hashed.find("sha256$") == 0, "hash_pin uses salted SHA-256");
    expect(Config::pin_matches("1234", hashed), "pin_matches accepts the PIN");
    expect(Config::pin_matches("0000", hashed) == false, "pin_matches rejects a wrong PIN");
    expect(Config::pin_length_ok("123") == false, "PIN shorter than 4 is rejected");

    Config with_pin;
    with_pin.system.pin_hash = hashed;
    const std::string public_json = with_pin.to_public_json_string();
    expect(public_json.find("\"pin_set\":true") != std::string::npos,
           "public JSON reports pin_set");
    expect(public_json.find("pin_hash") == std::string::npos,
           "public JSON omits pin_hash");
    expect(with_pin.to_json_string().find("pin_hash") != std::string::npos,
           "file JSON keeps pin_hash");

    const std::filesystem::path dir =
            std::filesystem::temp_directory_path()
            / ("doggy-config-" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string created = (dir / "doggy.json").string();

    std::string create_error;
    const Config written = Config::load_or_create(created, &create_error);
    expect(create_error.empty(), "load_or_create writes defaults without error");
    expect(std::filesystem::is_regular_file(created), "load_or_create creates the file");
    expect(written.servos.front_right_waist == 11, "created config keeps default channel");

    const Config reloaded = Config::load_file(created);
    expect(reloaded.i2c.servo_board.address == 0x40, "reloaded servo_board address is 0x40");
    expect(reloaded.i2c.imu.address == 0x68, "reloaded imu address is 0x68");
    expect(reloaded.i2c.ads.address == 0x48, "reloaded ads address is 0x48");
    expect(reloaded.servos.head_neck == 15, "reloaded head_neck is 15");

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

    const Config overlaid = Config::load_file(overlay);
    expect(overlaid.servos.head_neck == 14, "overlay changes head_neck");
    expect(overlaid.servos.front_right_waist == 11, "overlay keeps default waist");
    expect(overlaid.i2c.imu.address == 0x69, "overlay hex string address");
    expect(overlaid.i2c.servo_board.address == 0x40, "overlay keeps default servo address");

    const std::string numeric = (dir / "numeric.json").string();
    {
        std::ofstream out(numeric);
        out << R"({"i2c":{"servo_board":{"address":64}}})" << '\n';
    }

    const Config from_int = Config::load_file(numeric);
    expect(from_int.i2c.servo_board.address == 0x40, "integer 64 is address 0x40");

    bool invalid_threw = false;
    try {
        const std::string bad = (dir / "bad.json").string();
        {
            std::ofstream out(bad);
            out << "{ not json" << '\n';
        }
        Config::load_file(bad);
    } catch (const ConfigError &) {
        invalid_threw = true;
    }

    expect(invalid_threw, "invalid JSON throws ConfigError");

    bool range_threw = false;
    try {
        const std::string range = (dir / "range.json").string();
        {
            std::ofstream out(range);
            out << R"({"servos":{"head_neck":16}})" << '\n';
        }
        Config::load_file(range);
    } catch (const ConfigError &) {
        range_threw = true;
    }

    expect(range_threw, "channel 16 throws ConfigError");

    unsetenv("DOGGY_CONFIG");
    expect(Config::default_path() == kDefaultConfigPath,
           "default path is /etc/doggy/doggy.json");
    setenv("DOGGY_CONFIG", created.c_str(), 1);
    expect(Config::default_path() == created, "DOGGY_CONFIG overrides default path");
    unsetenv("DOGGY_CONFIG");

    std::filesystem::remove_all(dir);

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
