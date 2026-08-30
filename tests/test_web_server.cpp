#include "dog_api.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "tls_cert.h"
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
    Config config;
    bool busy = false;
    bool write_fail = false;

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

        if (id != items[0].id) {
            return CommandResult::not_found;
        }

        items[0].angle = angle;
        items[0].pwm = 200;
        return CommandResult::ok;
    }

    CommandResult disableServo(int id) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (id != items[0].id) {
            return CommandResult::not_found;
        }

        items[0].pwm = 0;
        return CommandResult::ok;
    }

    DogStatus getStatus() const override {
        DogStatus copy = status;
        copy.servos = items;
        return copy;
    }

    Config getConfig() const override {
        return config;
    }

    CommandResult replaceConfig(const Config &next) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (write_fail) {
            return CommandResult::failed;
        }

        const SystemConfig kept_pin = config.system;
        config = next;
        config.system = kept_pin;
        items[0].id = next.servos.front_right_waist;
        return CommandResult::ok;
    }

    CommandResult requestSystemAction(SystemAction action, const std::string &pin) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (action_pending) {
            return CommandResult::busy;
        }

        if (lockout) {
            return CommandResult::rate_limited;
        }

        if (config.system.pin_is_set() == false) {
            return CommandResult::pin_unset;
        }

        if (Config::pin_matches(pin, config.system.pin_hash) == false) {
            pin_failures += 1;
            if (pin_failures >= 5) {
                lockout = true;
                return CommandResult::rate_limited;
            }

            return CommandResult::pin_invalid;
        }

        last_action = action;
        action_pending = true;
        return CommandResult::ok;
    }

    CommandResult setSystemPin(const std::string &pin, const std::string &current_pin) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (Config::pin_length_ok(pin) == false) {
            return CommandResult::bad_pin;
        }

        if (config.system.pin_is_set()
                && Config::pin_matches(current_pin, config.system.pin_hash) == false) {
            return CommandResult::pin_invalid;
        }

        config.system.pin_hash = Config::hash_pin(pin);
        return CommandResult::ok;
    }

    DogStatus status;
    SystemAction last_action = SystemAction::restart;
    bool action_pending = false;
    bool lockout = false;
    int pin_failures = 0;
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

