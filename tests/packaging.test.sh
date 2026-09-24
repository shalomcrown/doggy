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
LORA_UNIT="$ROOT/packaging/doggy-lora.service"
MEDIAMTX_UNIT="$ROOT/packaging/mediamtx.service"
MEDIAMTX_CONFIG="$ROOT/packaging/mediamtx.yml"
MEDIAMTX_INSTALL="$ROOT/packaging/install-mediamtx.sh"
AVAHI_SERVICE="$ROOT/packaging/avahi/doggy.service"
POSTINST="$ROOT/packaging/debian/postinst"
PRERM="$ROOT/packaging/debian/prerm"
POSTRM="$ROOT/packaging/debian/postrm"

# ── CMake fetches httplib; tree is not vendored ──────────────────────────────
if grep -q 'FetchContent' "$CMAKE" && grep -q 'v0.52.0' "$CMAKE"; then
    pass "CMake FetchContent pins cpp-httplib v0.52.0"
else
    fail "CMake FetchContent pins cpp-httplib v0.52.0"
fi

if grep -q '7e0d6d716ca9308a144de249f16eb7907e93b85fb243da71bd2c2eab72ef72bb' "$CMAKE"; then
    pass "CMake pins httplib archive SHA256"
else
    fail "CMake pins httplib archive SHA256"
fi

if grep -q 'CPPHTTPLIB_MBEDTLS_SUPPORT' "$CMAKE"; then
    pass "CMake enables cpp-httplib Mbed TLS"
else
    fail "CMake enables cpp-httplib Mbed TLS"
fi

if grep -q 'SergiusTheBest/plog' "$CMAKE" && grep -q '1.1.11' "$CMAKE"; then
    pass "CMake FetchContent pins plog 1.1.11"
else
    fail "CMake FetchContent pins plog 1.1.11"
fi

if grep -q 'd60b8b35f56c7c852b7f00f58cbe9c1c2e9e59566c5b200512d0cdbb6309a7c2' "$CMAKE"; then
    pass "CMake pins plog archive SHA256"
else
    fail "CMake pins plog archive SHA256"
fi

if grep -q 'madler/zlib' "$CMAKE" && grep -q 'v1.3.1' "$CMAKE"; then
    pass "CMake FetchContent pins zlib v1.3.1"
else
    fail "CMake FetchContent pins zlib v1.3.1"
fi

if grep -q '17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c' "$CMAKE"; then
    pass "CMake pins zlib archive SHA256"
else
    fail "CMake pins zlib archive SHA256"
fi

if grep -q 'find_package(ZLIB' "$CMAKE"; then
    fail "CMake does not find_package host ZLIB"
else
    pass "CMake does not find_package host ZLIB"
fi

if grep -Eq 'target_link_libraries\(doggy PRIVATE.*dlib' "$CMAKE" \
        || grep -Eq 'target_link_libraries\(doggy PRIVATE.*i2c' "$CMAKE"; then
    fail "firmware does not link distro dlib or libi2c"
else
    pass "firmware does not link distro dlib or libi2c"
fi

if grep -q 'libdlib-dev' "$CMAKE" || grep -q 'libi2c-dev' "$CMAKE"; then
    fail "DEB depends do not include libdlib-dev or libi2c-dev"
else
    pass "DEB depends do not include libdlib-dev or libi2c-dev"
fi

if grep -Fq 'CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$"' "$CMAKE" \
        && grep -Fq 'set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")' "$CMAKE"; then
    pass "CPack Debian architecture is arm64 for aarch64/arm64 targets"
else
    fail "CPack Debian architecture is arm64 for aarch64/arm64 targets"
fi

if grep -Eq '(^|[[:space:]])crypt($|[[:space:]])' "$CMAKE"; then
    fail "CMake does not link libcrypt (missing in aarch64 cross sysroot)"
else
    pass "CMake does not link libcrypt (missing in aarch64 cross sysroot)"
fi

