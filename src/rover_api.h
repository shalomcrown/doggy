#ifndef ROVER_API_H
#define ROVER_API_H

#include "robot_api.h"

#include <vector>

// ================================================================================

class RoverApi : public RobotApi {
public:
    using RobotApi::RobotApi;
    virtual ~RoverApi() = default;
    virtual std::vector<MotorSnapshot> listMotors() = 0;
    virtual CommandResult setDrive(double speed, double turn) = 0;
};

#endif
