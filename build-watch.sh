#!/usr/bin/env bash
#
# Build one or both doggy watch firmware environments with PlatformIO.
#
# Usage:
#   ./build-watch.sh [--dry-run] [all|twatch-s3|waveshare-c6]
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
DRY_RUN=0
TARGET=all

# ================================================================================

die() {
    printf '[ERROR] %s\n' "$1" >&2
    exit 1
}

# ================================================================================

run_env() {
    local environment="$1"
    if [ "$DRY_RUN" -eq 1 ]; then
        printf 'pio run --project-dir %q --environment %q\n' \
            "$ROOT/watch" "$environment"
        return
    fi
    command -v pio >/dev/null 2>&1 \
        || die "PlatformIO (pio) is not installed"
    local version
    version="$(pio --version | awk '{print $NF}')"
    if [ "$(printf '%s\n' 6.2.0 "$version" | sort -V | head -n 1)" != 6.2.0 ]; then
        die "PlatformIO Core 6.2.0 or newer is required (found $version)"
    fi
    python3 "$ROOT/watch/scripts/repair_esptool.py" \
        || die "Failed to refresh PlatformIO esptool"
    pio run --project-dir "$ROOT/watch" --environment "$environment"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run)
            DRY_RUN=1
            ;;
        all|twatch-s3|waveshare-c6)
            TARGET="$1"
            ;;
        -h|--help)
            grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            die "Unknown watch target: $1"
            ;;
    esac
    shift
done

case "$TARGET" in
    all)
        run_env twatch-s3
        run_env waveshare-c6
        ;;
    twatch-s3|waveshare-c6)
        run_env "$TARGET"
        ;;
esac
