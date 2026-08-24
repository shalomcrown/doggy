#include "web_server.h"
#include "web_json.h"

#include "httplib.h"

#include <plog/Log.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

// ================================================================================

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    if (in.is_open() == false) {
        return {};
    }

    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

int status_for(CommandResult result) {
    switch (result) {
        case CommandResult::ok: return 200;
        case CommandResult::not_found: return 404;
        case CommandResult::busy: return 409;
        case CommandResult::bad_angle: return 400;
    }

    return 500;
}

std::string error_json(const char *code) {
    return std::string("{\"error\":\"") + code + "\"}";
}

}  // namespace

// ================================================================================

class WebServer::Impl {
public:
    DogApi &api;
    std::string index_html_path;
    std::string bind_host;
    int listen_port;
    httplib::Server server;
    std::thread worker;

    Impl(DogApi &api, std::string index_html_path, std::string bind_host, int port) :
        api(api),
        index_html_path(std::move(index_html_path)),
        bind_host(std::move(bind_host)),
        listen_port(port) {
        server.set_payload_max_length(4096);
        server.Get("/", [this](const httplib::Request &, httplib::Response &res) {
            const std::string html = read_file(this->index_html_path);
            if (html.empty()) {
                res.status = 500;
                res.set_content(error_json("index_missing"), "application/json");
                return;
            }

            res.set_content(html, "text/html; charset=utf-8");
        });

        server.Get("/api/servos", [this](const httplib::Request &, httplib::Response &res) {
            res.set_content(servos_to_json(this->api.listServos()), "application/json");
        });

        server.Get("/api/status", [this](const httplib::Request &, httplib::Response &res) {
            res.set_content(status_to_json(this->api.getStatus()), "application/json");
        });

        server.Post("/api/home", [this](const httplib::Request &, httplib::Response &res) {
            const CommandResult result = this->api.home();
            res.status = status_for(result);
            if (result == CommandResult::ok) {
                res.set_content(servos_to_json(this->api.listServos()), "application/json");
            } else if (result == CommandResult::busy) {
                res.set_content(error_json("busy"), "application/json");
            } else {
                res.set_content(error_json("home_failed"), "application/json");
            }
        });

        server.Post(R"(/api/servos/(\d+))", [this](const httplib::Request &req, httplib::Response &res) {
            int id = 0;
            try {
                id = std::stoi(req.matches[1]);
            } catch (const std::exception &) {
                res.status = 400;
                res.set_content(error_json("bad_id"), "application/json");
                return;
            }

            double angle = 0.0;
            if (parse_angle_json(req.body, angle) == false) {
                res.status = 400;
                res.set_content(error_json("bad_json"), "application/json");
                return;
            }

            const CommandResult result = this->api.setServoAngle(id, angle);
            res.status = status_for(result);
            if (result == CommandResult::ok) {
                res.set_content(servos_to_json(this->api.listServos()), "application/json");
                return;
            }

            if (result == CommandResult::not_found) {
                res.set_content(error_json("not_found"), "application/json");
                return;
            }

            if (result == CommandResult::busy) {
                res.set_content(error_json("busy"), "application/json");
                return;
            }

            res.set_content(error_json("bad_angle"), "application/json");
        });

        server.set_logger([](const httplib::Request &req, const httplib::Response &res) {
            const bool api =
                    req.path == "/api" || req.path.compare(0, 5, "/api/") == 0;
            const bool failed = res.status >= 400;
            if (api == false && failed == false) {
                return;
            }

            if (failed) {
                PLOG_ERROR << req.method << " " << req.path << " " << res.status;
                return;
            }

            if (req.method == "GET" && req.path == "/api/status") {
                return;
            }

            PLOG_INFO << req.method << " " << req.path << " " << res.status;
        });

        server.set_exception_handler(
                [](const httplib::Request &req, httplib::Response &res, std::exception_ptr) {
                    PLOG_ERROR << "exception " << req.method << " " << req.path;
                    res.status = 500;
                    res.set_content(error_json("internal"), "application/json");
                });
    }
};

// ================================================================================

WebServer::WebServer(DogApi &api, std::string index_html_path, std::string bind_host, int port) :
    impl(std::make_unique<Impl>(api, std::move(index_html_path), std::move(bind_host), port)) {
}

// ================================================================================

WebServer::~WebServer() {
    stop();
}

// ================================================================================

bool WebServer::start() {
    if (impl->listen_port == 0) {
        impl->listen_port = impl->server.bind_to_any_port(impl->bind_host);
        if (impl->listen_port < 0) {
            return false;
        }

        impl->worker = std::thread([this]() {
            impl->server.listen_after_bind();
        });
        impl->server.wait_until_ready();
        return true;
    }

    impl->worker = std::thread([this]() {
        impl->server.listen(impl->bind_host, impl->listen_port);
    });
    impl->server.wait_until_ready();
    return true;
}

// ================================================================================

void WebServer::run() {
    impl->server.listen(impl->bind_host, impl->listen_port);
}

// ================================================================================

void WebServer::stop() {
    impl->server.stop();
    if (impl->worker.joinable()) {
        impl->worker.join();
    }
}

// ================================================================================

int WebServer::port() const {
    return impl->listen_port;
}

// ================================================================================

std::string default_index_html_path() {
    if (const char *env = std::getenv("DOGGY_WEB_ROOT")) {
        const fs::path p = fs::path(env) / "index.html";
        if (fs::is_regular_file(p)) {
            return p.string();
        }
    }

#ifdef DOGGY_WEB_SOURCE_DIR
    {
        const fs::path p = fs::path(DOGGY_WEB_SOURCE_DIR) / "index.html";
        if (fs::is_regular_file(p)) {
            return p.string();
        }
    }

#endif
    const fs::path installed = "/usr/share/doggy/index.html";
    if (fs::is_regular_file(installed)) {
        return installed.string();
    }

    return {};
}
