#include "rover.h"
#include "motor_math.h"

#include <cmath>
#include <system_error>
#include <utility>

static constexpr double kMotorPwmFrequencyHz = 1526.0;

// ================================================================================

static Config default_rover_config() {
    return config_from_json(R"({"robot":{"type":"ROVER"}})");
}

// ================================================================================

static void add_i2c_error(DogStatus &status, const std::string &message) {
    for (const doggy::v1::Error &existing : status.errors()) {
        if (existing.code() == doggy::v1::i2c
                && existing.message() == message) {
            return;
        }
    }
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

static MotorSnapshot motor_snap(
        const doggy::v1::Motor &motor,
        int id,
        const char *name,
        int pwm) {
    MotorSnapshot item;
    item.set_id(id);
    item.set_name(name);
    item.set_pwm(pwm);
    item.set_enabled(motor.enabled());
    item.set_direction(motor.direction());
    return item;
}

// ================================================================================

static void write_motor_plan(
        ServoBoard &board,
        const MotorWritePlan &plan) {
    for (std::size_t i = 0; i < plan.size; ++i) {
        const MotorPwmWrite &write = plan.writes[i];
        board.set_pwm(write.channel, write.on, write.off);
    }
}

// ================================================================================

static int apply_motor_output(
        ServoBoard &board,
        const doggy::v1::Motor &motor,
        double speed) {
    const MotorSignal signal = motor_signal(
            speed,
            motor.enabled(),
            motor.direction() == doggy::v1::reverse);
    write_motor_plan(
            board,
            motor_write_plan(motor.pwm(), motor.in2(), motor.in1(), signal));
    return signal.pwm;
}

// ================================================================================

static void coast_motor_output(
        ServoBoard &board,
        const doggy::v1::Motor &motor) {
    write_motor_plan(
            board,
            motor_write_plan(motor.pwm(), motor.in2(), motor.in1(), {}));
}

// ================================================================================

static void brake_motor_output(
        ServoBoard &board,
        const doggy::v1::Motor &motor) {
    write_motor_plan(
            board,
            motor_write_plan(motor.pwm(), motor.in2(), motor.in1(), motor_brake_signal()));
}

// ================================================================================

Rover::Rover() : Rover(default_rover_config(), {}) {
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
    motor_board_(config.i2c().servo_board().bus(),
                 i2c_address_byte(config.i2c().servo_board())),
    imu(config.i2c().imu().bus(), i2c_address_byte(config.i2c().imu())),
    ads(config.i2c().ads().bus(), i2c_address_byte(config.i2c().ads())) {
    status_.set_type(doggy::v1::ROVER);
    try {
        motor_board_.set_pwm_freq(kMotorPwmFrequencyHz);
        coastMotorOutputsUnlocked();
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not initialize rover motor outputs");
    }
    if (motor_board_.isOpen() == false) {
        add_i2c_error(status_, "Could not open rover motor board");
    }
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
        motor_snap(config_.motors().front_left(), 0, "front-left", motor_pwm_[0]),
        motor_snap(config_.motors().front_right(), 1, "front-right", motor_pwm_[1]),
        motor_snap(config_.motors().rear_left(), 2, "rear-left", motor_pwm_[2]),
        motor_snap(config_.motors().rear_right(), 3, "rear-right", motor_pwm_[3])
    };
}

// ================================================================================

void Rover::applyMotorOutputsUnlocked(double speed) {
    std::array<int, 4> next{};
    next[0] = apply_motor_output(
            motor_board_, config_.motors().front_left(), speed);
    next[1] = apply_motor_output(
            motor_board_, config_.motors().front_right(), speed);
    next[2] = apply_motor_output(
            motor_board_, config_.motors().rear_left(), speed);
    next[3] = apply_motor_output(
            motor_board_, config_.motors().rear_right(), speed);
    motor_pwm_ = next;
}

// ================================================================================

void Rover::coastMotorOutputsUnlocked() {
    coast_motor_output(motor_board_, config_.motors().front_left());
    coast_motor_output(motor_board_, config_.motors().front_right());
    coast_motor_output(motor_board_, config_.motors().rear_left());
    coast_motor_output(motor_board_, config_.motors().rear_right());
    motor_pwm_.fill(0);
}

// ================================================================================

void Rover::brakeMotorOutputsUnlocked() {
    brake_motor_output(motor_board_, config_.motors().front_left());
    brake_motor_output(motor_board_, config_.motors().front_right());
    brake_motor_output(motor_board_, config_.motors().rear_left());
    brake_motor_output(motor_board_, config_.motors().rear_right());
    motor_pwm_.fill(0);
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

    try {
        applyMotorOutputsUnlocked(speed);
    } catch (const std::system_error &) {
        try {
            motor_board_.set_all_pwm(0, kPca9685FullOff);
        } catch (const std::system_error &) {
            add_i2c_error(status_, "Could not coast rover motor outputs");
        }
        motor_pwm_.fill(0);
        add_i2c_error(status_, "Could not set rover motor outputs");
        return CommandResult::failed;
    }
    status_.set_speed(speed);
    status_.set_turn(turn);
    return CommandResult::ok;
}

// ================================================================================

CommandResult Rover::stop() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        coastMotorOutputsUnlocked();
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not coast rover motor outputs");
        return CommandResult::failed;
    }
    status_.set_speed(0.0);
    status_.set_turn(0.0);
    return CommandResult::ok;
}

