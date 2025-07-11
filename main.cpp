#include <iostream>
#include <unistd.h>
#include "gpioPin.hpp"
#include "servo.h"

using namespace std;



int main() {
    cout << "Servo test!" << endl;

    Servo servo;
    servo.openBoard();

    while(true) {
      servo.set_pwm(0, 0, 370);
      usleep(1'000'000);
      servo.set_pwm(0, 0, 415);
      usleep(1'000'000);
      servo.set_pwm(0, 0, 460);
      usleep(1'000'000);
      servo.set_pwm(0, 0, 415);
      usleep(1'000'000);
    }

    return 0;
}
