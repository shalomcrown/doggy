#include "doggy.h"


Leg::Leg(int waistServo, int hipServo, int kneeServo, Servo &servo) :
    waistServo(waistServo), hipServo(hipServo), kneeServo(kneeServo), servo(servo) {
}

Head::Head(int neck, Servo &servo) : servo(servo), neck(neck) {
}

void Head::lookForward() {
    servo.set_angle(neck, 90);
}

// =================================================================

void Leg::setKnee(double angle) {
    servo.set_angle(kneeServo, angle);
}

void Leg::setHip(double angle) {
    servo.set_angle(hipServo, angle);
}

void Leg::setWaist(double angle) {
    servo.set_angle(waistServo, angle);
}

// =================================================================

void Leg::allToNinety() {
    servo.set_angle(waistServo, 90);
    servo.set_angle(hipServo, 90);
    servo.set_angle(kneeServo, 90);
}

// =================================================================

Dog::Dog() : servo(),
    head(15, servo),
    frontRight(11, 12, 13, servo),  frontLeft(2, 3, 4, servo),
    rearLeft(7, 6, 5, servo),       rearRight(8, 9,10, servo) {
}

// =================================================================

void Dog::allToNinety() {
    frontRight.allToNinety();
    frontLeft.allToNinety();
    rearLeft.allToNinety();
    head.lookForward();
}

// =================================================================
bool Dog::homing() {
    allToNinety();

    frontRight.setKnee(135);
    frontLeft.setKnee(135);
    rearLeft.setKnee(135);
    rearRight.setKnee(135);

    return true;
}
