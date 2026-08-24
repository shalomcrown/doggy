#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "dog_status.h"
#include "doggy.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "web_server.h"

#include <plog/Log.h>

int main() {
    const char *port_env = std::getenv("DOGGY_HTTP_PORT");
    int port = 8080;
    if (port_env != nullptr && port_env[0] != '\0') {
        port = std::atoi(port_env);
        if (port <= 0) {
            std::cerr << "Invalid DOGGY_HTTP_PORT" << std::endl;
            return 1;
        }
    }

    const std::string index = default_index_html_path();
    if (index.empty()) {
        std::cerr << "index.html not found (set DOGGY_WEB_ROOT)" << std::endl;
        return 1;
    }

    init_doggy_log();

    Dog doggy;
    for (const DogError &err : doggy.getStatus().errors) {
        PLOG_ERROR << "hardware i2c: " << err.message;
    }

    WebServer server(doggy, index, "0.0.0.0", port);
    if (server.start() == false) {
        std::cerr << "Failed to listen on port " << port << std::endl;
        return 1;
    }

    std::cout << "Doggy " << DOGGY_VERSION
              << " listening on http://0.0.0.0:" << port << std::endl;
    PLOG_INFO << "Doggy " << DOGGY_VERSION
              << " listening on http://0.0.0.0:" << port;

    while (true) {
        doggy.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(kDoggyLoopPeriodMs));
    }
}
