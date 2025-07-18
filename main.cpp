#include <iostream>
#include <unistd.h>

#include "gpioPin.hpp"
#include "doggy.h"
#include "imu.h"


using namespace std;



int main() {
    cout << "Doggy test!" << endl;

    Imu imu;

    for (int i = 100; i > 0; i--) {
        auto acc = imu.readAccelerometer();

        cout << "Accelerometer: " << acc << std::endl;
        usleep(300000);
    }


//    Dog doggy;
//    doggy.allToNinety();
//    usleep(10000000);


    return 0;
}
