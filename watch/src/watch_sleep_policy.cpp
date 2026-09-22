#include "watch_sleep_policy.h"

namespace {

const int kTimeoutSeconds[kWatchIdleTimeoutOptionCount] = {
        15,
        30,
        60,
        120,
        300,
        0,
};

const char kTimeoutLabels[] = "15 s\n30 s\n1 min\n2 min\n5 min\nNever";

}

// ================================================================================

const char *watch_idle_timeout_labels() {
    return kTimeoutLabels;
}

// ================================================================================

int watch_idle_timeout_seconds(std::size_t option) {
    if (option >= kWatchIdleTimeoutOptionCount) {
        return kWatchDefaultIdleTimeoutSeconds;
    }
    return kTimeoutSeconds[option];
}

// ================================================================================

std::size_t watch_idle_timeout_option(int seconds) {
    for (std::size_t option = 0;
            option < kWatchIdleTimeoutOptionCount;
            option += 1) {
        if (kTimeoutSeconds[option] == seconds) {
            return option;
        }
    }
    return watch_idle_timeout_option(kWatchDefaultIdleTimeoutSeconds);
}

// ================================================================================

bool watch_idle_should_sleep(
        unsigned long now_ms,
        unsigned long last_activity_ms,
        int timeout_seconds) {
    if (timeout_seconds <= 0) {
        return false;
    }
    const unsigned long timeout_ms =
            static_cast<unsigned long>(timeout_seconds) * 1000UL;
    return now_ms - last_activity_ms >= timeout_ms;
}
