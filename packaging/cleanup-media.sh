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
import time
from pathlib import Path

config_path = os.environ.get("DOGGY_CLEANUP_CONFIG", "/etc/doggy/doggy.json")
hours = 24
try:
    with open(config_path, encoding="utf-8") as handle:
        hours = int(json.load(handle).get("media", {}).get("retain_hours", 24))
except Exception:
    hours = 24
if hours < 1:
    hours = 24

cutoff = time.time() - hours * 3600
staging_cutoff = time.time() - 3600


def purge(directory):
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


purge(os.environ["DOGGY_RECORDINGS_DIR"])
purge(os.environ["DOGGY_SNAPSHOTS_DIR"])
PY
