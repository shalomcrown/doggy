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

static void add_i2c_error(DogStatus &status, const std::string &message) {
    doggy::v1::Error *err = status.add_errors();
    err->set_code(doggy::v1::i2c);
    err->set_message(message);
}

// ================================================================================

static void copy_vec3(doggy::v1::Vec3 *out, const Vec3 &in) {
    out->set_x(in.x);
    out->set_y(in.y);
    out->set_z(in.z);
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

Dog::Dog() : Dog(default_config(), {}) {
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
    board(config.i2c().servo_board().bus(), i2c_address_byte(config.i2c().servo_board())),
    imu(config.i2c().imu().bus(), i2c_address_byte(config.i2c().imu())),
    ads(config.i2c().ads().bus(), i2c_address_byte(config.i2c().ads())),
    frontRightWaist(board, config.servos().front_right_waist(), "front-right-waist"),
    frontRightHip(board, config.servos().front_right_hip(), "front-right-hip"),
    frontRightKnee(board, config.servos().front_right_knee(), "front-right-knee"),
    frontLeftWaist(board, config.servos().front_left_waist(), "front-left-waist"),
    frontLeftHip(board, config.servos().front_left_hip(), "front-left-hip"),
    frontLeftKnee(board, config.servos().front_left_knee(), "front-left-knee"),
    rearLeftWaist(board, config.servos().rear_left_waist(), "rear-left-waist"),
    rearLeftHip(board, config.servos().rear_left_hip(), "rear-left-hip"),
    rearLeftKnee(board, config.servos().rear_left_knee(), "rear-left-knee"),
    rearRightWaist(board, config.servos().rear_right_waist(), "rear-right-waist"),
    rearRightHip(board, config.servos().rear_right_hip(), "rear-right-hip"),
    rearRightKnee(board, config.servos().rear_right_knee(), "rear-right-knee"),
    headNeck(board, config.servos().head_neck(), "head-neck"),
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
        add_i2c_error(status, board.lastError());
    }

    if (imu.isOpen() == false) {
        add_i2c_error(status, i2c_open_failed("IMU", config.i2c().imu().bus(),
                i2c_address_byte(config.i2c().imu())));
    }

    if (ads.isOpen() == false) {
        add_i2c_error(status, i2c_open_failed("ADC", config.i2c().ads().bus(),
                i2c_address_byte(config.i2c().ads())));
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
        ServoSnapshot item;
        item.set_id(servo->id());
        item.set_name(servo->name());
        item.set_pwm(servo->pwm());
        if (servo->pwm() != 0) {
            item.set_angle(servo->angle());
        }
        items.push_back(std::move(item));
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
        add_i2c_error(status, ex.what());
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
        add_i2c_error(status, ex.what());
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
        add_i2c_error(status, ex.what());
    }

    return CommandResult::ok;
}

// ================================================================================

RobotType Dog::robotType() const {
    return doggy::v1::DOG;
}

// ================================================================================

DogStatus Dog::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DogStatus copy = status;
    copy.set_type(doggy::v1::DOG);
    copy.mutable_servos()->clear_items();
    for (const ServoSnapshot &item : snapshotUnlocked()) {
        *copy.mutable_servos()->add_items() = item;
    }
    copy.clear_motors();
    copy.clear_speed();
    copy.clear_turn();
    return copy;
}

// ================================================================================

CommandResult Dog::replaceConfig(const Config &config, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    const bool type_changed = config.robot().type() != doggy::v1::DOG;
    if (type_changed) {
        const CommandResult authorized = authorizePinUnlocked(pin);
        if (authorized != CommandResult::ok) {
            return authorized;
        }
    }

    try {
        frontRightWaist.rebindChannel(config.servos().front_right_waist());
        frontRightHip.rebindChannel(config.servos().front_right_hip());
        frontRightKnee.rebindChannel(config.servos().front_right_knee());
        frontLeftWaist.rebindChannel(config.servos().front_left_waist());
        frontLeftHip.rebindChannel(config.servos().front_left_hip());
        frontLeftKnee.rebindChannel(config.servos().front_left_knee());
        rearLeftWaist.rebindChannel(config.servos().rear_left_waist());
        rearLeftHip.rebindChannel(config.servos().rear_left_hip());
        rearLeftKnee.rebindChannel(config.servos().rear_left_knee());
        rearRightWaist.rebindChannel(config.servos().rear_right_waist());
        rearRightHip.rebindChannel(config.servos().rear_right_hip());
        rearRightKnee.rebindChannel(config.servos().rear_right_knee());
        headNeck.rebindChannel(config.servos().head_neck());
    } catch (const std::system_error &ex) {
        add_i2c_error(status, ex.what());
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
    doggy::v1::Imu *reading = status.mutable_imu();
    reading->set_ok(imu.isOpen());
    if (reading->ok() == false) {
        return;
    }

    try {
        copy_vec3(reading->mutable_accel(), imu.readAccelerometer());
        copy_vec3(reading->mutable_gyro(), imu.readGyro());
        reading->set_temperature_c(imu.readTemperature());
    } catch (const std::system_error &) {
        reading->set_ok(false);
    }
}

// ================================================================================

void Dog::pollBatteryUnlocked() {
    doggy::v1::Battery *reading = status.mutable_battery();
    reading->set_ok(ads.isOpen());
    if (reading->ok() == false) {
        return;
    }

    try {
        reading->set_voltage_v(ads.readBatteryVoltage());
    } catch (const std::system_error &) {
        reading->set_ok(false);
    }
}

// ================================================================================

void Dog::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    pollImuUnlocked();
    pollBatteryUnlocked();
}
