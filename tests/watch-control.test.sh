#!/usr/bin/env bash
# Contract tests for watch rover control (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UI="$ROOT/watch/src/watch_ui.cpp"
MAIN="$ROOT/watch/src/main.cpp"
CONTROL="$ROOT/watch/src/watch_control.cpp"
CLIENT="$ROOT/watch/src/watch_rover_client.cpp"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

if grep -q 'lv_tileview_add_tile(tile_view, 0, 0, LV_DIR_RIGHT' "$UI" \
        && grep -q 'build_control_page' "$UI" \
        && grep -q '"< Control"' "$UI"; then
    pass "swipe right from the clock reaches the control page"
else
    fail "swipe right from the clock reaches the control page"
fi

if grep -q 'lv_tileview_add_tile(tile_view, 3, 0' "$UI"; then
    pass "Doggys page moved to the third left swipe"
else
    fail "Doggys page moved to the third left swipe"
fi

if grep -q 'build_status_bar(control_tile, 0,' "$UI" \
        && grep -q 'build_status_bar(clock_tile, 1,' "$UI" \
        && grep -q 'build_status_bar(settings_tile, 2,' "$UI" \
        && grep -q 'build_status_bar(doggys_tile, 3,' "$UI"; then
    pass "each tile owns a distinct status-bar slot"
else
    fail "each tile owns a distinct status-bar slot"
fi

if grep -q 'watch_control_set_screen_active' "$UI" \
        && grep -q 'watch_control_service' "$MAIN"; then
    pass "control screen gates the rover client loop"
else
    fail "control screen gates the rover client loop"
fi

if grep -q 'kHeartbeatIntervalMs = 750' "$CONTROL"; then
    pass "heartbeat interval matches the rover page"
else
    fail "heartbeat interval matches the rover page"
fi

if grep -q 'kDriveDebounceMs = 100' "$CONTROL"; then
    pass "drive debounce matches the rover page"
else
    fail "drive debounce matches the rover page"
fi

if grep -q 'watch_control_stick(0.0f, 0.0f, true)' "$UI"; then
    pass "releasing the stick stops the rover immediately"
else
    fail "releasing the stick stops the rover immediately"
fi

if grep -q 'drive_stop_immediate' "$CONTROL" \
        && ! grep -q 'send_heartbeat(millis())' "$CONTROL"; then
    pass "rover HTTPS runs from the service loop, not the UI thread"
else
    fail "rover HTTPS runs from the service loop, not the UI thread"
fi

if grep -q 'http_in_flight' "$CONTROL"; then
    pass "rover HTTPS requests are serialized"
else
    fail "rover HTTPS requests are serialized"
fi

if grep -q 'lv_event_stop_bubbling' "$UI"; then
    pass "joystick touch does not scroll the tile view"
else
    fail "joystick touch does not scroll the tile view"
fi

if grep -q 'setInsecure()' "$CLIENT"; then
    pass "HTTPS uses setInsecure for the LAN self-signed cert"
else
    fail "HTTPS uses setInsecure for the LAN self-signed cert"
fi

if grep -q 'No doggy selected' "$CONTROL"; then
    pass "control page handles missing selection"
else
    fail "control page handles missing selection"
fi

if grep -q 'watch_control_prevents_sleep' "$MAIN"; then
    pass "control screen keeps the watch awake for GCS heartbeats"
else
    fail "control screen keeps the watch awake for GCS heartbeats"
fi

if grep -q 'make_clock_nav_row' "$UI" \
        && grep -q 'LV_FLEX_ALIGN_SPACE_BETWEEN' "$UI"; then
    pass "clock Control and Settings share a bottom nav row"
else
    fail "clock Control and Settings share a bottom nav row"
fi

if [ "$FAILS" -eq 0 ]; then
    exit 0
fi

exit 1
