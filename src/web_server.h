#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "dog_api.h"

#include <memory>
#include <string>

// ================================================================================

class WebServer {
public:
    WebServer(DogApi &api, std::string index_html_path, std::string bind_host, int port);
    ~WebServer();

    bool start();
    void run();
    void stop();
    int port() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

// ================================================================================

std::string default_index_html_path();

#endif
