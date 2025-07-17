cmake_minimum_required(VERSION 3.5)

project(doggy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(doggy main.cpp gpioPin.cpp gpioPin.hpp doggy.h
    i2c_interface.cpp i2c_interface.hpp servo.h servo.cpp doggy.cpp)
target_link_libraries(doggy gpiod pthread i2c)

install(TARGETS doggy
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
