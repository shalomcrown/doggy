#!/usr/bin/env bash
# Regression tests for FetchContent httplib + systemd packaging (no live dpkg).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FAILS=0

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

CMAKE="$ROOT/CMakeLists.txt"
UNIT="$ROOT/packaging/doggy.service"
POSTINST="$ROOT/packaging/debian/postinst"
PRERM="$ROOT/packaging/debian/prerm"
POSTRM="$ROOT/packaging/debian/postrm"

# ── CMake fetches httplib; tree is not vendored ──────────────────────────────
if grep -q 'FetchContent' "$CMAKE" && grep -q 'v0.18.3' "$CMAKE"; then
    pass "CMake FetchContent pins cpp-httplib v0.18.3"
else
    fail "CMake FetchContent pins cpp-httplib v0.18.3"
fi

if grep -q 'URL_HASH SHA256=' "$CMAKE"; then
    pass "CMake pins httplib archive SHA256"
else
    fail "CMake pins httplib archive SHA256"
fi

if grep -q 'third_party/cpp-httplib' "$CMAKE"; then
    fail "CMakeLists does not reference third_party/cpp-httplib"
else
    pass "CMakeLists does not reference third_party/cpp-httplib"
fi

if [ -e "$ROOT/third_party/cpp-httplib/httplib.h" ]; then
    fail "vendored httplib.h is removed from the source tree"
else
    pass "vendored httplib.h is removed from the source tree"
fi

if grep -q 'CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA' "$CMAKE" \
        && grep -q 'packaging/debian/postinst' "$CMAKE"; then
    pass "CPack ships Debian maintainer scripts"
else
    fail "CPack ships Debian maintainer scripts"
fi

if grep -q 'doggy.service' "$CMAKE"; then
    pass "CMake installs doggy.service"
else
    fail "CMake installs doggy.service"
fi

# ── unit file ────────────────────────────────────────────────────────────────
if [ -f "$UNIT" ]; then
    pass "doggy.service exists"
else
    fail "doggy.service exists"
fi

if [ -f "$UNIT" ] && grep -q '^User=doggy$' "$UNIT"; then
    pass "unit runs as User=doggy"
else
    fail "unit runs as User=doggy"
fi

if [ -f "$UNIT" ] && grep -q '^ExecStart=/usr/bin/doggy$' "$UNIT"; then
    pass "unit ExecStart is /usr/bin/doggy"
else
    fail "unit ExecStart is /usr/bin/doggy"
fi

if [ -f "$UNIT" ] && grep -q '^SupplementaryGroups=i2c$' "$UNIT"; then
    pass "unit SupplementaryGroups includes i2c"
else
    fail "unit SupplementaryGroups includes i2c"
fi

# ── maintainer scripts exist and are valid sh ────────────────────────────────
for script in "$POSTINST" "$PRERM" "$POSTRM"; do
    name=$(basename "$script")
    if [ -f "$script" ] && [ -x "$script" ]; then
        pass "$name is executable"
    else
        fail "$name is executable"
    fi

    if [ -f "$script" ] && sh -n "$script"; then
        pass "$name is valid sh"
    else
        fail "$name is valid sh"
    fi
done

if [ -f "$ROOT/packaging/debian/preinst" ]; then
    fail "no preinst (user is created in postinst)"
else
    pass "no preinst (user is created in postinst)"
fi

if [ -f "$POSTRM" ] && grep -q userdel "$POSTRM"; then
    fail "postrm does not call userdel"
else
    pass "postrm does not call userdel"
fi

# ── mocked postinst / prerm / postrm ─────────────────────────────────────────
MOCK_BIN="$TMPDIR/bin"
MOCK_STATE="$TMPDIR/state"
MOCK_LOG="$TMPDIR/log"
mkdir -p "$MOCK_BIN" "$MOCK_STATE" "$MOCK_LOG"

cat >"$MOCK_BIN/getent" <<'EOF'
#!/bin/sh
if [ "$1" = passwd ] && [ "$2" = doggy ]; then
    if [ -f "$DOGGY_MOCK_STATE/user_exists" ]; then
        echo "doggy:x:999:999:doggy robot dog:/nonexistent:/usr/sbin/nologin"
        exit 0
    fi

    exit 2
fi

if [ "$1" = group ] && [ "$2" = i2c ]; then
    if [ -f "$DOGGY_MOCK_STATE/no_i2c" ]; then
        exit 2
    fi

    echo "i2c:x:998:"
    exit 0
fi

exit 2
EOF

