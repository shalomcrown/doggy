cmake_minimum_required(VERSION 3.21)
include(CPack)

project(doggy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================

if(CMAKE_CROSSCOMPILING)
    if(NOT CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "/")
        message(FATAL_ERROR
            "aarch64 cross-compilation requires -DCMAKE_SYSROOT=/path/to/raspberry-pi-sysroot "
            "(or DOGGY_SYSROOT when using the ubuntu-aarch64-cross preset).\n"
            "Native Raspberry Pi builds do not need a sysroot."
        )
    endif()
endif()

# ==============================================================================

add_executable(doggy
    src/main.cpp
    src/doggy.h
    src/i2c_interface.cpp
    src/i2c_interface.hpp
    src/servo.h
    src/servo.cpp
    src/doggy.cpp
    src/imu.h
    src/imu.cpp
)
target_include_directories(doggy PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(doggy PRIVATE pthread i2c dlib)

install(TARGETS doggy RUNTIME DESTINATION bin)

# ==============================================================================

enable_testing()
add_test(
    NAME install-prereqs
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/install-prereqs.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_executable(test_i2c_header_types tests/test_i2c_header_types.cpp)
target_include_directories(test_i2c_header_types PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME i2c_header_types COMMAND test_i2c_header_types)

# ==============================================================================

set(CPACK_DEBIAN_PACKAGE_NAME "shaloms-doggy")
set(CPACK_DEBIAN_PACKAGE_VERSION "1.0.0")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Freenove doggy software")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libdlib-dev i2c-tools libi2c-dev zssh lrzsz vim")
