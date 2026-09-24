#include "watch_status.h"

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

static void expect_text(const char *actual, const char *wanted, const char *name) {
    if (std::strcmp(actual, wanted) == 0) {
        return;
    }
    std::cerr << "FAIL " << name << " (got \"" << actual << "\", wanted \""
              << wanted << "\")" << std::endl;
    failures += 1;
}

// ================================================================================

static void test_signal_bars() {
    expect(watch_signal_bars(-40, true) == 4, "strong RSSI fills every bar");
    expect(watch_signal_bars(-55, true) == 4, "boundary RSSI stays at four bars");
    expect(watch_signal_bars(-60, true) == 3, "good RSSI shows three bars");
    expect(watch_signal_bars(-70, true) == 2, "fair RSSI shows two bars");
    expect(watch_signal_bars(-80, true) == 1, "weak RSSI shows one bar");
    expect(watch_signal_bars(-95, true) == 0, "unusable RSSI shows no bars");
    expect(watch_signal_bars(-40, false) == 0, "disconnected Wi-Fi shows no bars");
    expect(watch_signal_bars(0, true) == 0, "missing RSSI reading shows no bars");
}

// ================================================================================

static void test_battery_level() {
    expect(watch_battery_level(100, true) == WatchBatteryLevel::Full,
           "full charge maps to the full symbol");
    expect(watch_battery_level(80, true) == WatchBatteryLevel::High,
           "80 percent maps to the high symbol");
    expect(watch_battery_level(55, true) == WatchBatteryLevel::Half,
           "55 percent maps to the half symbol");
    expect(watch_battery_level(30, true) == WatchBatteryLevel::Low,
           "30 percent maps to the low symbol");
    expect(watch_battery_level(5, true) == WatchBatteryLevel::Empty,
           "5 percent maps to the empty symbol");
    expect(watch_battery_level(50, false) == WatchBatteryLevel::Unknown,
           "boards without a gauge report unknown");
    expect(watch_battery_level(-1, true) == WatchBatteryLevel::Unknown,
           "a negative reading reports unknown");
    expect(watch_battery_level(150, true) == WatchBatteryLevel::Full,
           "an over-range reading clamps to full");
}

// ================================================================================

static void test_sync_age() {
    char text[48];
    watch_format_sync_age(-1, text, sizeof(text));
    expect_text(text, "Time not synchronized", "no sync says so plainly");

    watch_format_sync_age(0, text, sizeof(text));
    expect_text(text, "Time synchronized 00:00:00 ago", "fresh sync reads zero");

    watch_format_sync_age(12, text, sizeof(text));
    expect_text(text, "Time synchronized 00:00:12 ago", "seconds are zero padded");

    watch_format_sync_age(3723, text, sizeof(text));
    expect_text(text, "Time synchronized 01:02:03 ago", "hours and minutes carry");

    watch_format_sync_age(kWatchSyncAgeMaxSeconds + 60, text, sizeof(text));
    expect_text(text, "Time synchronized 99:59:59 ago", "long ages clamp");
}

// ================================================================================

static void test_connected_status() {
    expect_text(
            watch_connected_status(true),
            "",
            "a good clock leaves the status line to the sync-age line");
    expect_text(
            watch_connected_status(false),
            "Waiting for NTP",
            "a joined network without a clock still says what it waits for");
}

// ================================================================================

static void test_ssid() {
    char text[32];
    watch_format_ssid("home-2g", true, 12, text, sizeof(text));
    expect_text(text, "home-2g", "a short SSID is shown whole");

    watch_format_ssid("a-very-long-network-name", true, 12, text, sizeof(text));
    expect_text(text, "a-very-lo...", "a long SSID is truncated to the budget");

    watch_format_ssid("home-2g", false, 12, text, sizeof(text));
    expect_text(text, "No Wi-Fi", "a disconnected watch says No Wi-Fi");

    watch_format_ssid("", true, 12, text, sizeof(text));
    expect_text(text, "Wi-Fi", "a hidden SSID still labels the bar");

    watch_format_ssid(nullptr, true, 12, text, sizeof(text));
    expect_text(text, "Wi-Fi", "a null SSID does not crash");
}

// ================================================================================

static void test_status_time() {
    char text[16];
    watch_format_status_time(0, false, 0, text, sizeof(text));
    expect_text(text, "--:--:--", "invalid time has a stable placeholder");

    watch_format_status_time(0, true, 3 * 3600, text, sizeof(text));
    expect_text(text, "03:00:00", "status time applies the UTC offset");

    watch_format_status_time(86399, true, 3 * 3600, text, sizeof(text));
    expect_text(text, "02:59:59", "status time wraps across midnight");
}

// ================================================================================

int main() {
    test_signal_bars();
    test_battery_level();
    test_sync_age();
    test_connected_status();
    test_ssid();
    test_status_time();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