cat >"$MOCK_BIN/useradd" <<'EOF'
#!/bin/sh
printf 'useradd %s\n' "$*" >>"$DOGGY_MOCK_LOG"
touch "$DOGGY_MOCK_STATE/user_exists"
exit 0
EOF

cat >"$MOCK_BIN/usermod" <<'EOF'
#!/bin/sh
printf 'usermod %s\n' "$*" >>"$DOGGY_MOCK_LOG"
exit 0
EOF

cat >"$MOCK_BIN/systemctl" <<'EOF'
#!/bin/sh
printf 'systemctl %s\n' "$*" >>"$DOGGY_MOCK_LOG"
exit 0
EOF

chmod +x "$MOCK_BIN/getent" "$MOCK_BIN/useradd" "$MOCK_BIN/usermod" "$MOCK_BIN/systemctl"

run_script() {
    local script="$1"
    shift
    : >"$MOCK_LOG/commands"
    DOGGY_MOCK_STATE="$MOCK_STATE" \
    DOGGY_MOCK_LOG="$MOCK_LOG/commands" \
    PATH="$MOCK_BIN:$PATH" \
        "$script" "$@"
}

rm -f "$MOCK_STATE/user_exists" "$MOCK_STATE/no_i2c"
if [ -x "$POSTINST" ] && run_script "$POSTINST" configure; then
    pass "postinst configure exits 0 when user is missing"
else
    fail "postinst configure exits 0 when user is missing"
fi

if grep -q 'useradd' "$MOCK_LOG/commands" \
        && grep -q -- '--system' "$MOCK_LOG/commands" \
        && grep -q 'doggy' "$MOCK_LOG/commands"; then
    pass "postinst creates system user doggy when missing"
else
    fail "postinst creates system user doggy when missing"
fi

if grep -q 'usermod' "$MOCK_LOG/commands" && grep -q -- '-aG i2c' "$MOCK_LOG/commands"; then
    pass "postinst adds doggy to i2c when the group exists"
else
    fail "postinst adds doggy to i2c when the group exists"
fi

if grep -q 'systemctl enable doggy.service' "$MOCK_LOG/commands" \
        && grep -Eq 'systemctl (restart|start) doggy.service' "$MOCK_LOG/commands"; then
    pass "postinst enables and starts doggy.service"
else
    fail "postinst enables and starts doggy.service"
fi

: >"$MOCK_LOG/commands"
touch "$MOCK_STATE/user_exists"
if [ -x "$POSTINST" ] && run_script "$POSTINST" configure; then
    if grep -q useradd "$MOCK_LOG/commands"; then
        fail "postinst does not useradd when doggy already exists"
    else
        pass "postinst does not useradd when doggy already exists"
    fi
else
    fail "postinst does not useradd when doggy already exists"
fi

: >"$MOCK_LOG/commands"
touch "$MOCK_STATE/no_i2c"
if [ -x "$POSTINST" ] && run_script "$POSTINST" configure; then
    if grep -q usermod "$MOCK_LOG/commands"; then
        fail "postinst skips usermod when i2c group is missing"
    else
        pass "postinst skips usermod when i2c group is missing"
    fi
else
    fail "postinst skips usermod when i2c group is missing"
fi

rm -f "$MOCK_STATE/no_i2c"
: >"$MOCK_LOG/commands"
if [ -x "$PRERM" ] && run_script "$PRERM" remove; then
    if grep -q 'systemctl stop doggy.service' "$MOCK_LOG/commands"; then
        pass "prerm stop doggy.service on remove"
    else
        fail "prerm stop doggy.service on remove"
    fi
else
    fail "prerm stop doggy.service on remove"
fi

: >"$MOCK_LOG/commands"
if [ -x "$POSTRM" ] && run_script "$POSTRM" purge; then
    if grep -q 'systemctl disable doggy.service' "$MOCK_LOG/commands" \
            && grep -q userdel "$MOCK_LOG/commands"; then
        fail "postrm purge disables the unit and does not userdel"
    elif grep -q 'systemctl disable doggy.service' "$MOCK_LOG/commands"; then
        pass "postrm purge disables the unit and does not userdel"
    else
        fail "postrm purge disables the unit and does not userdel"
    fi
else
    fail "postrm purge disables the unit and does not userdel"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%s\n' "---- captured ----"
    if [ -f "$MOCK_LOG/commands" ]; then
        printf '== commands ==\n'
        cat "$MOCK_LOG/commands"
    fi

    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi

echo "All packaging tests passed."
exit 0