template <typename Client>
static httplib::Result get_retry(Client &cli, const char *path) {
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

    expect(https_redirect_location("192.168.1.5", "/", 443) == "https://192.168.1.5/",
           "redirect 443 omits port");
    expect(https_redirect_location("192.168.1.5:80", "/api/status", 443)
                   == "https://192.168.1.5/api/status",
           "redirect strips :80 and keeps path");
    expect(https_redirect_location("doggy.local", "/?x=1", 8443)
                   == "https://doggy.local:8443/?x=1",
           "redirect includes non-443 port and query");
    expect(https_redirect_location("evil.com/x", "/", 443).empty(),
           "redirect rejects Host with a path");
    expect(https_redirect_location("http://evil", "/", 443).empty(),
           "redirect rejects Host with a scheme");

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
    expect(page && page->body.find("data.servos") != std::string::npos,
           "page applies status servos");
    expect(page && page->body.find("applyServoReadings") != std::string::npos,
           "page updates servo rows in place");
    expect(page && page->body.find("enabled") != std::string::npos,
           "page posts enabled false to turn a servo off");
    expect(page && page->body.find(">Off<") != std::string::npos
                   || (page && page->body.find("\"Off\"") != std::string::npos),
           "page has an Off control");
    expect(page && page->body.find("id=\"config\"") != std::string::npos,
           "page has a Configuration section");
    expect(page && page->body.find("id=\"config-refresh\"") != std::string::npos,
           "page has a config Refresh control");
    expect(page && page->body.find("id=\"config-save\"") != std::string::npos,
           "page has a config Save control");
    expect(page && page->body.find("/api/config") != std::string::npos,
           "page fetches /api/config");
    expect(page && page->body.find("replaceAll(\"_\", \" \")") != std::string::npos,
           "page display-cases config keys");
    expect(page && page->body.find("charAt(0).toUpperCase") != std::string::npos,
           "page capitalizes only the first label letter");
    expect(page && page->body.find("I2C bus and address changes apply after restart")
                   != std::string::npos,
           "page says I2C changes apply after restart");
    expect(page && page->body.find("loadServos()") != std::string::npos,
           "page rebuilds the servo table after Save");
    expect(page && page->body.find("id=\"power\"") != std::string::npos,
           "page has a Power section");
    expect(page && page->body.find("id=\"system-restart\"") != std::string::npos,
           "page has Restart service");
    expect(page && page->body.find("id=\"system-reboot\"") != std::string::npos,
           "page has Reboot");
    expect(page && page->body.find("id=\"system-shutdown\"") != std::string::npos,
           "page has Shut down Pi");
    expect(page && page->body.find("/api/system") != std::string::npos,
           "page posts /api/system");
    expect(page && page->body.find("/api/system/pin") != std::string::npos,
           "page can set the system PIN");
    expect(page && page->body.find("SHUTDOWN") != std::string::npos,
           "page requires typing SHUTDOWN");

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
    expect(healthy && healthy->body.find("\"servos\"") != std::string::npos,
           "GET /api/status includes servos");
    expect(healthy && healthy->body.find("\"angle\":null") != std::string::npos,
           "GET /api/status angle is null when pwm is 0");

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
    expect(list && list->body.find("\"angle\":null") != std::string::npos,
           "GET /api/servos angle is null when pwm is 0");

    auto moved = cli.Post("/api/servos/11", "{\"angle\":45}", "application/json");
    expect(moved && moved->status == 200, "POST /api/servos/11 is 200");
    expect(moved && moved->body.find("\"angle\":45") != std::string::npos,
           "POST updates angle");

    auto statusMoved = cli.Get("/api/status");
    expect(statusMoved && statusMoved->body.find("\"angle\":45") != std::string::npos,
           "GET /api/status reflects last commanded angle");

    auto off = cli.Post("/api/servos/11", "{\"enabled\":false}", "application/json");
    expect(off && off->status == 200, "POST enabled false is 200");
    expect(off && off->body.find("\"pwm\":0") != std::string::npos,
           "POST enabled false sets pwm 0");
    expect(off && off->body.find("\"angle\":null") != std::string::npos,
           "POST enabled false sets angle null");

    auto enableOnly = cli.Post("/api/servos/11", "{\"enabled\":true}", "application/json");
    expect(enableOnly && enableOnly->status == 400, "POST enabled true without angle is 400");

    auto missingOff = cli.Post("/api/servos/99", "{\"enabled\":false}", "application/json");
    expect(missingOff && missingOff->status == 404, "unknown servo off is 404");

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

    auto cfg = cli.Get("/api/config");
    expect(cfg && cfg->status == 200, "GET /api/config is 200");
    expect(cfg && cfg->body.find("\"0x40\"") != std::string::npos,
           "GET /api/config has hex servo_board address");
    expect(cfg && cfg->body.find("\"front_right_waist\":11") != std::string::npos,
           "GET /api/config has default front_right_waist");

    auto put = cli.Put("/api/config",
                       R"({"servos":{"front_right_waist":1}})",
                       "application/json");
    expect(put && put->status == 200, "PUT /api/config is 200");
    expect(put && put->body.find("\"front_right_waist\":1") != std::string::npos,
           "PUT /api/config remaps front_right_waist");
    expect(put && put->body.find("\"0x40\"") != std::string::npos,
           "PUT /api/config keeps default hex addresses");
    expect(cfg && cfg->body.find("\"pin_set\":false") != std::string::npos,
           "GET /api/config reports pin_set false");
    expect(cfg && cfg->body.find("pin_hash") == std::string::npos,
           "GET /api/config omits pin_hash");

    auto unsetAct = cli.Post("/api/system",
                             R"({"action":"restart","pin":"1234"})",
                             "application/json");
    expect(unsetAct && unsetAct->status == 403, "system action without PIN is 403");
    expect(unsetAct && unsetAct->body.find("pin_unset") != std::string::npos,
           "system action without PIN is pin_unset");

    auto shortPin = cli.Post("/api/system/pin", R"({"pin":"12"})", "application/json");
    expect(shortPin && shortPin->status == 400, "short PIN is 400");

    auto setPin = cli.Post("/api/system/pin", R"({"pin":"1234"})", "application/json");
    expect(setPin && setPin->status == 200, "set PIN is 200");
    expect(setPin && setPin->body.find("\"pin_set\":true") != std::string::npos,
           "set PIN reports pin_set true");
    expect(setPin && setPin->body.find("pin_hash") == std::string::npos,
           "set PIN response omits pin_hash");

    auto badCurrent = cli.Post("/api/system/pin",
                               R"({"pin":"5678","current_pin":"0000"})",
                               "application/json");
    expect(badCurrent && badCurrent->status == 403, "wrong current PIN is 403");

    auto wrongPin = cli.Post("/api/system",
                             R"({"action":"reboot","pin":"0000"})",
                             "application/json");
    expect(wrongPin && wrongPin->status == 403, "wrong action PIN is 403");

    auto restarted = cli.Post("/api/system",
                              R"({"action":"restart","pin":"1234"})",
                              "application/json");
    expect(restarted && restarted->status == 202, "restart is 202");
    expect(restarted && restarted->body.find("\"action\":\"restart\"") != std::string::npos,
           "restart body names the action");

    auto pending = cli.Post("/api/system",
                            R"({"action":"shutdown","pin":"1234"})",
                            "application/json");
    expect(pending && pending->status == 409, "pending system action is 409");
    dog.action_pending = false;

    auto rebooted = cli.Post("/api/system",
                             R"({"action":"reboot","pin":"1234"})",
                             "application/json");
    expect(rebooted && rebooted->status == 202, "reboot is 202");
    dog.action_pending = false;

    auto halted = cli.Post("/api/system",
                           R"({"action":"shutdown","pin":"1234"})",
                           "application/json");
    expect(halted && halted->status == 202, "shutdown is 202");
    dog.action_pending = false;

    dog.pin_failures = 4;
    auto locked = cli.Post("/api/system",
                           R"({"action":"restart","pin":"9999"})",
                           "application/json");
    expect(locked && locked->status == 429, "fifth bad PIN is 429");
    dog.lockout = false;
    dog.pin_failures = 0;

    auto remapped = cli.Get("/api/servos");
    expect(remapped && remapped->body.find("\"id\":1") != std::string::npos,
           "channel remap updates servo id immediately");

    auto badCfg = cli.Put("/api/config", "not-json", "application/json");
    expect(badCfg && badCfg->status == 400, "PUT /api/config bad json is 400");
    expect(badCfg && badCfg->body.find("\"error\":\"bad_json\"") != std::string::npos,
           "PUT /api/config bad json uses bad_json");

    auto rangeCfg = cli.Put("/api/config",
                            R"({"servos":{"head_neck":16}})",
                            "application/json");
    expect(rangeCfg && rangeCfg->status == 400, "PUT /api/config range is 400");

    dog.write_fail = true;
    auto writeFail = cli.Put("/api/config",
                             R"({"servos":{"front_right_waist":2}})",
                             "application/json");
    expect(writeFail && writeFail->status == 500, "PUT /api/config write fail is 500");
    dog.write_fail = false;

    dog.busy = true;
    auto busyOff = cli.Post("/api/servos/1", "{\"enabled\":false}", "application/json");
    expect(busyOff && busyOff->status == 409, "busy disable is 409");
    auto busy = cli.Post("/api/home", "", "text/plain");
    expect(busy && busy->status == 409, "busy home is 409");
    auto busyCfg = cli.Put("/api/config",
                           R"({"servos":{"front_right_waist":3}})",
                           "application/json");
    expect(busyCfg && busyCfg->status == 409, "busy PUT /api/config is 409");

    server.stop();

    std::ifstream log_in(doggy_log_path(log_dir.string()));
    const std::string log_text{
            std::istreambuf_iterator<char>(log_in),
            std::istreambuf_iterator<char>()};
    expect(log_text.find("GET /api/servos 200") != std::string::npos,
           "API GET /api/servos is logged");
    expect(log_text.find("GET /api/config 200") != std::string::npos,
           "API GET /api/config is logged");
    expect(log_text.find("GET /api/status 200") == std::string::npos,
           "successful GET /api/status is not logged");
    expect(log_text.find("POST /api/servos/99 404") != std::string::npos,
           "API failure 404 is logged");
    expect(log_text.find("GET / 200") == std::string::npos,
           "index GET / is not logged as an API call");

    const std::filesystem::path tls_dir =
            std::filesystem::temp_directory_path()
            / ("doggy-web-tls-" + std::to_string(getpid()));
    std::filesystem::remove_all(tls_dir);
    std::filesystem::create_directories(tls_dir);
    const std::string cert = (tls_dir / "tls.crt").string();
    const std::string key = (tls_dir / "tls.key").string();
    std::string tls_error;
    expect(ensure_self_signed_tls_files(cert, key, &tls_error), "TLS test cert generated");

    dog.busy = false;
    WebListen listen;
    listen.bind_host = "127.0.0.1";
    listen.http_port = 0;
    listen.https_port = 0;
    listen.cert_path = cert;
    listen.key_path = key;
    WebServer tls(dog, index, listen);
    if (tls.start() == false) {
        std::cerr << "tls.start failed" << std::endl;
        std::filesystem::remove_all(tls_dir);
        return EXIT_FAILURE;
    }

    httplib::SSLClient scli("127.0.0.1", tls.port());
    scli.set_connection_timeout(2, 0);
    scli.enable_server_certificate_verification(false);
    scli.enable_server_hostname_verification(false);
    auto https_status = get_retry(scli, "/api/status");
    expect(https_status && https_status->status == 200, "HTTPS GET /api/status is 200");
    expect(https_status && https_status->body.find("\"servos\"") != std::string::npos,
           "HTTPS GET /api/status is the API");

    httplib::Client hcli("127.0.0.1", tls.plain_port());
    hcli.set_connection_timeout(2, 0);
    hcli.set_follow_location(false);
    auto redirected = hcli.Get("/api/status");
    expect(redirected && redirected->status == 301, "HTTP GET /api/status is 301");
    const std::string location = redirected ? redirected->get_header_value("Location") : "";
    expect(location.find("https://127.0.0.1:") == 0, "HTTP Location is https on the TLS port");
    expect(location.find("/api/status") != std::string::npos, "HTTP Location keeps the path");
    expect(redirected && redirected->body.find("\"servos\"") == std::string::npos,
           "HTTP redirect does not serve the API");
    expect(redirected && redirected->body.find("pin") == std::string::npos,
           "HTTP redirect body has no pin");

    auto http_pin = hcli.Post("/api/system",
                              R"({"action":"restart","pin":"1234"})",
                              "application/json");
    expect(http_pin && http_pin->status == 301, "HTTP POST /api/system is 301");
    expect(http_pin && http_pin->body.find("accepted") == std::string::npos,
           "HTTP POST /api/system does not accept a PIN");

    tls.stop();
    std::filesystem::remove_all(tls_dir);

    std::filesystem::remove_all(log_dir);

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
