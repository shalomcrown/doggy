#include "camera_pipeline.h"
#include "dog_api.h"
#include "doggy_log.h"
#include "doggy_version.h"
#include "media_store.h"
#include "rover_api.h"
#include "tls_cert.h"

#include <mbedtls/build_info.h>
#include "web_server.h"

#include "httplib.h"
#include "nlohmann/json.hpp"

#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <atomic>
#include <thread>
#include <unistd.h>
#include <vector>

// ================================================================================

static ServoSnapshot make_servo(int id, const char *name) {
    ServoSnapshot item;
    item.set_id(id);
    item.set_name(name);
    item.set_pwm(0);
    return item;
}

// ================================================================================

static MotorSnapshot make_motor(int id, const char *name) {
    MotorSnapshot item;
    item.set_id(id);
    item.set_name(name);
    item.set_pwm(0);
    item.set_enabled(true);
    item.set_direction(doggy::v1::forward);
    return item;
}

// ================================================================================

class FakeDog : public DogApi {
public:
    std::vector<ServoSnapshot> items{make_servo(11, "front-right-waist")};
    Config config = default_config();
    bool busy = false;
    bool write_fail = false;

    RobotType robotType() const override {
        return doggy::v1::DOG;
    }

    std::vector<ServoSnapshot> listServos() override {
        return items;
    }

    CommandResult home() override {
        if (busy) {
            return CommandResult::busy;
        }

        items[0].set_angle(135.0);
        items[0].set_pwm(400);
        return CommandResult::ok;
    }

    CommandResult setServoAngle(int id, double angle) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (angle < 0.0 || angle > 180.0) {
            return CommandResult::bad_angle;
        }

        if (id != items[0].id()) {
            return CommandResult::not_found;
        }

        items[0].set_angle(angle);
        items[0].set_pwm(200);
        return CommandResult::ok;
    }

    CommandResult disableServo(int id) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (id != items[0].id()) {
            return CommandResult::not_found;
        }

        items[0].set_pwm(0);
        items[0].clear_angle();
        return CommandResult::ok;
    }

    DogStatus getStatus() const override {
        DogStatus copy = status;
        copy.mutable_servos()->clear_items();
        for (const ServoSnapshot &item : items) {
            *copy.mutable_servos()->add_items() = item;
        }
        return copy;
    }

    Config getConfig() const override {
        return config;
    }

    CommandResult replaceConfig(const Config &next, const std::string &pin) override {
        if (busy) {
            return CommandResult::busy;
        }

        if (write_fail) {
            return CommandResult::failed;
        }

        const bool type_changed = next.robot().type() != config.robot().type();
        if (type_changed) {
            const CommandResult authorized = requestSystemAction(SystemAction::restart, pin);
            if (authorized != CommandResult::ok) {
                return authorized;
            }
        }

        const doggy::v1::System kept_pin = config.system();
        config = next;
        *config.mutable_system() = kept_pin;
        items[0].set_id(next.servos().front_right_waist());
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

        if (pin_is_set(config) == false) {
            return CommandResult::pin_unset;
        }

        if (pin_matches(pin, config.system().pin_hash()) == false) {
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

        if (pin_length_ok(pin) == false) {
            return CommandResult::bad_pin;
        }

        if (pin_is_set(config)
                && pin_matches(current_pin, config.system().pin_hash()) == false) {
            return CommandResult::pin_invalid;
        }

        config.mutable_system()->set_pin_hash(hash_pin(pin));
        return CommandResult::ok;
    }

    DogStatus status;
    SystemAction last_action = SystemAction::restart;
    bool action_pending = false;
    bool lockout = false;
    int pin_failures = 0;
};

// ================================================================================

class FakeRover : public RoverApi {
public:
    Config config = default_config();
    std::vector<MotorSnapshot> motors{
        make_motor(0, "front-left"),
        make_motor(1, "front-right"),
        make_motor(2, "rear-left"),
        make_motor(3, "rear-right")
    };
    double speed = 0.0;
    double turn = 0.0;
    bool drive_fail = false;
    int heartbeat_count = 0;
    CommandResult heartbeat_result = CommandResult::ok;

    FakeRover() {
        config.mutable_robot()->set_type(doggy::v1::ROVER);
    }

    RobotType robotType() const override {
        return doggy::v1::ROVER;
    }

    std::vector<MotorSnapshot> listMotors() override {
        return motors;
    }

    CommandResult setDrive(double next_speed, double next_turn) override {
        if (drive_fail) {
            return CommandResult::failed;
        }
        if (next_speed < -1.0 || next_speed > 1.0
                || next_turn < -1.0 || next_turn > 1.0) {
            return CommandResult::bad_drive;
        }
        speed = next_speed;
        turn = next_turn;
        return CommandResult::ok;
    }

