#!/usr/bin/env bash
# Regression: operator packaging is a separate amd64 DEB / NSIS client, not firmware.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FAILS=0
CMAKE="$ROOT/CMakeLists.txt"
OP="$ROOT/cmake/operator-package.cmake"
SCRIPT="$ROOT/build-operator.sh"

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; FAILS=$((FAILS + 1)); }

if [ -x "$SCRIPT" ]; then
    pass "build-operator.sh is executable"
else
    fail "build-operator.sh is executable"
fi

if grep -q 'DOGGY_OPERATOR_PACKAGE' "$CMAKE" \
        && grep -q 'cmake/operator-package.cmake' "$CMAKE"; then
    pass "firmware CMake can return into operator-package.cmake"
else
    fail "CMake can return into operator-package.cmake"
fi

if grep -q 'DOGGY_LORA_GOOS windows' "$OP" \
        && grep -q 'GOARCH' "$OP"; then
    pass "operator package cross-compiles Go for windows/amd64"
else
    fail "operator package cross-compiles Go for windows/amd64"
fi

if grep -q 'mingw' "$OP"; then
    fail "operator CMake must not require MinGW"
else
    pass "operator CMake does not use MinGW"
fi

if grep -q 'set(CPACK_GENERATOR "NSIS")' "$OP" \
        && grep -q 'sc create DoggyLoraOperator' "$OP" \
        && grep -q '127.0.0.1:8765' "$OP"; then
    pass "NSIS creates DoggyLoraOperator listening on localhost"
else
    fail "NSIS creates DoggyLoraOperator listening on localhost"
fi

if grep -qi 'LocalService' "$OP"; then
    fail "Windows service must not use LocalService (USB serial)"
else
    pass "Windows service is not LocalService"
fi

if grep -q 'shaloms-doggy-lora-operator' "$OP" \
        && grep -q 'set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")' "$OP"; then
    pass "Ubuntu operator DEB is shaloms-doggy-lora-operator amd64"
else
    fail "Ubuntu operator DEB is shaloms-doggy-lora-operator amd64"
fi

if grep -q 'packaging/operator/debian/postinst' "$OP"; then
    pass "operator DEB has its own maintainer scripts"
else
    fail "operator DEB has its own maintainer scripts"
fi

POSTINST="$ROOT/packaging/operator/debian/postinst"
if grep -q 'doggy-lora-operator.service' "$POSTINST" \
        && grep -q '/var/lib/doggy-lora' "$POSTINST"; then
    pass "operator postinst enables operator unit and config dir"
else
    fail "operator postinst enables operator unit and config dir"
fi

if grep -q 'doggy.service' "$POSTINST"; then
    fail "operator postinst must not start doggy.service"
else
    pass "operator postinst does not start doggy.service"
fi

if grep -q 'doggy-lora-operator-${DOGGY_VERSION}-win64' "$ROOT/cmake/cpack-stamp.cmake"; then
    pass "NSIS artifact name includes doggy-lora-operator and win64"
else
    fail "NSIS artifact name includes doggy-lora-operator and win64"
fi

UNIT="$ROOT/packaging/doggy-lora-operator.service"
if grep -q '127.0.0.1:8765' "$UNIT" \
        && grep -q 'config-file=/var/lib/doggy-lora/lora.json' "$UNIT"; then
    pass "operator unit binds localhost and uses /var/lib/doggy-lora"
else
    fail "operator unit binds localhost and uses /var/lib/doggy-lora"
fi

if grep -q 'golang.org/x/sys' "$ROOT/lora/go.mod"; then
    pass "go.mod includes golang.org/x/sys for Windows SCM"
else
    fail "go.mod includes golang.org/x/sys for Windows SCM"
fi

if grep -q 'google.golang.org/protobuf' "$ROOT/lora/go.mod"; then
    pass "go.mod includes protobuf for generated models"
else
    fail "go.mod includes protobuf for generated models"
fi

if grep -q 'windowsServiceName' "$ROOT/lora/cmd/doggy-lora/service_windows.go" \
        && grep -q 'svc.Run' "$ROOT/lora/cmd/doggy-lora/service_windows.go"; then
    pass "Windows build uses SCM svc.Run"
else
    fail "Windows build uses SCM svc.Run"
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
if (cd "$ROOT/lora" && CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -o "$TMPDIR/doggy-lora.exe" ./cmd/doggy-lora); then
    pass "GOOS=windows GOARCH=amd64 go build succeeds"
else
    fail "GOOS=windows GOARCH=amd64 go build succeeds"
fi

if [ "$FAILS" -ne 0 ]; then
    printf '%d test(s) failed\n' "$FAILS" >&2
    exit 1
fi
echo "All operator-package tests passed."
exit 0
