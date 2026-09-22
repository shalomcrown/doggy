#include "watch_network.h"

#include "board_hal.h"
#include "time_offset.h"
#include "watch_settings.h"
#include "watch_status.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_smartconfig.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <sys/time.h>

#include <cstring>

namespace {

// A WPA2 join settles well inside this, and the whole roster still falls through
// to ESP-Touch in under a minute.
inline constexpr unsigned long kAttemptTimeoutMs = 8000;
inline constexpr unsigned long kMillisecondsPerSecond = 1000;
inline constexpr std::size_t kSsidSize = 33;

// Walks the saved roster newest first, then the credentials the Wi-Fi stack
// remembers, and only then asks the phone for new ones.
std::size_t attempt_index = 0;
unsigned long attempt_started = 0;
bool provisioning = false;
bool credentials_received = false;
bool credentials_stored = false;
bool sntp_started = false;
bool radio_suspended = false;
const char *status_text = "Starting Wi-Fi";
char ssid_text[kSsidSize] = "";
// Written from the SNTP task, read from loop(); word-sized and monotonic.
volatile unsigned long last_sync_ms = 0;
volatile bool sync_seen = false;
volatile bool rtc_write_pending = false;

}

// ================================================================================

static void time_synchronized(struct timeval *) {
    last_sync_ms = millis();
    sync_seen = true;
    // The calendar chip shares the I2C bus with the power gauge the UI polls, so
    // the write itself waits for loop().
    rtc_write_pending = true;
}

// ================================================================================

static void provisioning_event(arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_SC_GOT_SSID_PSWD) {
        credentials_received = true;
    } else if (event == ARDUINO_EVENT_SC_SEND_ACK_DONE) {
        provisioning = false;
        credentials_received = false;
    }
}

// ================================================================================

// The Arduino SmartConfig wrapper forces esp_touch_v2_enable_crypt on with a
// NULL key, so the watch would wait for AES packets no phone app sends.
static bool start_provisioning() {
    if (WiFi.mode(WIFI_STA) == false) {
        return false;
    }
    esp_wifi_disconnect();
    if (esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2) != ESP_OK) {
        return false;
    }
    smartconfig_start_config_t config = SMARTCONFIG_START_CONFIG_DEFAULT();
    return esp_smartconfig_start(&config) == ESP_OK;
}

// ================================================================================

static void stop_provisioning() {
    esp_smartconfig_stop();
    provisioning = false;
    credentials_received = false;
}

// ================================================================================

static void remember_ssid() {
    const String current = WiFi.SSID();
    std::snprintf(ssid_text, sizeof(ssid_text), "%s", current.c_str());
}

// ================================================================================

// Runs once per join, so reconnecting to the same network after every wake costs
// no flash writes.
static void remember_credentials() {
    if (credentials_stored) {
        return;
    }
    const String ssid = WiFi.SSID();
    if (ssid.length() == 0) {
        return;
    }
    const String password = WiFi.psk();
    watch_settings_remember_wifi(ssid.c_str(), password.c_str());
    credentials_stored = true;
}

// ================================================================================

static void start_attempt() {
    attempt_started = millis();
    const std::size_t saved = watch_settings_wifi_count();
    if (attempt_index < saved) {
        const WatchWifiCredential *entries = watch_settings_wifi_entries();
        WiFi.begin(entries[attempt_index].ssid, entries[attempt_index].password);
        status_text = "Trying saved Wi-Fi";
        return;
    }
    if (attempt_index == saved) {
        WiFi.begin();
        status_text = "Connecting to Wi-Fi";
        return;
    }
    if (start_provisioning()) {
        provisioning = true;
        credentials_received = false;
        status_text = "Waiting for ESP-Touch v2";
        return;
    }
    status_text = "ESP-Touch v2 failed; retrying";
}

// ================================================================================

