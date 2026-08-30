#include "doggy.h"
#include "i2c_interface.hpp"
#include "utils.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <system_error>
#include <thread>

static std::string i2c_open_failed(const char *name, int bus, uint8_t address) {
    const unsigned char byte = static_cast<unsigned char>(address);
    std::ostringstream os;
    os << "Could not open " << name << " on " << i2c_device_path(bus)
       << " address 0x" << to_hex(&byte, 1);
    return os.str();
}

// ================================================================================

Leg::Leg(Servo &waist, Servo &hip, Servo &knee) :
    waist(waist), hip(hip), knee(knee) {
}

// ================================================================================

Head::Head(Servo &neck) : neck(neck) {
}

// ================================================================================

void Head::lookForward() {
    neck.setAngle(90);
}

// ================================================================================

void Leg::setKnee(double angle) {
    knee.setAngle(angle);
}

// ================================================================================

void Leg::setHip(double angle) {
    hip.setAngle(angle);
}

// ================================================================================

void Leg::setWaist(double angle) {
    waist.setAngle(angle);
}

// ================================================================================

void Leg::allToNinety() {
    waist.setAngle(90);
    hip.setAngle(90);
    knee.setAngle(90);
}

// ================================================================================

Dog::Dog() : Dog(Config{}, {}) {
}

// ================================================================================

Dog::Dog(const Config &config) : Dog(config, {}) {
}

// ================================================================================

Dog::Dog(const Config &config, std::string config_path) :
    Dog(config, std::move(config_path), std::make_unique<NullSystemControl>()) {
}

// ================================================================================

Dog::Dog(const Config &config, std::string config_path,
         std::unique_ptr<SystemControl> system_control) :
    board(config.i2c.servo_board.bus, config.i2c.servo_board.address),
    imu(config.i2c.imu.bus, config.i2c.imu.address),
    ads(config.i2c.ads.bus, config.i2c.ads.address),
    frontRightWaist(board, config.servos.front_right_waist, "front-right-waist"),
    frontRightHip(board, config.servos.front_right_hip, "front-right-hip"),
    frontRightKnee(board, config.servos.front_right_knee, "front-right-knee"),
    frontLeftWaist(board, config.servos.front_left_waist, "front-left-waist"),
    frontLeftHip(board, config.servos.front_left_hip, "front-left-hip"),
    frontLeftKnee(board, config.servos.front_left_knee, "front-left-knee"),
    rearLeftWaist(board, config.servos.rear_left_waist, "rear-left-waist"),
    rearLeftHip(board, config.servos.rear_left_hip, "rear-left-hip"),
    rearLeftKnee(board, config.servos.rear_left_knee, "rear-left-knee"),
    rearRightWaist(board, config.servos.rear_right_waist, "rear-right-waist"),
    rearRightHip(board, config.servos.rear_right_hip, "rear-right-hip"),
    rearRightKnee(board, config.servos.rear_right_knee, "rear-right-knee"),
    headNeck(board, config.servos.head_neck, "head-neck"),
    frontRight(frontRightWaist, frontRightHip, frontRightKnee),
    frontLeft(frontLeftWaist, frontLeftHip, frontLeftKnee),
    rearLeft(rearLeftWaist, rearLeftHip, rearLeftKnee),
    rearRight(rearRightWaist, rearRightHip, rearRightKnee),
    head(headNeck),
    config_(config),
    config_path_(std::move(config_path)),
    system_control_(system_control ? std::move(system_control)
                                  : std::make_unique<NullSystemControl>()),
    servos{
        &frontRightWaist, &frontRightHip, &frontRightKnee,
        &frontLeftWaist, &frontLeftHip, &frontLeftKnee,
        &rearLeftWaist, &rearLeftHip, &rearLeftKnee,
        &rearRightWaist, &rearRightHip, &rearRightKnee,
        &headNeck
    } {
    if (board.isOpen() == false) {
        status.errors.push_back(DogError{
            DogErrorCode::i2c,
            board.lastError()
        });
    }

    if (imu.isOpen() == false) {
        status.errors.push_back(DogError{
            DogErrorCode::i2c,
            i2c_open_failed("IMU", config.i2c.imu.bus, config.i2c.imu.address)
        });
    }

    if (ads.isOpen() == false) {
        status.errors.push_back(DogError{
            DogErrorCode::i2c,
            i2c_open_failed("ADC", config.i2c.ads.bus, config.i2c.ads.address)
        });
    }
}

// ================================================================================

void Dog::allToNinety() {
    frontRight.allToNinety();
    frontLeft.allToNinety();
    rearLeft.allToNinety();
    rearRight.allToNinety();
    head.lookForward();
}

// ================================================================================

Servo *Dog::findServo(int id) {
    for (Servo *servo : servos) {
        if (servo->id() == id) {
            return servo;
        }
    }

    return nullptr;
}

// ================================================================================

std::vector<ServoSnapshot> Dog::snapshotUnlocked() const {
    std::vector<ServoSnapshot> items;
    items.reserve(servos.size());
    for (const Servo *servo : servos) {
        items.push_back(ServoSnapshot{
            servo->id(),
            servo->name(),
            servo->angle(),
            servo->pwm()
        });
    }

    return items;
}

// ================================================================================

std::vector<ServoSnapshot> Dog::listServos() {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshotUnlocked();
}

// ================================================================================

CommandResult Dog::home() {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        allToNinety();
        frontRight.setKnee(135);
        frontLeft.setKnee(135);
        rearLeft.setKnee(135);
        rearRight.setKnee(135);
    } catch (const std::system_error &ex) {
        status.errors.push_back(DogError{DogErrorCode::i2c, ex.what()});
    }

    return CommandResult::ok;
}

