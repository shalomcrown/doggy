#include "watch_rover_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

namespace {

WiFiClientSecure g_tls;
HTTPClient g_http;
WatchDoggyTarget g_session_target{};
bool g_session_target_valid = false;

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

void close_session() {
    g_http.end();
    g_tls.stop();
    g_session_target_valid = false;
    g_session_target = {};
}

// ================================================================================

bool session_matches(const WatchDoggyTarget &target) {
    return g_session_target_valid && watch_doggy_target_equal(target, g_session_target);
}

// ================================================================================

WatchRoverPostResult post_on_session(
        const WatchDoggyTarget &target,
        const char *path,
        const char *body,
        const char *content_type) {
    char base[128] = {};
    if (build_base_url(target, base, sizeof(base)) == false) {
        return WatchRoverPostResult::network_error;
    }

    if (session_matches(target) == false) {
        close_session();
        g_session_target = target;
        g_session_target_valid = true;
    }

    char url[160] = {};
    if (std::snprintf(url, sizeof(url), "%s%s", base, path) <= 0) {
        return WatchRoverPostResult::network_error;
    }

    g_tls.setInsecure();
    if (g_http.begin(g_tls, url) == false) {
        close_session();
        return WatchRoverPostResult::network_error;
    }

    g_http.setReuse(true);
    g_http.setTimeout(2500);
    if (content_type != nullptr) {
        g_http.addHeader("Content-Type", content_type);
    }

    const int code = body == nullptr ? g_http.POST("") : g_http.POST(body);
    g_http.end();

    if (code == 409) {
        return WatchRoverPostResult::busy;
    }

    if (code == 404) {
        close_session();
        return WatchRoverPostResult::not_rover;
    }

    if (code < 200 || code >= 300) {
        close_session();
        return WatchRoverPostResult::http_error;
    }

    return WatchRoverPostResult::ok;
}

}  // namespace

// ================================================================================

WatchRoverPostResult watch_rover_post_heartbeat(const WatchDoggyTarget &target) {
    return post_on_session(target, "/api/heartbeat", nullptr, nullptr);
}

// ================================================================================

WatchRoverPostResult watch_rover_post_stop(const WatchDoggyTarget &target) {
    return post_on_session(target, "/api/stop", nullptr, nullptr);
}

// ================================================================================

void watch_rover_client_reset_session() {
    close_session();
}

// ================================================================================

WatchRoverPostResult watch_rover_post_drive(
        const WatchDoggyTarget &target,
        float speed,
        float turn) {
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

    return post_on_session(target, "/api/drive", body, "application/json");
}
