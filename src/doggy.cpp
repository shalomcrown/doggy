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
    DogApi(config, std::move(config_path), std::move(system_control)),
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
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotUnlocked();
}

// ================================================================================

CommandResult Dog::home() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
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

    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
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
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
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

RobotType Dog::robotType() const {
    return RobotType::dog;
}

// ================================================================================

DogStatus Dog::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DogStatus copy = status;
    copy.type = RobotType::dog;
    copy.servos = snapshotUnlocked();
    return copy;
}

// ================================================================================

CommandResult Dog::replaceConfig(const Config &config, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    const bool type_changed = config.robot.type != RobotType::dog;
    if (type_changed) {
        const CommandResult authorized = authorizePinUnlocked(pin);
        if (authorized != CommandResult::ok) {
            return authorized;
        }
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
    } catch (const std::system_error &ex) {
        status.errors.push_back(DogError{DogErrorCode::i2c, ex.what()});
        return CommandResult::failed;
    }

    const CommandResult saved = saveConfigUnlocked(config);
    if (saved != CommandResult::ok) {
        return saved;
    }
    if (type_changed) {
        scheduleSystemActionUnlocked(SystemAction::restart);
    }

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
    std::lock_guard<std::mutex> lock(mutex_);
    pollImuUnlocked();
    pollBatteryUnlocked();
}
