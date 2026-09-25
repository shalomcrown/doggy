#ifndef ROVER_H
#define ROVER_H

#include "ads7830.h"
#include "imu.h"
#include "rover_api.h"
#include "servo_board.h"
#include "system_control.h"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// ================================================================================

class Rover : public RoverApi {
private:
    ServoBoard motor_board_;
    std::array<int, 4> motor_pwm_{};
    DogStatus status_;
    std::optional<std::chrono::steady_clock::time_point> last_gcs_;
    std::jthread watchdog_thread_;

    std::vector<MotorSnapshot> snapshotUnlocked() const;
    bool outputsActiveUnlocked() const;
    void refreshGcsUnlocked();
    void expireGcsUnlocked(std::chrono::steady_clock::time_point now);
    void applyMotorOutputsUnlocked(double speed, double turn);
    void coastMotorOutputsUnlocked();
    void brakeMotorOutputsUnlocked();
    void pollImu(doggy::v1::Imu &reading);
    void pollBattery(doggy::v1::Battery &reading);

public:
    Imu imu;
    Ads7830 ads;

    Rover();
    explicit Rover(const Config &config);
    Rover(const Config &config, std::string config_path);
    Rover(const Config &config, std::string config_path,
          std::unique_ptr<SystemControl> system_control);

    ~Rover() override;

    RobotType robotType() const override;
    std::vector<MotorSnapshot> listMotors() override;
    CommandResult setDrive(double speed, double turn) override;
    CommandResult heartbeat() override;
    CommandResult stop();
    CommandResult brake();
    CommandResult runMotor(int id, double speed) override;
    CommandResult coastMotor(int id) override;
    CommandResult brakeMotor(int id) override;
    DogStatus getStatus() const override;
    CommandResult replaceConfig(const Config &config,
                                const std::string &pin = {}) override;
    void poll();
};

#endif
