#!/usr/bin/env bash
# Shared flash helper for ./install-watch-s3.sh and ./install-watch-c6.sh.
# Sourced, not executed.

WATCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WATCH_DRY_RUN=0
WATCH_PORT=""
WATCH_BY_ID_DIR="${WATCH_BY_ID_DIR:-/dev/serial/by-id}"

# ================================================================================

watch_die() {
    printf '[ERROR] %s\n' "$1" >&2
    exit 1
}

# ================================================================================

watch_port_allowed() {
    case "$1" in
        /dev/ttyACM*|/dev/ttyUSB*|/dev/ttyS*|/dev/serial/by-id/*)
              # by-id names embed the MAC, so ':' is part of a legitimate path.
            case "$1" in
                *[!A-Za-z0-9._:/-]*) return 1 ;;
            esac
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# ================================================================================

watch_parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --dry-run)
                WATCH_DRY_RUN=1
                ;;
            --port)
                [ "$#" -ge 2 ] || watch_die "--port requires a device path"
                WATCH_PORT="$2"
                watch_port_allowed "$WATCH_PORT" \
                    || watch_die "Invalid upload port: $WATCH_PORT"
                shift
                ;;
            -h|--help)
                grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
                exit 0
                ;;
            *)
                watch_die "Unknown argument: $1 (try --help)"
                ;;
        esac
        shift
    done
}

# ================================================================================

# /dev/ttyACM numbers are handed out in enumeration order, so with two watches
# attached the number identifies nothing. Flashing reboots the chip into
# download mode, which re-enumerates it and can move that number to the other
# board. The by-id name is built from the chip's own serial number and survives
# the reboot, so it is what the upload should be pinned to.
watch_resolve_port() {
    local -a attached=()
    local entry
    for entry in "$WATCH_BY_ID_DIR"/*USB_JTAG*-if00; do
        [ -e "$entry" ] || continue
        attached+=("$entry")
    done
    if [ "${#attached[@]}" -eq 0 ]; then
        # Nothing with a JTAG serial name, so a plain USB-UART bridge is still
        # possible. Leave the port unset and let PlatformIO detect it.
        return 0
    fi
    if [ "${#attached[@]}" -gt 1 ]; then
        printf '[ERROR] More than one watch is attached:\n' >&2
        printf '  %s\n' "${attached[@]}" >&2
        watch_die "Unplug the other watch or name one with --port"
    fi
    WATCH_PORT="${attached[0]}"
}

# ================================================================================

watch_flash() {
    local environment="$1"
    local -a cmd
    if [ -z "$WATCH_PORT" ]; then
        watch_resolve_port
    fi
    cmd=(
        pio run
        --project-dir "$WATCH_ROOT/watch"
        --environment "$environment"
        --target upload
    )
    if [ -n "$WATCH_PORT" ]; then
        cmd+=(--upload-port "$WATCH_PORT")
    fi
    if [ "$WATCH_DRY_RUN" -eq 1 ]; then
        printf '%s\n' "${cmd[*]}"
        return 0
    fi
    command -v pio >/dev/null 2>&1 \
        || watch_die "PlatformIO (pio) is not installed"
    local version
    version="$(pio --version | awk '{print $NF}')"
    if [ "$(printf '%s\n' 6.2.0 "$version" | sort -V | head -n 1)" != 6.2.0 ]; then
        watch_die "PlatformIO Core 6.2.0 or newer is required (found $version)"
    fi
    python3 "$WATCH_ROOT/watch/scripts/repair_esptool.py" \
        || watch_die "Failed to refresh PlatformIO esptool"
    "${cmd[@]}"
}
