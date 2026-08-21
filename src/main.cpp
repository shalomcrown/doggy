#include <iostream>
#include <unistd.h>

#include "doggy.h"
#include "imu.h"


using namespace std;



int main() {
    cout << "Doggy test!" << endl;


    Dog doggy;

    doggy.homing();

//    usleep(10000000);


    return 0;
}
