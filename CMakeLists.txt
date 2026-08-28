cmake_minimum_required(VERSION 3.21)

project(doggy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================

include(FetchContent)
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    URL_HASH SHA256=a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpp_httplib)

# ==============================================================================

FetchContent_Declare(
    plog
    URL https://github.com/SergiusTheBest/plog/archive/refs/tags/1.1.11.tar.gz
    URL_HASH SHA256=d60b8b35f56c7c852b7f00f58cbe9c1c2e9e59566c5b200512d0cdbb6309a7c2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(PLOG_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(plog)

# ==============================================================================

FetchContent_Declare(
    zlib
    URL https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
    URL_HASH SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
if(TARGET zlibstatic)
    set(DOGGY_ZLIB zlibstatic)
elseif(TARGET zlib)
    set(DOGGY_ZLIB zlib)
else()
    message(FATAL_ERROR "FetchContent zlib did not create zlibstatic or zlib")
endif()

# ==============================================================================

add_library(doggy_log STATIC
    src/doggy_log.cpp
)
target_include_directories(doggy_log
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(doggy_log PUBLIC plog::plog ${DOGGY_ZLIB})

# ==============================================================================

add_library(doggy_web STATIC
    src/web_server.cpp
)
target_include_directories(doggy_web
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(doggy_web PUBLIC pthread httplib::httplib doggy_log)
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
    src/ads7830.h
    src/ads7830.cpp
)
set(DOGGY_VERSION_BASE "1.0.0")
set(DOGGY_VERSION_HEADER "${CMAKE_BINARY_DIR}/generated/doggy_version.h")
add_custom_target(doggy_version_stamp ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}"
        -DDOGGY_VERSION_BASE="${DOGGY_VERSION_BASE}"
        -DDOGGY_GIT_ROOT="${CMAKE_SOURCE_DIR}"
        -DDOGGY_VERSION_HEADER="${DOGGY_VERSION_HEADER}"
        -P "${CMAKE_SOURCE_DIR}/cmake/doggy-version.cmake"
    BYPRODUCTS "${DOGGY_VERSION_HEADER}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Stamping doggy version (time + git hash)"
)
add_dependencies(doggy doggy_version_stamp)
add_dependencies(doggy_web doggy_version_stamp)

target_include_directories(doggy PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(doggy PRIVATE doggy_web)
target_compile_definitions(doggy
    PRIVATE
        DOGGY_WEB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/web"
)

install(TARGETS doggy RUNTIME DESTINATION bin)
install(FILES web/index.html DESTINATION share/doggy)
install(FILES packaging/doggy.service DESTINATION /etc/systemd/system)

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

add_test(
    NAME doggy-ssh
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/doggy-ssh.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME packaging
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/packaging.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME build-script
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/build-script.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME version-stamp
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/version-stamp.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_executable(test_pwm_math tests/test_pwm_math.cpp)
target_include_directories(test_pwm_math PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME pwm_math COMMAND test_pwm_math)

add_executable(test_ads7830 tests/test_ads7830.cpp)
target_include_directories(test_ads7830 PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME ads7830 COMMAND test_ads7830)

add_executable(test_servo_board
    tests/test_servo_board.cpp
    src/servo_board.cpp
    src/i2c_interface.cpp
)
target_include_directories(test_servo_board PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME servo_board COMMAND test_servo_board)

add_executable(test_web_server tests/test_web_server.cpp)
target_link_libraries(test_web_server PRIVATE doggy_web)
add_test(NAME web_server COMMAND test_web_server)
set_tests_properties(web_server PROPERTIES
    ENVIRONMENT "DOGGY_TEST_INDEX=${CMAKE_SOURCE_DIR}/web/index.html"
)

add_executable(test_dog_poll
    tests/test_dog_poll.cpp
    src/doggy.cpp
    src/imu.cpp
    src/ads7830.cpp
    src/servo.cpp
    src/servo_board.cpp
    src/i2c_interface.cpp
)
target_include_directories(test_dog_poll PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(test_dog_poll PRIVATE pthread)
add_test(NAME dog_poll COMMAND test_dog_poll)

add_executable(test_vec3 tests/test_vec3.cpp)
target_include_directories(test_vec3 PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME vec3 COMMAND test_vec3)

add_executable(test_doggy_log tests/test_doggy_log.cpp)
target_link_libraries(test_doggy_log PRIVATE doggy_log)
add_test(NAME doggy_log COMMAND test_doggy_log)

# ==============================================================================

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "shaloms-doggy")
set(CPACK_PACKAGE_VERSION "${DOGGY_VERSION_BASE}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Freenove doggy software")
set(CPACK_PACKAGE_CONTACT "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_NAME "shaloms-doggy")
set(CPACK_DEBIAN_PACKAGE_VERSION "${DOGGY_VERSION_BASE}")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack-stamp.cmake")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Freenove doggy software")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Shalom Crown <shalomcrown@cmail.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "i2c-tools, zssh, lrzsz, vim, systemd, debconf, ffmpeg")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_SOURCE_DIR}/packaging/debian/prerm;${CMAKE_SOURCE_DIR}/packaging/debian/postrm;${CMAKE_SOURCE_DIR}/packaging/debian/config;${CMAKE_SOURCE_DIR}/packaging/debian/templates"
)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# CPackDeb defaults Architecture to host `dpkg --print-architecture`. Cross
# builds from amd64 would then wrap an aarch64 binary in an amd64 package.
# Debian's name for aarch64 is arm64 (what Raspberry Pi OS dpkg expects).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
endif()
include(CPack)