    // ================================================================================

    CommandResult heartbeat() override {
        heartbeat_count += 1;
        return heartbeat_result;
    }

    DogStatus getStatus() const override {
        DogStatus result;
        result.set_type(doggy::v1::ROVER);
        result.set_speed(speed);
        result.set_turn(turn);
        for (const MotorSnapshot &item : motors) {
            *result.mutable_motors()->add_items() = item;
        }
        return result;
    }

    Config getConfig() const override {
        return config;
    }

    CommandResult replaceConfig(const Config &next, const std::string &) override {
        config = next;
        return CommandResult::ok;
    }

    CommandResult requestSystemAction(SystemAction, const std::string &) override {
        return CommandResult::ok;
    }

    CommandResult setSystemPin(const std::string &, const std::string &) override {
        return CommandResult::ok;
    }

    CommandResult stop() override {
        speed = 0.0;
        turn = 0.0;
        for (MotorSnapshot &item : motors) {
            item.set_pwm(0);
        }
        return CommandResult::ok;
    }

    CommandResult brake() override {
        speed = 0.0;
        turn = 0.0;
        for (MotorSnapshot &item : motors) {
            item.set_pwm(0);
        }
        return CommandResult::ok;
    }

    CommandResult runMotor(int id, double sp) override {
        if (id < 0 || id >= static_cast<int>(motors.size())) return CommandResult::not_found;
        motors[id].set_pwm(static_cast<int>(std::abs(sp) * 400));
        return CommandResult::ok;
    }

    CommandResult coastMotor(int id) override {
        if (id < 0 || id >= static_cast<int>(motors.size())) return CommandResult::not_found;
        motors[id].set_pwm(0);
        return CommandResult::ok;
    }

    CommandResult brakeMotor(int id) override {
        if (id < 0 || id >= static_cast<int>(motors.size())) return CommandResult::not_found;
        motors[id].set_pwm(0);
        return CommandResult::ok;
    }
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

// ================================================================================

class FakeCameraProcess final : public CameraProcess {
public:
    int starts = 0;
    bool is_running = false;

    // ================================================================================

    bool start(const CameraProcessSpec &) override {
        starts += 1;
        is_running = true;
        return true;
    }

    // ================================================================================

    bool running() override {
        return is_running;
    }

    // ================================================================================

    void stop() override {
        is_running = false;
    }
};

// ================================================================================

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

