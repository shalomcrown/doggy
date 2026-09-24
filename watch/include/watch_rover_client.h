#ifndef DOGGY_WATCH_ROVER_CLIENT_H
#define DOGGY_WATCH_ROVER_CLIENT_H

#include "watch_doggy.h"

// ================================================================================

enum class WatchRoverPostResult {
    ok,
    http_error,
    not_rover,
    busy,
    network_error,
};

// ================================================================================

WatchRoverPostResult watch_rover_post_heartbeat(const WatchDoggyTarget &target);

// ================================================================================

WatchRoverPostResult watch_rover_post_drive(
        const WatchDoggyTarget &target,
        float speed,
        float turn);

#endif
