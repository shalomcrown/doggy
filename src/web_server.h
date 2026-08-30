#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "dog_api.h"

#include <memory>
#include <string>

// ================================================================================

class WebListen {
public:
    std::string bind_host = "0.0.0.0";
    int http_port = 0;
    int https_port = -1;
    std::string cert_path;
    std::string key_path;
};

// ================================================================================

class WebServer {
public:
    WebServer(DogApi &api, std::string index_html_path, std::string bind_host, int port);
    WebServer(DogApi &api, std::string index_html_path, WebListen listen);
    ~WebServer();

    bool start();
    void run();
    void stop();
    int port() const;
    int plain_port() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

// ================================================================================

std::string https_redirect_location(const std::string &host_header,
                                    const std::string &target,
                                    int https_port);

// ================================================================================

std::string default_index_html_path();

#endif
