cmake_minimum_required(VERSION 3.21)

option(DOGGY_OPERATOR_PACKAGE "Package laptop operator client (no Pi firmware)" OFF)
set(DOGGY_OPERATOR_OS "linux" CACHE STRING "Operator GOOS: linux or windows")
if(DOGGY_OPERATOR_PACKAGE)
    project(doggy-lora-operator LANGUAGES NONE)
    include("${CMAKE_SOURCE_DIR}/cmake/operator-package.cmake")
    return()
endif()

project(doggy LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_program(GO_EXECUTABLE go)
if(NOT GO_EXECUTABLE)
    message(FATAL_ERROR "go not found; install golang-go (see ./install-prereqs.sh)")
endif()

# ==============================================================================

include(FetchContent)
FetchContent_Declare(
    cpp_httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.52.0.tar.gz
    URL_HASH SHA256=7e0d6d716ca9308a144de249f16eb7907e93b85fb243da71bd2c2eab72ef72bb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_WOLFSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_MBEDTLS_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
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

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# ==============================================================================

# Official release tarball (not GitHub's auto source.tar.gz — that omits
# generated files). https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7
FetchContent_Declare(
    mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
    URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mbedtls)
if(NOT TARGET mbedcrypto OR NOT TARGET mbedx509 OR NOT TARGET mbedtls)
    message(FATAL_ERROR "FetchContent mbedtls did not create mbedtls/mbedx509/mbedcrypto")
endif()

# ==============================================================================

# protobuf 3.21.x: C++ library without Abseil. Host `protoc` is used when
# cross-compiling; native builds compile protoc from this tarball.
FetchContent_Declare(
    protobuf
    URL https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz
    URL_HASH SHA256=4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR cmake
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
if(CMAKE_CROSSCOMPILING)
    find_program(DOGGY_PROTOC protoc REQUIRED)
    set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
else()
    set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(protobuf)
if(CMAKE_CROSSCOMPILING)
    set(DOGGY_PROTOC_CMD "${DOGGY_PROTOC}")
else()
    set(DOGGY_PROTOC_CMD "$<TARGET_FILE:protobuf::protoc>")
endif()
if(NOT TARGET protobuf::libprotobuf)
    message(FATAL_ERROR "FetchContent protobuf did not create protobuf::libprotobuf")
endif()

set(DOGGY_PROTO "${CMAKE_SOURCE_DIR}/proto/doggy.proto")
set(DOGGY_PB_CC "${CMAKE_BINARY_DIR}/generated/doggy.pb.cc")
set(DOGGY_PB_H "${CMAKE_BINARY_DIR}/generated/doggy.pb.h")
set(DOGGY_PB_GO "${CMAKE_SOURCE_DIR}/lora/internal/pb/doggy.pb.go")
set(DOGGY_PROTOC_GEN_GO "${CMAKE_BINARY_DIR}/protoc-gen-go")
add_custom_command(
    OUTPUT "${DOGGY_PROTOC_GEN_GO}"
    COMMAND "${CMAKE_COMMAND}" -E env
        GOBIN="${CMAKE_BINARY_DIR}"
        GOFLAGS=""
        "${GO_EXECUTABLE}" install google.golang.org/protobuf/cmd/protoc-gen-go@v1.34.2
    COMMENT "Installing protoc-gen-go"
)
add_custom_command(
    OUTPUT "${DOGGY_PB_CC}" "${DOGGY_PB_H}" "${DOGGY_PB_GO}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_SOURCE_DIR}/lora/internal/pb"
    COMMAND "${DOGGY_PROTOC_CMD}"
        --cpp_out="${CMAKE_BINARY_DIR}/generated"
        -I "${CMAKE_SOURCE_DIR}/proto"
        "${DOGGY_PROTO}"
    COMMAND "${DOGGY_PROTOC_CMD}"
        --plugin=protoc-gen-go="${DOGGY_PROTOC_GEN_GO}"
        --go_out="${CMAKE_SOURCE_DIR}/lora"
        --go_opt=module=doggy-lora
        -I "${CMAKE_SOURCE_DIR}/proto"
        "${DOGGY_PROTO}"
    DEPENDS "${DOGGY_PROTO}" "${DOGGY_PROTOC_GEN_GO}"
    COMMENT "Generating C++/Go protobuf sources"
)
add_custom_target(doggy_proto ALL DEPENDS "${DOGGY_PB_CC}" "${DOGGY_PB_H}" "${DOGGY_PB_GO}")

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
    src/robot_api.cpp
    src/system_control.cpp
    src/config.cpp
    src/utils.cpp
    src/tls_cert.cpp
    "${DOGGY_PB_CC}"
)
target_include_directories(doggy_web
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(doggy_web PUBLIC
    pthread
    httplib::httplib
    doggy_log
    nlohmann_json::nlohmann_json
    mbedtls
    mbedx509
    mbedcrypto
    protobuf::libprotobuf
)
target_compile_definitions(doggy_web
    PUBLIC
        CPPHTTPLIB_MBEDTLS_SUPPORT
    PRIVATE
        DOGGY_WEB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/web"
)

# ==============================================================================

add_executable(doggy
    src/main.cpp
    src/doggy.h
    src/doggy.cpp
    src/rover.h
    src/rover.cpp
    src/motor_math.h
    src/i2c_interface.cpp
    src/i2c_interface.hpp
    src/servo_board.h
    src/servo_board.cpp
    src/pca9685.cpp
    src/servo.h
    src/servo.cpp
    src/imu.h
    src/imu.cpp
    src/ads7830.h
    src/ads7830.cpp
    src/system_control.h
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
add_dependencies(doggy_web doggy_version_stamp doggy_proto)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(DOGGY_GOARCH arm64)
else()
    set(DOGGY_GOARCH amd64)
endif()
set(DOGGY_LORA_BIN "${CMAKE_BINARY_DIR}/doggy-lora")
file(GLOB_RECURSE DOGGY_LORA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/lora/*.go"
    "${CMAKE_SOURCE_DIR}/lora/go.mod"
)
add_custom_command(
    OUTPUT "${DOGGY_LORA_BIN}"
    COMMAND "${CMAKE_COMMAND}" -E env
        CGO_ENABLED=0
        GOOS=linux
        GOARCH=${DOGGY_GOARCH}
        "${GO_EXECUTABLE}" build -o "${DOGGY_LORA_BIN}" ./cmd/doggy-lora
    DEPENDS ${DOGGY_LORA_SOURCES} "${DOGGY_PB_GO}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lora"
    COMMENT "Building doggy-lora (${DOGGY_GOARCH})"
)
add_custom_target(doggy_lora ALL DEPENDS "${DOGGY_LORA_BIN}")
install(PROGRAMS "${DOGGY_LORA_BIN}" DESTINATION bin)

# ==============================================================================

target_include_directories(doggy PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(doggy PRIVATE doggy_web nlohmann_json::nlohmann_json)
target_compile_definitions(doggy
    PRIVATE
        DOGGY_WEB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/web"
)

install(TARGETS doggy RUNTIME DESTINATION bin)
install(FILES web/index.html web/rover.html web/lora.html DESTINATION share/doggy)
install(FILES packaging/doggy.service packaging/doggy-lora.service
        packaging/doggy-lora-operator.service DESTINATION /etc/systemd/system)
install(FILES packaging/doggy-lora.desktop DESTINATION share/applications)
install(FILES packaging/50-shaloms-doggy.rules DESTINATION share/polkit-1/rules.d)

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

add_executable(test_pwm_math
    tests/test_pwm_math.cpp
    src/config.cpp
    src/utils.cpp
    "${DOGGY_PB_CC}"
)
target_include_directories(test_pwm_math PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(test_pwm_math PRIVATE nlohmann_json::nlohmann_json mbedcrypto protobuf::libprotobuf)
add_dependencies(test_pwm_math doggy_proto)
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

add_executable(test_motor_math tests/test_motor_math.cpp)
target_include_directories(test_motor_math PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME motor_math COMMAND test_motor_math)

add_executable(test_rover_drive
    tests/test_rover_drive.cpp
    src/rover.cpp
    src/imu.cpp
    src/ads7830.cpp
    src/servo_board.cpp
    src/i2c_interface.cpp
    src/config.cpp
    src/utils.cpp
    src/robot_api.cpp
    src/system_control.cpp
    "${DOGGY_PB_CC}"
)
target_include_directories(test_rover_drive PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(test_rover_drive PRIVATE pthread nlohmann_json::nlohmann_json mbedcrypto protobuf::libprotobuf)
add_dependencies(test_rover_drive doggy_proto)
add_test(NAME rover_drive COMMAND test_rover_drive)

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
    src/config.cpp
    src/utils.cpp
    src/robot_api.cpp
    src/system_control.cpp
    "${DOGGY_PB_CC}"
)
target_include_directories(test_dog_poll PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(test_dog_poll PRIVATE pthread nlohmann_json::nlohmann_json mbedcrypto protobuf::libprotobuf)
add_dependencies(test_dog_poll doggy_proto)
add_test(NAME dog_poll COMMAND test_dog_poll)

add_executable(test_vec3 tests/test_vec3.cpp)
target_include_directories(test_vec3 PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME vec3 COMMAND test_vec3)

add_executable(test_tls_cert tests/test_tls_cert.cpp)
target_link_libraries(test_tls_cert PRIVATE doggy_web)
add_test(NAME tls_cert COMMAND test_tls_cert)

add_executable(test_utils
    tests/test_utils.cpp
    src/utils.cpp
)
target_include_directories(test_utils PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME utils COMMAND test_utils)

add_executable(test_config
    tests/test_config.cpp
    src/config.cpp
    src/utils.cpp
    "${DOGGY_PB_CC}"
)
target_include_directories(test_config PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)
target_link_libraries(test_config PRIVATE nlohmann_json::nlohmann_json mbedcrypto protobuf::libprotobuf)
add_dependencies(test_config doggy_proto)
add_test(NAME config COMMAND test_config)

add_executable(test_doggy_log tests/test_doggy_log.cpp)
target_link_libraries(test_doggy_log PRIVATE doggy_log)
add_test(NAME doggy_log COMMAND test_doggy_log)

add_test(
    NAME doggy-lora
    COMMAND "${GO_EXECUTABLE}" test ./...
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lora"
)

add_test(
    NAME operator-package
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/operator-package.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(
    NAME build-operator-script
    COMMAND bash "${CMAKE_SOURCE_DIR}/tests/build-operator-script.test.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

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
