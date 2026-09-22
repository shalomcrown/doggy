#include "watch_wifi_roster.h"

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

static void expect_text(
        const char *actual,
        const char *wanted,
        const char *name) {
    if (std::strcmp(actual, wanted) == 0) {
        return;
    }
    std::cerr << "FAIL " << name << " (got \"" << actual << "\", wanted \""
              << wanted << "\")" << std::endl;
    failures += 1;
}

// ================================================================================

static void test_insert_and_promote() {
    WatchWifiCredential entries[kWatchWifiRosterCapacity]{};
    std::size_t count = 0;
    count = watch_wifi_roster_upsert(entries, count, "one", "old-pass");
    count = watch_wifi_roster_upsert(entries, count, "two", "two-pass");
    expect(count == 2, "two credentials are retained");
    expect_text(entries[0].ssid, "two", "new credential moves to front");
    expect_text(entries[1].ssid, "one", "older credential moves back");

    count = watch_wifi_roster_upsert(entries, count, "one", "new-pass");
    expect(count == 2, "updating a credential does not grow the roster");
    expect_text(entries[0].ssid, "one", "used credential is promoted");
    expect_text(entries[0].password, "new-pass", "promoted password is updated");
    expect_text(entries[1].ssid, "two", "previous leader moves back");
}

// ================================================================================

static void test_capacity_and_validation() {
    WatchWifiCredential entries[kWatchWifiRosterCapacity]{};
    std::size_t count = 0;
    const char *names[] = {"one", "two", "three", "four", "five", "six"};
    for (const char *name : names) {
        count = watch_wifi_roster_upsert(entries, count, name, "password");
    }
    expect(count == kWatchWifiRosterCapacity, "roster never exceeds five");
    expect_text(entries[0].ssid, "six", "sixth credential becomes most recent");
    expect_text(entries[4].ssid, "two", "oldest credential is evicted");

    count = watch_wifi_roster_upsert(entries, count, "", "password");
    expect(count == kWatchWifiRosterCapacity, "empty SSID is rejected");
    count = watch_wifi_roster_upsert(entries, count, nullptr, "password");
    expect(count == kWatchWifiRosterCapacity, "null SSID is rejected");
}

// ================================================================================

int main() {
    test_insert_and_promote();
    test_capacity_and_validation();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