// ================================================================================

bool Dog::homing() {
    return home() == CommandResult::ok;
}

// ================================================================================

CommandResult Dog::setServoAngle(int id, double angle) {
    if (angle < 0.0 || angle > board.servoMaxAngle()) {
        return CommandResult::bad_angle;
    }

    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    Servo *servo = findServo(id);
    if (servo == nullptr) {
        return CommandResult::not_found;
    }

    try {
        servo->setAngle(angle);
    } catch (const std::system_error &ex) {
        status.errors.push_back(DogError{DogErrorCode::i2c, ex.what()});
    }

    return CommandResult::ok;
}

// ================================================================================

CommandResult Dog::disableServo(int id) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    Servo *servo = findServo(id);
    if (servo == nullptr) {
        return CommandResult::not_found;
    }

    try {
        servo->off();
    } catch (const std::system_error &ex) {
        status.errors.push_back(DogError{DogErrorCode::i2c, ex.what()});
    }

    return CommandResult::ok;
}

// ================================================================================

DogStatus Dog::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex);
    DogStatus copy = status;
    copy.servos = snapshotUnlocked();
    return copy;
}

// ================================================================================

Config Dog::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex);
    return config_;
}

// ================================================================================

CommandResult Dog::replaceConfig(const Config &config) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        frontRightWaist.rebindChannel(config.servos.front_right_waist);
        frontRightHip.rebindChannel(config.servos.front_right_hip);
        frontRightKnee.rebindChannel(config.servos.front_right_knee);
        frontLeftWaist.rebindChannel(config.servos.front_left_waist);
        frontLeftHip.rebindChannel(config.servos.front_left_hip);
        frontLeftKnee.rebindChannel(config.servos.front_left_knee);
        rearLeftWaist.rebindChannel(config.servos.rear_left_waist);
        rearLeftHip.rebindChannel(config.servos.rear_left_hip);
        rearLeftKnee.rebindChannel(config.servos.rear_left_knee);
        rearRightWaist.rebindChannel(config.servos.rear_right_waist);
        rearRightHip.rebindChannel(config.servos.rear_right_hip);
        rearRightKnee.rebindChannel(config.servos.rear_right_knee);
        headNeck.rebindChannel(config.servos.head_neck);
        const SystemConfig kept_pin = config_.system;
        config_ = config;
        config_.system = kept_pin;
        if (config_path_.empty() == false) {
            Config::save_file(config_, config_path_);
        }
    } catch (const std::system_error &ex) {
        status.errors.push_back(DogError{DogErrorCode::i2c, ex.what()});
        return CommandResult::failed;
    } catch (const ConfigError &) {
        return CommandResult::failed;
    }

    return CommandResult::ok;
}

// ================================================================================

CommandResult Dog::setSystemPin(const std::string &pin, const std::string &current_pin) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    if (Config::pin_length_ok(pin) == false) {
        return CommandResult::bad_pin;
    }

    if (config_.system.pin_is_set()) {
        if (Config::pin_matches(current_pin, config_.system.pin_hash) == false) {
            return CommandResult::pin_invalid;
        }
    }

    try {
        config_.system.pin_hash = Config::hash_pin(pin);
        if (config_path_.empty() == false) {
            Config::save_file(config_, config_path_);
        }
    } catch (const ConfigError &) {
        return CommandResult::failed;
    }

    pin_failures_ = 0;
    pin_lockout_until_ = {};
    return CommandResult::ok;
}

// ================================================================================

CommandResult Dog::requestSystemAction(SystemAction action, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    if (system_action_pending_) {
        return CommandResult::busy;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < pin_lockout_until_) {
        return CommandResult::rate_limited;
    }

    if (config_.system.pin_is_set() == false) {
        return CommandResult::pin_unset;
    }

    if (Config::pin_matches(pin, config_.system.pin_hash) == false) {
        pin_failures_ += 1;
        if (pin_failures_ >= kPinMaxFailures) {
            pin_lockout_until_ = now + std::chrono::seconds(kPinLockoutSeconds);
            pin_failures_ = 0;
            return CommandResult::rate_limited;
        }

        return CommandResult::pin_invalid;
    }

    pin_failures_ = 0;
    system_action_pending_ = true;
    SystemControl *control = system_control_.get();
    std::thread([this, action, control]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(kSystemActionDelayMs));
        control->perform(action);
        std::lock_guard<std::mutex> done(mutex);
        system_action_pending_ = false;
    }).detach();
    return CommandResult::ok;
}

// ================================================================================

void Dog::pollImuUnlocked() {
    ImuReading reading;
    reading.ok = imu.isOpen();
    if (reading.ok == false) {
        status.imu = reading;
        return;
    }

    try {
        reading.accel = imu.readAccelerometer();
        reading.gyro = imu.readGyro();
        reading.temperature_c = imu.readTemperature();
    } catch (const std::system_error &) {
        reading.ok = false;
    }

    status.imu = reading;
}

// ================================================================================

void Dog::pollBatteryUnlocked() {
    BatteryReading reading;
    reading.ok = ads.isOpen();
    if (reading.ok == false) {
        status.battery = reading;
        return;
    }

    try {
        reading.voltage_v = ads.readBatteryVoltage();
    } catch (const std::system_error &) {
        reading.ok = false;
    }

    status.battery = reading;
}

// ================================================================================

void Dog::poll() {
    std::lock_guard<std::mutex> lock(mutex);
    pollImuUnlocked();
    pollBatteryUnlocked();
}
