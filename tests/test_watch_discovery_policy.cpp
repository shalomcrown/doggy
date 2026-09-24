#include "watch_discovery_policy.h"

#include <cstdlib>
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

static void test_search_retry() {
    expect(
            watch_discovery_should_retry(0, 1),
            "an empty first query retries");
    expect(
            watch_discovery_should_retry(0, 2),
            "an empty second query retries");
    expect(
            watch_discovery_should_retry(0, kWatchDiscoveryMaxAttempts)
                    == false,
            "an empty last query does not retry");
    expect(
            watch_discovery_should_retry(1, 1) == false,
            "a query that accepted a result does not retry");
    expect(
            watch_discovery_should_retry(0, 0) == false,
            "zero completed attempts is not a retry");
}

// ================================================================================

static void test_search_expiry() {
    expect(
            watch_discovery_search_expired(0) == false,
            "a search that just started has not expired");
    expect(
            watch_discovery_search_expired(kWatchDiscoveryTimeoutMs) == false,
            "a search is given its whole window before the grace period");
    expect(
            watch_discovery_search_expired(
                    kWatchDiscoveryTimeoutMs + kWatchDiscoveryGraceMs - 1)
                    == false,
            "the last millisecond of the grace period still waits");
    expect(
            watch_discovery_search_expired(
                    kWatchDiscoveryTimeoutMs + kWatchDiscoveryGraceMs),
            "a search past its window and grace period has expired");
}

// ================================================================================

int main() {
    test_search_retry();
    test_search_expiry();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
