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
    expect(status_to_json(empty) == "{\"errors\":[]}", "empty status json");

    DogStatus withI2c;
    withI2c.errors.push_back(DogError{
        DogErrorCode::i2c,
        "Could not open i2c bus.: No such file or directory"
    });
    const std::string statusJson = status_to_json(withI2c);
    expect(statusJson.find("\"code\":\"i2c\"") != std::string::npos, "status json has i2c code");
    expect(statusJson.find("Could not open i2c bus.") != std::string::npos,
           "status json has i2c message");

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
