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
    virtual CommandResult heartbeat() = 0;
    // Request the rover to stop (clear drive state and set motors to coast/stop semantics)
    virtual CommandResult stop() = 0;
    // Request the rover to apply an active brake (drive outputs forced into brake state)
    virtual CommandResult brake() = 0;
    // Run a single motor by id with a signed speed [-1.0,1.0]
    virtual CommandResult runMotor(int id, double speed) = 0;
    // Coast (freewheel) a single motor by id
    virtual CommandResult coastMotor(int id) = 0;
    // Apply brake for a single motor by id
    virtual CommandResult brakeMotor(int id) = 0;
};

#endif
