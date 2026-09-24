#ifndef DOGGY_WATCH_DISCOVERY_POLICY_H
#define DOGGY_WATCH_DISCOVERY_POLICY_H

#include <cstddef>

// The mDNS component is given this long to collect answers, and it signals the
// search done when the window closes.
inline constexpr unsigned long kWatchDiscoveryTimeoutMs = 3000;

// A search that has not reported itself finished well past its own window is
// not going to, so the page must stop waiting rather than hold the UI and the
// sleep timer open forever.
inline constexpr unsigned long kWatchDiscoveryGraceMs = 2000;

// An empty first query often races MDNS.begin(), so the watch retries rather
// than latching "No doggys found" until the user taps Refresh.
inline constexpr unsigned kWatchDiscoveryMaxAttempts = 3;

// ================================================================================

bool watch_discovery_search_expired(unsigned long elapsed_ms);

// ================================================================================

// completed_attempts is 1-based: the first finished doggy query is 1.
bool watch_discovery_should_retry(
        std::size_t accepted,
        unsigned completed_attempts);

#endif