if grep -q 'Mbed-TLS/mbedtls' "$CMAKE" && grep -q 'mbedtls-3.6.7' "$CMAKE"; then
    pass "CMake FetchContent pins Mbed TLS 3.6.7"
else
    fail "CMake FetchContent pins Mbed TLS 3.6.7"
fi

if grep -q 'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6' "$CMAKE"; then
    pass "CMake pins Mbed TLS archive SHA256"
else
    fail "CMake pins Mbed TLS archive SHA256"
fi

if grep -q 'nlohmann/json' "$CMAKE" && grep -q 'v3.11.3' "$CMAKE"; then
    pass "CMake FetchContent pins nlohmann/json v3.11.3"
else
    fail "CMake FetchContent pins nlohmann/json v3.11.3"
fi

if grep -q 'd6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d' "$CMAKE"; then
    pass "CMake pins nlohmann/json archive SHA256"
else
    fail "CMake pins nlohmann/json archive SHA256"
fi

if grep -q 'ensure_config_dir' "$POSTINST" \
        && grep -q '/etc/doggy' "$POSTINST"; then
    pass "postinst creates /etc/doggy for the JSON config"
else
    fail "postinst creates /etc/doggy for the JSON config"
fi

cfg_call_line=$(grep -n 'ensure_config_dir' "$POSTINST" | tail -1 | cut -d: -f1)
reload_line=$(grep -n 'reload_and_start' "$POSTINST" | tail -1 | cut -d: -f1)
if [ -n "$cfg_call_line" ] && [ -n "$reload_line" ] \
        && [ "$cfg_call_line" -lt "$reload_line" ]; then
    pass "postinst creates the config directory before starting doggy.service"
else
    fail "postinst creates the config directory before starting doggy.service"
fi

if [ -f "$UNIT" ] && grep -q '^LogsDirectory=doggy$' "$UNIT"; then
    pass "unit LogsDirectory is doggy"
else
    fail "unit LogsDirectory is doggy"
fi

if [ -f "$UNIT" ] && grep -q '^LogsDirectoryMode=0755$' "$UNIT"; then
    pass "unit log directory is world-traversable"
else
    fail "unit log directory is world-traversable"
fi

restart_line=$(grep -n 'reload_and_start' "$POSTINST" | tail -1 | cut -d: -f1)
i2c_call_line=$(grep -n 'enable_i2c_boot_config' "$POSTINST" | tail -1 | cut -d: -f1)
if [ -n "$restart_line" ] && [ -n "$i2c_call_line" ] \
        && [ "$restart_line" -lt "$i2c_call_line" ]; then
    pass "postinst restarts doggy.service before the I2C reboot prompt"
else
    fail "postinst restarts doggy.service before the I2C reboot prompt"
fi

if grep -q 'i2c/smbus.h' "$ROOT/src/i2c_interface.cpp" \
        || grep -q 'i2c/smbus.h' "$ROOT/src/servo_board.cpp"; then
    fail "I2C sources do not include libi2c i2c/smbus.h"
else
    pass "I2C sources do not include libi2c i2c/smbus.h"
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

if grep -q 'doggy.service' "$CMAKE" \
        && grep -q '/etc/systemd/system' "$CMAKE"; then
    pass "CMake installs doggy.service to /etc/systemd/system"
else
    fail "CMake installs doggy.service to /etc/systemd/system"
fi

if grep -q 'packaging/avahi/doggy.service' "$CMAKE" \
        && grep -q '/etc/avahi/services' "$CMAKE" \
        && grep -q 'avahi-daemon' "$CMAKE"; then
    pass "package installs and depends on Avahi doggy discovery"
else
    fail "package installs and depends on Avahi doggy discovery"
fi

if [ -f "$AVAHI_SERVICE" ] \
        && grep -q '<name replace-wildcards="yes">%h</name>' "$AVAHI_SERVICE" \
        && grep -q '<type>_doggy._tcp</type>' "$AVAHI_SERVICE" \
        && grep -q '<port>443</port>' "$AVAHI_SERVICE" \
        && grep -q '<txt-record>txtvers=1</txt-record>' "$AVAHI_SERVICE" \
        && grep -q '<txt-record>proto=https</txt-record>' "$AVAHI_SERVICE"; then
    pass "Avahi advertises the versioned doggy HTTPS service"
