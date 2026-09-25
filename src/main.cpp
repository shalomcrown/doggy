#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "config.h"
#include "camera_pipeline.h"
#include "media_store.h"
#include "dog_status.h"
#include "doggy.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "rover.h"
#include "system_control.h"
#include "tls_cert.h"
#include "web_server.h"

#include <memory>

#include <plog/Log.h>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_shutdown_requested{false};

// ================================================================================

void on_shutdown_signal(int) {
    g_shutdown_requested.store(true);
}

}  // namespace

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

    init_doggy_log();

    std::string create_error;
    Config config;
    try {
        config = config_load_or_create(config_default_path(), &create_error);
    } catch (const ConfigError &ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    if (create_error.empty() == false) {
        PLOG_ERROR << create_error;
    }

    const std::string index = default_index_html_path(config.robot().type());
    if (index.empty()) {
        std::cerr << "robot web page not found (set DOGGY_WEB_ROOT)" << std::endl;
        return 1;
    }

    const fs::path tls_dir = fs::path(config_default_path()).parent_path();
    const std::string cert_path = path_from_env_or("DOGGY_TLS_CERT", tls_dir / "tls.crt");
    const std::string key_path = path_from_env_or("DOGGY_TLS_KEY", tls_dir / "tls.key");
    std::string tls_error;
    if (ensure_self_signed_tls_files(cert_path, key_path, &tls_error) == false) {
        std::cerr << tls_error << std::endl;
        return 1;
    }

    std::unique_ptr<RobotApi> robot;
    Dog *dog = nullptr;
    Rover *rover = nullptr;
    if (config.robot().type() == doggy::v1::ROVER) {
        auto instance = std::make_unique<Rover>(
                config, config_default_path(), std::make_unique<SystemdControl>());
        rover = instance.get();
        robot = std::move(instance);
    } else {
        auto instance = std::make_unique<Dog>(
                config, config_default_path(), std::make_unique<SystemdControl>());
        dog = instance.get();
        robot = std::move(instance);
    }

    for (const std::string &message : status_error_messages(robot->getStatus())) {
        PLOG_ERROR << "hardware i2c: " << message;
    }

    WebListen listen;
    listen.bind_host = "0.0.0.0";
    listen.https_port = https_port;
    listen.http_port = http_port;
    listen.cert_path = cert_path;
    listen.key_path = key_path;

    CameraPipeline camera_pipeline;
    MediaStore media_store;
    WebServer server(*robot, index, listen, &camera_pipeline, &media_store);
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

    std::signal(SIGTERM, on_shutdown_signal);
    std::signal(SIGINT, on_shutdown_signal);

    while (g_shutdown_requested.load() == false) {
        if (dog != nullptr) {
            dog->poll();
        } else {
            rover->poll();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDoggyLoopPeriodMs));
    }

    PLOG_INFO << "Shutting down — stopping rover outputs";
    server.stop();
    if (rover != nullptr) {
        rover->stop();
    }

    return 0;
}
