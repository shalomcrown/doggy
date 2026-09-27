#include "watch_rover_worker.h"

#include "watch_doggy.h"
#include "watch_rover_client.h"
#include "watch_settings.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

inline constexpr unsigned long kWorkerPeriodMs = 25;
inline constexpr unsigned long kHeartbeatIntervalMs = 750;
inline constexpr unsigned long kDriveResendMs = 50;
inline constexpr unsigned long kHeartbeatForceWhileDrivingMs = 1200;
inline constexpr float kDriveEpsilon = 0.02f;
inline constexpr std::size_t kWorkerStackWords = 8192;

enum class WorkerAction {
    None,
    Stop,
    Drive,
    Heartbeat,
};

struct WorkerPlan {
    WorkerAction action = WorkerAction::None;
    WatchDoggyTarget target{};
    bool target_valid = false;
    float speed = 0.0f;
    float turn = 0.0f;
};

std::mutex state_mutex;
TaskHandle_t worker_task = nullptr;

bool wifi_connected = false;
bool screen_active = false;
bool stick_active = false;
bool stop_pending = false;
bool rover_confirmed = true;
WatchDoggyTarget active_target{};
bool active_target_valid = false;

float command_speed = 0.0f;
float command_turn = 0.0f;
float last_sent_speed = 0.0f;
float last_sent_turn = 0.0f;
unsigned long last_heartbeat_ms = 0;
unsigned long last_drive_send_ms = 0;

char message[96] = "Swipe right from the clock for rover control.";
std::uint32_t message_revision = 0;

// ================================================================================

void set_message_locked(const char *text) {
    char next[96] = {};
    if (text != nullptr) {
        std::snprintf(next, sizeof(next), "%s", text);
    }

    if (std::strncmp(message, next, sizeof(message)) == 0) {
        return;
    }

    std::snprintf(message, sizeof(message), "%s", next);
    message_revision += 1;
}

// ================================================================================

void copy_selected_target_locked() {
    const WatchDoggyTarget *target = watch_settings_selected_doggy();
    if (target == nullptr || watch_doggy_target_valid(*target) == false) {
        active_target_valid = false;
        active_target = {};
        return;
    }

    active_target = *target;
    active_target_valid = true;
}

// ================================================================================

bool drive_command_changed_locked(float speed, float turn) {
    return std::fabs(speed - last_sent_speed) > kDriveEpsilon
            || std::fabs(turn - last_sent_turn) > kDriveEpsilon;
}

// ================================================================================

bool heartbeat_due_locked(unsigned long now_ms) {
    return last_heartbeat_ms == 0
            || now_ms - last_heartbeat_ms >= kHeartbeatIntervalMs;
}

// ================================================================================

bool heartbeat_urgent_while_driving_locked(unsigned long now_ms) {
    return last_heartbeat_ms != 0
            && now_ms - last_heartbeat_ms >= kHeartbeatForceWhileDrivingMs;
}

// ================================================================================

WorkerPlan build_plan_locked(unsigned long now_ms) {
    WorkerPlan plan{};

    if (stop_pending) {
        command_speed = 0.0f;
        command_turn = 0.0f;
        stick_active = false;
        if (wifi_connected && active_target_valid) {
            plan.action = WorkerAction::Stop;
            plan.target = active_target;
            plan.target_valid = true;
        }
        return plan;
    }

    if (screen_active == false) {
        return plan;
    }

    if (wifi_connected == false) {
        set_message_locked("Check WiFi — rover offline");
        return plan;
    }

    if (active_target_valid == false) {
        set_message_locked("No doggy selected — open Doggys");
        return plan;
    }

    if (rover_confirmed == false) {
        return plan;
    }

    plan.target = active_target;
    plan.target_valid = true;

    const bool driving = stick_active
            || drive_command_changed_locked(command_speed, command_turn);
    const bool heartbeat_due = heartbeat_due_locked(now_ms);
    const bool heartbeat_urgent = heartbeat_urgent_while_driving_locked(now_ms);

    if (driving && heartbeat_due && heartbeat_urgent == false) {
        plan.action = WorkerAction::Drive;
        plan.speed = command_speed;
        plan.turn = command_turn;
        return plan;
    }

    if (heartbeat_due) {
        plan.action = WorkerAction::Heartbeat;
        return plan;
    }

    const bool resend_due =
            stick_active && now_ms - last_drive_send_ms >= kDriveResendMs;
    const bool command_changed =
            drive_command_changed_locked(command_speed, command_turn);
    if (command_changed || resend_due) {
        plan.action = WorkerAction::Drive;
        plan.speed = command_speed;
        plan.turn = command_turn;
    }

    return plan;
}

// ================================================================================

void apply_stop_result_locked(WatchRoverPostResult result) {
    if (result == WatchRoverPostResult::busy) {
        set_message_locked("Rover busy — stopping…");
        return;
    }

    if (result == WatchRoverPostResult::not_rover) {
        rover_confirmed = false;
        stop_pending = false;
        set_message_locked("Selected robot is not a rover");
        return;
    }

    if (result != WatchRoverPostResult::ok) {
        set_message_locked("Stop failed — retrying…");
        return;
    }

    stop_pending = false;
    rover_confirmed = true;
    last_sent_speed = 0.0f;
    last_sent_turn = 0.0f;
    last_drive_send_ms = millis();
    set_message_locked("Rover stopped");
}

