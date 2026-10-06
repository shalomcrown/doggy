#include "config.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "sim_link.h"
#include "simulated_rover.h"
#include "telemetry.h"
#include "web_server.h"

#include <plog/Log.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_shutdown_requested{false};

// ================================================================================

void on_shutdown_signal(int) {
    g_shutdown_requested.store(true);
}

// ================================================================================

std::string home_path(const char *suffix) {
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return suffix;
    }
    return std::string(home) + "/" + suffix;
}

// ================================================================================

std::string robot_config_path() {
    const char *env = std::getenv("DOGGY_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return home_path(".config/doggy/simulator/doggy.json");
}

// ================================================================================

std::string settings_path() {
    const char *env = std::getenv("DOGGY_SIM_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return home_path(".config/doggy/simulator.json");
}

// ================================================================================

std::string telemetry_directory() {
    const char *env = std::getenv("DOGGY_TELEMETRY_DIR");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return home_path(".config/doggy/simulator/telemetry");
}

// ================================================================================

int env_port(const char *name, int current) {
    const char *value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return current;
    }
    return std::atoi(value);
}

// ================================================================================

void apply_env(SimSettings &settings) {
    settings.http_port = env_port("DOGGY_SIM_HTTP_PORT", settings.http_port);
    const char *spawn = std::getenv("DOGGY_SIM_SPAWN");
    if (spawn != nullptr && std::strcmp(spawn, "0") == 0) {
        settings.spawn = false;
    }
}

// ================================================================================

pid_t spawn_group(const std::vector<std::string> &args) {
    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const std::string &arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
}

// ================================================================================

void stop_children(const std::vector<pid_t> &children) {
    for (const pid_t pid : children) {
        if (pid > 0) {
            kill(-pid, SIGTERM);
        }
    }
    for (const pid_t pid : children) {
        if (pid > 0) {
            waitpid(pid, nullptr, 0);
        }
    }
}

// ================================================================================

Config load_rover_config(const std::string &path) {
    fs::create_directories(fs::path(path).parent_path());
    if (fs::is_regular_file(path)) {
        Config config = config_load_file(path);
        if (config.robot().type() != doggy::v1::ROVER) {
            config.mutable_robot()->set_type(doggy::v1::ROVER);
        }
        return config;
    }
    Config config = config_from_json(R"({"robot":{"type":"ROVER"}})");
    config_save_file(config, path);
    return config;
}

// ================================================================================

std::string world_path() {
    const char *env = std::getenv("DOGGY_SIM_WORLD");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
#ifdef DOGGY_SIM_SOURCE_DIR
    return std::string(DOGGY_SIM_SOURCE_DIR) + "/sim/worlds/street.sdf";
#else
    return "sim/worlds/street.sdf";
#endif
}

}  // namespace

// ================================================================================

int main() {
    init_doggy_log();

    SimSettings settings;
    std::string settings_error;
    if (sim_load_settings(settings_path(), settings, settings_error) == false) {
        std::cerr << settings_error << std::endl;
        return 1;
    }
    apply_env(settings);
    if (settings.http_port < 1 || settings.http_port > 65535) {
        std::cerr << "Invalid DOGGY_SIM_HTTP_PORT" << std::endl;
        return 1;
    }

    const std::string config_path = robot_config_path();
    Config config;
    try {
        config = load_rover_config(config_path);
    } catch (const ConfigError &ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    const std::string index = default_index_html_path(doggy::v1::ROVER);
    if (index.empty()) {
        std::cerr << "rover web page not found (set DOGGY_WEB_ROOT)" << std::endl;
        return 1;
    }

    std::vector<pid_t> children;
    if (settings.spawn) {
        const std::string world = world_path();
        if (fs::is_regular_file(world)) {
            const pid_t gazebo = spawn_group({"gz", "sim", "-r", world});
            if (gazebo > 0) {
                children.push_back(gazebo);
            } else {
                PLOG_WARNING << "could not start gz sim";
            }
        } else {
            PLOG_WARNING << "simulator world not found: " << world;
        }
    }

    auto rover = std::make_unique<SimulatedRover>(config, config_path, settings);
    TelemetryLog telemetry(telemetry_directory());
    telemetry_bind(telemetry.ok() ? &telemetry : nullptr);
    WebListen listen;
    listen.bind_host = settings.bind_host;
    listen.http_port = settings.http_port;
    listen.https_port = -1;
    WebServer server(*rover, index, listen, nullptr, nullptr);
    std::vector<OfferedCamera> offers;
    for (const SimCameraOffer &item : rover->cameraOffers()) {
        OfferedCamera offer;
        offer.id = item.id;
        offer.name = item.name;
        offer.webrtc_url = item.webrtc_url;
        offers.push_back(std::move(offer));
    }
    server.offerCameras(std::move(offers));
    if (server.start() == false) {
        std::cerr << "Failed to listen on http://" << settings.bind_host << ":"
                  << settings.http_port << std::endl;
        telemetry_bind(nullptr);
        telemetry.close();
        rover.reset();
        stop_children(children);
        return 1;
    }

    std::cout << "Doggy simulator " << DOGGY_VERSION
              << " listening on http://" << settings.bind_host << ":"
              << server.port() << std::endl;
    PLOG_INFO << "Doggy simulator listening on http://" << settings.bind_host
              << ":" << server.port();

    std::signal(SIGTERM, on_shutdown_signal);
    std::signal(SIGINT, on_shutdown_signal);

    while (g_shutdown_requested.load() == false) {
        rover->poll();
        telemetry.sample(rover->getStatus(), rover->getConfig());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    telemetry_bind(nullptr);
    telemetry.close();
    rover.reset();
    stop_children(children);
    return 0;
}
