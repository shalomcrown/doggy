# Changelog — doggy

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) · Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Guiding principle:** A changelog is written for humans — your users and teammates — not for machines.  
> Document *what changed and why it matters*, not what files you touched.

---

## [Unreleased]

> Work merged but not yet shipped. Move entries to a versioned section on release.

### Added
- Dual-board PlatformIO watch project for LilyGO T-Watch S3 and Waveshare
  ESP32-C6 Touch AMOLED 2.06. The first slice uses shared LVGL 9 screens,
  ESP-Touch v2 Wi-Fi provisioning, NTP, and an NVS-backed fixed UTC offset
  selected on the watch. `build-watch.sh` builds either or both boards without
  adding ESP32 toolchains to the normal Pi package build. Native/cross
  `install-prereqs.sh` installs PlatformIO Core >= 6.2.0 with `pipx` because
  Debian/Ubuntu apt still ships an incompatible 4.x package.
  `./install-watch-s3.sh` and `./install-watch-c6.sh` build and USB-flash
  each board.
- Always-on MPEG-TS camera recording (hourly MediaMTX segments), JPEG snapshots, list/download APIs, and hourly retention from `media.retain_hours` (default 24). File names are `<hostname>-<camera id>-YYYY-MM-DD-HHMMSS.{ts,jpg}`.
- `robot.turn_gain_min` (default 0.25) so low-speed steering is less twitchy while full-speed turns stay authoritative.

### Changed
- Rover **Drive** arcade-mixes steering: turn is added on the left and subtracted from the right, then both sides are scaled if either would leave `[-1, 1]`. Rest spin is scaled by `turn_gain_min`; at full speed the stick still uses full turn.
- Rover GCS heartbeat over Wi-Fi or LoRa: the rover page posts every 750 ms, `robot.gcs_timeout_s` is configurable from 2–60 seconds (default 3), and loss of the page or channel coasts every motor.
- `GET /api/status` now includes current Linux Unix time; both pages display it in UTC.

### Changed
- Rover **Drive** arcade-mixes steering: turn is added on the left and subtracted on the right, then both sides are scaled if either would leave `[-1, 1]`, so steering authority falls as speed rises without a fixed gain.
- **Breaking behavior:** one-shot rover drive and individual-motor commands no longer run indefinitely. API clients must call `POST /api/heartbeat` or reissue motion inside `robot.gcs_timeout_s`; telemetry status reads do not count as liveness.
- Rover Stop/Brake (and per-motor Coast/Brake) zero the on-page speed sliders after the existing command; `POST /api/stop` and `POST /api/brake` report commanded `speed`/`turn` as 0.

### Added
- The watch sleeps to save battery. After a minute without a touch it blanks the
  panel, drops the Wi-Fi radio, and halts the CPU; touching the screen wakes it,
  and the T-Watch S3 also wakes on a wrist raise. The timeout is configurable on
  the watch (15 s, 30 s, 1 min, 2 min, 5 min, or Never). Sleep is held off while
  ESP-Touch is listening, since pairing needs both the screen and the radio.
- The watch remembers its last five Wi-Fi networks and retries them newest
  first on boot and after every wake, so it reconnects without a phone. Only
  when all of them fail does it fall back to ESP-Touch v2. **Pair Wi-Fi** in
  settings starts ESP-Touch immediately instead of waiting the roster out. The
  roster lives in unencrypted NVS, the same exposure the ESP32 Wi-Fi stack's own
  stored credentials already carry.
- The T-Watch S3 keeps time across a power cycle using its PCF8563 calendar
  chip. Every NTP sync is copied to it and a cold boot seeds the clock from it,
  so the watch shows a real time before Wi-Fi comes up. The Waveshare C6 has no
  such chip and still waits for NTP.

### Changed
- The watch settings page scrolls and no longer saves as you touch it. Moving a
  roller edits a draft that reaches storage only on **Save**; **Cancel**,
  **< Clock**, swiping back, and falling asleep all discard it, so a pocket
  touch cannot quietly change the clock.
- The watch clock page is now white on black, with a status bar showing the
  joined Wi-Fi SSID, battery symbol and percentage, and a four-bar signal meter.
  The date includes the weekday, and a new line reports
  `Time synchronized HH:MM:SS ago` measured from the SNTP sync callback. The
  T-Watch S3 reads its AXP2101 gauge; the Waveshare C6 shows `--` until its
  battery path is confirmed.

### Fixed
- The watch clock page no longer reports the same sync twice. The network
  status line said `Time synchronized` while the line below it already read
  `Time synchronized HH:MM:SS ago`. The status line now describes only the
  network path and goes blank once the clock is good, so a later Wi-Fi drop
  still shows `Connecting to Wi-Fi` while the elapsed time keeps counting.
