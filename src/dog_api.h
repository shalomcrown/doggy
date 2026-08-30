#ifndef DOG_API_H
#define DOG_API_H

#include "config.h"
#include "dog_status.h"

#include <string>
#include <vector>

// ================================================================================

enum class SystemAction {
    restart,
    reboot,
    shutdown
};

// ================================================================================

enum class CommandResult {
    ok,
    not_found,
    busy,
    bad_angle,
    failed,
    pin_unset,
    pin_invalid,
    bad_pin,
    rate_limited
};

// ================================================================================

class DogApi {
public:
    virtual ~DogApi() = default;
    virtual std::vector<ServoSnapshot> listServos() = 0;
    virtual CommandResult home() = 0;
    virtual CommandResult setServoAngle(int id, double angle) = 0;
    virtual CommandResult disableServo(int id) = 0;
    virtual DogStatus getStatus() const = 0;
    virtual Config getConfig() const = 0;
    virtual CommandResult replaceConfig(const Config &config) = 0;
    virtual CommandResult requestSystemAction(SystemAction action, const std::string &pin) = 0;
    virtual CommandResult setSystemPin(const std::string &pin, const std::string &current_pin) = 0;
};

#endif


