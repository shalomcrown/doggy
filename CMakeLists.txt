cmake_minimum_required(VERSION 3.21)

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

add_library(doggy_web STATIC
    src/web_server.cpp
)
target_include_directories(doggy_web
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/third_party/cpp-httplib"
)
target_link_libraries(doggy_web PUBLIC pthread)
target_compile_definitions(doggy_web
    PRIVATE
        DOGGY_WEB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/web"
)

# ==============================================================================

add_executable(doggy
    src/main.cpp
    src/doggy.h
    src/doggy.cpp
    src/i2c_interface.cpp
    src/i2c_interface.hpp
    src/servo_board.h
    src/servo_board.cpp
    src/servo.h
    src/servo.cpp
    src/imu.h
    src/imu.cpp
)
target_include_directories(doggy PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(doggy PRIVATE doggy_web i2c dlib)
target_compile_definitions(doggy
    PRIVATE
        DOGGY_WEB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/web"
)

install(TARGETS doggy RUNTIME DESTINATION bin)
install(FILES web/index.html DESTINATION share/doggy)

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

add_test(
    NAME install-remote
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/install-remote.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_executable(test_pwm_math tests/test_pwm_math.cpp)
target_include_directories(test_pwm_math PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME pwm_math COMMAND test_pwm_math)

add_executable(test_web_server tests/test_web_server.cpp)
target_link_libraries(test_web_server PRIVATE doggy_web)
add_test(NAME web_server COMMAND test_web_server)
set_tests_properties(web_server PROPERTIES
    ENVIRONMENT "DOGGY_TEST_INDEX=${CMAKE_SOURCE_DIR}/web/index.html"
)

# ==============================================================================

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "shaloms-doggy")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Freenove doggy software")
set(CPACK_PACKAGE_CONTACT "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_NAME "shaloms-doggy")
set(CPACK_DEBIAN_PACKAGE_VERSION "1.0.0")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Freenove doggy software")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libdlib-dev, i2c-tools, libi2c-dev, zssh, lrzsz, vim")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
