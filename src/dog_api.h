#ifndef DOG_API_H
#define DOG_API_H

#include "robot_api.h"

#include <vector>

// ================================================================================

class DogApi : public RobotApi {
public:
    using RobotApi::RobotApi;
    virtual ~DogApi() = default;
    virtual std::vector<ServoSnapshot> listServos() = 0;
    virtual CommandResult home() = 0;
    virtual CommandResult setServoAngle(int id, double angle) = 0;
    virtual CommandResult disableServo(int id) = 0;
};

#endif


