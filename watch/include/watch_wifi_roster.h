#ifndef DOGGY_WATCH_WIFI_ROSTER_H
#define DOGGY_WATCH_WIFI_ROSTER_H

#include <cstddef>

inline constexpr std::size_t kWatchWifiRosterCapacity = 5;
inline constexpr std::size_t kWatchWifiSsidSize = 33;
inline constexpr std::size_t kWatchWifiPasswordSize = 65;

// ================================================================================

struct WatchWifiCredential {
    char ssid[kWatchWifiSsidSize];
    char password[kWatchWifiPasswordSize];
};

// ================================================================================

std::size_t watch_wifi_roster_upsert(
        WatchWifiCredential *entries,
        std::size_t count,
        const char *ssid,
        const char *password);

#endif
