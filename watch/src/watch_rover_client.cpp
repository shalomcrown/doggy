#include "watch_rover_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

namespace {

// ================================================================================

bool build_base_url(const WatchDoggyTarget &target, char *url, std::size_t length) {
    if (watch_doggy_target_valid(target) == false || length < 16) {
        return false;
    }

    const int written = std::snprintf(
            url,
            length,
            "https://%s:%u",
            target.hostname,
            static_cast<unsigned>(target.port));
    return written > 0 && static_cast<std::size_t>(written) < length;
}

// ================================================================================

WatchRoverPostResult post_empty(const WatchDoggyTarget &target, const char *path) {
    char base[128] = {};
    if (build_base_url(target, base, sizeof(base)) == false) {
        return WatchRoverPostResult::network_error;
    }

    char url[160] = {};
    if (std::snprintf(url, sizeof(url), "%s%s", base, path) <= 0) {
        return WatchRoverPostResult::network_error;
    }

    WiFiClientSecure tls;
    tls.setInsecure();
    HTTPClient http;
    if (http.begin(tls, url) == false) {
        return WatchRoverPostResult::network_error;
    }

    http.setTimeout(3000);
    const int code = http.POST("");
    http.end();
    if (code == 409) {
        return WatchRoverPostResult::busy;
    }

    if (code == 404) {
        return WatchRoverPostResult::not_rover;
    }

    if (code < 200 || code >= 300) {
        return WatchRoverPostResult::http_error;
    }

    return WatchRoverPostResult::ok;
}

}  // namespace

// ================================================================================

WatchRoverPostResult watch_rover_post_heartbeat(const WatchDoggyTarget &target) {
    return post_empty(target, "/api/heartbeat");
}

// ================================================================================

WatchRoverPostResult watch_rover_post_drive(
        const WatchDoggyTarget &target,
        float speed,
        float turn) {
    char base[128] = {};
    if (build_base_url(target, base, sizeof(base)) == false) {
        return WatchRoverPostResult::network_error;
    }

    char url[160] = {};
    if (std::snprintf(url, sizeof(url), "%s/api/drive", base) <= 0) {
        return WatchRoverPostResult::network_error;
    }

    char body[64] = {};
    const int body_len = std::snprintf(
            body,
            sizeof(body),
            "{\"speed\":%.4f,\"turn\":%.4f}",
            static_cast<double>(speed),
            static_cast<double>(turn));
    if (body_len <= 0 || static_cast<std::size_t>(body_len) >= sizeof(body)) {
        return WatchRoverPostResult::network_error;
    }

    WiFiClientSecure tls;
    tls.setInsecure();
    HTTPClient http;
    if (http.begin(tls, url) == false) {
        return WatchRoverPostResult::network_error;
    }

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(3000);
    const int code = http.POST(body);
    http.end();
    if (code == 409) {
        return WatchRoverPostResult::busy;
    }

    if (code == 404) {
        return WatchRoverPostResult::not_rover;
    }

    if (code < 200 || code >= 300) {
        return WatchRoverPostResult::http_error;
    }

    return WatchRoverPostResult::ok;
}