    auto camera_process = std::make_unique<FakeCameraProcess>();
    FakeCameraProcess *camera_process_ptr = camera_process.get();
    CameraPipeline camera_pipeline(std::move(camera_process));
    const std::filesystem::path media_root = std::filesystem::temp_directory_path()
            / ("doggy-web-media-" + std::to_string(getpid()));
    const std::filesystem::path recordings = media_root / "recordings";
    const std::filesystem::path snapshots = media_root / "snapshots";
    const std::filesystem::path ffmpeg = media_root / "ffmpeg";
    std::filesystem::create_directories(recordings);
    std::filesystem::create_directories(snapshots);
    {
        std::ofstream out(recordings / "pi-2026-09-20-093301.ts", std::ios::binary);
        out << "mpegts-bytes";
    }
    {
        std::ofstream out(ffmpeg);
        out << "#!/bin/sh\n"
            << "for a in \"$@\"; do\n"
            << "  case \"$a\" in *.jpg) printf JPEG > \"$a\" ;; esac\n"
            << "done\n";
    }
    std::filesystem::permissions(ffmpeg, std::filesystem::perms::owner_exec | std::filesystem::perms::owner_read);
    MediaStore media_store(recordings.string(), snapshots.string(), ffmpeg.string());
    FakeDog dog;
    dog.config.mutable_cameras()->mutable_items(0)->set_source("v4l2");
    dog.config.mutable_cameras()->mutable_items(0)->set_device("/dev/video0");
    WebServer server(dog, index, "127.0.0.1", 0, &camera_pipeline, &media_store);
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
    expect(page && page->body.find("id=\"linux-time\"") != std::string::npos,
           "dog page displays Linux time");
    expect(page && page->body.find("id=\"camera-video\"") != std::string::npos
                   && page->body.find("MediaMTXWebRTCReader") != std::string::npos,
           "dog page has local WebRTC camera playback");
    expect(page && page->body.find("id=\"camera-snapshot\"") != std::string::npos
                   && page->body.find("/api/recordings/") != std::string::npos,
           "dog page lists recordings and takes snapshots");
    expect(page && page->body.find("class=\"camera-media\"") != std::string::npos
                   && page->body.find("class=\"camera-media\"")
                              > page->body.find("id=\"camera-video\""),
           "dog page puts media controls beside the picture, not under it");
    expect(page && page->body.find("<hr class=\"section-rule\">") != std::string::npos
                   && page->body.find("<hr class=\"section-rule\">")
                              < page->body.find("id=\"config\""),
           "dog page rules off the configuration section");
    expect(page && page->body.find("[\"robot\", \"turn_gain_min\"]") != std::string::npos
                   && page->body.find("[\"media\", \"retain_hours\"]") != std::string::npos,
           "dog page edits turn gain and media retention");
    expect(page && page->body.find("toISOString()") != std::string::npos,
           "dog page formats Linux time as UTC ISO-8601");
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
    expect(page && page->body.find("/api/motors") == std::string::npos,
           "dog page does not fetch /api/motors");
    expect(page && page->body.find("failureText(res, \"Save failed\")") != std::string::npos,
           "dog page shows the server's save failure reason");
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
    expect(page && page->body.find("id=\"robot-type\"") != std::string::npos,
           "dog page can change robot type");
    expect(page && page->body.find("id=\"config-pin\"") != std::string::npos,
           "dog page has a PIN field for type change");
    expect(page && page->body.find("[\"lora\"") != std::string::npos,
           "dog page can edit lora settings");
    expect(page && page->body.find("lora-band") != std::string::npos,
           "dog page uses HF/LF dongle select for lora.band");
    expect(page && page->body.find("lora-baud") != std::string::npos,
           "dog page can set lora serial baud");
    expect(page && page->body.find("[\"lora\", \"lbt\"]") != std::string::npos,
           "dog page can set numeric lora LBT");
    expect(page && page->body.find("[\"robot\", \"gcs_timeout_s\"]")
                   != std::string::npos,
           "dog page preserves the rover GCS timeout");
    expect(page && page->body.find("camera-rotation") != std::string::npos
                   && page->body.find(
                              "[\"cameras\", \"items\", index, \"rotation_deg\"]")
                              != std::string::npos,
           "dog page edits camera rotation");
    expect(page && page->body.find("lora-air-key") != std::string::npos,
           "dog page can set lora air_key");
    expect(page && page->body.find("Refresh the page") != std::string::npos,
           "page warns to refresh after a type change");
    expect(page && page->body.find("position: fixed") != std::string::npos
                   && page->body.find("#status:empty") != std::string::npos,
           "dog page pins the status line so Save feedback stays visible");

    const std::time_t before_status = std::time(nullptr);
    auto healthy = cli.Get("/api/status");
    expect(healthy && healthy->status == 200, "GET /api/status is 200");
    expect(healthy && healthy->body.find("\"imu\"") != std::string::npos,
           "GET /api/status includes imu");
    expect(healthy && healthy->body.find("\"battery\"") != std::string::npos,
           "GET /api/status includes battery");
    expect(healthy && healthy->body.find("\"ok\":false") != std::string::npos,
           "GET /api/status imu.ok is false by default");
    expect(healthy && healthy->body.find(std::string("\"version\":\"") + DOGGY_VERSION + "\"")
                   != std::string::npos,
           "GET /api/status includes stamped version");
    expect(healthy && healthy->body.find("\"unix_time\":") != std::string::npos,
           "GET /api/status includes Linux unix time");
    const nlohmann::json healthy_json = nlohmann::json::parse(healthy->body);
    const std::time_t unix_time =
            static_cast<std::time_t>(std::stoll(
                    healthy_json.at("unix_time").get<std::string>()));
    expect(unix_time >= before_status && unix_time <= std::time(nullptr),
           "GET /api/status stamps current whole-second Linux time");
    expect(healthy && healthy->body.find("\"type\":\"DOG\"") != std::string::npos,
           "dog status includes DOG type");
    expect(healthy && healthy->body.find("\"servos\"") != std::string::npos,
           "GET /api/status includes servos");
    expect(healthy && healthy->body.find("\"angle\"") == std::string::npos,
           "GET /api/status omits angle when pwm is 0");
    auto dog_heartbeat = cli.Post("/api/heartbeat", "", "text/plain");
    expect(dog_heartbeat && dog_heartbeat->status == 404,
           "POST /api/heartbeat on dog is 404");
    auto dog_cameras = cli.Get("/api/cameras");
    expect(dog_cameras && dog_cameras->status == 200
                   && dog_cameras->body.find("\"id\":\"cam0\"") != std::string::npos
                   && dog_cameras->body.find(
                              "https://127.0.0.1:8889/cam0/whep")
                              != std::string::npos,
           "GET /api/cameras returns dog WHEP stream");
    expect(camera_process_ptr->starts == 1,
           "first camera GET starts one feeder");
    auto recordings_list = cli.Get("/api/recordings");
    expect(recordings_list && recordings_list->status == 200
                   && recordings_list->body.find("pi-2026-09-20-093301.ts")
                              != std::string::npos,
           "GET /api/recordings lists MPEG-TS files");
    auto recording_file = cli.Get("/api/recordings/pi-2026-09-20-093301.ts");
    expect(recording_file && recording_file->status == 200
                   && recording_file->body.find("mpegts-bytes") != std::string::npos,
           "GET /api/recordings copies a live MPEG-TS file");
    auto traversal = cli.Get("/api/recordings/..%2Fetc%2Fpasswd");
    expect(traversal && traversal->status == 404,
           "GET /api/recordings rejects traversal");
    auto snapshot = cli.Post("/api/snapshots", "", "text/plain");
    expect(snapshot && snapshot->status == 200
                   && snapshot->body.find(".jpg") != std::string::npos,
           "POST /api/snapshots writes a JPEG");
    dog.config.mutable_cameras()->mutable_items(0)->set_enabled(false);
    auto disabled_cameras = cli.Get("/api/cameras");
    expect(disabled_cameras && disabled_cameras->status == 404,
           "GET /api/cameras is 404 when cameras are disabled");
    dog.config.mutable_cameras()->mutable_items(0)->set_enabled(true);

