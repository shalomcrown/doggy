#include "dog_api.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "web_server.h"

#include "httplib.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// ================================================================================

class FakeDog : public DogApi {
public:
    std::vector<ServoSnapshot> items{
        {11, "front-right-waist", 0.0, 0}
    };
    bool busy = false;

    std::vector<ServoSnapshot> listServos() override {
        return items;
    }

    CommandResult home() override {
        if (busy) {
            return CommandResult::busy;
        }

        items[0].angle = 135.0;
        items[0].pwm = 400;
        return CommandResult::ok;
    }

    CommandResult setServoAngle(int id, double angle) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (angle < 0.0 || angle > 180.0) {
            return CommandResult::bad_angle;
        }

        if (id != 11) {
            return CommandResult::not_found;
        }

        items[0].angle = angle;
        items[0].pwm = 200;
        return CommandResult::ok;
    }

    DogStatus getStatus() const override {
        return status;
    }

    DogStatus status;
};

// ================================================================================

static int failures = 0;

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }

    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

static httplib::Result get_retry(httplib::Client &cli, const char *path) {
    httplib::Result res;
    for (int i = 0; i < 50; ++i) {
        res = cli.Get(path);
        if (res) {
            return res;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return res;
}

int main() {
    const char *index = std::getenv("DOGGY_TEST_INDEX");
    if (index == nullptr || index[0] == '\0') {
        std::cerr << "DOGGY_TEST_INDEX not set" << std::endl;
        return EXIT_FAILURE;
    }

    const std::filesystem::path log_dir =
            std::filesystem::temp_directory_path()
            / ("doggy-web-log-" + std::to_string(getpid()));
    std::filesystem::remove_all(log_dir);
    std::filesystem::create_directories(log_dir);
    DoggyLogOptions log_options;
    log_options.directory = log_dir.string();
    log_options.roll_on_start = false;
    init_doggy_log(log_options);

    FakeDog dog;
    WebServer server(dog, index, "127.0.0.1", 0);
    if (server.start() == false) {
        std::cerr << "server.start failed" << std::endl;
        return EXIT_FAILURE;
    }

    httplib::Client cli("127.0.0.1", server.port());
    cli.set_connection_timeout(1, 0);

    auto page = get_retry(cli, "/");
    expect(page && page->status == 200, "GET / is 200");
    expect(page && page->body.find("IDLE_MS = 100") != std::string::npos,
           "page uses 100ms idle debounce");
    expect(page && page->body.find("POLL_MS = 200") != std::string::npos,
           "page polls status every 200ms");
    expect(page && page->body.find("Home") != std::string::npos, "page has Home button");
    expect(page && page->body.find("/api/status") != std::string::npos,
           "page fetches /api/status");
    expect(page && page->body.find("id=\"imu\"") != std::string::npos,
           "page has an IMU section");
    expect(page && page->body.find("id=\"battery\"") != std::string::npos,
           "page has a battery section");
    expect(page && page->body.find("id=\"page-title\"") != std::string::npos,
           "page has a title heading");
    expect(page && page->body.find("data.version") != std::string::npos,
           "page applies status version");
    expect(page && page->body.find("titleEl.textContent") != std::string::npos,
           "page sets title via textContent");

    auto healthy = cli.Get("/api/status");
    expect(healthy && healthy->status == 200, "GET /api/status is 200");
    expect(healthy && healthy->body.find("\"errors\":[]") != std::string::npos,
           "GET /api/status empty errors");
    expect(healthy && healthy->body.find("\"imu\"") != std::string::npos,
           "GET /api/status includes imu");
    expect(healthy && healthy->body.find("\"battery\"") != std::string::npos,
           "GET /api/status includes battery");
    expect(healthy && healthy->body.find("\"ok\":false") != std::string::npos,
           "GET /api/status imu.ok is false by default");
    expect(healthy && healthy->body.find("\"voltage_v\"") != std::string::npos,
           "GET /api/status includes voltage_v");
    expect(healthy && healthy->body.find(std::string("\"version\":\"") + DOGGY_VERSION + "\"")
                   != std::string::npos,
           "GET /api/status includes stamped version");

    dog.status.imu.ok = true;
    dog.status.imu.temperature_c = 37.5;
    dog.status.imu.accel = {0.1, 0.2, 0.3};
    dog.status.imu.gyro = {1.0, 2.0, 3.0};
    auto imuOk = cli.Get("/api/status");
    expect(imuOk && imuOk->body.find("\"ok\":true") != std::string::npos,
           "GET /api/status imu.ok true");
    expect(imuOk && imuOk->body.find("\"temperature_c\":37.5") != std::string::npos,
           "GET /api/status temperature");
    expect(imuOk && imuOk->body.find("\"x\":0.1") != std::string::npos,
           "GET /api/status accel x");

    dog.status.battery.ok = true;
    dog.status.battery.voltage_v = 7.4;
    auto batteryOk = cli.Get("/api/status");
    expect(batteryOk && batteryOk->body.find("\"voltage_v\":7.4") != std::string::npos,
           "GET /api/status battery voltage");

    dog.status.errors.push_back(DogError{DogErrorCode::i2c, "Could not open i2c bus.: No such file or directory"});
    auto unhealthy = cli.Get("/api/status");
    expect(unhealthy && unhealthy->body.find("\"code\":\"i2c\"") != std::string::npos,
           "GET /api/status reports i2c code");
    expect(unhealthy && unhealthy->body.find("Could not open i2c bus.") != std::string::npos,
           "GET /api/status reports i2c message");

    auto list = cli.Get("/api/servos");
    expect(list && list->status == 200, "GET /api/servos is 200");
    expect(list && list->body.find("\"items\"") != std::string::npos,
           "GET /api/servos has items");
    expect(list && list->body.find("front-right-waist") != std::string::npos,
           "GET /api/servos has servo name");

    auto moved = cli.Post("/api/servos/11", "{\"angle\":45}", "application/json");
    expect(moved && moved->status == 200, "POST /api/servos/11 is 200");
    expect(moved && moved->body.find("\"angle\":45") != std::string::npos,
           "POST updates angle");

    auto missing = cli.Post("/api/servos/99", "{\"angle\":45}", "application/json");
    expect(missing && missing->status == 404, "unknown servo is 404");

    auto bad = cli.Post("/api/servos/11", "{\"angle\":200}", "application/json");
    expect(bad && bad->status == 400, "angle 200 is 400");

    auto junk = cli.Post("/api/servos/11", "not-json", "application/json");
    expect(junk && junk->status == 400, "bad json is 400");

    auto homed = cli.Post("/api/home", "", "text/plain");
    expect(homed && homed->status == 200, "POST /api/home is 200");
    expect(homed && homed->body.find("\"angle\":135") != std::string::npos,
           "home updates snapshot");

    dog.busy = true;
    auto busy = cli.Post("/api/home", "", "text/plain");
    expect(busy && busy->status == 409, "busy home is 409");

    server.stop();

    std::ifstream log_in(doggy_log_path(log_dir.string()));
    const std::string log_text{
            std::istreambuf_iterator<char>(log_in),
            std::istreambuf_iterator<char>()};
    expect(log_text.find("GET /api/servos 200") != std::string::npos,
           "API GET /api/servos is logged");
    expect(log_text.find("GET /api/status 200") == std::string::npos,
           "successful GET /api/status is not logged");
    expect(log_text.find("POST /api/servos/99 404") != std::string::npos,
           "API failure 404 is logged");
    expect(log_text.find("GET / 200") == std::string::npos,
           "index GET / is not logged as an API call");

    std::filesystem::remove_all(log_dir);

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
