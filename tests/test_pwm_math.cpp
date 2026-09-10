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

static ServoSnapshot make_item(int id, const char *name, int pwm, bool has_angle, double angle) {
    ServoSnapshot item;
    item.set_id(id);
    item.set_name(name);
    item.set_pwm(pwm);
    if (has_angle) {
        item.set_angle(angle);
    }
    return item;
}

int main() {
    const uint16_t ticks = pwm_ticks_from_angle(90.0, 50.0, 0.5, 2.0, 180.0);
    expect(ticks == 307, "90 degrees at 50 Hz is 307 ticks");

    expect(pwm_ticks_from_angle(-10.0, 50.0, 0.5, 2.0, 180.0)
                   == pwm_ticks_from_angle(0.0, 50.0, 0.5, 2.0, 180.0),
           "negative angle clamps to 0");

    const std::vector<ServoSnapshot> items = {
        make_item(11, "front-right-waist", 307, true, 90.0)
    };
    const std::string json = servos_to_json(items);
    expect(json.find("\"items\"") != std::string::npos, "json has items wrapper");
    expect(json.find("front-right-waist") != std::string::npos, "json has name");
    expect(json.find("\"id\":11") != std::string::npos, "json has id");
    expect(json.find("\"angle\":90") != std::string::npos, "json has numeric angle when pwm nonzero");

    const std::string offJson = servos_to_json({make_item(11, "front-right-waist", 0, false, 90.0)});
    expect(offJson.find("\"angle\"") == std::string::npos,
           "json omits angle when pwm is 0");
    expect(offJson.find("\"pwm\":0") != std::string::npos, "json has pwm 0");

    double angle = 0.0;
    bool disable = false;
    expect(parse_servo_post("{\"angle\":120.5}", disable, angle), "parse angle json");
    expect(std::abs(angle - 120.5) < 0.001, "parsed angle value");
    expect(parse_servo_post("{}", disable, angle) == false, "missing angle fails");

    expect(parse_servo_post("{\"enabled\":false}", disable, angle), "parse enabled false");
    expect(disable, "enabled false sets disable");
    expect(parse_servo_post("{\"enabled\": false, \"angle\":10}", disable, angle),
           "enabled false wins over angle");
    expect(disable, "enabled false with angle still disables");
    expect(parse_servo_post("{\"angle\":45}", disable, angle), "parse angle still works");
    expect(disable == false, "angle post is not disable");
    expect(std::abs(angle - 45.0) < 0.001, "parsed servo post angle");
    expect(parse_servo_post("{\"enabled\":true}", disable, angle) == false,
           "enabled true without angle fails");
    expect(parse_servo_post("{}", disable, angle) == false, "empty servo post fails");

    DogStatus empty;
    const std::string emptyJson = status_to_json(empty, "1.2.3-fixture");
    expect(emptyJson.find("\"version\":\"1.2.3-fixture\"") != std::string::npos,
           "empty status json has version");
    expect(emptyJson.find("\"imu\"") != std::string::npos, "empty status json has imu");
    expect(emptyJson.find("\"battery\"") != std::string::npos, "empty status json has battery");
    expect(emptyJson.find("\"ok\":false") != std::string::npos, "empty status imu.ok false");
    expect(emptyJson.find("\"type\":\"DOG\"") != std::string::npos,
           "empty status json has DOG type");

    DogStatus withBattery;
    withBattery.mutable_battery()->set_ok(true);
    withBattery.mutable_battery()->set_voltage_v(7.4);
    const std::string batteryJson = status_to_json(withBattery, "1.2.3-fixture");
    expect(batteryJson.find("\"ok\":true") != std::string::npos,
           "status json battery.ok true");
    expect(batteryJson.find("\"voltage_v\":7.4") != std::string::npos,
           "status json battery voltage");

    DogStatus withI2c;
    doggy::v1::Error *err = withI2c.add_errors();
    err->set_code(doggy::v1::i2c);
    err->set_message("Could not open i2c bus.: No such file or directory");
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
