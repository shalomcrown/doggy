#include "rover.h"

#include <cmath>
#include <system_error>
#include <utility>

// ================================================================================

Rover::Rover() : Rover(Config{}, {}) {
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
    imu(config.i2c.imu.bus, config.i2c.imu.address),
    ads(config.i2c.ads.bus, config.i2c.ads.address) {
    status_.type = RobotType::rover;
    if (imu.isOpen() == false) {
        status_.errors.push_back(
                DogError{DogErrorCode::i2c, "Could not open rover IMU"});
    }
    if (ads.isOpen() == false) {
        status_.errors.push_back(
                DogError{DogErrorCode::i2c, "Could not open rover ADC"});
    }
}

// ================================================================================

RobotType Rover::robotType() const {
    return RobotType::rover;
}

// ================================================================================

std::vector<MotorSnapshot> Rover::snapshotUnlocked() const {
    return {
        {config_.motors.front_left.channel, "front-left", 0,
         config_.motors.front_left.enabled, config_.motors.front_left.direction},
        {config_.motors.front_right.channel, "front-right", 0,
         config_.motors.front_right.enabled, config_.motors.front_right.direction},
        {config_.motors.rear_left.channel, "rear-left", 0,
         config_.motors.rear_left.enabled, config_.motors.rear_left.direction},
        {config_.motors.rear_right.channel, "rear-right", 0,
         config_.motors.rear_right.enabled, config_.motors.rear_right.direction}
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
    status_.speed = speed;
    status_.turn = turn;
    return CommandResult::ok;
}

// ================================================================================

DogStatus Rover::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DogStatus copy = status_;
    copy.type = RobotType::rover;
    copy.motors = snapshotUnlocked();
    return copy;
}

// ================================================================================

CommandResult Rover::replaceConfig(const Config &config, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    const bool type_changed = config.robot.type != RobotType::rover;
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
    ImuReading reading;
    reading.ok = imu.isOpen();
    if (reading.ok == false) {
        status_.imu = reading;
        return;
    }

    try {
        reading.accel = imu.readAccelerometer();
        reading.gyro = imu.readGyro();
        reading.temperature_c = imu.readTemperature();
    } catch (const std::system_error &) {
        reading.ok = false;
    }
    status_.imu = reading;
}

// ================================================================================

void Rover::pollBatteryUnlocked() {
    BatteryReading reading;
    reading.ok = ads.isOpen();
    if (reading.ok == false) {
        status_.battery = reading;
        return;
    }

    try {
        reading.voltage_v = ads.readBatteryVoltage();
    } catch (const std::system_error &) {
        reading.ok = false;
    }
    status_.battery = reading;
}

// ================================================================================

void Rover::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    pollImuUnlocked();
    pollBatteryUnlocked();
}
