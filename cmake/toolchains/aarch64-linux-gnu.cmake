# Toolchain for building a Raspberry Pi aarch64 binary from an x86_64 Ubuntu host.
#
# Required:
#   - aarch64-linux-gnu-gcc / aarch64-linux-gnu-g++ on PATH
#   - CMAKE_SYSROOT pointing at a Raspberry Pi OS aarch64 sysroot (libraries
#     and headers for libi2c and dlib). Configure fails without it.
#
# Example:
#   export DOGGY_SYSROOT=/opt/rpi-sysroot
#   cmake --preset ubuntu-aarch64-cross

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
