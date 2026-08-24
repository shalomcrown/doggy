#ifndef DOGGY_H
#define DOGGY_H

#include "dog_api.h"
#include "imu.h"
#include "servo.h"
#include "servo_board.h"

#include <mutex>
#include <vector>

#define ADAFRUIT_SERVO_BUS 1
#define ADAFRUIT_SERVO_ID 0x40

// ================================================================================

class Leg {
public:
    Servo &waist;
    Servo &hip;
    Servo &knee;
    Imu imu;

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
    void allToNinety();

    std::vector<ServoSnapshot> listServos() override;
    CommandResult home() override;
    CommandResult setServoAngle(int id, double angle) override;
    DogStatus getStatus() const override;
    bool homing();

    DogStatus status;

private:
    std::vector<Servo *> servos;
    mutable std::mutex mutex;

    Servo *findServo(int id);
    std::vector<ServoSnapshot> snapshotUnlocked() const;
};

#endif


