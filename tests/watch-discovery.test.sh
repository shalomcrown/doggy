#!/usr/bin/env bash
# Contract tests for non-blocking watch mDNS discovery (no hardware).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISCOVERY="$ROOT/watch/src/watch_discovery.cpp"
UI="$ROOT/watch/src/watch_ui.cpp"
SETTINGS="$ROOT/watch/src/watch_settings.cpp"
FAILS=0

# ================================================================================

pass() { printf 'PASS %s\n' "$1"; }

# ================================================================================

fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

# ================================================================================

if grep -q 'mdns_query_async_new' "$DISCOVERY" \
        && grep -q 'mdns_query_async_get_results' "$DISCOVERY" \
        && grep -q 'mdns_query_async_delete' "$DISCOVERY"; then
    pass "discovery uses the non-blocking IDF mDNS query"
else
    fail "discovery uses the non-blocking IDF mDNS query"
fi

if grep -q 'MDNS\.queryService' "$DISCOVERY"; then
    fail "discovery never runs the three-second synchronous query"
else
    pass "discovery never runs the three-second synchronous query"
fi

if grep -q 'watch_discovery_service' "$ROOT/watch/src/main.cpp" \
        && grep -q 'watch_discovery_refresh' "$UI"; then
    pass "main loop polls discovery and the UI exposes refresh"
else
    fail "main loop polls discovery and the UI exposes refresh"
fi

if grep -q 'lv_tileview_add_tile(tile_view, 3, 0' "$UI" \
        && grep -q '"Doggys"' "$UI"; then
    pass "a third left swipe reaches the Doggys page"
else
    fail "a third left swipe reaches the Doggys page"
fi

if grep -q '"doggy-target"' "$SETTINGS" \
        && grep -q 'watch_settings_set_selected_doggy' "$SETTINGS"; then
    pass "selected doggy persists in the existing settings store"
else
    fail "selected doggy persists in the existing settings store"
fi

if grep -q 'WiFi\.setSleep(false)' "$DISCOVERY" \
        && grep -q 'WiFi\.setSleep(true)' "$DISCOVERY"; then
    pass "the radio leaves modem sleep while multicast answers are expected"
else
    fail "the radio leaves modem sleep while multicast answers are expected"
fi

if grep -q 'watch_discovery_search_expired' "$DISCOVERY"; then
    pass "a search that never finishes releases the page"
else
    fail "a search that never finishes releases the page"
fi

if grep -q 'watch_discovery_should_retry' "$DISCOVERY"; then
    pass "an empty doggy query retries through the policy helper"
else
    fail "an empty doggy query retries through the policy helper"
fi

if grep -q '_services._dns-sd' "$DISCOVERY"; then
    pass "an empty result is followed by a service-enumeration probe"
else
    fail "an empty result is followed by a service-enumeration probe"
fi

if grep -q 'attempt=%u/%u' "$DISCOVERY"; then
    pass "the doggy search log names the attempt"
else
    fail "the doggy search log names the attempt"
fi

for NEEDLE in 'discovery: mDNS begin' \
        'discovery: result instance=' \
        'discovery: doggy search raw=' \
        'discovery: probe raw='; do
    if grep -q "$NEEDLE" "$DISCOVERY"; then
        pass "serial log reports '$NEEDLE'"
    else
        fail "serial log reports '$NEEDLE'"
    fi
done

if awk '
    /Serial\.printf\(/ { in_printf = 1 }
    in_printf && /\\r\\n/ { saw_cr = 1 }
    in_printf && /;/ {
        if (saw_cr == 0) { missing += 1 }
        in_printf = 0
        saw_cr = 0
    }
    END { exit missing + 0 }
' "$DISCOVERY"; then
    pass "every discovery serial line ends with CR+LF"
else
    fail "every discovery serial line ends with CR+LF"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All watch discovery tests passed."
