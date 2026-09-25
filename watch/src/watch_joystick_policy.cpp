#include "watch_joystick_policy.h"

#include <cmath>

// ================================================================================

float watch_joystick_apply_deadzone(float value, float dead) {
    const float magnitude = std::fabs(value);
    if (magnitude <= dead) {
        return 0.0f;
    }

    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * (magnitude - dead) / (1.0f - dead);
}

// ================================================================================

void watch_joystick_normalize_stick_offset(
        float delta_x,
        float delta_y,
        float travel,
        float *normalized_x,
        float *normalized_y) {
    if (normalized_x == nullptr || normalized_y == nullptr || travel <= 0.0f) {
        return;
    }

    float nx = delta_x / travel;
    float ny = delta_y / travel;
    const float mag = std::hypot(nx, ny);
    if (mag > 1.0f) {
        nx /= mag;
        ny /= mag;
    }

    *normalized_x = nx;
    *normalized_y = ny;
}

// ================================================================================

void watch_joystick_drive_from_stick(
        float nx,
        float ny,
        float dead,
        float *speed,
        float *turn) {
    if (speed == nullptr || turn == nullptr) {
        return;
    }

    const float mag = std::hypot(nx, ny);
    if (mag > 1.0f) {
        nx /= mag;
        ny /= mag;
    }

    *turn = watch_joystick_apply_deadzone(nx, dead);
    *speed = watch_joystick_apply_deadzone(ny, dead);
}
