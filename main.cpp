#include <iostream>
#include <unistd.h>

#include "gpioPin.hpp"
#include "servo.h"


using namespace std;



int main() {

    cout << "Servo test!" << endl;

    Servo servo;

    while(true) {
      usleep(1'000'000);
      servo.set_angle(0, 0);
      usleep(1'000'000);
      servo.set_angle(0, 10);
      usleep(1'000'000);
      servo.set_angle(0, 45);
      usleep(1'000'000);
      servo.set_angle(0, 90);
      usleep(1'000'000);
      servo.set_angle(0, 180);
    }

    return 0;
}