else
    fail "Avahi advertises the versioned doggy HTTPS service"
fi

if grep -q '50-shaloms-doggy.rules' "$CMAKE" \
        && grep -q 'share/polkit-1/rules.d' "$CMAKE"; then
    pass "CMake installs the doggy polkit rule"
else
    fail "CMake installs the doggy polkit rule"
fi

POLKIT="$ROOT/packaging/50-shaloms-doggy.rules"
if [ -f "$POLKIT" ] \
        && grep -q 'subject.user !== "doggy"' "$POLKIT" \
        && grep -q 'doggy.service' "$POLKIT" \
        && grep -q 'restart' "$POLKIT" \
        && grep -q 'login1.power-off' "$POLKIT" \
        && grep -q 'login1.reboot' "$POLKIT"; then
    pass "polkit rule is scoped to doggy restart/reboot/poweroff"
else
    fail "polkit rule is scoped to doggy restart/reboot/poweroff"
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

if [ -f "$UNIT" ] && grep -q '^NoNewPrivileges=yes$' "$UNIT"; then
    pass "unit keeps NoNewPrivileges=yes"
else
    fail "unit keeps NoNewPrivileges=yes"
fi

if [ -f "$UNIT" ] && grep -q '^AmbientCapabilities=CAP_NET_BIND_SERVICE$' "$UNIT"; then
    pass "unit AmbientCapabilities is CAP_NET_BIND_SERVICE"
else
    fail "unit AmbientCapabilities is CAP_NET_BIND_SERVICE"
fi

if [ -f "$UNIT" ] && grep -q '^CapabilityBoundingSet=CAP_NET_BIND_SERVICE$' "$UNIT"; then
    pass "unit CapabilityBoundingSet is CAP_NET_BIND_SERVICE"
else
    fail "unit CapabilityBoundingSet is CAP_NET_BIND_SERVICE"
fi

if [ -f "$UNIT" ] && grep -q '^ExecStart=/usr/bin/doggy$' "$UNIT"; then
    pass "unit ExecStart is /usr/bin/doggy"
else
    fail "unit ExecStart is /usr/bin/doggy"
fi

if [ -f "$LORA_UNIT" ] && grep -q '^ExecStart=/usr/bin/doggy-lora --mode=robot$' "$LORA_UNIT"; then
    pass "lora unit ExecStart is doggy-lora robot mode"
else
    fail "lora unit ExecStart is doggy-lora robot mode"
fi

if grep -q 'doggy-lora.service' "$CMAKE" && grep -q 'lora.html' "$CMAKE"; then
    pass "CMake installs doggy-lora unit and lora.html"
else
    fail "CMake installs doggy-lora unit and lora.html"
fi

if [ -f "$UNIT" ] && grep -Eq '^SupplementaryGroups=i2c( |$)' "$UNIT"; then
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

if [ -f "$ROOT/packaging/debian/config" ] && [ -f "$ROOT/packaging/debian/templates" ]; then
    pass "debconf config and templates exist"
else
    fail "debconf config and templates exist"
fi

if [ -f "$ROOT/packaging/debian/config" ] && sh -n "$ROOT/packaging/debian/config"; then
    pass "config is valid sh"
else
    fail "config is valid sh"
fi

if grep -q 'shaloms-doggy/reboot-now' "$ROOT/packaging/debian/templates"; then
    pass "templates ask to reboot after enabling I2C"
else
    fail "templates ask to reboot after enabling I2C"
fi

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

if [ "$1" = group ] && [ "$2" = dialout ]; then
    echo "dialout:x:20:"
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

cat >"$MOCK_BIN/chown" <<'EOF'
#!/bin/sh
printf 'chown %s\n' "$*" >>"$DOGGY_MOCK_LOG"
exit 0
EOF

