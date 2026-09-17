#include "rover.h"

#include <cstdlib>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    const Config config = config_from_json(R"({"robot":{"type":"ROVER"}})");
    expect(config.i2c().servo_board().address() == "0x60",
           "rover bonnet defaults to address 0x60");
    expect(config.motors().front_left().pwm() == 2
                    && config.motors().front_left().in2() == 3
                    && config.motors().front_left().in1() == 4,
           "front-left defaults to PWM2 PWM3 PWM4");
    expect(config.motors().front_right().pwm() == 5
                    && config.motors().rear_right().pwm() == 8
                    && config.motors().rear_left().pwm() == 11,
           "motor groups proceed clockwise from front-left");

    Rover rover(config);
    expect(rover.setDrive(0.5, 0.75) == CommandResult::ok,
           "valid speed command succeeds without hardware");
    const DogStatus moving = rover.getStatus();
    expect(moving.speed() == 0.5 && moving.turn() == 0.75,
           "status stores speed and unmixed turn");
    expect(moving.motors().items_size() == 4,
           "rover status contains four motors");
    for (int i = 0; i < moving.motors().items_size(); ++i) {
        expect(moving.motors().items(i).id() == i,
               "motor status ID is stable");
        expect(moving.motors().items(i).pwm() == 2048,
               "all motors receive the same speed PWM");
    }
    expect(rover.setDrive(0.5, -0.75) == CommandResult::ok,
           "changing turn with the same speed succeeds");
    expect(rover.getStatus().motors().items(0).pwm() == 2048,
           "turn does not change motor PWM in this slice");
    expect(rover.stop() == CommandResult::ok,
           "stop command succeeds and clears motor speed");
    expect(rover.getStatus().speed() == 0.0 && rover.getStatus().turn() == 0.0,
           "stop clears the reported drive state");
    expect(rover.brake() == CommandResult::ok,
           "brake command succeeds");

    Config remapped = rover.getConfig();
    remapped.mutable_motors()->mutable_front_left()->set_pwm(0);
    expect(rover.replaceConfig(remapped) == CommandResult::ok,
           "motor channel remap applies immediately");
    expect(rover.getStatus().motors().items(0).pwm() == 2048,
           "channel remap reapplies the last speed");

    Config disabled_config = config;
    disabled_config.mutable_motors()->mutable_front_left()->set_enabled(false);
    Rover disabled(disabled_config);
    expect(disabled.setDrive(1.0, 0.0) == CommandResult::ok,
           "drive with one disabled motor succeeds");
    const DogStatus disabled_status = disabled.getStatus();
    expect(disabled_status.motors().items(0).pwm() == 0,
           "disabled motor reports coast PWM");
    expect(disabled_status.motors().items(1).pwm() == 4095,
           "enabled motor reports full PWM");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
