#include "web_server.h"
#include "web_json.h"
#include "doggy_version.h"

#include "httplib.h"

#include <plog/Log.h>

#include <cctype>
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
        case CommandResult::failed: return 500;
        case CommandResult::pin_unset:
        case CommandResult::pin_invalid: return 403;
        case CommandResult::bad_pin: return 400;
        case CommandResult::rate_limited: return 429;
    }

    return 500;
}

std::string error_json(const char *code) {
    return std::string("{\"error\":\"") + code + "\"}";
}

bool is_digits(const std::string &text) {
    if (text.empty()) {
        return false;
    }

    for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }

    return true;
}

std::string host_without_port(const std::string &host) {
    if (host.empty()) {
        return {};
    }

    if (host.front() == '[') {
        const std::size_t close = host.find(']');
        if (close == std::string::npos) {
            return {};
        }

        const std::string name = host.substr(0, close + 1);
        if (close + 1 == host.size()) {
            return name;
        }

        if (host[close + 1] == ':' && is_digits(host.substr(close + 2))) {
            return name;
        }

        return {};
    }

    const std::size_t colon = host.rfind(':');
    if (colon != std::string::npos && colon > 0 && is_digits(host.substr(colon + 1))) {
        if (host.find(':') != colon) {
            return {};
        }

        return host.substr(0, colon);
    }

    return host;
}

bool host_name_ok(const std::string &name) {
    if (name.empty() || name.size() > 253) {
        return false;
    }

    if (name.front() == '[') {
        if (name.back() != ']' || name.size() < 3) {
            return false;
        }

        for (std::size_t i = 1; i + 1 < name.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            if (std::isxdigit(c) == 0 && name[i] != ':' && name[i] != '.') {
                return false;
            }
        }

        return true;
    }

    if (name.find("..") != std::string::npos) {
        return false;
    }

    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) == 0 && ch != '.' && ch != '-') {
            return false;
        }
    }

    return true;
}

