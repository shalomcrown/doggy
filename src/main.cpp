#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "config.h"
#include "dog_status.h"
#include "doggy.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "system_control.h"
#include "tls_cert.h"
#include "web_server.h"

#include <memory>

#include <plog/Log.h>

namespace fs = std::filesystem;

// ================================================================================

static int port_from_env(const char *name, int fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    const int port = std::atoi(value);
    if (port <= 0) {
        std::cerr << "Invalid " << name << std::endl;
        std::exit(1);
    }

    return port;
}

// ================================================================================

static std::string path_from_env_or(const char *name, const fs::path &fallback) {
    const char *value = std::getenv(name);
    if (value != nullptr && value[0] != '\0') {
        return value;
    }

    return fallback.string();
}

// ================================================================================

int main() {
    const int https_port = port_from_env("DOGGY_HTTPS_PORT", 443);
    const int http_port = port_from_env("DOGGY_HTTP_PORT", 80);

    const std::string index = default_index_html_path();
    if (index.empty()) {
        std::cerr << "index.html not found (set DOGGY_WEB_ROOT)" << std::endl;
        return 1;
    }

    init_doggy_log();

    std::string create_error;
    Config config;
    try {
        config = Config::load_or_create(Config::default_path(), &create_error);
    } catch (const ConfigError &ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    if (create_error.empty() == false) {
        PLOG_ERROR << create_error;
    }

    const fs::path tls_dir = fs::path(Config::default_path()).parent_path();
    const std::string cert_path = path_from_env_or("DOGGY_TLS_CERT", tls_dir / "tls.crt");
    const std::string key_path = path_from_env_or("DOGGY_TLS_KEY", tls_dir / "tls.key");
    std::string tls_error;
    if (ensure_self_signed_tls_files(cert_path, key_path, &tls_error) == false) {
        std::cerr << tls_error << std::endl;
        return 1;
    }

    Dog doggy(config, Config::default_path(), std::make_unique<SystemdControl>());
    for (const DogError &err : doggy.getStatus().errors) {
        PLOG_ERROR << "hardware i2c: " << err.message;
    }

    WebListen listen;
    listen.bind_host = "0.0.0.0";
    listen.https_port = https_port;
    listen.http_port = http_port;
    listen.cert_path = cert_path;
    listen.key_path = key_path;

    WebServer server(doggy, index, listen);
    if (server.start() == false) {
        std::cerr << "Failed to listen on https://" << listen.bind_host << ":"
                  << https_port << std::endl;
        return 1;
    }

    std::cout << "Doggy " << DOGGY_VERSION
              << " listening on https://0.0.0.0:" << server.port()
              << " (http://0.0.0.0:" << server.plain_port() << " redirects)"
              << std::endl;
    PLOG_INFO << "Doggy " << DOGGY_VERSION
              << " listening on https://0.0.0.0:" << server.port()
              << " (http://0.0.0.0:" << server.plain_port() << " redirects)";

    while (true) {
        doggy.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(kDoggyLoopPeriodMs));
    }
}
