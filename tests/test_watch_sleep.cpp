#include "watch_sleep_policy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

static int failures = 0;

// ================================================================================

static void expect(bool condition, const char *name) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static void test_timeout_options() {
    const int wanted[] = {15, 30, 60, 120, 300, 0};
    for (std::size_t option = 0;
            option < kWatchIdleTimeoutOptionCount;
            option += 1) {
        expect(
                watch_idle_timeout_seconds(option) == wanted[option],
                "timeout option maps to seconds");
        expect(
                watch_idle_timeout_option(wanted[option]) == option,
                "timeout seconds map to option");
    }
    expect(
            watch_idle_timeout_option(999) == 2,
            "invalid saved timeout falls back to one minute");
}

// ================================================================================

// A label list shorter or longer than the timeout table would silently map the
// settings roller onto the wrong timeout.
static void test_labels_match_timeouts() {
    const char *labels = watch_idle_timeout_labels();
    std::size_t lines = 1;
    for (const char *cursor = labels; *cursor != '\0'; cursor += 1) {
        if (*cursor == '\n') {
            lines += 1;
        }
    }
    expect(
            lines == kWatchIdleTimeoutOptionCount,
            "roller offers exactly one label per timeout");
    expect(
            std::strncmp(labels, "15 s", 4) == 0,
            "labels start at the shortest timeout");
    expect(
            std::strcmp(labels + std::strlen(labels) - 5, "Never") == 0,
            "labels end with the option that disables sleep");
}

// ================================================================================

static void test_idle_deadline() {
    expect(
            watch_idle_should_sleep(60999, 1000, 60) == false,
            "watch stays awake before deadline");
    expect(
            watch_idle_should_sleep(61000, 1000, 60),
            "watch sleeps exactly at deadline");
    expect(
            watch_idle_should_sleep(999999, 0, 0) == false,
            "Never disables idle sleep");
    expect(
            watch_idle_should_sleep(10, 0xFFFFFFF0UL, 0) == false,
            "Never remains disabled across millis wrap");
    expect(
            watch_idle_should_sleep(20, 0xFFFFFFF0UL, 0) == false,
            "disabled timeout ignores a wrapped elapsed value");
}

// ================================================================================

int main() {
    test_timeout_options();
    test_labels_match_timeouts();
    test_idle_deadline();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
