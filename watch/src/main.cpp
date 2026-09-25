#include "board_hal.h"
#include "watch_control.h"
#include "watch_discovery.h"
#include "watch_network.h"
#include "watch_settings.h"
#include "watch_sleep_policy.h"
#include "watch_ui.h"

#include <Arduino.h>
#include <lvgl.h>

namespace {

inline constexpr unsigned long kUiRefreshMs = 250;
unsigned long last_ui_refresh = 0;
bool application_ready = false;

}

// ================================================================================

void setup() {
    watch_settings_begin();
    if (watch_board_begin() == false) {
        return;
    }
    watch_ui_begin();
    watch_network_begin();
    watch_control_begin();
    application_ready = true;
}

// ================================================================================

static WatchUiState collect_ui_state() {
    WatchUiState state{};
    state.utc_time = watch_network_utc_time();
    state.time_valid = watch_network_time_valid();
    state.network_status = watch_network_status();
    state.ssid = watch_network_ssid();
    state.wifi_connected = watch_network_connected();
    state.rssi = watch_network_rssi();
    state.sync_age_seconds = watch_network_sync_age_seconds();
    state.battery_valid = watch_board_battery(
            state.battery_percent,
            state.battery_charging);
    return state;
}

// ================================================================================

// LVGL already tracks the last touch, so the idle clock needs no separate
// bookkeeping and cannot drift away from what the user actually did.
static void service_idle_sleep() {
    // Pairing needs the screen and the radio up however long the phone takes.
    if (watch_network_provisioning_active()) {
        lv_display_trigger_activity(nullptr);
        return;
    }
    if (watch_discovery_state() == WatchDiscoveryState::Searching) {
        // The IDF query is asynchronous, but the radio must stay up until its
        // bounded three-second window closes.
        lv_display_trigger_activity(nullptr);
        return;
    }
    if (watch_control_prevents_sleep()) {
        lv_display_trigger_activity(nullptr);
        return;
    }
    const bool due = watch_idle_should_sleep(
            lv_display_get_inactive_time(nullptr),
            0,
            watch_settings_idle_timeout_seconds());
    if (due == false) {
        return;
    }

    watch_ui_sleep();
    watch_network_suspend();
    watch_board_sleep();
    watch_network_resume();
    // Both panels were powered down mid-frame, and LVGL only redraws what it
    // thinks changed, so the whole screen is marked dirty.
    lv_obj_invalidate(lv_screen_active());
    // The wake touch reaches the panel, not LVGL, so the idle clock is restarted
    // by hand.
    lv_display_trigger_activity(nullptr);
}

// ================================================================================

void loop() {
    if (application_ready == false) {
        delay(1000);
        return;
    }
    watch_board_poll_ui();
    watch_network_service();
    watch_discovery_service(watch_network_connected());
    watch_control_service(watch_network_connected(), millis());
    if (millis() - last_ui_refresh >= kUiRefreshMs) {
        last_ui_refresh = millis();
        watch_ui_update(collect_ui_state());
    }
    watch_board_service();
    service_idle_sleep();
}
