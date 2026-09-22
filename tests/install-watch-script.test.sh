#!/usr/bin/env bash
# Contract tests for per-board watch flash scripts (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
S3="$ROOT/install-watch-s3.sh"
C6="$ROOT/install-watch-c6.sh"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# ================================================================================

if [ -x "$S3" ] && [ -x "$C6" ]; then
    pass "both watch install scripts are executable"
else
    fail "both watch install scripts are executable"
fi

if bash -n "$S3" && bash -n "$C6"; then
    pass "both watch install scripts are valid bash"
else
    fail "both watch install scripts are valid bash"
fi

if grep -q 'twatch-s3' "$S3" && ! grep -q 'waveshare-c6' "$S3"; then
    pass "S3 installer targets only twatch-s3"
else
    fail "S3 installer targets only twatch-s3"
fi

if grep -q 'waveshare-c6' "$C6" && ! grep -q 'twatch-s3' "$C6"; then
    pass "C6 installer targets only waveshare-c6"
else
    fail "C6 installer targets only waveshare-c6"
fi

if "$S3" --dry-run >"$TMPDIR/s3.log" 2>&1 \
        && grep -q 'pio run --project-dir' "$TMPDIR/s3.log" \
        && grep -q 'twatch-s3' "$TMPDIR/s3.log" \
        && grep -q -- '--target upload' "$TMPDIR/s3.log" \
        && ! grep -q 'waveshare-c6' "$TMPDIR/s3.log"; then
    pass "S3 dry-run uploads twatch-s3"
else
    fail "S3 dry-run uploads twatch-s3"
fi

if "$C6" --dry-run >"$TMPDIR/c6.log" 2>&1 \
        && grep -q 'waveshare-c6' "$TMPDIR/c6.log" \
        && grep -q -- '--target upload' "$TMPDIR/c6.log" \
        && ! grep -q 'twatch-s3' "$TMPDIR/c6.log"; then
    pass "C6 dry-run uploads waveshare-c6"
else
    fail "C6 dry-run uploads waveshare-c6"
fi

if "$S3" --dry-run --port /dev/ttyACM0 >"$TMPDIR/port.log" 2>&1 \
        && grep -q -- '--upload-port /dev/ttyACM0' "$TMPDIR/port.log"; then
    pass "watch installer accepts a USB serial port"
else
    fail "watch installer accepts a USB serial port"
fi

if "$S3" --dry-run --port '$(reboot)' >/dev/null 2>&1; then
    fail "unsafe upload port is rejected"
else
    pass "unsafe upload port is rejected"
fi

BY_ID='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_A0:F2:62:E0:9B:D0-if00'
if "$S3" --dry-run --port "$BY_ID" >"$TMPDIR/by-id.log" 2>&1 \
        && grep -q -- "--upload-port $BY_ID" "$TMPDIR/by-id.log"; then
    pass "watch installer accepts a serial by-id path"
else
    fail "watch installer accepts a serial by-id path"
fi

if "$C6" --dry-run --not-a-flag >/dev/null 2>&1; then
    fail "unknown argument exits non-zero"
else
    pass "unknown argument exits non-zero"
fi

# /dev/ttyACM numbers are handed out in enumeration order, and flashing reboots
# the chip into download mode, which re-enumerates it. The by-id name is built
# from the chip's serial number, so it survives that reboot.
ONE_DIR="$TMPDIR/by-id-one"
TWO_DIR="$TMPDIR/by-id-two"
NONE_DIR="$TMPDIR/by-id-none"
C6_ID='usb-Espressif_USB_JTAG_serial_debug_unit_98:A3:16:A7:31:9C-if00'
S3_ID='usb-Espressif_USB_JTAG_serial_debug_unit_A0:F2:62:E0:9B:D0-if00'
mkdir -p "$ONE_DIR" "$TWO_DIR" "$NONE_DIR"
touch "$ONE_DIR/$C6_ID" "$TWO_DIR/$C6_ID" "$TWO_DIR/$S3_ID"

if WATCH_BY_ID_DIR="$ONE_DIR" "$C6" --dry-run >"$TMPDIR/one.log" 2>&1 \
        && grep -q -- "--upload-port $ONE_DIR/$C6_ID" "$TMPDIR/one.log"; then
    pass "a single attached watch is flashed through its stable by-id path"
else
    fail "a single attached watch is flashed through its stable by-id path"
fi

# Guessing between two attached watches risks overwriting the wrong one.
if WATCH_BY_ID_DIR="$TWO_DIR" "$C6" --dry-run >"$TMPDIR/two.log" 2>&1; then
    fail "two attached watches stop the flash instead of guessing"
else
    if grep -q "$C6_ID" "$TMPDIR/two.log" && grep -q "$S3_ID" "$TMPDIR/two.log"; then
        pass "two attached watches stop the flash instead of guessing"
    else
        fail "two attached watches stop the flash instead of guessing"
    fi
fi

if WATCH_BY_ID_DIR="$TWO_DIR" "$C6" --dry-run --port /dev/ttyACM1 \
        >"$TMPDIR/two-port.log" 2>&1 \
        && grep -q -- '--upload-port /dev/ttyACM1' "$TMPDIR/two-port.log"; then
    pass "an explicit port still wins with two watches attached"
else
    fail "an explicit port still wins with two watches attached"
fi

# A board behind a plain USB-UART bridge has no by-id JTAG name at all, so
# PlatformIO's own detection has to stay reachable.
if WATCH_BY_ID_DIR="$NONE_DIR" "$C6" --dry-run >"$TMPDIR/none.log" 2>&1 \
        && grep -q 'waveshare-c6' "$TMPDIR/none.log"; then
    if grep -q -- '--upload-port' "$TMPDIR/none.log"; then
        fail "no by-id device leaves detection to PlatformIO"
    else
        pass "no by-id device leaves detection to PlatformIO"
    fi
else
    fail "no by-id device leaves detection to PlatformIO"
fi

# pioarduino esptool 5.4.0 crashes mid-upload:
# AttributeError: EsptoolLogger has no attribute _get_progress_print_file
if grep -q 'pioarduino/esptool/releases/download/v5.3.1/esptool.zip' "$ROOT/watch/platformio.ini" \
        && grep -q 'platform_packages' "$ROOT/watch/platformio.ini" \
        && grep -q 'scripts/ensure_esptool.py' "$ROOT/watch/platformio.ini" \
        && grep -q 'repair_esptool.py' "$ROOT/watch/flash.sh" \
        && ! grep -q 'esptoolpy-v5.4.0' "$ROOT/watch/platformio.ini"; then
    pass "PlatformIO pins esptoolpy 5.3.1 instead of broken 5.4.0"
else
    fail "PlatformIO pins esptoolpy 5.3.1 instead of broken 5.4.0"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All watch install-script tests passed."
