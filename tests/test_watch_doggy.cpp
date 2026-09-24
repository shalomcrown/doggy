#include "watch_doggy.h"

#include <cstdio>
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

static WatchDoggyTarget target(
        const char *instance,
        const char *hostname,
        std::uint16_t port) {
    WatchDoggyTarget value{};
    std::snprintf(value.instance, sizeof(value.instance), "%s", instance);
    std::snprintf(value.hostname, sizeof(value.hostname), "%s", hostname);
    value.port = port;
    return value;
}

// ================================================================================

static void test_validation() {
    expect(
            watch_doggy_target_valid(target("Kitchen", "kitchen.local", 443)),
            "complete target is valid");
    expect(
            watch_doggy_target_valid(target("", "kitchen.local", 443)) == false,
            "empty instance is rejected");
    expect(
            watch_doggy_target_valid(target("Kitchen", "", 443)) == false,
            "empty hostname is rejected");
    expect(
            watch_doggy_target_valid(target("Kitchen", "kitchen.local", 0))
                    == false,
            "zero port is rejected");
}

// ================================================================================

static void test_bounded_network_copy() {
    WatchDoggyTarget value{};
    expect(
            watch_doggy_target_set(
                    value,
                    "Kitchen",
                    "kitchen.local",
                    443),
            "bounded network target is copied");
    expect(
            std::strcmp(value.instance, "Kitchen") == 0
                    && std::strcmp(value.hostname, "kitchen.local") == 0,
            "network target text is preserved");

    char oversized[kWatchDoggyHostnameSize + 1];
    std::memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    expect(
            watch_doggy_target_set(value, "Kitchen", oversized, 443) == false,
            "overlong mDNS hostname is rejected");
}

// ================================================================================

static void test_add_and_deduplicate() {
    WatchDoggyTarget entries[kWatchDoggyCapacity]{};
    std::size_t count = watch_doggy_list_add(
            entries,
            0,
            target("Kitchen", "kitchen.local", 443));
    count = watch_doggy_list_add(
            entries,
            count,
            target("Garage", "garage.local", 443));
    expect(count == 2, "distinct doggys are retained");

    count = watch_doggy_list_add(
            entries,
            count,
            target("Kitchen", "kitchen.local", 8443));
    expect(count == 2, "duplicate service does not grow the list");
    expect(entries[0].port == 8443, "duplicate service refreshes its port");
}

// ================================================================================

static void test_capacity_and_invalid_input() {
    WatchDoggyTarget entries[kWatchDoggyCapacity]{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < kWatchDoggyCapacity; index += 1) {
        char instance[24];
        char hostname[32];
        std::snprintf(instance, sizeof(instance), "Doggy %zu", index);
        std::snprintf(hostname, sizeof(hostname), "doggy-%zu.local", index);
        count = watch_doggy_list_add(
                entries,
                count,
                target(instance, hostname, 443));
    }
    expect(count == kWatchDoggyCapacity, "list fills to its fixed capacity");

    count = watch_doggy_list_add(
            entries,
            count,
            target("Overflow", "overflow.local", 443));
    expect(count == kWatchDoggyCapacity, "ninth result is ignored");

    count = watch_doggy_list_add(entries, count, target("", "bad.local", 443));
    expect(count == kWatchDoggyCapacity, "invalid result is ignored");
}

// ================================================================================

int main() {
    test_validation();
    test_bounded_network_copy();
    test_add_and_deduplicate();
    test_capacity_and_invalid_input();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
