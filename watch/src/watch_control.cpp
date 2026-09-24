#include "watch_control.h"

#include "watch_doggy.h"
#include "watch_joystick_policy.h"
#include "watch_rover_client.h"
#include "watch_rover_status.h"
#include "watch_settings.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

namespace {

inline constexpr unsigned long kHeartbeatIntervalMs = 750;
inline constexpr unsigned long kDriveDebounceMs = 100;

bool screen_active = false;
bool rover_confirmed = false;
bool rover_checked = false;
bool rover_check_pending = false;
bool http_in_flight = false;
bool drive_stop_immediate = false;
bool drive_retry_after_busy = false;
unsigned long last_heartbeat_ms = 0;
unsigned long drive_deadline_ms = 0;
float pending_speed = 0.0f;
float pending_turn = 0.0f;
bool drive_pending = false;
bool stick_held = false;
char message[96] = "Swipe right from the clock for rover control.";

// ================================================================================

void set_message(const char *text) {
    if (text == nullptr) {
        message[0] = '\0';
        return;
    }

    std::snprintf(message, sizeof(message), "%s", text);
}

// ================================================================================

bool fetch_rover_type(const WatchDoggyTarget &target) {
    char base[128] = {};
    if (watch_doggy_target_valid(target) == false) {
        return false;
    }

    const int written = std::snprintf(
            base,
            sizeof(base),
            "https://%s:%u/api/status",
            target.hostname,
            static_cast<unsigned>(target.port));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(base)) {
        return false;
    }

    WiFiClientSecure tls;
    tls.setInsecure();
    HTTPClient http;
    if (http.begin(tls, base) == false) {
        return false;
    }

    http.setTimeout(4000);
    const int code = http.GET();
    String body = http.getString();
    http.end();
    if (code < 200 || code >= 300) {
        return false;
    }

    return watch_rover_status_is_rover(body.c_str(), body.length());
}

// ================================================================================

void note_drive_result(WatchRoverPostResult result, float speed, float turn) {
    if (result == WatchRoverPostResult::busy) {
        drive_retry_after_busy = true;
        set_message("Rover busy — try again");
        return;
    }

    drive_retry_after_busy = false;
    if (result == WatchRoverPostResult::not_rover) {
        rover_confirmed = false;
        set_message("Selected robot is not a rover");
        return;
    }

    if (result != WatchRoverPostResult::ok) {
        set_message("Drive failed — check Wi-Fi");
        return;
    }

    if (speed == 0.0f && turn == 0.0f) {
        set_message("Rover stopped");
    } else if (message[0] != '\0' && std::strstr(message, "busy") == nullptr) {
        set_message("");
    }
}

// ================================================================================

void post_drive(float speed, float turn) {
    const WatchDoggyTarget *target = watch_settings_selected_doggy();
    if (target == nullptr || watch_doggy_target_valid(*target) == false) {
        return;
    }

    if (rover_checked && rover_confirmed == false) {
        return;
    }

    http_in_flight = true;
    const WatchRoverPostResult result =
            watch_rover_post_drive(*target, speed, turn);
    http_in_flight = false;
    note_drive_result(result, speed, turn);
}

// ================================================================================

void post_heartbeat(unsigned long now_ms) {
    const WatchDoggyTarget *target = watch_settings_selected_doggy();
    if (target == nullptr || watch_doggy_target_valid(*target) == false) {
        return;
    }

    if (rover_check_pending) {
        http_in_flight = true;
        rover_confirmed = fetch_rover_type(*target);
        http_in_flight = false;
        rover_checked = true;
        rover_check_pending = false;
        if (rover_confirmed == false) {
            set_message("Selected robot is not a rover");
            return;
        }
    }

    if (rover_confirmed == false) {
        return;
    }

    http_in_flight = true;
    const WatchRoverPostResult result = watch_rover_post_heartbeat(*target);
    http_in_flight = false;
    last_heartbeat_ms = now_ms;
    if (result == WatchRoverPostResult::ok) {
        if (message[0] == '\0' || std::strstr(message, "heartbeat") != nullptr) {
            set_message("");
        }
        return;
    }

    if (result == WatchRoverPostResult::busy) {
        set_message("Rover busy");
        return;
    }

    set_message("Heartbeat failed — rover may stop");
}

// ================================================================================

void service_drive(unsigned long now_ms) {
    if (drive_stop_immediate) {
        drive_stop_immediate = false;
        drive_pending = false;
        drive_retry_after_busy = false;
        pending_speed = 0.0f;
        pending_turn = 0.0f;
        post_drive(0.0f, 0.0f);
        return;
    }

    if (drive_retry_after_busy) {
        drive_pending = true;
        drive_deadline_ms = now_ms;
    }

    if (drive_pending == false) {
        return;
    }

    if (now_ms < drive_deadline_ms) {
        return;
    }

    drive_pending = false;
    const float speed = pending_speed;
    const float turn = pending_turn;
    post_drive(speed, turn);

    if (stick_held && drive_retry_after_busy == false) {
        drive_pending = true;
        drive_deadline_ms = now_ms + kDriveDebounceMs;
    }
}

}  // namespace

// ================================================================================

void watch_control_begin() {
    screen_active = false;
}

// ================================================================================

void watch_control_set_screen_active(bool active) {
    if (screen_active == active) {
        return;
    }

    screen_active = active;
    rover_checked = false;
    rover_confirmed = false;
    rover_check_pending = false;
    drive_pending = false;
    drive_stop_immediate = false;
    drive_retry_after_busy = false;
    stick_held = false;
    pending_speed = 0.0f;
    pending_turn = 0.0f;
    last_heartbeat_ms = 0;

    if (screen_active == false) {
        drive_stop_immediate = true;
        set_message("Swipe right from the clock for rover control.");
        return;
    }

    watch_control_refresh_message();
    rover_check_pending = true;
    last_heartbeat_ms = 0;
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
    if (screen_active == false) {
        return;
    }

    const WatchDoggyTarget *target = watch_settings_selected_doggy();
    if (target == nullptr || watch_doggy_target_valid(*target) == false) {
        set_message("No doggy selected — open Doggys");
        return;
    }

    set_message("");
}

// ================================================================================

void watch_control_message(char *buffer, std::size_t length) {
    if (buffer == nullptr || length == 0) {
        return;
    }

    std::snprintf(buffer, length, "%s", message);
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

    if (rover_checked && rover_confirmed == false) {
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

    pending_speed = speed;
    pending_turn = turn;
    const unsigned long now_ms = millis();

    if (released) {
        stick_held = false;
        pending_speed = 0.0f;
        pending_turn = 0.0f;
        drive_pending = false;
        drive_stop_immediate = true;
        return;
    }

    stick_held = true;
    drive_pending = true;
    drive_deadline_ms = now_ms + kDriveDebounceMs;
}

// ================================================================================

void watch_control_service(bool wifi_connected, unsigned long now_ms) {
    if (wifi_connected == false || http_in_flight) {
        return;
    }

    if (drive_stop_immediate) {
        service_drive(now_ms);
        return;
    }

    if (screen_active == false) {
        return;
    }

    const bool heartbeat_due = last_heartbeat_ms == 0
            || now_ms - last_heartbeat_ms >= kHeartbeatIntervalMs
            || rover_check_pending;

    // One HTTPS transaction per loop tick — heartbeat keeps the GCS watchdog fed
    // even while the stick is held and drive commands are debounced.
    if (heartbeat_due) {
        post_heartbeat(now_ms);
        return;
    }

    service_drive(now_ms);
}
