#ifndef DOGGY_WATCH_STATUS_H
#define DOGGY_WATCH_STATUS_H

#include <cstddef>

inline constexpr int kWatchSignalBarCount = 4;
inline constexpr long kWatchSyncAgeMaxSeconds = 359999;

// ================================================================================

enum class WatchBatteryLevel {
    Unknown,
    Empty,
    Low,
    Half,
    High,
    Full,
};

// ================================================================================

// Wi-Fi bars from RSSI in dBm. 0 means associated but unusable, or no link.
int watch_signal_bars(int rssi, bool connected);

// ================================================================================

WatchBatteryLevel watch_battery_level(int percent, bool valid);

// ================================================================================

// "Time synchronized HH:MM:SS ago", or a waiting message when never synced.
void watch_format_sync_age(long seconds, char *out, std::size_t size);

// ================================================================================

// Status line for a joined network. Empty once the clock is good: the sync-age
// line already reports that, and two labels saying it is one too many.
const char *watch_connected_status(bool time_valid);

// ================================================================================

// SSID trimmed to the status bar, with an ellipsis when it does not fit.
void watch_format_ssid(
        const char *ssid,
        bool connected,
        std::size_t max_characters,
        char *out,
        std::size_t size);

#endif
