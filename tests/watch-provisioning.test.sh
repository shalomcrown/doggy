#!/usr/bin/env bash
# Contract tests for watch ESP-Touch v2 provisioning (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NETWORK="$ROOT/watch/src/watch_network.cpp"
UI="$ROOT/watch/src/watch_ui.cpp"
SETTINGS="$ROOT/watch/src/watch_settings.cpp"
FAILS=0

# ================================================================================

pass() { printf 'PASS %s\n' "$1"; }

# ================================================================================

fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

# ================================================================================

# WiFi.beginSmartConfig(SC_TYPE_ESPTOUCH_V2) sets esp_touch_v2_enable_crypt with a
# NULL key, so the watch waits for AES packets no phone app sends.
if grep -q 'WiFi\.beginSmartConfig(' "$NETWORK"; then
    fail "provisioning avoids the Arduino beginSmartConfig crypt default"
else
    pass "provisioning avoids the Arduino beginSmartConfig crypt default"
fi

if grep -q 'esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2)' "$NETWORK" \
        && grep -q 'esp_smartconfig_start' "$NETWORK" \
        && grep -q 'SMARTCONFIG_START_CONFIG_DEFAULT' "$NETWORK"; then
    pass "provisioning starts ESP-Touch v2 through the IDF API"
else
    fail "provisioning starts ESP-Touch v2 through the IDF API"
fi

if grep -q 'esp_touch_v2_enable_crypt = true' "$NETWORK"; then
    fail "ESP-Touch v2 crypt stays disabled"
else
    pass "ESP-Touch v2 crypt stays disabled"
fi

if grep -q 'esp_smartconfig_stop' "$NETWORK"; then
    pass "provisioning stops through the IDF API"
else
    fail "provisioning stops through the IDF API"
fi

if grep -q 'ARDUINO_EVENT_SC_SEND_ACK_DONE' "$NETWORK" \
        && grep -q 'ARDUINO_EVENT_SC_GOT_SSID_PSWD' "$NETWORK"; then
    pass "provisioning tracks its own SmartConfig events"
else
    fail "provisioning tracks its own SmartConfig events"
fi

if grep -q 'watch_network_start_provisioning' "$NETWORK" \
        && grep -q 'watch_network_start_provisioning' "$UI"; then
    pass "settings can start ESP-Touch immediately"
else
    fail "settings can start ESP-Touch immediately"
fi

# The network layer retries saved networks and hands joins to the settings store,
# which is the only place that bounds the roster and touches flash.
if grep -q 'watch_settings_wifi_entries' "$NETWORK" \
        && grep -q 'watch_settings_remember_wifi' "$NETWORK" \
        && grep -q 'watch_wifi_roster_upsert' "$SETTINGS"; then
    pass "network service persists a bounded Wi-Fi roster"
else
    fail "network service persists a bounded Wi-Fi roster"
fi

# Sleep must not cut the radio out from under a phone that is mid-handshake.
if grep -q 'if (provisioning) {' "$NETWORK" \
        && grep -q 'watch_network_provisioning_active' "$NETWORK"; then
    pass "provisioning holds the radio through sleep"
else
    fail "provisioning holds the radio through sleep"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All watch provisioning tests passed."