    dog.status.mutable_imu()->set_ok(true);
    dog.status.mutable_imu()->set_temperature_c(37.5);
    dog.status.mutable_imu()->mutable_accel()->set_x(0.1);
    dog.status.mutable_imu()->mutable_accel()->set_y(0.2);
    dog.status.mutable_imu()->mutable_accel()->set_z(0.3);
    dog.status.mutable_imu()->mutable_gyro()->set_x(1.0);
    dog.status.mutable_imu()->mutable_gyro()->set_y(2.0);
    dog.status.mutable_imu()->mutable_gyro()->set_z(3.0);
    auto imuOk = cli.Get("/api/status");
    expect(imuOk && imuOk->body.find("\"ok\":true") != std::string::npos,
           "GET /api/status imu.ok true");
    expect(imuOk && imuOk->body.find("\"temperature_c\":37.5") != std::string::npos,
           "GET /api/status temperature");
    expect(imuOk && imuOk->body.find("\"x\":0.1") != std::string::npos,
           "GET /api/status accel x");

    dog.status.mutable_battery()->set_ok(true);
    dog.status.mutable_battery()->set_voltage_v(7.4);
    auto batteryOk = cli.Get("/api/status");
    expect(batteryOk && batteryOk->body.find("\"voltage_v\":7.4") != std::string::npos,
           "GET /api/status battery voltage");

    doggy::v1::Error *status_err = dog.status.add_errors();
    status_err->set_code(doggy::v1::i2c);
    status_err->set_message("Could not open i2c bus.: No such file or directory");
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
    expect(list && list->body.find("\"angle\"") == std::string::npos,
           "GET /api/servos omits angle when pwm is 0");

    auto dogMotors = cli.Get("/api/motors");
    expect(dogMotors && dogMotors->status == 404, "GET /api/motors on dog is 404");

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
    expect(off && off->body.find("\"angle\"") == std::string::npos,
           "POST enabled false omits angle");

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
    expect(cfg && cfg->body.find("\"lora\"") != std::string::npos,
           "GET /api/config includes lora");
    expect(cfg && cfg->body.find("air_key_set") != std::string::npos,
           "GET /api/config reports air_key_set");
    expect(cfg && cfg->body.find("\"air_key\"") == std::string::npos,
           "GET /api/config omits air_key");

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

    auto typeWithoutPin = cli.Put(
            "/api/config", R"({"robot":{"type":"ROVER"}})", "application/json");
    expect(typeWithoutPin && typeWithoutPin->status == 403,
           "type change without configured PIN is 403");

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

    auto typeWrongPin = cli.Put(
            "/api/config",
            R"({"robot":{"type":"ROVER"},"pin":"0000"})",
            "application/json");
    expect(typeWrongPin && typeWrongPin->status == 403,
           "type change with wrong PIN is 403");