- The Waveshare C6 status bar is visible again. The bar was pinned to the top
  edge across the full panel width, and that panel is a rounded rectangle, so
  the SSID, battery, and signal meter sat under the corner mask. Each board now
  declares how many pixels of every screen edge its panel can hide, and both
  watch pages inset their content by that much: 48 px on the C6, 0 on the
  square T-Watch S3 display.
- ESP-Touch v2 provisioning now completes. `WiFi.beginSmartConfig()` enables
  ESP-Touch v2 encryption with a NULL key, so the watch ignored every
  provisioning packet and the phone app never found it. The watch starts
  SmartConfig through the IDF API with encryption off; leave the app's AES key
  field empty.
- Watch USB flash no longer dies after connecting. PlatformIO's pioarduino
  `esptool` 5.4.0 crashes in the progress bar (`_get_progress_print_file`);
  both boards pin 5.3.1 until that release is fixed. USB-Serial/JTAG does not
  need a BOOT-button sequence.
- Releasing the joystick now actually stops the rover. The status poll could write the still-moving speed/turn back into the sliders in the gap between centering the stick and the debounced `POST /api/drive`, so the rover kept turning in place. Drive commands are now posted with the values captured when they were queued, the poll leaves the sliders alone while a command is pending, and releasing the stick sends the stop immediately instead of one debounce later. Losing pointer capture, releasing the button off the pad, hiding the tab, or leaving the window all spring the stick back too.
- A rejected **Save** on the configuration page now says why. `PUT /api/config` adds an optional `message` to the error body naming the offending field (for example `config lora.txch out of range`), both pages show it instead of a bare "Save failed", and failed API calls are logged with their JSON error body instead of only the status code. Config-write failures still return a generic message because the reason contains the config file path — that reason goes to the log.

