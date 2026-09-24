#include "watch_doggy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// ================================================================================

static bool bounded_text_valid(const char *text, std::size_t capacity) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    return std::memchr(text, '\0', capacity) != nullptr;
}

}

// ================================================================================

bool watch_doggy_target_set(
        WatchDoggyTarget &target,
        const char *instance,
        const char *hostname,
        std::uint16_t port) {
    if (instance == nullptr || hostname == nullptr || port == 0) {
        return false;
    }
    if (std::memchr(instance, '\0', sizeof(target.instance)) == nullptr
            || std::memchr(hostname, '\0', sizeof(target.hostname)) == nullptr
            || instance[0] == '\0'
            || hostname[0] == '\0') {
        return false;
    }
    WatchDoggyTarget value{};
    std::snprintf(value.instance, sizeof(value.instance), "%s", instance);
    std::snprintf(value.hostname, sizeof(value.hostname), "%s", hostname);
    value.port = port;
    target = value;
    return true;
}

// ================================================================================

bool watch_doggy_target_valid(const WatchDoggyTarget &target) {
    return bounded_text_valid(target.instance, sizeof(target.instance))
            && bounded_text_valid(target.hostname, sizeof(target.hostname))
            && target.port != 0;
}

// ================================================================================

bool watch_doggy_target_equal(
        const WatchDoggyTarget &left,
        const WatchDoggyTarget &right) {
    if (watch_doggy_target_valid(left) == false
            || watch_doggy_target_valid(right) == false) {
        return false;
    }
    return std::strcmp(left.instance, right.instance) == 0
            && std::strcmp(left.hostname, right.hostname) == 0
            && left.port == right.port;
}

// ================================================================================

std::size_t watch_doggy_list_add(
        WatchDoggyTarget *entries,
        std::size_t count,
        const WatchDoggyTarget &candidate) {
    count = std::min(count, kWatchDoggyCapacity);
    if (entries == nullptr || watch_doggy_target_valid(candidate) == false) {
        return count;
    }
    for (std::size_t index = 0; index < count; index += 1) {
        if (std::strcmp(entries[index].instance, candidate.instance) == 0
                && std::strcmp(
                        entries[index].hostname,
                        candidate.hostname) == 0) {
            entries[index] = candidate;
            return count;
        }
    }
    if (count == kWatchDoggyCapacity) {
        return count;
    }
    entries[count] = candidate;
    return count + 1;
}