    auto typeChanged = cli.Put(
            "/api/config",
            R"({"robot":{"type":"ROVER"},"pin":"1234"})",
            "application/json");
    expect(typeChanged && typeChanged->status == 202,
           "type change with PIN is 202");
    expect(typeChanged && typeChanged->body.find("pin_hash") == std::string::npos,
           "type change response omits PIN hash");
    dog.action_pending = false;
    dog.config.mutable_robot()->set_type(doggy::v1::DOG);

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
    expect(badCfg && badCfg->body.find("\"message\":\"invalid config JSON\"") != std::string::npos,
           "PUT /api/config bad json explains the body was unparsable");
    expect(badCfg && badCfg->body.find("protobuf") == std::string::npos
                   && badCfg->body.find("INVALID_ARGUMENT") == std::string::npos,
           "PUT /api/config bad json does not leak parser detail");

    auto rangeCfg = cli.Put("/api/config",
                            R"({"servos":{"head_neck":16}})",
                            "application/json");
    expect(rangeCfg && rangeCfg->status == 400, "PUT /api/config range is 400");
    expect(rangeCfg && rangeCfg->body.find("head_neck") != std::string::npos
                   && rangeCfg->body.find("out of range") != std::string::npos,
           "PUT /api/config range names the rejected field");

    auto dupCfg = cli.Put("/api/config",
                          R"({"motors":{"front_left":{"pwm":5}}})",
                          "application/json");
    expect(dupCfg && dupCfg->status == 400, "PUT /api/config duplicate channel is 400");
    expect(dupCfg && dupCfg->body.find("duplicate motor channel") != std::string::npos,
           "PUT /api/config duplicate channel says which rule failed");

    dog.write_fail = true;
    auto writeFail = cli.Put("/api/config",
                             R"({"servos":{"front_right_waist":2}})",
                             "application/json");
    expect(writeFail && writeFail->status == 500, "PUT /api/config write fail is 500");
    expect(writeFail && writeFail->body.find("\"error\":\"config_write\"") != std::string::npos,
           "PUT /api/config write fail uses config_write");
    expect(writeFail && writeFail->body.find("doggy.json") == std::string::npos
                   && writeFail->body.find("/etc/") == std::string::npos,
           "PUT /api/config write fail does not disclose the config path");
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

