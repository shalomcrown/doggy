#include "watch_rover_status.h"

#include <cstring>

// ================================================================================

bool watch_rover_status_is_rover(const char *body, std::size_t length) {
    if (body == nullptr || length == 0) {
        return false;
    }

    return std::strstr(body, "\"type\":\"ROVER\"") != nullptr
            || std::strstr(body, "\"type\": \"ROVER\"") != nullptr;
}
