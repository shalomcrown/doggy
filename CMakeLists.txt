cmake_minimum_required(VERSION 3.5)
include(CPack)

project(doggy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(doggy main.cpp gpioPin.cpp gpioPin.hpp doggy.h
    i2c_interface.cpp i2c_interface.hpp servo.h servo.cpp doggy.cpp
    imu.h imu.cpp)
target_link_libraries(doggy gpiod pthread i2c dlib)


set(CPACK_DEBIAN_PACKAGE_NAME "shaloms-doggy")
set(CPACK_DEBIAN_PACKAGE_VERSION "1.0.0")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Freenove doggy software")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libdlib-dev  libgpiod-dev i2c-tools libi2c-dev zssh lrzsz vim")


install(TARGETS doggy LIBRARY DESTINATION bin)
