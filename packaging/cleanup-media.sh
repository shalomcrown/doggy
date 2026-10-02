#!/bin/sh
set -eu

config="${DOGGY_CONFIG:-/etc/doggy/doggy.json}"
recordings="${DOGGY_RECORDINGS_DIR:-/var/lib/doggy/recordings}"
snapshots="${DOGGY_SNAPSHOTS_DIR:-/var/lib/doggy/snapshots}"

export DOGGY_CLEANUP_CONFIG="$config"
export DOGGY_RECORDINGS_DIR="$recordings"
export DOGGY_SNAPSHOTS_DIR="$snapshots"

python3 - <<'PY'
import json
import os
import sys
import time
from pathlib import Path

config_path = os.environ.get("DOGGY_CLEANUP_CONFIG", "/etc/doggy/doggy.json")
hours = 24
min_free_mb = 512
try:
    with open(config_path, encoding="utf-8") as handle:
        media = json.load(handle).get("media", {})
        hours = int(media.get("retain_hours", 24))
        min_free_mb = int(media.get("min_free_mb", 512))
except Exception:
    hours = 24
    min_free_mb = 512
if hours < 1 or hours > 168:
    hours = 24
if min_free_mb < 64 or min_free_mb > 1048576:
    min_free_mb = 512

now = time.time()
cutoff = now - hours * 3600
staging_cutoff = now - 3600
# MediaMTX flushes the open segment about once a second, so its mtime stays new.
active_cutoff = now - 180
floor_bytes = min_free_mb * 1024 * 1024

override_raw = os.environ.get("DOGGY_CLEANUP_FREE_BYTES", "")
override_free = None
if override_raw != "":
    try:
        override_free = int(override_raw)
    except ValueError:
        override_free = None
freed_override = 0


# ================================================================================

def free_bytes(directory):
    if override_free is not None:
        return override_free + freed_override
    st = os.statvfs(directory)
    return st.f_bavail * st.f_frsize


# ================================================================================

def purge_age(directory):
    root = Path(directory)
    if root.is_dir() == False:
        return
    for path in root.iterdir():
        if path.is_file() == False:
            continue
        name = path.name
        try:
            mtime = path.stat().st_mtime
        except OSError:
            continue
        staging = name.startswith(".dl-")
        if staging:
            if mtime < staging_cutoff:
                path.unlink(missing_ok=True)
            continue
        if mtime < cutoff:
            path.unlink(missing_ok=True)


# ================================================================================

def purge_recordings_for_space(directory):
    global freed_override
    root = Path(directory)
    if root.is_dir() == False:
        return
    try:
        available = free_bytes(directory)
    except OSError:
        return
    if available >= floor_bytes:
        return

    candidates = []
    for path in root.iterdir():
        if path.is_file() == False:
            continue
        if path.suffix != ".ts":
            continue
        if path.name.startswith(".dl-"):
            continue
        try:
            st = path.stat()
        except OSError:
            continue
        if st.st_mtime >= active_cutoff:
            continue
        candidates.append((st.st_mtime, path.name, st.st_size, path))
    candidates.sort()

    for _mtime, _name, size, path in candidates:
        if available >= floor_bytes:
            return
        try:
            path.unlink()
        except OSError:
            continue
        if override_free is not None:
            freed_override += size
        try:
            after = free_bytes(directory)
        except OSError:
            return
        if size > 0 and after <= available:
            print(
                    "cleanup: delete did not free space; leaving remaining recordings",
                    file=sys.stderr)
            return
        available = after

    if available < floor_bytes:
        print(
                "cleanup: free space still below min_free_mb; open recordings were left in place",
                file=sys.stderr)


purge_age(os.environ["DOGGY_RECORDINGS_DIR"])
purge_age(os.environ["DOGGY_SNAPSHOTS_DIR"])
purge_recordings_for_space(os.environ["DOGGY_RECORDINGS_DIR"])
PY
