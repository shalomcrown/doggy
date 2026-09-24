#include "watch_discovery_policy.h"

// ================================================================================

bool watch_discovery_search_expired(unsigned long elapsed_ms) {
    return elapsed_ms >= kWatchDiscoveryTimeoutMs + kWatchDiscoveryGraceMs;
}

// ================================================================================

bool watch_discovery_should_retry(
        std::size_t accepted,
        unsigned completed_attempts) {
    if (accepted > 0) {
        return false;
    }
    if (completed_attempts == 0) {
        return false;
    }
    return completed_attempts < kWatchDiscoveryMaxAttempts;
}
