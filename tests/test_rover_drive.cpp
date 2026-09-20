#include "rover.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

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
    const Config config = config_from_json(
            R"({"robot":{"type":"ROVER","turn_gain_min":1.0}})");
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
    expect(rover.setDrive(0.5, 0.0) == CommandResult::ok,
           "valid speed command succeeds without hardware");
    const DogStatus moving = rover.getStatus();
    expect(moving.speed() == 0.5 && moving.turn() == 0.0,
           "status stores commanded speed and turn");
    expect(moving.motors().items_size() == 4,
           "rover status contains four motors");
    for (int i = 0; i < moving.motors().items_size(); ++i) {
        expect(moving.motors().items(i).id() == i,
               "motor status ID is stable");
        expect(moving.motors().items(i).pwm() == 2048,
               "straight drive sends the same PWM to every motor");
    }
    expect(rover.setDrive(0.5, 0.75) == CommandResult::ok,
           "changing turn with the same speed succeeds");
    expect(rover.getStatus().motors().items(0).pwm() == 4095
                    && rover.getStatus().motors().items(2).pwm() == 4095,
           "positive turn raises left-side PWM");
    expect(rover.getStatus().motors().items(1).pwm() == 819
                    && rover.getStatus().motors().items(3).pwm() == 819,
           "positive turn lowers right-side PWM");
    expect(rover.setDrive(0.0, 1.0) == CommandResult::ok,
           "pivot command succeeds");
    expect(rover.getStatus().motors().items(0).pwm() == 4095
                    && rover.getStatus().motors().items(1).pwm() == 4095,
           "in-place spin drives both sides at full PWM");

    expect(rover.setDrive(0.5, 0.0) == CommandResult::ok,
           "straight drive before remap succeeds");

    Config remapped = rover.getConfig();
    remapped.mutable_motors()->mutable_front_left()->set_pwm(0);
    expect(rover.replaceConfig(remapped) == CommandResult::ok,
           "motor channel remap applies immediately");
    expect(rover.getStatus().motors().items(0).pwm() == 2048,
           "channel remap reapplies the last speed");

    expect(rover.stop() == CommandResult::ok,
           "stop command succeeds and clears motor speed");
    expect(rover.getStatus().speed() == 0.0 && rover.getStatus().turn() == 0.0,
           "stop clears the reported drive state");
    expect(rover.brake() == CommandResult::ok,
           "brake command succeeds");
    expect(rover.setDrive(0.4, 0.2) == CommandResult::ok,
           "drive after stop succeeds");
    expect(rover.brake() == CommandResult::ok,
           "brake from motion succeeds");
    expect(rover.getStatus().speed() == 0.0 && rover.getStatus().turn() == 0.0,
           "brake clears the reported drive state");
    expect(rover.getStatus().motors().items(0).pwm() == 0,
           "brake clears commanded motor PWM");

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

    Config watchdog_config = config;
    watchdog_config.mutable_robot()->set_gcs_timeout_s(2);
    Rover watchdog_rover(watchdog_config);
    expect(watchdog_rover.setDrive(0.5, 0.0) == CommandResult::ok,
           "drive command arms the GCS watchdog");
    expect(watchdog_rover.heartbeat() == CommandResult::ok,
           "heartbeat refreshes the GCS watchdog");
    watchdog_rover.poll();
    expect(watchdog_rover.getStatus().motors().items(0).pwm() == 2048,
           "fresh heartbeat keeps rover motors running");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    expect(watchdog_rover.getStatus().speed() == 0.5,
           "status reads do not refresh GCS liveness");
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    const DogStatus timed_out = watchdog_rover.getStatus();
    expect(timed_out.speed() == 0.0 && timed_out.turn() == 0.0,
           "expired GCS watchdog clears drive state");
    expect(timed_out.motors().items(0).pwm() == 0,
           "expired GCS watchdog coasts rover motors");
    bool has_gcs_timeout = false;
    for (const doggy::v1::Error &error : timed_out.errors()) {
        if (error.code() == doggy::v1::gcs) {
            has_gcs_timeout = true;
        }
    }
    expect(has_gcs_timeout,
           "expired GCS watchdog reports a distinct GCS error");

    Config tame = config_from_json(R"({"robot":{"type":"ROVER"}})");
    Rover tame_rover(tame);
    expect(tame_rover.setDrive(0.0, 1.0) == CommandResult::ok,
           "default gain pivot succeeds");
    expect(tame_rover.getStatus().motors().items(0).pwm() == 1024
                    && tame_rover.getStatus().motors().items(1).pwm() == 1024,
           "default turn_gain_min 0.25 quarters rest-spin PWM");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
