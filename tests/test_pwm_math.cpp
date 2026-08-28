#include "pwm_math.h"
#include "web_json.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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
    const uint16_t ticks = pwm_ticks_from_angle(90.0, 50.0, 0.5, 2.0, 180.0);
    expect(ticks == 307, "90 degrees at 50 Hz is 307 ticks");

    expect(pwm_ticks_from_angle(-10.0, 50.0, 0.5, 2.0, 180.0)
                   == pwm_ticks_from_angle(0.0, 50.0, 0.5, 2.0, 180.0),
           "negative angle clamps to 0");

    const std::vector<ServoSnapshot> items = {
        {11, "front-right-waist", 90.0, 307}
    };
    const std::string json = servos_to_json(items);
    expect(json.find("\"items\"") != std::string::npos, "json has items wrapper");
    expect(json.find("front-right-waist") != std::string::npos, "json has name");
    expect(json.find("\"id\":11") != std::string::npos, "json has id");

    double angle = 0.0;
    expect(parse_angle_json("{\"angle\":120.5}", angle), "parse angle json");
    expect(std::abs(angle - 120.5) < 0.001, "parsed angle value");
    expect(parse_angle_json("{}", angle) == false, "missing angle fails");

    DogStatus empty;
    const std::string emptyJson = status_to_json(empty, "1.2.3-fixture");
    expect(emptyJson.find("\"version\":\"1.2.3-fixture\"") != std::string::npos,
           "empty status json has version");
    expect(emptyJson.find("\"errors\":[]") != std::string::npos, "empty status json errors");
    expect(emptyJson.find("\"imu\"") != std::string::npos, "empty status json has imu");
    expect(emptyJson.find("\"battery\"") != std::string::npos, "empty status json has battery");
    expect(emptyJson.find("\"ok\":false") != std::string::npos, "empty status imu.ok false");
    expect(emptyJson.find("\"voltage_v\"") != std::string::npos,
           "empty status json has voltage_v");

    DogStatus withBattery;
    withBattery.battery.ok = true;
    withBattery.battery.voltage_v = 7.4;
    const std::string batteryJson = status_to_json(withBattery, "1.2.3-fixture");
    expect(batteryJson.find("\"ok\":true") != std::string::npos,
           "status json battery.ok true");
    expect(batteryJson.find("\"voltage_v\":7.4") != std::string::npos,
           "status json battery voltage");

    DogStatus withI2c;
    withI2c.errors.push_back(DogError{
        DogErrorCode::i2c,
        "Could not open i2c bus.: No such file or directory"
    });
    const std::string statusJson = status_to_json(withI2c, "1.2.3-fixture");
    const std::string escapedJson = status_to_json(empty, "a\"b");
    expect(escapedJson.find("\"version\":\"a\\\"b\"") != std::string::npos,
           "status json version is json-escaped");
    expect(statusJson.find("\"code\":\"i2c\"") != std::string::npos, "status json has i2c code");
    expect(statusJson.find("Could not open i2c bus.") != std::string::npos,
           "status json has i2c message");

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
