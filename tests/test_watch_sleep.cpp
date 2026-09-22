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

// The QMI8658 wake-on-motion line has no resting level, so arming the level it
// already shows makes light sleep return the instant it is entered.
static void test_toggling_wake_level() {
    expect(
            watch_toggling_wake_level(true) == kWatchWakeLevelLow,
            "a high toggling line arms the low level");
    expect(
            watch_toggling_wake_level(false) == kWatchWakeLevelHigh,
            "a low toggling line arms the high level");
}

// ================================================================================

static void test_active_low_armable() {
    expect(
            watch_active_low_wake_armable(true),
            "an idle active-low line can be armed");
    expect(
            watch_active_low_wake_armable(false) == false,
            "an asserted active-low line is left unarmed");
}

// ================================================================================

// A watch that light sleeps on a computer's cable takes its USB-Serial/JTAG
// peripheral down with it, and flashing needs that link to reach the chip.
static void test_usb_host_blocks_light_sleep() {
    expect(
            watch_sleep_uses_light_sleep(false),
            "a watch on battery still light sleeps");
    expect(
            watch_sleep_uses_light_sleep(true) == false,
            "a watch on a host stays reachable");
}

// ================================================================================

int main() {
    test_timeout_options();
    test_labels_match_timeouts();
    test_idle_deadline();
    test_toggling_wake_level();
    test_active_low_armable();
    test_usb_host_blocks_light_sleep();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