    const std::filesystem::path rover_index =
            std::filesystem::path(index).parent_path() / "rover.html";
    FakeRover rover;
    rover.config.mutable_cameras()->mutable_items(0)->set_source("v4l2");
    rover.config.mutable_cameras()->mutable_items(0)->set_device("/dev/video0");
    WebServer rover_server(
            rover, rover_index.string(), "127.0.0.1", 0, &camera_pipeline);
    expect(rover_server.start(), "rover server starts");
    httplib::Client rover_cli("127.0.0.1", rover_server.port());
    auto rover_page = get_retry(rover_cli, "/");
    expect(rover_page && rover_page->body.find("/api/motors") != std::string::npos,
           "rover page fetches /api/motors");
    expect(rover_page && rover_page->body.find("\"Run\"") != std::string::npos,
           "rover page has Run control");
    expect(rover_page && rover_page->body.find("id=\"steering\"") != std::string::npos,
           "rover page has steering slider");
    expect(rover_page && rover_page->body.find("id=\"speed\"") != std::string::npos,
           "rover page has speed slider");
    expect(rover_page && rover_page->body.find("class=\"drive-layout\"") != std::string::npos,
           "rover drive row lays out sliders, camera, and joystick");
    expect(rover_page && rover_page->body.find("id=\"drive-video\"") != std::string::npos,
           "rover page reserves the camera slot");
    expect(rover_page && rover_page->body.find("<video id=\"camera-video\"")
                   != std::string::npos
                   && rover_page->body.find("MediaMTXWebRTCReader")
                              != std::string::npos,
           "rover camera slot plays WebRTC");
    expect(rover_page && rover_page->body.find("id=\"camera-snapshot\"") != std::string::npos
                   && rover_page->body.find("[\"robot\", \"turn_gain_min\"]")
                              != std::string::npos,
           "rover page snapshots and edits turn gain");
    expect(rover_page && rover_page->body.find("class=\"drive-media\"") != std::string::npos
                   && rover_page->body.find("class=\"drive-media\"")
                              > rover_page->body.find("id=\"joystick\"")
                   && rover_page->body.find("class=\"drive-media\"")
                              > rover_page->body.find("id=\"drive-video\""),
           "rover media controls sit under the joystick, not under the video");
    expect(rover_page && rover_page->body.find("<hr class=\"section-rule\">") != std::string::npos
                   && rover_page->body.find("<hr class=\"section-rule\">")
                              < rover_page->body.find("id=\"config\""),
           "rover page rules off the configuration section");
    expect(rover_page && rover_page->body.find("id=\"joystick\"") != std::string::npos,
           "rover page has a drive joystick");
    expect(rover_page && rover_page->body.find("HEARTBEAT_MS = 750") != std::string::npos
                   && rover_page->body.find("/api/heartbeat") != std::string::npos,
           "rover page sends a heartbeat every 750 ms");
    expect(rover_page && rover_page->body.find("heartbeatBusy") != std::string::npos
                   && rover_page->body.find("setInterval(sendHeartbeat, HEARTBEAT_MS)")
                              != std::string::npos,
           "rover page suppresses overlapping heartbeat requests");
    expect(rover_page && rover_page->body.find("sendBeacon") == std::string::npos
                   && rover_page->body.find("beforeunload") == std::string::npos,
           "rover page does not extend liveness while closing");
    expect(rover_page && rover_page->body.find("id=\"gcs-status\"") != std::string::npos
                   && rover_page->body.find("heartbeatFailures >= 2")
                              != std::string::npos,
           "rover page reports persistent heartbeat failure");
    expect(rover_page && rover_page->body.find("id=\"linux-time\"") != std::string::npos,
           "rover page displays Linux time");
    expect(rover_page && rover_page->body.find("toISOString()") != std::string::npos,
           "rover page formats Linux time as UTC ISO-8601");
    expect(rover_page
                   && rover_page->body.find("[\"robot\", \"gcs_timeout_s\"]")
                              != std::string::npos,
           "rover page edits the GCS timeout");
    expect(rover_page
                   && rover_page->body.find("camera-rotation")
                              != std::string::npos
                   && rover_page->body.find(
                              "[\"cameras\", \"items\", index, \"rotation_deg\"]")
                              != std::string::npos,
           "rover page edits camera rotation");
    expect(rover_page && rover_page->body.find("input.min = \"2\"") != std::string::npos
                   && rover_page->body.find("input.max = \"60\"") != std::string::npos,
           "rover page constrains GCS timeout to the API range");
    expect(rover_page && rover_page->body.find("JOYSTICK_DEADZONE = 0.15") != std::string::npos
                   && rover_page->body.find("applyAxisDeadzone") != std::string::npos,
           "joystick dead zone zeros steer and speed independently");
    expect(rover_page && rover_page->body.find("ArrowUp") != std::string::npos,
           "joystick is keyboard operable");
    expect(rover_page
                   && rover_page->body.find("\"pointerup\", releaseJoystick") != std::string::npos
                   && rover_page->body.find("\"pointercancel\", releaseJoystick") != std::string::npos
                   && rover_page->body.find("\"lostpointercapture\", releaseJoystick")
                              != std::string::npos,
           "joystick springs back to center on release, cancel, and lost capture");
    expect(rover_page && rover_page->body.find("function releaseJoystick") != std::string::npos
                   && rover_page->body.find("sendDriveNow();") != std::string::npos,
           "releasing the joystick stops the rover immediately, not after the debounce");
    expect(rover_page && rover_page->body.find("sendDrive(command)") != std::string::npos
                   && rover_page->body.find("body: JSON.stringify({ speed: command.speed, "
                                            "turn: command.turn })")
                              != std::string::npos,
           "drive posts the queued command, not whatever the sliders hold when it fires");
    expect(rover_page && rover_page->body.find("const driveSettled") != std::string::npos
                   && rover_page->body.find("pendingDrive === null && driveTimer === null")
                              != std::string::npos,
           "status poll does not overwrite sliders while a drive command is in flight");
    expect(rover_page && rover_page->body.find("id=\"drive-brake\"") != std::string::npos,
           "rover page has drive brake");
    expect(rover_page && rover_page->body.find("failureText(res, \"Save failed\")") != std::string::npos,
           "rover page shows the server's save failure reason");
    expect(rover_page && rover_page->body.find("speedEl.value = \"0\"") != std::string::npos
                   && rover_page->body.find("/api/brake") != std::string::npos,
           "drive brake zeros speed sliders without posting /api/drive");
    expect(rover_page && rover_page->body.find("zeroAllMotorSpeedSliders()") != std::string::npos
                   && rover_page->body.find("/api/stop") != std::string::npos,
           "drive stop zeros motor speed sliders");
    expect(rover_page && rover_page->body.find("clearTimeout(driveTimer)") != std::string::npos,
           "stop and brake cancel a pending drive post");
    expect(rover_page && rover_page->body.find("zeroMotorSpeedSlider(speedInput, speedVal)")
                   != std::string::npos,
           "per-motor coast and brake zero that motor slider");
    expect(rover_page && rover_page->body.find("id=\"robot-type\"") != std::string::npos,
           "rover page can change robot type");
    expect(rover_page && rover_page->body.find("id=\"config-pin\"") != std::string::npos,
           "rover page has a PIN field for type change");
    expect(rover_page && rover_page->body.find("[\"lora\"") != std::string::npos,
           "rover page can edit lora settings");
    expect(rover_page && rover_page->body.find("lora-band") != std::string::npos,
           "rover page uses HF/LF dongle select for lora.band");
    expect(rover_page && rover_page->body.find("lora-baud") != std::string::npos,
           "rover page can set lora serial baud");
    expect(rover_page && rover_page->body.find("[\"motors\", key, \"pwm\"]")
                   != std::string::npos
                   && rover_page->body.find("[\"motors\", key, \"in1\"]")
                   != std::string::npos
                   && rover_page->body.find("[\"motors\", key, \"in2\"]")
                   != std::string::npos,
           "rover page edits each motor function channel separately");
    expect(rover_page && rover_page->body.find("[\"lora\", \"lbt\"]") != std::string::npos,
           "rover page can set numeric lora LBT");
    expect(rover_page && rover_page->body.find("lora-air-key") != std::string::npos,
           "rover page can set lora air_key");
    expect(rover_page && rover_page->body.find("Refresh the page") != std::string::npos,
           "rover page warns to refresh after a type change");
    expect(rover_page && rover_page->body.find("position: fixed") != std::string::npos
                   && rover_page->body.find("#status:empty") != std::string::npos,
           "rover page pins the status line so Save feedback stays visible");
    auto rover_status = rover_cli.Get("/api/status");
    expect(rover_status && rover_status->body.find("\"type\":\"ROVER\"") != std::string::npos,
           "rover status includes ROVER type");
    expect(rover_status && rover_status->body.find("\"motors\"") != std::string::npos,
           "rover status includes motors");
    expect(rover_status && rover_status->body.find("\"servos\"") == std::string::npos,
           "rover status omits servos");
    auto rover_cameras = rover_cli.Get("/api/cameras");
    expect(rover_cameras && rover_cameras->status == 200,
           "GET /api/cameras works on rover");
    expect(camera_process_ptr->starts == 1,
           "rover camera GET reuses the running feeder");
    auto heartbeat = rover_cli.Post("/api/heartbeat", "", "text/plain");
    expect(heartbeat && heartbeat->status == 200,
           "POST /api/heartbeat on rover is 200");
    expect(rover.heartbeat_count == 1,
           "POST /api/heartbeat refreshes rover liveness");
    rover.heartbeat_result = CommandResult::busy;
    auto busy_heartbeat = rover_cli.Post("/api/heartbeat", "", "text/plain");
    expect(busy_heartbeat && busy_heartbeat->status == 409
                   && busy_heartbeat->body.find("\"error\":\"busy\"")
                              != std::string::npos,
           "busy POST /api/heartbeat is 409 busy");
    rover.heartbeat_result = CommandResult::ok;
    auto mlist = rover_cli.Get("/api/motors");
    expect(mlist && mlist->status == 200, "GET /api/motors is 200");
    expect(mlist && mlist->body.find("\"items\"") != std::string::npos,
           "GET /api/motors has items");
    expect(mlist && mlist->body.find("front-left") != std::string::npos,
           "GET /api/motors has motor name");
    auto run = rover_cli.Post("/api/motors/0", "{\"speed\":0.5}", "application/json");
    expect(run && run->status == 200, "POST /api/motors/0 is 200");
    auto motorsAfter = rover_cli.Get("/api/motors");
    expect(motorsAfter && motorsAfter->status == 200, "GET /api/motors after run is 200");
    expect(motorsAfter && motorsAfter->body.find("\"pwm\":200") != std::string::npos,
           "GET /api/motors reports pwm");
    auto drive = rover_cli.Post(
            "/api/drive", R"({"speed":0.5,"turn":-0.25})", "application/json");
    expect(drive && drive->status == 200, "valid rover drive is 200");
    expect(drive && drive->body.find("\"speed\":0.5") != std::string::npos,
           "drive response reports speed");
    auto stopped = rover_cli.Post("/api/stop", "", "text/plain");
    expect(stopped && stopped->status == 200
                   && stopped->body.find("\"speed\":0") != std::string::npos,
           "POST /api/stop reports speed 0");
    auto driven = rover_cli.Post(
            "/api/drive", R"({"speed":0.5,"turn":-0.25})", "application/json");
    expect(driven && driven->status == 200, "drive after stop is 200");
    auto braked = rover_cli.Post("/api/brake", "", "text/plain");
    expect(braked && braked->status == 200
                   && braked->body.find("\"speed\":0") != std::string::npos
                   && braked->body.find("\"turn\":0") != std::string::npos,
           "POST /api/brake reports speed and turn 0");
    auto bad_drive = rover_cli.Post(
            "/api/drive", R"({"speed":2,"turn":0})", "application/json");
    expect(bad_drive && bad_drive->status == 400, "out-of-range rover drive is 400");
    rover.drive_fail = true;
    auto failed_drive = rover_cli.Post(
            "/api/drive", R"({"speed":0.5,"turn":0})", "application/json");
    expect(failed_drive && failed_drive->status == 500
                   && failed_drive->body.find("motor_io") != std::string::npos,
           "motor I2C failure is 500 motor_io");
    auto rover_home = rover_cli.Post("/api/home", "", "text/plain");
    expect(rover_home && rover_home->status == 404, "home on rover is 404");
    auto rover_servos = rover_cli.Get("/api/servos");
    expect(rover_servos && rover_servos->status == 404, "servos on rover is 404");
    rover_server.stop();