### Added
- Numeric LoRa `lbt` setting (0–255, default 0) on firmware and operator setup pages; the sidecar applies the exact value with `AT+LBT` before channel commands.
- Robot type `DOG` or `ROVER` in `doggy.json` (`robot.type`; missing type is a dog). Changing type on either page requires the system PIN, restarts `doggy.service`, and the UI tells you to refresh. A rover page (`rover.html`) has steering/speed sliders and a motor table; `POST /api/drive` now maps normalized speed to all enabled motors through the Adafruit bonnet while retaining turn for later steering.
- `doggy-lora` Go sidecar for a Waveshare USB-TO-LoRa-xF dongle: AT apply/monitor, then a framed protobuf + AES-256-GCM hop so the laptop operator can call the same `/api/*` as Wi‑Fi. `/api/lora` on the operator is local radio config (including write-only `air_key`). Channels are TXCH/RXCH 0–80 for HF or LF. Laptop clients: `./build-operator.sh` produces a separate amd64 Ubuntu DEB (`shaloms-doggy-lora-operator`) and a Windows NSIS installer with a LocalSystem `DoggyLoraOperator` service (HTTP on 127.0.0.1:8765).
- Domain models in `proto/doggy.proto`; CMake runs `protoc` for C++ (`doggy.pb.cc`) and Go (`doggy.pb.go`). HTTP uses ProtoJSON; LoRa uses binary `AirRequest`/`AirResponse`.
- In-process HTTPS on port 443 (cpp-httplib v0.52.0 + Mbed TLS). Port 80 only 301-redirects; it does not serve the page or APIs. A self-signed cert is created once under `/etc/doggy/` (`tls.crt` / `tls.key`). The unit keeps `NoNewPrivileges=yes` and adds `CAP_NET_BIND_SERVICE`.
- Power controls: `POST /api/system` (`restart` / `reboot` / `shutdown`) and `POST /api/system/pin`. LAN PIN is a salted SHA-256 hash via FetchContent Mbed TLS 3.6.7; the Unix password is never used. The DEB installs a polkit rule so user `doggy` can restart only `doggy.service` and reboot/poweroff.
- `GET`/`PUT /api/config` and a Configuration section on the page. Channel changes apply immediately; I2C bus/address apply on restart.
- JSON config at `DOGGY_CONFIG` or `/etc/doggy/doggy.json` (servo channels; I2C bus/address for the servo board, IMU, and ADS7830). Missing file is created with compiled defaults. `postinst` creates `/etc/doggy` owned by `doggy`.
- `GET /api/status` includes servo PWM/angle (`servos.items`); the page table follows that poll. Angle is omitted when PWM is 0 (startup or Off).
- Per-servo **Off** on the page (`POST /api/servos/{id}` `{ "enabled": false }`) writes PWM 0.
- `GET /api/status` includes the stamped `version`; the web title line and browser tab show `Doggy <version>`.
- `./install-prereqs.sh` auto-selects native vs cross from host arch (toolchain only; no sysroot).
- `./build.sh` packages a `.deb`: native `native-release` on aarch64/arm64, `ubuntu-aarch64-cross` on other hosts.
- CMake FetchContent zlib 1.3.1; I2C SMBus uses the kernel ioctl; IMU uses `Vec3` instead of dlib so cross builds need no Pi sysroot.
- Firmware main loop samples the body IMU and ADS7830 battery ADC every 200 ms; `GET /api/status` includes cached `imu` and `battery` readings and the page polls them five times per second.
- Process logs at `/var/log/doggy/doggy.log` (plog): roll on start and at the size cap, keep 10 files; gzip archives are `doggy-YYYY-MM-DD-HHMM.log.gz`; `/api` calls and failures are logged (successful `GET /api/status` is skipped so 5 Hz polling does not fill the log). Config saves also log `type` and `type_changed`.
- Repo-root `./doggy` opens an SSH session as user `doggy` (`zssh` if present, else `ssh`); extra args are jump hosts; unknown-host prompts are disabled.
- `install-prereqs.sh` installs Raspberry Pi native build deps and Ubuntu aarch64 cross tools (Qt Creator and editors included by default). `--mode windows` installs NSIS for the operator installer; firmware is still not a Windows target.
- CMake presets `native-debug`, `native-release`, and `ubuntu-aarch64-cross`.
- `install.sh` copies a `.deb` to a remote host over SSH and installs it with `apt-get`.
- In-process web UI (cpp-httplib): Home button, servo table, sliders (100ms idle before send).
- `ServoBoard` is the PCA9685; `Servo` is one named channel with last commanded angle/PWM.
- DEB installs `doggy.service` to `/etc/systemd/system`, creates system user `doggy` if missing, and starts the UI on boot.
- Each build stamps version `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (binary, startup line, and DEB).
- `GET /api/status` and a page banner for hardware errors (I2C open failure no longer kills the process).
- DEB `postinst` enables `dtparam=i2c_arm=on` in the Pi boot config when missing and asks to reboot.

### Fixed
- Config file failures now name the file and the rejected field instead of a bare `invalid config JSON` (for example `/etc/doggy/doggy.json: invalid config JSON: INVALID_ARGUMENT:(lora) listen_before_talk: Cannot find field.`). HTTP `PUT /api/config` still answers with the generic `bad_json` code.
- LoRa fragment retries now use bounded exponential backoff plus jitter, reducing synchronized retransmission failures when a radio defers transmission.
- LoRa LBT is no longer modelled as a boolean: the Waveshare command accepts an unsigned 8-bit value, and `AT+LBT=1` can prevent a noisy endpoint from transmitting. Existing configs must replace `listen_before_talk` with numeric `lbt`.
- Firmware no longer crashes at startup when I2C devices fail to open: startup now copies `getStatus()` before reading `errors` (C++20 does not keep that temporary alive for a range-for).
- LoRa helper now applies host serial settings: `lora.device` and `lora.baud` (default 115200, 8N1) before AT. USB-CDC may ignore baud; the port still has to be set.
- LoRa AT is sent one command at a time: 200 ms after `+++`, then wait for `OK` (or `ERROR`) before the next line.

### Changed
- Rover motor configuration replaces `motors.<name>.channel` with independent `pwm`, `in2`, and `in1` PCA9685 channels. Existing rover configs must add all three fields per motor and set the bonnet address to `0x60` if they persisted the former `0x40` default.
- ⚠ Breaking: `lora.air_key` is now always an 8–128 byte passphrase. Both ends derive the AES-256 key with SHA-256; 64 hex characters are hashed as a passphrase instead of decoded as a raw key. Upgrade both ends together and keep the same configured string.
- Log files under `/var/log/doggy/` are world-readable (`0644`); the directory is `0755`. Config saves log robot type and `type_changed` without the PIN.
- `./install.sh` multiplexes `scp` and `ssh` on one ControlMaster connection so the SSH login password is not asked again for the install step. Auto-detect installs the newest `shaloms-doggy_*.deb` (Pi firmware), not a laptop operator package even if that file is newer.
- Shared helpers (`to_hex`, `hashes_equal`) live in `src/utils.cpp`; callers use those instead of local copies.
- Servo JSON omits `angle` when PWM is 0 (was `null`). The bundled page treats a missing angle as unknown.
- ⚠ Breaking: C++/Go models come from `proto/doggy.proto` (`protoc`). HTTP is ProtoJSON; LoRa carries typed `AirRequest`/`AirResponse` instead of an HTTP-shaped envelope. I2C addresses are hex strings only.
- ⚠ Breaking: application sources moved to `src/`. Configure with the new CMake presets instead of listing files at the repo root.
- ⚠ Breaking: cpp-httplib is downloaded by CMake (no `third_party/cpp-httplib`). Configure needs network or `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB`.
- ⚠ Breaking: installing the DEB enables and starts `doggy.service` as user `doggy` (stop a manual `./doggy` first, or `systemctl disable --now doggy`).
- Removed unused Arduino-style `gpioPin` helper (libgpiod v1, never called). Firmware talks to the dog over I2C only.

- `--mode windows` on `install-prereqs.sh` also installs `nsis-common` (NSIS stubs/plugins: MUI2, nsExec). The `nsis` compiler package alone is not enough for CPack.
- Save, Set PIN, and the power buttons now show their result. The page status line is pinned to the bottom of the window instead of sitting at the top, where messages such as a wrong PIN or "Type saved" scrolled out of view.
- Cross-built `.deb` packages are Debian architecture `arm64` (not host `amd64`).
- `postinst` restarts `doggy.service` before the I2C reboot prompt; `prerm` no longer stops the unit on upgrade.
- `install()` now uses `RUNTIME DESTINATION bin` for the `doggy` executable.
- I2C headers compile under C++20: `i2c_interface.hpp` now includes `<cstdint>` so `uint8_t` is defined.

---

## [2.36.0] — 2026-06-09

### Added
- **Backwards compatibility policy** — every expert agent and playbook now checks for BC breaks before writing any code. A structured `⚠️ BC BREAK` notice (what changes, who is affected, severity, migration path) is required and blocks implementation until explicitly approved. Defined once in `BEST-PRACTICES.md`; enforced across all 9 experts, the Critic `[BC]` dimension, and 5 playbooks (add-feature, bug-fix, api-integration, release, refactor).
- **Critic `[BC]` review dimension** — 8th dimension added to the adversarial review. Checks API contract changes, field removals, env/config renames, exported interface changes, auth mechanism changes, and whether the BC notice was issued and approved. Non-migratable break = Critical; undocumented migratable break = High.
- **Team adoption presentation** — `presentation/team-adoption.html` (12-slide deck) and `presentation/STORY-PLAN.md` (story beats, live demo script, objection handling) for onboarding developer teams.

### Changed
- **Release playbook BC scan** — Step 3b now scans all commits since last tag for BC breaks before semver determination. Any BC break forces a Major bump; blocks if bump is downgraded without explicit approval.
- **Cursor multi-model documentation** — `.cursor/README.md` and `SYNC-POINTS.md` now document that model switches within Cursor (GPT-4o → Claude → Gemini) are invisible to the platform and should be treated as mini-handoffs: run session-end before switching models mid-task.

---

<!-- ─── VERSION HISTORY ─────────────────────────────────────────────────────────

Copy this block for each release (newest version always at the top):

## [X.Y.Z] — YYYY-MM-DD

### Added
- New capability that users can now do, didn't exist before

### Changed
- Existing behavior that works differently now (describe the delta, not the implementation)

### Deprecated
- Feature still works but will be removed in a future version — include migration path

### Removed
- Feature or endpoint removed; describe the replacement if one exists

### Fixed
- Bug that was affecting users: what broke, under what condition, now resolved

### Security
- Vulnerability patched (include CVE identifier if applicable)

─── AUTHORING RULES ──────────────────────────────────────────────────────────

✓  One line per change — if a change needs a paragraph, it belongs in the release notes
✓  User-visible only — skip internal refactors, test updates, CI tweaks unless they affect behavior
✓  Omit empty sections — if nothing was removed, drop the Removed section entirely
✓  Breaking changes → put in Removed or Changed AND mark clearly: "⚠ Breaking:"
✓  Link to issues/PRs inline where relevant: "Fixed crash on empty input (#123)"
✓  Use past tense: "Added", "Fixed", "Removed" — not "Adds", "Fixes", "Removes"
✓  Semver bump guide: Added/Changed new behavior = minor · Fixed only = patch · Breaking = major

─── EXAMPLE ENTRY ────────────────────────────────────────────────────────────

## [2.1.0] — 2026-03-15

### Added
- Export to PDF now supports password protection
- New `/api/v2/reports` endpoint with pagination and filtering

### Changed
- Dashboard load time reduced from ~4s to <400ms by switching to server-side pagination
- ⚠ Breaking: `/api/v1/reports` removed — migrate to `/api/v2/reports` (see docs/migration.md)

### Fixed
- Fixed crash when uploading files larger than 50 MB on slow connections (#412)
- Corrected timezone handling for users in UTC-offset regions (#389)

### Security
- Patched stored XSS vulnerability in comment field (CVE-2026-10234)

──────────────────────────────────────────────────────────────────────────────
-->

<!-- Version comparison links — update after each release (replace YOUR_ORG/YOUR_REPO) -->
<!-- [Unreleased]: https://github.com/YOUR_ORG/YOUR_REPO/compare/vLAST...HEAD -->
<!-- [X.Y.Z]:      https://github.com/YOUR_ORG/YOUR_REPO/compare/vPREV...vX.Y.Z -->
