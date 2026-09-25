#include "watch_joystick_policy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool condition, const char *name) {
    if (condition) {
        return;
    }

    std::cerr << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static void expect_near(float left, float right, const char *name) {
    if (std::fabs(left - right) < 0.0001f) {
        return;
    }

    std::cerr << "FAIL " << name << " got " << left << " expected " << right
              << std::endl;
    failures += 1;
}

// ================================================================================

int main() {
    expect_near(
            watch_joystick_apply_deadzone(0.0f, kWatchJoystickDeadzone),
            0.0f,
            "dead zone center is zero");
    expect_near(
            watch_joystick_apply_deadzone(0.1f, kWatchJoystickDeadzone),
            0.0f,
            "inside dead zone is zero");
    expect_near(
            watch_joystick_apply_deadzone(1.0f, kWatchJoystickDeadzone),
            1.0f,
            "full throw is unchanged");

    float speed = 0.0f;
    float turn = 0.0f;
    watch_joystick_drive_from_stick(0.0f, 0.0f, kWatchJoystickDeadzone, &speed, &turn);
    expect_near(speed, 0.0f, "center stick speed is zero");
    expect_near(turn, 0.0f, "center stick turn is zero");

    watch_joystick_drive_from_stick(0.0f, 1.0f, kWatchJoystickDeadzone, &speed, &turn);
    expect_near(speed, 1.0f, "full forward");
    expect_near(turn, 0.0f, "forward has no turn");

    watch_joystick_drive_from_stick(1.0f, 0.0f, kWatchJoystickDeadzone, &speed, &turn);
    expect_near(speed, 0.0f, "full right has no speed");
    expect_near(turn, 1.0f, "full right turn");

    float nx = 0.0f;
    float ny = 0.0f;
    watch_joystick_normalize_stick_offset(0.0f, 70.0f, 70.0f, &nx, &ny);
    expect_near(nx, 0.0f, "pad-top offset has no turn");
    expect_near(ny, 1.0f, "pad-top offset is full forward");
    watch_joystick_normalize_stick_offset(80.0f, 80.0f, 70.0f, &nx, &ny);
    const float rim_mag = std::hypot(nx, ny);
    expect_near(rim_mag, 1.0f, "corner beyond rim clamps to unit circle");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
