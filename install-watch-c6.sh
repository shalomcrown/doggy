#!/usr/bin/env bash
#
# install-watch-c6.sh
#
# Build and flash Waveshare ESP32-C6 Touch AMOLED 2.06 firmware over USB
# with PlatformIO.
#
# Usage:
#   ./install-watch-c6.sh [--dry-run] [--port /dev/ttyACM0]
set -eu

# shellcheck source=watch/flash.sh
. "$(cd "$(dirname "$0")" && pwd)/watch/flash.sh"

watch_parse_args "$@"
watch_flash waveshare-c6
