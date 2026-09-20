#!/bin/sh
set -eu

src="${DOGGY_MEDIAMTX_TEMPLATE:-/etc/doggy/mediamtx.yml}"
dst="${DOGGY_MEDIAMTX_RENDERED:-/run/doggy/mediamtx.yml}"
recordings="${DOGGY_RECORDINGS_DIR:-/var/lib/doggy/recordings}"
snapshots="${DOGGY_SNAPSHOTS_DIR:-/var/lib/doggy/snapshots}"

host="${DOGGY_HOSTNAME:-$(hostname 2>/dev/null || printf 'doggy')}"
san=$(printf '%s' "$host" | tr -c 'A-Za-z0-9.-' '-' | sed 's/^[.-]*//;s/[.-]*$//')
if [ -z "$san" ]; then
    san=doggy
fi
case "$san" in
    [A-Za-z0-9]*) ;;
    *) san="h$san" ;;
esac

mkdir -p "$(dirname "$dst")" "$recordings" "$snapshots"
sed "s|__HOSTNAME__|$san|g" "$src" > "$dst"
