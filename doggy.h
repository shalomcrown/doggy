#ifndef DOGGY_H
#define DOGGY_H

#define ADAFRUIT_SERVO_BUS 1
#define ADAFRUIT_SERVO_ID 0x40

#include "servo.h"

class Leg {
public:
    int waistServo;
    int hipServo;
    int kneeServo;

    Servo &servo;
    Leg(int waistServo, int hipServo, int kneeServo, Servo &servo);

    void allToNinety();
};

class Dog {
public:
    Servo servo;
    Leg frontRight;
    Leg frontLeft;
    Leg rearLeft;
    Leg rearRight;

    Dog();
    void allToNinety();

};

#endif // DOGGY_H