void register_api(httplib::Server &server, DogApi &api, const std::string &index_html_path) {
    server.set_payload_max_length(4096);
    server.Get("/", [&index_html_path](const httplib::Request &, httplib::Response &res) {
        const std::string html = read_file(index_html_path);
        if (html.empty()) {
            res.status = 500;
            res.set_content(error_json("index_missing"), "application/json");
            return;
        }

        res.set_content(html, "text/html; charset=utf-8");
    });

    server.Get("/api/servos", [&api](const httplib::Request &, httplib::Response &res) {
        res.set_content(servos_to_json(api.listServos()), "application/json");
    });

    server.Get("/api/status", [&api](const httplib::Request &, httplib::Response &res) {
        res.set_content(status_to_json(api.getStatus(), DOGGY_VERSION), "application/json");
    });

    server.Get("/api/config", [&api](const httplib::Request &, httplib::Response &res) {
        res.set_content(api.getConfig().to_public_json_string(), "application/json");
    });

    server.Put("/api/config", [&api](const httplib::Request &req, httplib::Response &res) {
        Config config;
        try {
            config = Config::from_json_string(req.body);
        } catch (const ConfigError &) {
            res.status = 400;
            res.set_content(error_json("bad_json"), "application/json");
            return;
        }

        const CommandResult result = api.replaceConfig(config);
        res.status = status_for(result);
        if (result == CommandResult::ok) {
            res.set_content(api.getConfig().to_public_json_string(), "application/json");
            return;
        }

        if (result == CommandResult::busy) {
            res.set_content(error_json("busy"), "application/json");
            return;
        }

        res.set_content(error_json("config_write"), "application/json");
    });

    server.Post("/api/system", [&api](const httplib::Request &req, httplib::Response &res) {
        SystemAction action = SystemAction::restart;
        std::string pin;
        if (parse_system_post(req.body, action, pin) == false) {
            res.status = 400;
            res.set_content(error_json("bad_json"), "application/json");
            return;
        }

        const CommandResult result = api.requestSystemAction(action, pin);
        res.status = result == CommandResult::ok ? 202 : status_for(result);
        if (result == CommandResult::ok) {
            res.set_content(system_accepted_json(action), "application/json");
            return;
        }

        if (result == CommandResult::pin_unset) {
            res.set_content(error_json("pin_unset"), "application/json");
            return;
        }

        if (result == CommandResult::pin_invalid) {
            res.set_content(error_json("pin_invalid"), "application/json");
            return;
        }

        if (result == CommandResult::busy) {
            res.set_content(error_json("busy"), "application/json");
            return;
        }

        if (result == CommandResult::rate_limited) {
            res.set_content(error_json("rate_limited"), "application/json");
            return;
        }

        res.set_content(error_json("system_action"), "application/json");
    });

    server.Post("/api/system/pin", [&api](const httplib::Request &req, httplib::Response &res) {
        std::string pin;
        std::string current_pin;
        if (parse_pin_post(req.body, pin, current_pin) == false) {
            res.status = 400;
            res.set_content(error_json("bad_json"), "application/json");
            return;
        }

        const CommandResult result = api.setSystemPin(pin, current_pin);
        res.status = status_for(result);
        if (result == CommandResult::ok) {
            res.set_content(api.getConfig().to_public_json_string(), "application/json");
            return;
        }

        if (result == CommandResult::bad_pin) {
            res.set_content(error_json("bad_pin"), "application/json");
            return;
        }

        if (result == CommandResult::pin_invalid) {
            res.set_content(error_json("pin_invalid"), "application/json");
            return;
        }

        if (result == CommandResult::busy) {
            res.set_content(error_json("busy"), "application/json");
            return;
        }

        res.set_content(error_json("config_write"), "application/json");
    });

    server.Post("/api/home", [&api](const httplib::Request &, httplib::Response &res) {
        const CommandResult result = api.home();
        res.status = status_for(result);
        if (result == CommandResult::ok) {
            res.set_content(servos_to_json(api.listServos()), "application/json");
        } else if (result == CommandResult::busy) {
            res.set_content(error_json("busy"), "application/json");
        } else {
            res.set_content(error_json("home_failed"), "application/json");
        }
    });

    server.Post(R"(/api/servos/(\d+))", [&api](const httplib::Request &req, httplib::Response &res) {
        int id = 0;
        try {
            id = std::stoi(req.matches[1]);
        } catch (const std::exception &) {
            res.status = 400;
            res.set_content(error_json("bad_id"), "application/json");
            return;
        }

        double angle = 0.0;
        bool disable = false;
        if (parse_servo_post(req.body, disable, angle) == false) {
            res.status = 400;
            res.set_content(error_json("bad_json"), "application/json");
            return;
        }

        const CommandResult result = disable
                ? api.disableServo(id)
                : api.setServoAngle(id, angle);
        res.status = status_for(result);
        if (result == CommandResult::ok) {
            res.set_content(servos_to_json(api.listServos()), "application/json");
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
        const bool api_path =
                req.path == "/api" || req.path.compare(0, 5, "/api/") == 0;
        const bool failed = res.status >= 400;
        if (api_path == false && failed == false) {
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

void register_redirect(httplib::Server &server, int https_port) {
    server.set_payload_max_length(1024);
    server.set_pre_routing_handler(
            [https_port](const httplib::Request &req, httplib::Response &res) {
                const std::string location = https_redirect_location(
                        req.get_header_value("Host"), req.target, https_port);
                if (location.empty()) {
                    res.status = 400;
                    res.set_content(error_json("bad_host"), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                res.status = 301;
                res.set_header("Location", location);
                res.set_content("", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            });
}

}  // namespace

// ================================================================================

std::string https_redirect_location(const std::string &host_header,
                                    const std::string &target,
                                    int https_port) {
    const std::string name = host_without_port(host_header);
    if (host_name_ok(name) == false) {
        return {};
    }

    const std::string path = target.empty() ? "/" : target;
    if (path.front() != '/') {
        return {};
    }

    std::string location = "https://";
    location += name;
    if (https_port > 0 && https_port != 443) {
        location += ':';
        location += std::to_string(https_port);
    }

    location += path;
    return location;
}

// ================================================================================

class WebServer::Impl {
public:
    DogApi &api;
    std::string index_html_path;
    WebListen listen;
    std::unique_ptr<httplib::SSLServer> https;
    std::unique_ptr<httplib::Server> http;
    std::thread https_worker;
    std::thread http_worker;

    Impl(DogApi &api, std::string index_html_path, WebListen listen) :
        api(api),
        index_html_path(std::move(index_html_path)),
        listen(std::move(listen)) {
        if (this->listen.https_port >= 0) {
            https = std::make_unique<httplib::SSLServer>(
                    this->listen.cert_path.c_str(), this->listen.key_path.c_str());
            if (https->is_valid()) {
                register_api(*https, this->api, this->index_html_path);
            } else {
                https.reset();
            }
        }

        if (this->listen.https_port < 0) {
            http = std::make_unique<httplib::Server>();
            register_api(*http, this->api, this->index_html_path);
            return;
        }

        if (this->listen.http_port >= 0) {
            http = std::make_unique<httplib::Server>();
            register_redirect(*http, this->listen.https_port);
        }
    }
};

// ================================================================================

WebServer::WebServer(DogApi &api, std::string index_html_path, std::string bind_host,
                     int port) :
    WebServer(api, std::move(index_html_path),
              WebListen{std::move(bind_host), port, -1, {}, {}}) {
}

// ================================================================================

WebServer::WebServer(DogApi &api, std::string index_html_path, WebListen listen) :
    impl(std::make_unique<Impl>(api, std::move(index_html_path), std::move(listen))) {
}

// ================================================================================

WebServer::~WebServer() {
    stop();
}

// ================================================================================

static bool start_server(httplib::Server &server, const std::string &host, int &port,
                         std::thread &worker) {
    if (port == 0) {
        port = server.bind_to_any_port(host);
        if (port < 0) {
            return false;
        }

        worker = std::thread([&server]() {
            server.listen_after_bind();
        });
        server.wait_until_ready();
        return true;
    }

    worker = std::thread([&server, &host, port]() {
        server.listen(host, port);
    });
    server.wait_until_ready();
    return true;
}

// ================================================================================

bool WebServer::start() {
    if (impl->listen.https_port >= 0 && impl->https == nullptr) {
        return false;
    }

    if (impl->https != nullptr) {
        if (start_server(*impl->https, impl->listen.bind_host, impl->listen.https_port,
                         impl->https_worker) == false) {
            return false;
        }

        if (impl->http != nullptr) {
            register_redirect(*impl->http, impl->listen.https_port);
        }
    }

    if (impl->http != nullptr) {
        if (start_server(*impl->http, impl->listen.bind_host, impl->listen.http_port,
                         impl->http_worker) == false) {
            stop();
            return false;
        }
    }

    return impl->https != nullptr || impl->http != nullptr;
}

// ================================================================================

void WebServer::run() {
    httplib::Server *server = impl->https
            ? static_cast<httplib::Server *>(impl->https.get())
            : impl->http.get();
    if (server == nullptr) {
        return;
    }

    const int listen_port = port();
    server->listen(impl->listen.bind_host, listen_port);
}

// ================================================================================

void WebServer::stop() {
    if (impl->https != nullptr) {
        impl->https->stop();
    }

    if (impl->http != nullptr) {
        impl->http->stop();
    }

    if (impl->https_worker.joinable()) {
        impl->https_worker.join();
    }

    if (impl->http_worker.joinable()) {
        impl->http_worker.join();
    }
}

// ================================================================================

int WebServer::port() const {
    if (impl->listen.https_port >= 0) {
        return impl->listen.https_port;
    }

    return impl->listen.http_port;
}

// ================================================================================

int WebServer::plain_port() const {
    return impl->listen.http_port;
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
