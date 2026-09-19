#include "robot_api.h"

#include "system_control.h"

#include <plog/Log.h>

#include <thread>
#include <utility>

// ================================================================================

RobotApi::RobotApi() : RobotApi(default_config(), {}, std::make_unique<NullSystemControl>()) {
}

// ================================================================================

RobotApi::RobotApi(const Config &config, std::string config_path,
                   std::unique_ptr<SystemControl> system_control) :
    config_(config),
    config_path_(std::move(config_path)),
    system_control_(system_control ? std::move(system_control)
                                   : std::make_unique<NullSystemControl>()) {
}

// ================================================================================

RobotApi::~RobotApi() = default;

// ================================================================================

Config RobotApi::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

// ================================================================================

CommandResult RobotApi::authorizePinUnlocked(const std::string &pin) {
    const auto now = std::chrono::steady_clock::now();
    if (now < pin_lockout_until_) {
        return CommandResult::rate_limited;
    }

    if (pin_is_set(config_) == false) {
        return CommandResult::pin_unset;
    }

    if (pin_matches(pin, config_.system().pin_hash()) == false) {
        pin_failures_ += 1;
        if (pin_failures_ >= kPinMaxFailures) {
            pin_lockout_until_ = now + std::chrono::seconds(kPinLockoutSeconds);
            pin_failures_ = 0;
            return CommandResult::rate_limited;
        }

        return CommandResult::pin_invalid;
    }

    pin_failures_ = 0;
    return CommandResult::ok;
}

// ================================================================================

CommandResult RobotApi::saveConfigUnlocked(const Config &config) {
    const doggy::v1::System kept_pin = config_.system();
    Config next = config;
    *next.mutable_system() = kept_pin;
    try {
        if (config_path_.empty() == false) {
            config_save_file(next, config_path_);
        }
    } catch (const ConfigError &ex) {
        // The reason names the config file, so it is logged rather than returned to the caller.
        PLOG_ERROR << "config save failed: " << ex.what();
        return CommandResult::failed;
    }

    config_ = std::move(next);
    return CommandResult::ok;
}

// ================================================================================

void RobotApi::scheduleSystemActionUnlocked(SystemAction action) {
    system_action_pending_ = true;
    SystemControl *control = system_control_.get();
    std::thread([this, action, control]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(kSystemActionDelayMs));
        control->perform(action);
        std::lock_guard<std::mutex> done(mutex_);
        system_action_pending_ = false;
    }).detach();
}

// ================================================================================

CommandResult RobotApi::requestSystemAction(SystemAction action, const std::string &pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false || system_action_pending_) {
        return CommandResult::busy;
    }

    const CommandResult authorized = authorizePinUnlocked(pin);
    if (authorized != CommandResult::ok) {
        return authorized;
    }

    scheduleSystemActionUnlocked(action);
    return CommandResult::ok;
}

// ================================================================================

CommandResult RobotApi::setSystemPin(const std::string &pin,
                                     const std::string &current_pin) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock() == false || system_action_pending_) {
        return CommandResult::busy;
    }

    if (pin_length_ok(pin) == false) {
        return CommandResult::bad_pin;
    }

    if (pin_is_set(config_)
            && pin_matches(current_pin, config_.system().pin_hash()) == false) {
        return CommandResult::pin_invalid;
    }

    try {
        Config next = config_;
        next.mutable_system()->set_pin_hash(hash_pin(pin));
        if (config_path_.empty() == false) {
            config_save_file(next, config_path_);
        }
        config_ = std::move(next);
    } catch (const ConfigError &ex) {
        PLOG_ERROR << "PIN save failed: " << ex.what();
        return CommandResult::failed;
    }

    pin_failures_ = 0;
    pin_lockout_until_ = {};
    return CommandResult::ok;
}
