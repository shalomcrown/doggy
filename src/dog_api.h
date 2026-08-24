#ifndef DOG_API_H
#define DOG_API_H

#include "dog_status.h"

#include <string>
#include <vector>

// ================================================================================

struct ServoSnapshot {
    int id;
    std::string name;
    double angle;
    int pwm;
};

// ================================================================================

enum class CommandResult {
    ok,
    not_found,
    busy,
    bad_angle
};

// ================================================================================

class DogApi {
public:
    virtual ~DogApi() = default;
    virtual std::vector<ServoSnapshot> listServos() = 0;
    virtual CommandResult home() = 0;
    virtual CommandResult setServoAngle(int id, double angle) = 0;
    virtual DogStatus getStatus() const = 0;
};

#endif


