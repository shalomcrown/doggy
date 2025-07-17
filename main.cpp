#include <iostream>
#include <unistd.h>

#include "gpioPin.hpp"
#include "doggy.h"


using namespace std;



int main() {

    cout << "Doggy test!" << endl;

    Dog doggy;

    doggy.allToNinety();

    usleep(1000000);
    return 0;
}
