#ifndef DOGGY_WATCH_SETTINGS_H
#define DOGGY_WATCH_SETTINGS_H

#include "watch_doggy.h"
#include "watch_wifi_roster.h"

#include <cstddef>
#include <cstdint>

// ================================================================================

void watch_settings_begin();

// ================================================================================

int watch_settings_offset_hours();

// ================================================================================

int watch_settings_offset_minutes();

// ================================================================================

void watch_settings_set_offset(int hours, int minutes);

// ================================================================================

// Seconds of no touch before the watch sleeps. 0 means the watch never sleeps.
int watch_settings_idle_timeout_seconds();

// ================================================================================

// Ignores any value outside the choices the settings page offers.
void watch_settings_set_idle_timeout_seconds(int seconds);

// ================================================================================

const WatchDoggyTarget *watch_settings_selected_doggy();

// ================================================================================

void watch_settings_set_selected_doggy(const WatchDoggyTarget &target);

// ================================================================================

// Saved networks, most recently joined first.
std::size_t watch_settings_wifi_count();

// ================================================================================

const WatchWifiCredential *watch_settings_wifi_entries();

// ================================================================================

// Moves this network to the front of the roster, evicting the oldest once the
// roster is full. Writes to flash only when something actually changed.
void watch_settings_remember_wifi(const char *ssid, const char *password);

#endif
