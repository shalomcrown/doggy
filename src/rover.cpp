#include "rover.h"

#include <cmath>
#include <system_error>
#include <utility>

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

static MotorSnapshot motor_snap(const doggy::v1::Motor &motor, const char *name) {
    MotorSnapshot item;
    item.set_id(motor.channel());
    item.set_name(name);
    item.set_pwm(0);
    item.set_enabled(motor.enabled());
    item.set_direction(motor.direction());
    return item;
}

// ================================================================================

Rover::Rover() : Rover(default_config(), {}) {
}

// ================================================================================

Rover::Rover(const Config &config) : Rover(config, {}) {
}

// ================================================================================

Rover::Rover(const Config &config, std::string config_path) :
    Rover(config, std::move(config_path), std::make_unique<NullSystemControl>()) {
}

// ================================================================================

Rover::Rover(const Config &config, std::string config_path,
             std::unique_ptr<SystemControl> system_control) :
    RoverApi(config, std::move(config_path), std::move(system_control)),
    imu(config.i2c().imu().bus(), i2c_address_byte(config.i2c().imu())),
    ads(config.i2c().ads().bus(), i2c_address_byte(config.i2c().ads())) {
    status_.set_type(doggy::v1::ROVER);
    if (imu.isOpen() == false) {
        add_i2c_error(status_, "Could not open rover IMU");
    }
    if (ads.isOpen() == false) {
        add_i2c_error(status_, "Could not open rover ADC");
    }
}

// ================================================================================

RobotType Rover::robotType() const {
    return doggy::v1::ROVER;
}

// ================================================================================

std::vector<MotorSnapshot> Rover::snapshotUnlocked() const {
    return {
        motor_snap(config_.motors().front_left(), "front-left"),
        motor_snap(config_.motors().front_right(), "front-right"),
        motor_snap(config_.motors().rear_left(), "rear-left"),
        motor_snap(config_.motors().rear_right(), "rear-right")
    };
}

// ================================================================================

std::vector<MotorSnapshot> Rover::listMotors() {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotUnlocked();
}

// ================================================================================

CommandResult Rover::setDrive(double speed, double turn) {
    if (std::isfinite(speed) == false || std::isfinite(turn) == false
            || speed < -1.0 || speed > 1.0 || turn < -1.0 || turn > 1.0) {
        return CommandResult::bad_drive;
    }

    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    // TODO: Convert normalized speed/turn to motor PWM and direction once the
    // motor driver and mixing rules are selected.
    status_.set_speed(speed);
    status_.set_turn(turn);
    return CommandResult::ok;
}

// ================================================================================

DogStatus Rover::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DogStatus copy = status_;
    copy.set_type(doggy::v1::ROVER);
    copy.mutable_motors()->clear_items();
    for (const MotorSnapshot &item : snapshotUnlocked()) {
        *copy.mutable_motors()->add_items() = item;
    }
    copy.clear_servos();
    return copy;
}

// ================================================================================

CommandResult Rover::replaceConfig(const Config &config, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    const bool type_changed = config.robot().type() != doggy::v1::ROVER;
    if (type_changed) {
        const CommandResult authorized = authorizePinUnlocked(pin);
        if (authorized != CommandResult::ok) {
            return authorized;
        }
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

void Rover::pollImuUnlocked() {
    doggy::v1::Imu *reading = status_.mutable_imu();
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

void Rover::pollBatteryUnlocked() {
    doggy::v1::Battery *reading = status_.mutable_battery();
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

void Rover::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    pollImuUnlocked();
    pollBatteryUnlocked();
}
