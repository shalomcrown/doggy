#ifndef DOGGY_H
#define DOGGY_H

#include "imu.h"
#define ADAFRUIT_SERVO_BUS 1
#define ADAFRUIT_SERVO_ID 0x40

#include "servo.h"

class Leg {
public:
    int waistServo;
    int hipServo;
    int kneeServo;

    Servo &servo;
    Imu imu;
    Leg(int waistServo, int hipServo, int kneeServo, Servo &servo);

    void allToNinety();
    void setKnee(double angle);
    void setHip(double angle);
    void setWaist(double angle);
};

class Head {
public:
    Servo &servo;
    int neck;
    Head(int neck, Servo &servo);
    void lookForward();
};

class Dog {
public:
    Servo servo;
    Leg frontRight;
    Leg frontLeft;
    Leg rearLeft;
    Leg rearRight;
    Head head;


    Dog();
    void allToNinety();
    bool homing();

};

#endif // DOGGY_H
