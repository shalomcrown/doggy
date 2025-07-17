#include "doggy.h"


Leg::Leg(int waistServo, int hipServo, int kneeServo, Servo &servo) :
    waistServo(waistServo), hipServo(hipServo), kneeServo(kneeServo), servo(servo) {
}

// =================================================================

void Leg::allToNinety() {
    servo.set_angle(waistServo, 90);
    servo.set_angle(hipServo, 90);
    servo.set_angle(kneeServo, 90);
}

// =================================================================

Dog::Dog() : servo(),
    frontRight(11, 12, 13, servo),  frontLeft(2, 3, 4, servo),
    rearLeft(7, 6, 5, servo),       rearRight(8, 9,10, servo) {
}

// =================================================================

void Dog::allToNinety() {
    frontRight.allToNinety();
    frontLeft.allToNinety();
    rearLeft.allToNinety();
}
