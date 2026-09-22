#ifndef DOGGY_WATCH_SLEEP_POLICY_H
#define DOGGY_WATCH_SLEEP_POLICY_H

#include <cstddef>

inline constexpr int kWatchDefaultIdleTimeoutSeconds = 60;
inline constexpr std::size_t kWatchIdleTimeoutOptionCount = 6;

// ================================================================================

int watch_idle_timeout_seconds(std::size_t option);

// ================================================================================

// The settings roller's choices, newline separated, in the same order as the
// timeouts themselves so the two can never drift apart.
const char *watch_idle_timeout_labels();

// ================================================================================

std::size_t watch_idle_timeout_option(int seconds);

// ================================================================================

bool watch_idle_should_sleep(
        unsigned long now_ms,
        unsigned long last_activity_ms,
        int timeout_seconds);

#endif
