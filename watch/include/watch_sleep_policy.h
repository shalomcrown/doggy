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

// ================================================================================

enum WatchWakeLevel {
    kWatchWakeLevelLow,
    kWatchWakeLevelHigh,
};

// ================================================================================

// The QMI8658 toggles its wake-on-motion line on every event instead of resting
// at a fixed level, and a level-triggered wake fires the moment it is armed
// against the level already on the pin. Arming the opposite level makes the next
// toggle the one that wakes the watch, whichever way it goes.
WatchWakeLevel watch_toggling_wake_level(bool line_high);

// ================================================================================

// A line with a real active-low polarity that is still asserted when sleep
// begins would wake the watch immediately, so it is left out of this sleep.
bool watch_active_low_wake_armable(bool line_high);

// ================================================================================

// Light sleep stops clocking the USB-Serial/JTAG peripheral, but the host keeps
// the port enumerated, so it stays visible while answering nothing: a watch that
// light sleeps on a computer's cable cannot be flashed or monitored again until
// someone touches it. With a host on the bus the panel still goes dark and the
// wake lines are polled instead, which costs run current the cable is paying for.
bool watch_sleep_uses_light_sleep(bool usb_host_attached);

#endif
