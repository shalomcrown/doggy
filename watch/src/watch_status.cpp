#include "watch_status.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

inline constexpr int kExcellentRssi = -55;
inline constexpr int kGoodRssi = -65;
inline constexpr int kFairRssi = -75;
inline constexpr int kWeakRssi = -85;
// Arduino returns 0 dBm when no reading is available, never as a real level.
inline constexpr int kMissingRssi = 0;
inline constexpr int kFullPercent = 95;
inline constexpr int kHighPercent = 70;
inline constexpr int kHalfPercent = 45;
inline constexpr int kLowPercent = 20;
inline constexpr long kSecondsPerHour = 3600;
inline constexpr long kSecondsPerMinute = 60;
inline constexpr std::size_t kEllipsisLength = 3;

}

// ================================================================================

int watch_signal_bars(int rssi, bool connected) {
    if (connected == false || rssi >= kMissingRssi) {
        return 0;
    }
    if (rssi >= kExcellentRssi) {
        return 4;
    }
    if (rssi >= kGoodRssi) {
        return 3;
    }
    if (rssi >= kFairRssi) {
        return 2;
    }
    if (rssi >= kWeakRssi) {
        return 1;
    }
    return 0;
}

// ================================================================================

WatchBatteryLevel watch_battery_level(int percent, bool valid) {
    if (valid == false || percent < 0) {
        return WatchBatteryLevel::Unknown;
    }
    if (percent >= kFullPercent) {
        return WatchBatteryLevel::Full;
    }
    if (percent >= kHighPercent) {
        return WatchBatteryLevel::High;
    }
    if (percent >= kHalfPercent) {
        return WatchBatteryLevel::Half;
    }
    if (percent >= kLowPercent) {
        return WatchBatteryLevel::Low;
    }
    return WatchBatteryLevel::Empty;
}

// ================================================================================

void watch_format_sync_age(long seconds, char *out, std::size_t size) {
    if (out == nullptr || size == 0) {
        return;
    }
    if (seconds < 0) {
        std::snprintf(out, size, "Time not synchronized");
        return;
    }
    if (seconds > kWatchSyncAgeMaxSeconds) {
        seconds = kWatchSyncAgeMaxSeconds;
    }
    const long hours = seconds / kSecondsPerHour;
    const long minutes = (seconds % kSecondsPerHour) / kSecondsPerMinute;
    const long remainder = seconds % kSecondsPerMinute;
    std::snprintf(
            out,
            size,
            "Time synchronized %02ld:%02ld:%02ld ago",
            hours,
            minutes,
            remainder);
}

// ================================================================================

const char *watch_connected_status(bool time_valid) {
    return time_valid ? "" : "Waiting for NTP";
}

// ================================================================================

void watch_format_ssid(
        const char *ssid,
        bool connected,
        std::size_t max_characters,
        char *out,
        std::size_t size) {
    if (out == nullptr || size == 0) {
        return;
    }
    if (connected == false) {
        std::snprintf(out, size, "No Wi-Fi");
        return;
    }
    if (ssid == nullptr || ssid[0] == '\0') {
        std::snprintf(out, size, "Wi-Fi");
        return;
    }

    const std::size_t length = std::strlen(ssid);
    std::size_t budget = max_characters;
    if (budget > size - 1) {
        budget = size - 1;
    }
    if (length <= budget) {
        std::snprintf(out, size, "%s", ssid);
        return;
    }
    if (budget <= kEllipsisLength) {
        std::snprintf(out, size, "%.*s", static_cast<int>(budget), ssid);
        return;
    }
    const int kept = static_cast<int>(budget - kEllipsisLength);
    std::snprintf(out, size, "%.*s...", kept, ssid);
}

// ================================================================================

void watch_format_status_time(
        std::time_t utc,
        bool valid,
        int utc_offset_seconds,
        char *out,
        std::size_t size) {
    if (out == nullptr || size == 0) {
        return;
    }
    if (valid == false) {
        std::snprintf(out, size, "--:--:--");
        return;
    }
    const std::time_t local_time = utc + utc_offset_seconds;
    std::tm broken_down{};
    gmtime_r(&local_time, &broken_down);
    std::strftime(out, size, "%H:%M:%S", &broken_down);
}
