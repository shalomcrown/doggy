#ifndef ROVER_H
#define ROVER_H

#include "ads7830.h"
#include "imu.h"
#include "rover_api.h"
#include "system_control.h"

#include <memory>
#include <string>
#include <vector>

// ================================================================================

class Rover : public RoverApi {
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
    DogStatus getStatus() const override;
    CommandResult replaceConfig(const Config &config,
                                const std::string &pin = {}) override;
    void poll();

private:
    DogStatus status_;

    std::vector<MotorSnapshot> snapshotUnlocked() const;
    void pollImuUnlocked();
    void pollBatteryUnlocked();
};

#endif
