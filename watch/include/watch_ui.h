#ifndef DOGGY_WATCH_UI_H
#define DOGGY_WATCH_UI_H

#include <ctime>

// ================================================================================

struct WatchUiState {
    std::time_t utc_time;
    bool time_valid;
    const char *network_status;
    const char *ssid;
    bool wifi_connected;
    int rssi;
    long sync_age_seconds;
    int battery_percent;
    bool battery_charging;
    bool battery_valid;
};

// ================================================================================

void watch_ui_begin();

// ================================================================================

void watch_ui_update(const WatchUiState &state);

// ================================================================================

// Returns to the clock and throws away an unsaved settings edit, so the watch
// never wakes up mid-edit.
void watch_ui_sleep();

#endif