    std::ifstream log_in(doggy_log_path(log_dir.string()));
    const std::string log_text{
            std::istreambuf_iterator<char>(log_in),
            std::istreambuf_iterator<char>()};
    const bool servos_logged_with_peer =
            log_text.find("127.0.0.1 GET /api/servos 200") != std::string::npos
            || log_text.find("::1 GET /api/servos 200") != std::string::npos;
    expect(servos_logged_with_peer,
           "API GET /api/servos is logged with remote address");
    expect(log_text.find("GET /api/config 200 {") != std::string::npos,
           "successful GET /api/config is logged with one-line JSON");
    expect(log_text.find("PUT /api/config 202 ") != std::string::npos
                   && log_text.find("\"type\":\"ROVER\"") != std::string::npos,
           "type change is logged with one-line request JSON");
    expect(log_text.find("\"pin\":\"1234\"") == std::string::npos
                   && log_text.find("pin=1234") == std::string::npos,
           "log does not contain the PIN");
    expect(log_text.find("PUT /api/config 400 {\"error\":\"bad_json\",\"message\":")
                   != std::string::npos,
           "failed config save is logged with its reason");
    expect(log_text.find("head_neck channel out of range") != std::string::npos,
           "log names the rejected config field");
    expect(log_text.find("GET /api/status 200") != std::string::npos,
           "successful GET /api/status is logged");
    expect(log_text.find("POST /api/heartbeat 200") != std::string::npos,
           "successful POST /api/heartbeat is logged");
    expect(log_text.find("POST /api/drive 200 {\"speed\":0.5,\"turn\":-0.25}")
                   != std::string::npos,
           "successful POST /api/drive logs request JSON on one line");
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

