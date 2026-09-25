#!/usr/bin/env bash
# Contract tests for watch rover control (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UI="$ROOT/watch/src/watch_ui.cpp"
MAIN="$ROOT/watch/src/main.cpp"
CONTROL="$ROOT/watch/src/watch_control.cpp"
WORKER="$ROOT/watch/src/watch_rover_worker.cpp"
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
    pass "main loop feeds Wi-Fi state to rover control"
else
    fail "main loop feeds Wi-Fi state to rover control"
fi

if grep -q 'kHeartbeatIntervalMs = 750' "$WORKER"; then
    pass "heartbeat interval matches the rover page"
else
    fail "heartbeat interval matches the rover page"
fi

if grep -q 'kDriveResendMs = 50' "$WORKER"; then
    pass "drive resend is tuned for watch responsiveness"
else
    fail "drive resend is tuned for watch responsiveness"
fi

if grep -q 'watch_control_stick(0.0f, 0.0f, true)' "$UI"; then
    pass "releasing the stick stops the rover immediately"
else
    fail "releasing the stick stops the rover immediately"
fi

if grep -q 'xTaskCreate' "$WORKER" \
        && grep -q 'watch_rover_worker_begin' "$CONTROL" \
        && ! grep -q 'watch_rover_post_drive' "$CONTROL"; then
    pass "rover HTTPS runs on a dedicated FreeRTOS task"
else
    fail "rover HTTPS runs on a dedicated FreeRTOS task"
fi

if grep -q 'drive_command_changed_locked' "$WORKER"; then
    pass "worker sends drive when speed or turn change"
else
    fail "worker sends drive when speed or turn change"
fi

if grep -q 'set_tileview_scroll_enabled' "$UI" \
        && grep -q 'watch_joystick_normalize_stick_offset' "$UI"; then
    pass "control page locks tile swipes and uses web stick math"
else
    fail "control page locks tile swipes and uses web stick math"
fi

if grep -q 'watch_board_poll_ui' "$MAIN"; then
    pass "LVGL runs on the main loop independently of rover HTTPS"
else
    fail "LVGL runs on the main loop independently of rover HTTPS"
fi

if grep -q 'setReuse(true)' "$CLIENT"; then
    pass "rover HTTPS reuses the TLS session"
else
    fail "rover HTTPS reuses the TLS session"
fi

if grep -q 'watch_rover_post_stop' "$CLIENT" \
        && grep -q '"/api/stop"' "$CLIENT" \
        && grep -q 'apply_stop_result_locked' "$WORKER" \
        && grep -q 'Stop failed — retrying' "$WORKER"; then
    pass "stop uses POST /api/stop and retries until it succeeds"
else
    fail "stop uses POST /api/stop and retries until it succeeds"
fi

if grep -q 'watch_rover_client_reset_session' "$CLIENT" \
        && grep -q 'watch_rover_client_reset_session' "$WORKER"; then
    pass "TLS session resets after Pi reboot or entering control"
else
    fail "TLS session resets after Pi reboot or entering control"
fi

if grep -q 'setInsecure()' "$CLIENT"; then
    pass "HTTPS uses setInsecure for the LAN self-signed cert"
else
    fail "HTTPS uses setInsecure for the LAN self-signed cert"
fi

if grep -q 'No doggy selected' "$WORKER"; then
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