cat >"$MOCK_BIN/curl" <<'EOF'
#!/bin/sh
printf 'curl %s\n' "$*" >>"$DOGGY_MOCK_LOG"
exit 1
EOF

cat >"$MOCK_BIN/dpkg" <<'EOF'
#!/bin/sh
printf 'dpkg %s\n' "$*" >>"$DOGGY_MOCK_LOG"
if [ "$1" = --print-architecture ]; then
    echo amd64
    exit 0
fi
exit 0
EOF

chmod +x "$MOCK_BIN/getent" "$MOCK_BIN/useradd" "$MOCK_BIN/usermod" "$MOCK_BIN/systemctl" "$MOCK_BIN/chown" "$MOCK_BIN/curl" "$MOCK_BIN/dpkg"

BOOT_ON="$TMPDIR/boot-on.txt"
printf 'dtparam=i2c_arm=on\n' >"$BOOT_ON"
mkdir -p "$TMPDIR/modules-load" "$TMPDIR/run"

run_script() {
    local script="$1"
    shift
    : >"$MOCK_LOG/commands"
    DOGGY_MOCK_STATE="$MOCK_STATE" \
    DOGGY_MOCK_LOG="$MOCK_LOG/commands" \
    DOGGY_BOOT_CONFIG="${DOGGY_BOOT_CONFIG:-$BOOT_ON}" \
    DOGGY_SKIP_DEBCONF=1 \
    DOGGY_REBOOT_REQUIRED="$TMPDIR/run/reboot-required" \
    DOGGY_REBOOT_PKGS="$TMPDIR/run/reboot-required.pkgs" \
    DOGGY_MODULES_LOAD_DIR="$TMPDIR/modules-load" \
    DOGGY_LOG_DIR="$TMPDIR/var-log-doggy" \
    DOGGY_CONFIG_DIR="$TMPDIR/etc-doggy" \
    DOGGY_RECORDINGS_DIR="$TMPDIR/var-lib-doggy/recordings" \
    DOGGY_SNAPSHOTS_DIR="$TMPDIR/var-lib-doggy/snapshots" \
    DOGGY_SKIP_MEDIAMTX_INSTALL=1 \
    DOGGY_SKIP_TLS=1 \
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

if grep -q 'systemctl enable doggy-lora.service' "$MOCK_LOG/commands"; then
    pass "postinst enables doggy-lora.service"
else
    fail "postinst enables doggy-lora.service"
fi

if grep -q -- '-aG dialout' "$MOCK_LOG/commands"; then
    pass "postinst adds doggy to dialout when the group exists"
else
    fail "postinst adds doggy to dialout when the group exists"
fi

if [ -d "$TMPDIR/var-log-doggy" ]; then
    pass "postinst creates the doggy log directory"
else
    fail "postinst creates the doggy log directory"
fi

if [ -d "$TMPDIR/etc-doggy" ]; then
    pass "postinst creates the doggy config directory"
else
    fail "postinst creates the doggy config directory"
fi

if grep -q 'chown doggy:doggy' "$MOCK_LOG/commands" \
        && grep -q "$TMPDIR/etc-doggy" "$MOCK_LOG/commands"; then
    pass "postinst chowns the config directory to doggy"
else
    fail "postinst chowns the config directory to doggy"
fi

cfg_mode=$(stat -c '%a' "$TMPDIR/etc-doggy" 2>/dev/null || true)
if [ "$cfg_mode" = "755" ]; then
    pass "postinst sets config directory mode 0755"
else
    fail "postinst sets config directory mode 0755"
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
    if grep -q -- '-aG i2c' "$MOCK_LOG/commands"; then
        fail "postinst skips i2c usermod when i2c group is missing"
    else
        pass "postinst skips usermod when i2c group is missing"
    fi
else
    fail "postinst skips usermod when i2c group is missing"
fi

rm -f "$TMPDIR/run/reboot-required" "$TMPDIR/run/reboot-required.pkgs"
printf 'arm_64bit=1\n' >"$TMPDIR/boot-missing.txt"
if DOGGY_BOOT_CONFIG="$TMPDIR/boot-missing.txt" run_script "$POSTINST" configure; then
    if grep -q '^dtparam=i2c_arm=on$' "$TMPDIR/boot-missing.txt" \
            && [ -f "$TMPDIR/run/reboot-required" ]; then
        pass "postinst appends dtparam=i2c_arm=on and flags reboot"
    else
        fail "postinst appends dtparam=i2c_arm=on and flags reboot"
    fi
else
    fail "postinst appends dtparam=i2c_arm=on and flags reboot"
fi

if [ -f "$TMPDIR/modules-load/shaloms-doggy-i2c.conf" ] \
        && grep -q '^i2c-dev$' "$TMPDIR/modules-load/shaloms-doggy-i2c.conf"; then
    pass "postinst loads i2c-dev via modules-load.d"
else
    fail "postinst loads i2c-dev via modules-load.d"
fi

rm -f "$TMPDIR/run/reboot-required"
printf '#dtparam=i2c_arm=on\n' >"$TMPDIR/boot-commented.txt"
if DOGGY_BOOT_CONFIG="$TMPDIR/boot-commented.txt" run_script "$POSTINST" configure; then
    if grep -q '^dtparam=i2c_arm=on$' "$TMPDIR/boot-commented.txt" \
            && grep -q '^#dtparam=i2c_arm=on$' "$TMPDIR/boot-commented.txt"; then
        fail "postinst uncomments dtparam=i2c_arm=on"
    elif grep -q '^dtparam=i2c_arm=on$' "$TMPDIR/boot-commented.txt"; then
        pass "postinst uncomments dtparam=i2c_arm=on"
    else
        fail "postinst uncomments dtparam=i2c_arm=on"
    fi
else
    fail "postinst uncomments dtparam=i2c_arm=on"
fi

rm -f "$TMPDIR/run/reboot-required"
printf 'dtparam=i2c_arm=off\n' >"$TMPDIR/boot-off.txt"
if DOGGY_BOOT_CONFIG="$TMPDIR/boot-off.txt" run_script "$POSTINST" configure; then
    if grep -q '^dtparam=i2c_arm=on$' "$TMPDIR/boot-off.txt"; then
        pass "postinst turns dtparam=i2c_arm=off into on"
    else
        fail "postinst turns dtparam=i2c_arm=off into on"
    fi
else
    fail "postinst turns dtparam=i2c_arm=off into on"
fi

rm -f "$TMPDIR/run/reboot-required" "$TMPDIR/run/reboot-required.pkgs"
if run_script "$POSTINST" configure; then
    if [ -f "$TMPDIR/run/reboot-required" ]; then
        fail "already-enabled I2C does not flag reboot"
    else
        pass "already-enabled I2C does not flag reboot"
    fi
else
    fail "already-enabled I2C does not flag reboot"
fi

rm -f "$MOCK_STATE/no_i2c"
: >"$MOCK_LOG/commands"
if [ -x "$PRERM" ] && run_script "$PRERM" remove; then
    if grep -q 'systemctl stop doggy.service' "$MOCK_LOG/commands" \
            && grep -q 'systemctl stop doggy-lora.service' "$MOCK_LOG/commands"; then
        pass "prerm stop doggy.service on remove"
    else
        fail "prerm stop doggy.service on remove"
    fi
else
    fail "prerm stop doggy.service on remove"
fi

: >"$MOCK_LOG/commands"
if [ -x "$PRERM" ] && run_script "$PRERM" upgrade; then
    if grep -q 'systemctl stop doggy.service' "$MOCK_LOG/commands"; then
        fail "prerm does not stop doggy.service on upgrade"
    else
        pass "prerm does not stop doggy.service on upgrade"
    fi
else
    fail "prerm does not stop doggy.service on upgrade"
fi

: >"$MOCK_LOG/commands"
if [ -x "$POSTRM" ] && run_script "$POSTRM" purge; then
    if grep -q 'systemctl disable doggy.service' "$MOCK_LOG/commands" \
            && grep -q userdel "$MOCK_LOG/commands"; then
        fail "postrm purge disables the unit and does not userdel"
    elif grep -q 'systemctl disable doggy.service' "$MOCK_LOG/commands" \
            && grep -q 'systemctl disable doggy-lora.service' "$MOCK_LOG/commands"; then
        pass "postrm purge disables the unit and does not userdel"
    else
        fail "postrm purge disables the unit and does not userdel"
    fi
else
    fail "postrm purge disables the unit and does not userdel"
fi

if [ -f "$MEDIAMTX_UNIT" ] \
        && grep -q '^User=doggy$' "$MEDIAMTX_UNIT" \
        && grep -q '/usr/lib/doggy/mediamtx' "$MEDIAMTX_UNIT"; then
    pass "MediaMTX service runs pinned doggy installation as doggy"
else
    fail "MediaMTX service runs pinned doggy installation as doggy"
fi

if [ -f "$MEDIAMTX_CONFIG" ] \
        && grep -q '^rtspAddress: 127.0.0.1:8554$' "$MEDIAMTX_CONFIG" \
        && grep -q '^apiAddress: 127.0.0.1:8887$' "$MEDIAMTX_CONFIG" \
        && grep -q '^webrtcAddress: :8889$' "$MEDIAMTX_CONFIG" \
        && grep -q '^webrtcLocalUDPAddress: :8189$' "$MEDIAMTX_CONFIG" \
        && grep -q '^webrtcEncryption: true$' "$MEDIAMTX_CONFIG"; then
    pass "MediaMTX keeps control local and exposes encrypted WebRTC"
else
    fail "MediaMTX keeps control local and exposes encrypted WebRTC"
fi

if [ -x "$MEDIAMTX_INSTALL" ] \
        && grep -q 'v1.21.0' "$MEDIAMTX_INSTALL" \
        && grep -q 'dpkg --print-architecture' "$MEDIAMTX_INSTALL" \
        && grep -q -- '--version' "$MEDIAMTX_INSTALL"; then
    pass "MediaMTX installer pins version and selects Debian architecture"
else
    fail "MediaMTX installer pins version and selects Debian architecture"
fi

MEDIAMTX_BIN="$TMPDIR/lib/doggy/mediamtx"
MEDIAMTX_VERSION_FILE="$TMPDIR/lib/doggy/mediamtx.version"
mkdir -p "$(dirname "$MEDIAMTX_BIN")"
cat >"$MEDIAMTX_BIN" <<'EOF'
#!/bin/sh
printf 'v1.21.0\n'
EOF
chmod +x "$MEDIAMTX_BIN"
: >"$MOCK_LOG/commands"
if DOGGY_MOCK_LOG="$MOCK_LOG/commands" \
        DOGGY_MEDIAMTX_BIN="$MEDIAMTX_BIN" \
        DOGGY_MEDIAMTX_VERSION_FILE="$MEDIAMTX_VERSION_FILE" \
        PATH="$MOCK_BIN:$PATH" \
        "$MEDIAMTX_INSTALL"; then
    if grep -q curl "$MOCK_LOG/commands"; then
        fail "MediaMTX installer skips download when the pinned version is present"
    elif grep -q '^v1.21.0$' "$MEDIAMTX_VERSION_FILE"; then
        pass "MediaMTX installer skips download when the pinned version is present"
    else
        fail "MediaMTX installer skips download when the pinned version is present"
    fi
else
    fail "MediaMTX installer skips download when the pinned version is present"
fi

cat >"$MEDIAMTX_BIN" <<'EOF'
#!/bin/sh
printf 'v1.15.6\n'
EOF
chmod +x "$MEDIAMTX_BIN"
: >"$MOCK_LOG/commands"
if DOGGY_MOCK_LOG="$MOCK_LOG/commands" \
        DOGGY_MEDIAMTX_BIN="$MEDIAMTX_BIN" \
        DOGGY_MEDIAMTX_VERSION_FILE="$MEDIAMTX_VERSION_FILE" \
        PATH="$MOCK_BIN:$PATH" \
        "$MEDIAMTX_INSTALL"; then
    fail "MediaMTX installer downloads when an older version is installed"
elif grep -q curl "$MOCK_LOG/commands"; then
    pass "MediaMTX installer downloads when an older version is installed"
else
    fail "MediaMTX installer downloads when an older version is installed"
fi

if grep -q '^SupplementaryGroups=i2c video$' "$UNIT" \
        && grep -q 'mediamtx.service' "$UNIT" \
        && grep -q 'avahi-daemon.service' "$UNIT" \
        && grep -q 'ReadWritePaths=/var/lib/doggy' "$UNIT"; then
    pass "doggy service has video access and starts after MediaMTX and Avahi"
else
    fail "doggy service has video access and starts after MediaMTX and Avahi"
fi

if grep -q '^pathDefaults:$' "$MEDIAMTX_CONFIG" \
        && grep -q '^  record: true$' "$MEDIAMTX_CONFIG" \
        && grep -q '^  recordFormat: mpegts$' "$MEDIAMTX_CONFIG" \
        && grep -q '^  recordSegmentDuration: 1h$' "$MEDIAMTX_CONFIG" \
        && grep -q '^  recordDeleteAfter: 0s$' "$MEDIAMTX_CONFIG" \
        && grep -q '^  recordPath: .*__HOSTNAME__.*%path' "$MEDIAMTX_CONFIG"; then
    pass "MediaMTX records hourly MPEG-TS under pathDefaults with %path"
else
    fail "MediaMTX records hourly MPEG-TS under pathDefaults with %path"
fi

if grep -q '^record:' "$MEDIAMTX_CONFIG" \
        || grep -q '^recordPath:' "$MEDIAMTX_CONFIG"; then
    fail "MediaMTX config avoids deprecated top-level record keys"
else
    pass "MediaMTX config avoids deprecated top-level record keys"
fi

if grep -q 'render-mediamtx-config.sh' "$MEDIAMTX_UNIT" \
        && grep -q 'ReadWritePaths=/var/lib/doggy' "$MEDIAMTX_UNIT" \
        && grep -q '/run/doggy/mediamtx.yml' "$MEDIAMTX_UNIT"; then
    pass "MediaMTX renders hostname into recordPath under ProtectSystem"
else
    fail "MediaMTX renders hostname into recordPath under ProtectSystem"
fi

CLEANUP_TIMER="$ROOT/packaging/doggy-cleanup-media.timer"
CLEANUP_UNIT="$ROOT/packaging/doggy-cleanup-media.service"
CLEANUP_SCRIPT="$ROOT/packaging/cleanup-media.sh"
RENDER_SCRIPT="$ROOT/packaging/render-mediamtx-config.sh"
if [ -f "$CLEANUP_TIMER" ] && grep -q '^OnCalendar=hourly$' "$CLEANUP_TIMER" \
        && grep -q 'cleanup-media.sh' "$CLEANUP_UNIT" \
        && grep -q 'doggy-cleanup-media.timer' "$POSTINST" \
        && grep -q 'ensure_media_dirs' "$POSTINST"; then
    pass "hourly media cleanup timer is packaged and enabled"
else
    fail "hourly media cleanup timer is packaged and enabled"
fi

RENDER_OUT="$TMPDIR/mediamtx-rendered.yml"
if DOGGY_HOSTNAME=pi-test \
        DOGGY_MEDIAMTX_TEMPLATE="$MEDIAMTX_CONFIG" \
        DOGGY_MEDIAMTX_RENDERED="$RENDER_OUT" \
        DOGGY_RECORDINGS_DIR="$TMPDIR/recordings" \
        DOGGY_SNAPSHOTS_DIR="$TMPDIR/snapshots" \
        sh "$RENDER_SCRIPT" \
        && grep -q 'pi-test-%path-%Y-%m-%d-%H%M%S' "$RENDER_OUT"; then
    pass "MediaMTX config render substitutes the hostname"
else
    fail "MediaMTX config render substitutes the hostname"
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