// ================================================================================

CommandResult Rover::brake() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        brakeMotorOutputsUnlocked();
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not brake rover motor outputs");
        return CommandResult::failed;
    }
    status_.set_speed(0.0);
    status_.set_turn(0.0);
    return CommandResult::ok;
}

// ==============================================================================

CommandResult Rover::runMotor(int id, double speed) {
    if (std::isfinite(speed) == false || speed < -1.0 || speed > 1.0) {
        return CommandResult::bad_drive;
    }

    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        switch (id) {
            case 0:
                motor_pwm_[0] = apply_motor_output(
                        motor_board_, config_.motors().front_left(), speed);
                break;
            case 1:
                motor_pwm_[1] = apply_motor_output(
                        motor_board_, config_.motors().front_right(), speed);
                break;
            case 2:
                motor_pwm_[2] = apply_motor_output(
                        motor_board_, config_.motors().rear_left(), speed);
                break;
            case 3:
                motor_pwm_[3] = apply_motor_output(
                        motor_board_, config_.motors().rear_right(), speed);
                break;
            default:
                return CommandResult::not_found;
        }
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not set rover motor outputs");
        return CommandResult::failed;
    }

    return CommandResult::ok;
}

// ==============================================================================

CommandResult Rover::coastMotor(int id) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        switch (id) {
            case 0:
                coast_motor_output(motor_board_, config_.motors().front_left());
                motor_pwm_[0] = 0;
                break;
            case 1:
                coast_motor_output(motor_board_, config_.motors().front_right());
                motor_pwm_[1] = 0;
                break;
            case 2:
                coast_motor_output(motor_board_, config_.motors().rear_left());
                motor_pwm_[2] = 0;
                break;
            case 3:
                coast_motor_output(motor_board_, config_.motors().rear_right());
                motor_pwm_[3] = 0;
                break;
            default:
                return CommandResult::not_found;
        }
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not coast rover motor outputs");
        return CommandResult::failed;
    }

    return CommandResult::ok;
}

// ==============================================================================

CommandResult Rover::brakeMotor(int id) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }

    try {
        switch (id) {
            case 0:
                brake_motor_output(motor_board_, config_.motors().front_left());
                motor_pwm_[0] = 0;
                break;
            case 1:
                brake_motor_output(motor_board_, config_.motors().front_right());
                motor_pwm_[1] = 0;
                break;
            case 2:
                brake_motor_output(motor_board_, config_.motors().rear_left());
                motor_pwm_[2] = 0;
                break;
            case 3:
                brake_motor_output(motor_board_, config_.motors().rear_right());
                motor_pwm_[3] = 0;
                break;
            default:
                return CommandResult::not_found;
        }
    } catch (const std::system_error &) {
        add_i2c_error(status_, "Could not brake rover motor outputs");
        return CommandResult::failed;
    }

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

    try {
        coastMotorOutputsUnlocked();
    } catch (const std::system_error &) {
        motor_pwm_.fill(0);
        add_i2c_error(status_, "Could not coast rover motors before configuration");
        return CommandResult::failed;
    }

    const CommandResult saved = saveConfigUnlocked(config);
    if (saved != CommandResult::ok) {
        try {
            applyMotorOutputsUnlocked(status_.speed());
        } catch (const std::system_error &) {
            motor_pwm_.fill(0);
            add_i2c_error(status_, "Could not restore rover motors after configuration failure");
        }
        return saved;
    }
    if (type_changed) {
        scheduleSystemActionUnlocked(SystemAction::restart);
        return CommandResult::ok;
    }

    try {
        applyMotorOutputsUnlocked(status_.speed());
    } catch (const std::system_error &) {
        motor_pwm_.fill(0);
        add_i2c_error(status_, "Could not apply rover motor configuration");
        return CommandResult::failed;
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