    // Concurrent handshakes share one Mbed TLS DRBG and key context. Without
    // Mbed TLS locking they corrupt it, and every later handshake is signed
    // invalidly until the process restarts.
    const int handshake_threads = 16;
    const int handshake_rounds = 4;
    std::atomic<int> handshake_failures{0};
    std::atomic<bool> handshake_go{false};
    std::vector<std::thread> handshakers;
    for (int i = 0; i < handshake_threads; ++i) {
        handshakers.emplace_back([&tls, &handshake_failures, &handshake_go]() {
            while (handshake_go.load() == false) {
                std::this_thread::yield();
            }
            for (int round = 0; round < handshake_rounds; ++round) {
                httplib::SSLClient client("127.0.0.1", tls.port());
                client.set_connection_timeout(5, 0);
                client.enable_server_certificate_verification(false);
                client.enable_server_hostname_verification(false);
                auto res = client.Get("/api/status");
                if (res == nullptr || res->status != 200) {
                    handshake_failures += 1;
                }
            }
        });
    }
    handshake_go = true;
    for (std::thread &worker : handshakers) {
        worker.join();
    }
    expect(handshake_failures == 0, "concurrent HTTPS handshakes all succeed");

    // The race above only reproduces on slower hardware, so the build option
    // that makes the shared contexts safe is asserted directly.
#if defined(MBEDTLS_THREADING_C)
    expect(true, "Mbed TLS is built with threading support");
#else
    expect(false, "Mbed TLS is built with threading support");
#endif

    httplib::SSLClient after_burst("127.0.0.1", tls.port());
    after_burst.set_connection_timeout(5, 0);
    after_burst.enable_server_certificate_verification(false);
    after_burst.enable_server_hostname_verification(false);
    auto after = after_burst.Get("/api/status");
    expect(after && after->status == 200,
           "HTTPS still works after a burst of concurrent handshakes");

    tls.stop();
    std::filesystem::remove_all(tls_dir);

    std::filesystem::remove_all(log_dir);
    std::filesystem::remove_all(media_root);

    if (failures != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
