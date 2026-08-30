#ifndef DOGGY_H
#define DOGGY_H

#include "ads7830.h"
#include "config.h"
#include "dog_api.h"
#include "imu.h"
#include "servo.h"
#include "servo_board.h"
#include "system_control.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

inline constexpr int kDoggyLoopPeriodMs = 200;
inline constexpr int kSystemActionDelayMs = 1000;
inline constexpr int kPinMaxFailures = 5;
inline constexpr int kPinLockoutSeconds = 30;

// ================================================================================

class Leg {
public:
    Servo &waist;
    Servo &hip;
    Servo &knee;

    Leg(Servo &waist, Servo &hip, Servo &knee);

    void allToNinety();
    void setKnee(double angle);
    void setHip(double angle);
    void setWaist(double angle);
};

// ================================================================================

class Head {
public:
    Servo &neck;

    Head(Servo &neck);
    void lookForward();
};

// ================================================================================

class Dog : public DogApi {
public:
    ServoBoard board;
    Imu imu;
    Ads7830 ads;
    Servo frontRightWaist;
    Servo frontRightHip;
    Servo frontRightKnee;
    Servo frontLeftWaist;
    Servo frontLeftHip;
    Servo frontLeftKnee;
    Servo rearLeftWaist;
    Servo rearLeftHip;
    Servo rearLeftKnee;
    Servo rearRightWaist;
    Servo rearRightHip;
    Servo rearRightKnee;
    Servo headNeck;
    Leg frontRight;
    Leg frontLeft;
    Leg rearLeft;
    Leg rearRight;
    Head head;

    Dog();
    explicit Dog(const Config &config);
    Dog(const Config &config, std::string config_path);
    Dog(const Config &config, std::string config_path,
        std::unique_ptr<SystemControl> system_control);
    void allToNinety();
    void poll();

    std::vector<ServoSnapshot> listServos() override;
    CommandResult home() override;
    CommandResult setServoAngle(int id, double angle) override;
    CommandResult disableServo(int id) override;
    DogStatus getStatus() const override;
    Config getConfig() const override;
    CommandResult replaceConfig(const Config &config) override;
    CommandResult requestSystemAction(SystemAction action, const std::string &pin) override;
    CommandResult setSystemPin(const std::string &pin, const std::string &current_pin) override;
    bool homing();

    DogStatus status;

private:
    Config config_;
    std::string config_path_;
    std::unique_ptr<SystemControl> system_control_;
    std::vector<Servo *> servos;
    mutable std::mutex mutex;
    bool system_action_pending_ = false;
    int pin_failures_ = 0;
    std::chrono::steady_clock::time_point pin_lockout_until_{};

    Servo *findServo(int id);
    std::vector<ServoSnapshot> snapshotUnlocked() const;
    void pollImuUnlocked();
    void pollBatteryUnlocked();
};

#endif


