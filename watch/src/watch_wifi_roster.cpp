#include "watch_wifi_roster.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// ================================================================================

std::size_t watch_wifi_roster_upsert(
        WatchWifiCredential *entries,
        std::size_t count,
        const char *ssid,
        const char *password) {
    if (entries == nullptr || ssid == nullptr || ssid[0] == '\0') {
        return std::min(count, kWatchWifiRosterCapacity);
    }
    const std::size_t ssid_length = std::strlen(ssid);
    const std::size_t password_length =
            password == nullptr ? 0 : std::strlen(password);
    if (ssid_length >= kWatchWifiSsidSize
            || password_length >= kWatchWifiPasswordSize) {
        return std::min(count, kWatchWifiRosterCapacity);
    }

    count = std::min(count, kWatchWifiRosterCapacity);
    std::size_t existing = count;
    for (std::size_t index = 0; index < count; index += 1) {
        if (std::strcmp(entries[index].ssid, ssid) == 0) {
            existing = index;
            break;
        }
    }

    WatchWifiCredential newest{};
    std::snprintf(newest.ssid, sizeof(newest.ssid), "%s", ssid);
    std::snprintf(
            newest.password,
            sizeof(newest.password),
            "%s",
            password == nullptr ? "" : password);
    const std::size_t shift_end =
            existing < count ? existing : std::min(count, kWatchWifiRosterCapacity - 1);
    for (std::size_t index = shift_end; index > 0; index -= 1) {
        entries[index] = entries[index - 1];
    }
    entries[0] = newest;
    if (existing == count && count < kWatchWifiRosterCapacity) {
        count += 1;
    }
    return count;
}
