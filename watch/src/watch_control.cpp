#include "watch_control.h"

#include "watch_joystick_policy.h"
#include "watch_rover_worker.h"
#include "watch_settings.h"

#include <cstdio>

// ================================================================================

static bool screen_active = false;

// ================================================================================

void watch_control_begin() {
    screen_active = false;
    watch_rover_worker_begin();
}

// ================================================================================

void watch_control_set_screen_active(bool active) {
    if (screen_active == active) {
        return;
    }

    screen_active = active;
    watch_rover_worker_set_screen_active(active);
    if (active) {
        watch_control_refresh_message();
    }
}

// ================================================================================

bool watch_control_screen_active() {
    return screen_active;
}

// ================================================================================

bool watch_control_prevents_sleep() {
    return screen_active;
}

// ================================================================================

void watch_control_refresh_message() {
    watch_rover_worker_refresh_selection_message();
}

// ================================================================================

void watch_control_message(char *buffer, std::size_t length) {
    watch_rover_worker_message(buffer, length);
}

// ================================================================================

void watch_control_stick(float normalized_x, float normalized_y, bool released) {
    if (screen_active == false) {
        return;
    }

    const WatchDoggyTarget *target = watch_settings_selected_doggy();
    if (target == nullptr || watch_doggy_target_valid(*target) == false) {
        return;
    }

    float speed = 0.0f;
    float turn = 0.0f;
    watch_joystick_drive_from_stick(
            normalized_x,
            normalized_y,
            kWatchJoystickDeadzone,
            &speed,
            &turn);

    if (released) {
        watch_rover_worker_set_drive_command(0.0f, 0.0f, false);
        return;
    }

    watch_rover_worker_set_drive_command(speed, turn, true);
}

// ================================================================================

void watch_control_service(bool wifi_connected, unsigned long) {
    watch_rover_worker_set_wifi_connected(wifi_connected);
}