// ================================================================================

void apply_drive_result_locked(
        WatchRoverPostResult result,
        float speed,
        float turn) {
    if (result == WatchRoverPostResult::busy) {
        set_message_locked("Rover busy — try again");
        return;
    }

    if (result == WatchRoverPostResult::not_rover) {
        rover_confirmed = false;
        set_message_locked("Selected robot is not a rover");
        return;
    }

    if (result != WatchRoverPostResult::ok) {
        set_message_locked("Rover unreachable — check Pi / Wi‑Fi");
        return;
    }

    rover_confirmed = true;
    last_sent_speed = speed;
    last_sent_turn = turn;
    last_drive_send_ms = millis();
    if (speed == 0.0f && turn == 0.0f) {
        set_message_locked("Rover stopped");
        return;
    }

    if (message[0] != '\0' && std::strstr(message, "busy") == nullptr) {
        set_message_locked("");
    }
}

// ================================================================================

void apply_heartbeat_result_locked(WatchRoverPostResult result) {
    last_heartbeat_ms = millis();
    if (result == WatchRoverPostResult::ok) {
        if (message[0] == '\0' || std::strstr(message, "heartbeat") != nullptr
                || std::strstr(message, "unreachable") != nullptr) {
            set_message_locked("");
        }
        return;
    }

    if (result == WatchRoverPostResult::busy) {
        set_message_locked("Rover busy");
        return;
    }

    set_message_locked("Heartbeat failed — rover may stop");
}

// ================================================================================

void execute_plan(const WorkerPlan &plan) {
    if (plan.action == WorkerAction::Stop) {
        if (plan.target_valid == false) {
            return;
        }

        const WatchRoverPostResult result = watch_rover_post_stop(plan.target);
        std::lock_guard<std::mutex> lock(state_mutex);
        apply_stop_result_locked(result);
        return;
    }

    if (plan.action == WorkerAction::None || plan.target_valid == false) {
        return;
    }

    if (plan.action == WorkerAction::Drive) {
        const WatchRoverPostResult result = watch_rover_post_drive(
                plan.target,
                plan.speed,
                plan.turn);
        std::lock_guard<std::mutex> lock(state_mutex);
        apply_drive_result_locked(result, plan.speed, plan.turn);
        return;
    }

    if (plan.action == WorkerAction::Heartbeat) {
        const WatchRoverPostResult result =
                watch_rover_post_heartbeat(plan.target);
        std::lock_guard<std::mutex> lock(state_mutex);
        apply_heartbeat_result_locked(result);
    }
}

// ================================================================================

void worker_loop() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kWorkerPeriodMs));

        WorkerPlan plan{};
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            plan = build_plan_locked(millis());
        }

        execute_plan(plan);
    }
}

// ================================================================================

void worker_entry(void *) {
    worker_loop();
}

}  // namespace

// ================================================================================

void watch_rover_worker_begin() {
    if (worker_task != nullptr) {
        return;
    }

    xTaskCreate(
            worker_entry,
            "rover_api",
            kWorkerStackWords,
            nullptr,
            1,
            &worker_task);
}

// ================================================================================

void watch_rover_worker_set_wifi_connected(bool connected) {
    bool was_connected = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        was_connected = wifi_connected;
        wifi_connected = connected;
    }

    if (connected && was_connected == false) {
        watch_rover_client_reset_session();
    }
}

// ================================================================================

void watch_rover_worker_set_screen_active(bool active) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (screen_active == active) {
        return;
    }

    screen_active = active;
    stick_active = false;
    command_speed = 0.0f;
    command_turn = 0.0f;
    last_heartbeat_ms = 0;
    rover_confirmed = true;
    copy_selected_target_locked();

    if (screen_active) {
        watch_rover_client_reset_session();
    }

    if (screen_active == false) {
        stop_pending = true;
        set_message_locked("Swipe right from the clock for rover control.");
        return;
    }

    if (active_target_valid == false) {
        set_message_locked("No doggy selected — open Doggys");
        return;
    }

    set_message_locked("");
}

// ================================================================================

void watch_rover_worker_set_drive_command(
        float speed,
        float turn,
        bool stick) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (screen_active == false) {
        return;
    }

    if (active_target_valid == false) {
        set_message_locked("No doggy selected — open Doggys");
        return;
    }

    if (rover_confirmed == false) {
        return;
    }

    command_speed = speed;
    command_turn = turn;
    stick_active = stick;
    if (stick) {
        stop_pending = false;
    } else {
        stop_pending = true;
    }
}

// ================================================================================

void watch_rover_worker_request_stop() {
    std::lock_guard<std::mutex> lock(state_mutex);
    stop_pending = true;
    stick_active = false;
    command_speed = 0.0f;
    command_turn = 0.0f;
}

// ================================================================================

void watch_rover_worker_refresh_selection_message() {
    std::lock_guard<std::mutex> lock(state_mutex);
    copy_selected_target_locked();
    if (screen_active == false) {
        return;
    }

    if (active_target_valid == false) {
        set_message_locked("No doggy selected — open Doggys");
        return;
    }

    set_message_locked("");
}

// ================================================================================

void watch_rover_worker_message(char *buffer, std::size_t length) {
    if (buffer == nullptr || length == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex);
    std::snprintf(buffer, length, "%s", message);
}

// ================================================================================

std::uint32_t watch_rover_worker_message_revision() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return message_revision;
}
