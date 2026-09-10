#ifndef ROBOT_API_H
#define ROBOT_API_H

#include "config.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

class SystemControl;

inline constexpr int kSystemActionDelayMs = 1000;
inline constexpr int kPinMaxFailures = 5;
inline constexpr int kPinLockoutSeconds = 30;

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
    bad_drive,
    failed,
    pin_unset,
    pin_invalid,
    bad_pin,
    rate_limited
};

// ================================================================================

class RobotApi {
public:
    RobotApi();
    RobotApi(const Config &config, std::string config_path,
             std::unique_ptr<SystemControl> system_control);
    virtual ~RobotApi();

    virtual RobotType robotType() const = 0;
    virtual DogStatus getStatus() const = 0;
    virtual Config getConfig() const;
    virtual CommandResult replaceConfig(const Config &config,
                                        const std::string &pin = {}) = 0;
    virtual CommandResult requestSystemAction(SystemAction action, const std::string &pin);
    virtual CommandResult setSystemPin(const std::string &pin,
                                       const std::string &current_pin);

protected:
    Config config_;
    std::string config_path_;
    std::unique_ptr<SystemControl> system_control_;
    mutable std::mutex mutex_;

    CommandResult authorizePinUnlocked(const std::string &pin);
    CommandResult saveConfigUnlocked(const Config &config);
    void scheduleSystemActionUnlocked(SystemAction action);

private:
    bool system_action_pending_ = false;
    int pin_failures_ = 0;
    std::chrono::steady_clock::time_point pin_lockout_until_{};
};

#endif
