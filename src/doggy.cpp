#include "doggy.h"

// ================================================================================

Leg::Leg(Servo &waist, Servo &hip, Servo &knee) :
    waist(waist), hip(hip), knee(knee) {
}

// ================================================================================

Head::Head(Servo &neck) : neck(neck) {
}

// ================================================================================

void Head::lookForward() {
    neck.setAngle(90);
}

// ================================================================================

void Leg::setKnee(double angle) {
    knee.setAngle(angle);
}

// ================================================================================

void Leg::setHip(double angle) {
    hip.setAngle(angle);
}

// ================================================================================

void Leg::setWaist(double angle) {
    waist.setAngle(angle);
}

// ================================================================================

void Leg::allToNinety() {
    waist.setAngle(90);
    hip.setAngle(90);
    knee.setAngle(90);
}

// ================================================================================

Dog::Dog() :
    board(),
    frontRightWaist(board, 11, "front-right-waist"),
    frontRightHip(board, 12, "front-right-hip"),
    frontRightKnee(board, 13, "front-right-knee"),
    frontLeftWaist(board, 2, "front-left-waist"),
    frontLeftHip(board, 3, "front-left-hip"),
    frontLeftKnee(board, 4, "front-left-knee"),
    rearLeftWaist(board, 7, "rear-left-waist"),
    rearLeftHip(board, 6, "rear-left-hip"),
    rearLeftKnee(board, 5, "rear-left-knee"),
    rearRightWaist(board, 8, "rear-right-waist"),
    rearRightHip(board, 9, "rear-right-hip"),
    rearRightKnee(board, 10, "rear-right-knee"),
    headNeck(board, 15, "head-neck"),
    frontRight(frontRightWaist, frontRightHip, frontRightKnee),
    frontLeft(frontLeftWaist, frontLeftHip, frontLeftKnee),
    rearLeft(rearLeftWaist, rearLeftHip, rearLeftKnee),
    rearRight(rearRightWaist, rearRightHip, rearRightKnee),
    head(headNeck),
    servos{
        &frontRightWaist, &frontRightHip, &frontRightKnee,
        &frontLeftWaist, &frontLeftHip, &frontLeftKnee,
        &rearLeftWaist, &rearLeftHip, &rearLeftKnee,
        &rearRightWaist, &rearRightHip, &rearRightKnee,
        &headNeck
    } {
}

// ================================================================================

void Dog::allToNinety() {
    frontRight.allToNinety();
    frontLeft.allToNinety();
    rearLeft.allToNinety();
    rearRight.allToNinety();
    head.lookForward();
}

// ================================================================================

Servo *Dog::findServo(int id) {
    for (Servo *servo : servos) {
        if (servo->id() == id) {
            return servo;
        }
    }
    return nullptr;
}

// ================================================================================

std::vector<ServoSnapshot> Dog::snapshotUnlocked() const {
    std::vector<ServoSnapshot> items;
    items.reserve(servos.size());
    for (const Servo *servo : servos) {
        items.push_back(ServoSnapshot{
            servo->id(),
            servo->name(),
            servo->angle(),
            servo->pwm()
        });
    }
    return items;
}

// ================================================================================

std::vector<ServoSnapshot> Dog::listServos() {
    std::lock_guard<std::mutex> lock(mutex);
    return snapshotUnlocked();
}

// ================================================================================

CommandResult Dog::home() {
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }
    allToNinety();
    frontRight.setKnee(135);
    frontLeft.setKnee(135);
    rearLeft.setKnee(135);
    rearRight.setKnee(135);
    return CommandResult::ok;
}

// ================================================================================

bool Dog::homing() {
    return home() == CommandResult::ok;
}

// ================================================================================

CommandResult Dog::setServoAngle(int id, double angle) {
    if (angle < 0.0 || angle > board.servoMaxAngle()) {
        return CommandResult::bad_angle;
    }
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() == false) {
        return CommandResult::busy;
    }
    Servo *servo = findServo(id);
    if (servo == nullptr) {
        return CommandResult::not_found;
    }
    servo->setAngle(angle);
    return CommandResult::ok;
}
