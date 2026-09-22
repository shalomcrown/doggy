#!/usr/bin/env bash
#
# install-watch-s3.sh
#
# Build and flash LilyGO T-Watch S3 firmware over USB with PlatformIO.
#
# Usage:
#   ./install-watch-s3.sh [--dry-run] [--port /dev/ttyACM0]
set -eu

# shellcheck source=watch/flash.sh
. "$(cd "$(dirname "$0")" && pwd)/watch/flash.sh"

watch_parse_args "$@"
watch_flash twatch-s3
