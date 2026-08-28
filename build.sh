#!/usr/bin/env bash
#
# build.sh
#
# Configure and package doggy. On aarch64/arm64 hosts this is a native
# Raspberry Pi release build; on any other arch it cross-compiles with the
# ubuntu-aarch64-cross preset (no sysroot).
#
# Usage:
#   ./build.sh [options]
#
# Host arch is uname -m; override with DOGGY_HOST_MACHINE (tests).
#
# Options:
#   --dry-run     Print cmake commands, do not execute
#   -h, --help    Show this help
set -eu
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
DRY_RUN=0
# ── colours ───────────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    C_RED='\033[0;31m'; C_YELLOW='\033[1;33m'; C_RESET='\033[0m'
else
    C_RED=''; C_YELLOW=''; C_RESET=''
fi
die() { printf '%b[ERROR]%b %s\n' "$C_RED" "$C_RESET" "$1" >&2; exit 1; }
run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '%b[DRY-RUN]%b  %s\n' "$C_YELLOW" "$C_RESET" "$*"
        return 0
    fi
    "$@"
}
# ── arg parsing ───────────────────────────────────────────────────────────────
while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help)
            grep '^#' "$0" | grep -v '^#!' | sed 's/^# \{0,1\}//'
            exit 0 ;;
        -*) die "Unknown argument: $1 (try --help)" ;;
        *) die "Unknown argument: $1 (try --help)" ;;
    esac
done
# ── preset ────────────────────────────────────────────────────────────────────
HOST_MACHINE="${DOGGY_HOST_MACHINE:-$(uname -m)}"
case "$HOST_MACHINE" in
    aarch64|arm64) PRESET=native-release ;;
    *) PRESET=ubuntu-aarch64-cross ;;
esac
run cmake --preset "$PRESET"
run cmake --build --preset "$PRESET" --target package