// Past the end of the list the walk starts over, so a radio that refused to open
// ESP-Touch still retries every saved network instead of stalling.
static void advance_attempt() {
    if (attempt_index > watch_settings_wifi_count()) {
        attempt_index = 0;
    } else {
        attempt_index += 1;
    }
    start_attempt();
}

// ================================================================================

// The calendar chip carries the watch across a power cycle, so the clock page has
// a time to show before NTP answers.
static void seed_clock_from_rtc() {
    if (std::time(nullptr) >= kWatchMinimumValidTime) {
        return;
    }
    std::time_t stored = 0;
    if (watch_board_rtc_read(stored) == false) {
        return;
    }
    timeval seeded{};
    seeded.tv_sec = stored;
    settimeofday(&seeded, nullptr);
}

// ================================================================================

void watch_network_begin() {
    seed_clock_from_rtc();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(provisioning_event, ARDUINO_EVENT_SC_GOT_SSID_PSWD);
    WiFi.onEvent(provisioning_event, ARDUINO_EVENT_SC_SEND_ACK_DONE);
    sntp_set_time_sync_notification_cb(time_synchronized);
    attempt_index = 0;
    start_attempt();
}

// ================================================================================

void watch_network_service() {
    if (rtc_write_pending) {
        rtc_write_pending = false;
        watch_board_rtc_write(std::time(nullptr));
    }
    if (radio_suspended) {
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        status_text = watch_connected_status(watch_network_time_valid());
        remember_ssid();
        remember_credentials();
        if (sntp_started == false) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            sntp_started = true;
        }
        if (provisioning) {
            stop_provisioning();
        }
        // A later drop retries this network first, since remembering it just put
        // it at the head of the roster.
        attempt_index = 0;
        attempt_started = millis();
        return;
    }

    sntp_started = false;
    credentials_stored = false;
    ssid_text[0] = '\0';
    if (provisioning) {
        status_text = credentials_received
                ? "Joining provisioned Wi-Fi"
                : "Waiting for ESP-Touch v2";
        return;
    }

    if (millis() - attempt_started < kAttemptTimeoutMs) {
        return;
    }
    advance_attempt();
}

// ================================================================================

void watch_network_start_provisioning() {
    if (provisioning) {
        return;
    }
    radio_suspended = false;
    if (start_provisioning()) {
        provisioning = true;
        credentials_received = false;
        status_text = "Waiting for ESP-Touch v2";
        return;
    }
    status_text = "ESP-Touch v2 failed; retrying";
}

// ================================================================================

bool watch_network_provisioning_active() {
    return provisioning;
}

// ================================================================================

void watch_network_suspend() {
    if (provisioning) {
        return;
    }
    WiFi.disconnect(true, false);
    radio_suspended = true;
    sntp_started = false;
    credentials_stored = false;
    ssid_text[0] = '\0';
    status_text = "Wi-Fi off while asleep";
}

// ================================================================================

void watch_network_resume() {
    if (radio_suspended == false) {
        return;
    }
    radio_suspended = false;
    WiFi.mode(WIFI_STA);
    attempt_index = 0;
    start_attempt();
}

// ================================================================================

const char *watch_network_status() {
    return status_text;
}

// ================================================================================

bool watch_network_time_valid() {
    return std::time(nullptr) >= kWatchMinimumValidTime;
}

// ================================================================================

std::time_t watch_network_utc_time() {
    return std::time(nullptr);
}

// ================================================================================

bool watch_network_connected() {
    return WiFi.status() == WL_CONNECTED;
}

// ================================================================================

const char *watch_network_ssid() {
    return ssid_text;
}

// ================================================================================

int watch_network_rssi() {
    if (watch_network_connected() == false) {
        return 0;
    }
    return static_cast<int>(WiFi.RSSI());
}

// ================================================================================

long watch_network_sync_age_seconds() {
    if (sync_seen == false) {
        return -1;
    }
    const unsigned long elapsed = millis() - last_sync_ms;
    return static_cast<long>(elapsed / kMillisecondsPerSecond);
}
