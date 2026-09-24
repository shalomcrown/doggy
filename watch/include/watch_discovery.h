#ifndef DOGGY_WATCH_DISCOVERY_H
#define DOGGY_WATCH_DISCOVERY_H

#include "watch_doggy.h"

#include <cstddef>
#include <cstdint>

// ================================================================================

enum class WatchDiscoveryState {
    Idle,
    Offline,
    Searching,
    Complete,
    Error,
};

// ================================================================================

void watch_discovery_service(bool wifi_connected);

// ================================================================================

void watch_discovery_refresh();

// ================================================================================

WatchDiscoveryState watch_discovery_state();

// ================================================================================

const char *watch_discovery_status();

// ================================================================================

const WatchDoggyTarget *watch_discovery_entries();

// ================================================================================

std::size_t watch_discovery_count();

// ================================================================================

std::uint32_t watch_discovery_revision();

#endif
