#include <cstdlib>
#include <iostream>

#include "doggy.h"
#include "doggy_version.h"
#include "web_server.h"

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

    Dog doggy;
    WebServer server(doggy, index, "0.0.0.0", port);
    std::cout << "Doggy " << DOGGY_VERSION
              << " listening on http://0.0.0.0:" << port << std::endl;
    server.run();
    return 0;
}
