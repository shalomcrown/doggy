#ifndef ROVER_H
#define ROVER_H

#include "ads7830.h"
#include "imu.h"
#include "rover_api.h"
#include "servo_board.h"
#include "system_control.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// ================================================================================

class Rover : public RoverApi {
private:
    ServoBoard motor_board_;
    std::array<int, 4> motor_pwm_{};
    DogStatus status_;

    std::vector<MotorSnapshot> snapshotUnlocked() const;
    void applyMotorOutputsUnlocked(double speed);
    void coastMotorOutputsUnlocked();
    void brakeMotorOutputsUnlocked();
    void pollImuUnlocked();
    void pollBatteryUnlocked();

public:
    Imu imu;
    Ads7830 ads;

    Rover();
    explicit Rover(const Config &config);
    Rover(const Config &config, std::string config_path);
    Rover(const Config &config, std::string config_path,
          std::unique_ptr<SystemControl> system_control);

    RobotType robotType() const override;
    std::vector<MotorSnapshot> listMotors() override;
    CommandResult setDrive(double speed, double turn) override;
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
