#include "watch_discovery.h"

#include "watch_discovery_policy.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <mdns.h>

#include <cstdio>

namespace {

// Every mDNS responder answers the service enumeration meta-query regardless of
// what it publishes, so it separates "the watch hears nothing on this network"
// from "the watch hears the network but not this service".
inline constexpr char kProbeService[] = "_services._dns-sd";
inline constexpr char kProbeProtocol[] = "_udp";
inline constexpr char kDoggyService[] = "_doggy";
inline constexpr char kDoggyProtocol[] = "_tcp";

enum class SearchKind {
    None,
    Doggy,
    Probe,
};

WatchDoggyTarget entries[kWatchDoggyCapacity]{};
std::size_t entry_count = 0;
mdns_search_once_t *search = nullptr;
SearchKind search_kind = SearchKind::None;
unsigned long search_started = 0;
bool expiry_reported = false;
WatchDiscoveryState discovery_state = WatchDiscoveryState::Idle;
std::uint32_t revision = 0;
bool mdns_ready = false;
bool power_save_held = false;
bool refresh_requested = false;
unsigned doggy_attempts = 0;

// ================================================================================

static void set_state(WatchDiscoveryState state) {
    if (discovery_state == state) {
        return;
    }
    discovery_state = state;
    revision += 1;
}

// ================================================================================

// mDNS answers arrive as multicast, which a station in modem sleep is only
// listening for around beacons, so the radio stays fully awake for the few
// seconds a search is running.
static void hold_power_save() {
    if (power_save_held) {
        return;
    }
    WiFi.setSleep(false);
    power_save_held = true;
}

// ================================================================================

static void release_power_save() {
    if (power_save_held == false) {
        return;
    }
    WiFi.setSleep(true);
    power_save_held = false;
}

// ================================================================================

static bool begin_mdns() {
    if (mdns_ready) {
        return true;
    }
    char hostname[32];
    const unsigned long long suffix =
            static_cast<unsigned long long>(ESP.getEfuseMac() & 0xFFFFFFULL);
    std::snprintf(
            hostname,
            sizeof(hostname),
            "doggy-watch-%06llx",
            suffix);
    mdns_ready = MDNS.begin(hostname);
    Serial.printf(
            "discovery: mDNS begin %s %s\r\n",
            hostname,
            mdns_ready ? "ok" : "FAILED");
    return mdns_ready;
}

// ================================================================================

static void collect_doggys(mdns_result_t *results) {
    entry_count = 0;
    std::size_t raw_count = 0;
    for (mdns_result_t *result = results;
            result != nullptr;
            result = result->next) {
        raw_count += 1;
        // The log runs before validation, so the widths bound what an untrusted
        // responder can print.
        Serial.printf(
                "discovery: result instance=%.63s hostname=%.63s port=%u\r\n",
                result->instance_name == nullptr
                        ? "(none)"
                        : result->instance_name,
                result->hostname == nullptr ? "(none)" : result->hostname,
                static_cast<unsigned>(result->port));
        WatchDoggyTarget candidate{};
        if (watch_doggy_target_set(
                candidate,
                result->instance_name,
                result->hostname,
                result->port)) {
            entry_count = watch_doggy_list_add(
                    entries,
                    entry_count,
                    candidate);
        }
    }
    Serial.printf(
            "discovery: doggy search raw=%u accepted=%u attempt=%u/%u\r\n",
            static_cast<unsigned>(raw_count),
            static_cast<unsigned>(entry_count),
            doggy_attempts,
            kWatchDiscoveryMaxAttempts);
}

// ================================================================================

// Diagnostic only: the enumerated service types are logged and thrown away, so
// nothing the probe hears can reach the list or the stored selection.
static void report_probe(mdns_result_t *results) {
    std::size_t raw_count = 0;
    for (mdns_result_t *result = results;
            result != nullptr;
            result = result->next) {
        raw_count += 1;
        Serial.printf(
                "discovery: probe sees %.63s\r\n",
                result->instance_name == nullptr
                        ? "(none)"
                        : result->instance_name);
    }
    Serial.printf(
            "discovery: probe raw=%u\r\n",
            static_cast<unsigned>(raw_count));
}

// ================================================================================

static bool start_search(SearchKind kind) {
    if (begin_mdns() == false) {
        return false;
    }
    hold_power_save();
    const bool probing = kind == SearchKind::Probe;
    search = mdns_query_async_new(
            nullptr,
            probing ? kProbeService : kDoggyService,
            probing ? kProbeProtocol : kDoggyProtocol,
            MDNS_TYPE_PTR,
            kWatchDiscoveryTimeoutMs,
            kWatchDoggyCapacity,
            nullptr);
    if (search == nullptr) {
        Serial.printf("discovery: query start FAILED\r\n");
        return false;
    }
    search_kind = kind;
    search_started = millis();
    expiry_reported = false;
    Serial.printf(
            "discovery: %s query started\r\n",
            probing ? "probe" : "doggy");
    return true;
}

// ================================================================================

// A search that will not report itself finished cannot be deleted either, so the
// pointer is kept and polled while the page is released back to the user.
static void abandon_search() {
    if (expiry_reported) {
        return;
    }
    expiry_reported = true;
    Serial.printf("discovery: search did not finish in time\r\n");
    release_power_save();
    set_state(WatchDiscoveryState::Error);
}

// ================================================================================

static void finish_search() {
    mdns_result_t *results = nullptr;
    if (mdns_query_async_get_results(search, 0, &results, nullptr) == false) {
        if (watch_discovery_search_expired(millis() - search_started)) {
            abandon_search();
        }
        return;
    }

    const SearchKind finished = search_kind;
    mdns_query_async_delete(search);
    search = nullptr;
    search_kind = SearchKind::None;

    if (finished == SearchKind::Doggy) {
        doggy_attempts += 1;
        collect_doggys(results);
    } else {
        report_probe(results);
    }
    if (results != nullptr) {
        mdns_query_results_free(results);
    }

    if (finished == SearchKind::Doggy
            && watch_discovery_should_retry(entry_count, doggy_attempts)
            && start_search(SearchKind::Doggy)) {
        return;
    }

    // Nothing was found, so the probe answers the only question left: whether
    // this watch can hear the network at all.
    if (finished == SearchKind::Doggy
            && entry_count == 0
            && start_search(SearchKind::Probe)) {
        return;
    }
    release_power_save();
    set_state(WatchDiscoveryState::Complete);
}

}

