#include "watch_settings.h"

#include "time_offset.h"
#include "watch_sleep_policy.h"

#include <Preferences.h>

#include <cstring>

namespace {

inline constexpr char kWifiKey[] = "wifi";
inline constexpr char kDoggyTargetKey[] = "doggy-target";

Preferences preferences;
int offset_hours = kWatchDefaultOffsetHours;
int offset_minutes = kWatchDefaultOffsetMinutes;
int idle_timeout_seconds = kWatchDefaultIdleTimeoutSeconds;
WatchWifiCredential wifi_entries[kWatchWifiRosterCapacity]{};
std::size_t wifi_count = 0;
WatchDoggyTarget selected_doggy{};
bool selected_doggy_valid = false;
bool preferences_ready = false;

}

// ================================================================================

// NVS can hand back a short or corrupt blob after a failed write, so every entry
// is forced to terminate and the roster stops at the first empty name.
static void sanitize_wifi_entries() {
    for (std::size_t index = 0; index < wifi_count; index += 1) {
        wifi_entries[index].ssid[kWatchWifiSsidSize - 1] = '\0';
        wifi_entries[index].password[kWatchWifiPasswordSize - 1] = '\0';
        if (wifi_entries[index].ssid[0] == '\0') {
            wifi_count = index;
            return;
        }
    }
}

// ================================================================================

static void load_wifi_entries() {
    const std::size_t stored = preferences.getBytesLength(kWifiKey);
    if (stored == 0
            || stored > sizeof(wifi_entries)
            || stored % sizeof(WatchWifiCredential) != 0) {
        return;
    }
    if (preferences.getBytes(kWifiKey, wifi_entries, stored) != stored) {
        std::memset(wifi_entries, 0, sizeof(wifi_entries));
        return;
    }
    wifi_count = stored / sizeof(WatchWifiCredential);
    sanitize_wifi_entries();
}

// ================================================================================

static void load_selected_doggy() {
    if (preferences.getBytesLength(kDoggyTargetKey)
            != sizeof(selected_doggy)) {
        return;
    }
    if (preferences.getBytes(
            kDoggyTargetKey,
            &selected_doggy,
            sizeof(selected_doggy)) != sizeof(selected_doggy)) {
        selected_doggy = {};
        return;
    }
    selected_doggy_valid = watch_doggy_target_valid(selected_doggy);
    if (selected_doggy_valid == false) {
        selected_doggy = {};
    }
}

// ================================================================================

void watch_settings_begin() {
    if (preferences.begin("doggy-watch", false) == false) {
        return;
    }
    preferences_ready = true;
    offset_hours = preferences.getInt(
            "tz-hour",
            kWatchDefaultOffsetHours);
    offset_minutes = preferences.getInt(
            "tz-minute",
            kWatchDefaultOffsetMinutes);
    if (offset_hours < kWatchMinimumOffsetHours
            || offset_hours > kWatchMaximumOffsetHours) {
        offset_hours = kWatchDefaultOffsetHours;
    }
    if (offset_minutes != 0
            && offset_minutes != 15
            && offset_minutes != 30
            && offset_minutes != 45) {
        offset_minutes = kWatchDefaultOffsetMinutes;
    }
    // A value the settings page cannot offer round-trips back to the default.
    const int saved_timeout = preferences.getInt(
            "idle",
            kWatchDefaultIdleTimeoutSeconds);
    idle_timeout_seconds = watch_idle_timeout_seconds(
            watch_idle_timeout_option(saved_timeout));
    load_wifi_entries();
    load_selected_doggy();
}

// ================================================================================

int watch_settings_offset_hours() {
    return offset_hours;
}

// ================================================================================

int watch_settings_offset_minutes() {
    return offset_minutes;
}

// ================================================================================

void watch_settings_set_offset(int hours, int minutes) {
    if (hours < kWatchMinimumOffsetHours
            || hours > kWatchMaximumOffsetHours) {
        return;
    }
    if (minutes != 0 && minutes != 15 && minutes != 30 && minutes != 45) {
        return;
    }
    offset_hours = hours;
    offset_minutes = minutes;
    if (preferences_ready) {
        preferences.putInt("tz-hour", offset_hours);
        preferences.putInt("tz-minute", offset_minutes);
    }
}

// ================================================================================

int watch_settings_idle_timeout_seconds() {
    return idle_timeout_seconds;
}

// ================================================================================

void watch_settings_set_idle_timeout_seconds(int seconds) {
    if (watch_idle_timeout_seconds(watch_idle_timeout_option(seconds))
            != seconds) {
        return;
    }
    idle_timeout_seconds = seconds;
    if (preferences_ready) {
        preferences.putInt("idle", idle_timeout_seconds);
    }
}

// ================================================================================

const WatchDoggyTarget *watch_settings_selected_doggy() {
    return selected_doggy_valid ? &selected_doggy : nullptr;
}

// ================================================================================

void watch_settings_set_selected_doggy(const WatchDoggyTarget &target) {
    if (watch_doggy_target_valid(target) == false) {
        return;
    }
    if (selected_doggy_valid
            && watch_doggy_target_equal(selected_doggy, target)) {
        return;
    }
    selected_doggy = target;
    selected_doggy_valid = true;
    if (preferences_ready) {
        preferences.putBytes(
                kDoggyTargetKey,
                &selected_doggy,
                sizeof(selected_doggy));
    }
}

// ================================================================================

std::size_t watch_settings_wifi_count() {
    return wifi_count;
}

// ================================================================================

const WatchWifiCredential *watch_settings_wifi_entries() {
    return wifi_entries;
}

// ================================================================================

void watch_settings_remember_wifi(const char *ssid, const char *password) {
    WatchWifiCredential previous[kWatchWifiRosterCapacity]{};
    std::memcpy(previous, wifi_entries, sizeof(previous));
    const std::size_t previous_count = wifi_count;
    wifi_count = watch_wifi_roster_upsert(
            wifi_entries,
            wifi_count,
            ssid,
            password);
    // Rejoining the same network on every wake must not burn a flash write.
    if (wifi_count == previous_count
            && std::memcmp(previous, wifi_entries, sizeof(previous)) == 0) {
        return;
    }
    if (preferences_ready) {
        preferences.putBytes(
                kWifiKey,
                wifi_entries,
                wifi_count * sizeof(WatchWifiCredential));
    }
}
