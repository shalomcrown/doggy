#ifndef DOGGY_WATCH_DOGGY_H
#define DOGGY_WATCH_DOGGY_H

#include <cstddef>
#include <cstdint>

inline constexpr std::size_t kWatchDoggyCapacity = 8;
inline constexpr std::size_t kWatchDoggyInstanceSize = 64;
inline constexpr std::size_t kWatchDoggyHostnameSize = 64;

// ================================================================================

struct WatchDoggyTarget {
    char instance[kWatchDoggyInstanceSize];
    char hostname[kWatchDoggyHostnameSize];
    std::uint16_t port;
};

// ================================================================================

bool watch_doggy_target_set(
        WatchDoggyTarget &target,
        const char *instance,
        const char *hostname,
        std::uint16_t port);

// ================================================================================

bool watch_doggy_target_valid(const WatchDoggyTarget &target);

// ================================================================================

bool watch_doggy_target_equal(
        const WatchDoggyTarget &left,
        const WatchDoggyTarget &right);

// ================================================================================

std::size_t watch_doggy_list_add(
        WatchDoggyTarget *entries,
        std::size_t count,
        const WatchDoggyTarget &candidate);

#endif