// ================================================================================

void watch_discovery_service(bool wifi_connected) {
    if (search != nullptr) {
        finish_search();
    }
    if (wifi_connected == false) {
        if (search == nullptr) {
            release_power_save();
            if (mdns_ready) {
                MDNS.end();
                mdns_ready = false;
            }
            doggy_attempts = 0;
            set_state(WatchDiscoveryState::Offline);
        }
        return;
    }
    if (refresh_requested && search == nullptr) {
        refresh_requested = false;
        doggy_attempts = 0;
        if (start_search(SearchKind::Doggy)) {
            set_state(WatchDiscoveryState::Searching);
            return;
        }
        release_power_save();
        set_state(WatchDiscoveryState::Error);
    }
}

// ================================================================================

void watch_discovery_refresh() {
    refresh_requested = true;
}

// ================================================================================

WatchDiscoveryState watch_discovery_state() {
    return discovery_state;
}

// ================================================================================

const char *watch_discovery_status() {
    switch (discovery_state) {
        case WatchDiscoveryState::Idle:
            return "Tap Refresh to find doggys";
        case WatchDiscoveryState::Offline:
            return "Connect Wi-Fi to find doggys";
        case WatchDiscoveryState::Searching:
            return "Searching...";
        case WatchDiscoveryState::Complete:
            return entry_count == 0 ? "No doggys found" : "";
        case WatchDiscoveryState::Error:
        default:
            return "Discovery failed; tap Refresh";
    }
}

// ================================================================================

const WatchDoggyTarget *watch_discovery_entries() {
    return entries;
}

// ================================================================================

std::size_t watch_discovery_count() {
    return entry_count;
}

// ================================================================================

std::uint32_t watch_discovery_revision() {
    return revision;
}
