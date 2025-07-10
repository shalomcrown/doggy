cmake_minimum_required(VERSION 3.5)

project(doggy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(doggy main.cpp gpioPin.cpp gpioPin.hpp)
target_link_libraries(doggy gpiod pthread)

install(TARGETS doggy
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
