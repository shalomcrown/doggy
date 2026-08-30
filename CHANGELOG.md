# Changelog — doggy

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) · Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Guiding principle:** A changelog is written for humans — your users and teammates — not for machines.  
> Document *what changed and why it matters*, not what files you touched.

---

## [Unreleased]

> Work merged but not yet shipped. Move entries to a versioned section on release.

### Added
- In-process HTTPS on port 443 (cpp-httplib v0.52.0 + Mbed TLS). Port 80 only 301-redirects; it does not serve the page or APIs. A self-signed cert is created once under `/etc/doggy/` (`tls.crt` / `tls.key`). The unit keeps `NoNewPrivileges=yes` and adds `CAP_NET_BIND_SERVICE`.
- Power controls: `POST /api/system` (`restart` / `reboot` / `shutdown`) and `POST /api/system/pin`. LAN PIN is a salted SHA-256 hash via FetchContent Mbed TLS 3.6.7; the Unix password is never used. The DEB installs a polkit rule so user `doggy` can restart only `doggy.service` and reboot/poweroff.
- `GET`/`PUT /api/config` and a Configuration section on the page. Channel changes apply immediately; I2C bus/address apply on restart.
- JSON config at `DOGGY_CONFIG` or `/etc/doggy/doggy.json` (servo channels; I2C bus/address for the servo board, IMU, and ADS7830). Missing file is created with compiled defaults. `postinst` creates `/etc/doggy` owned by `doggy`.
- `GET /api/status` includes servo PWM/angle (`servos.items`); the page table follows that poll. Angle is `null` when PWM is 0 (startup or Off).
- Per-servo **Off** on the page (`POST /api/servos/{id}` `{ "enabled": false }`) writes PWM 0.
- `GET /api/status` includes the stamped `version`; the web title line and browser tab show `Doggy <version>`.
- `./install-prereqs.sh` auto-selects native vs cross from host arch (toolchain only; no sysroot).
- `./build.sh` packages a `.deb`: native `native-release` on aarch64/arm64, `ubuntu-aarch64-cross` on other hosts.
- CMake FetchContent zlib 1.3.1; I2C SMBus uses the kernel ioctl; IMU uses `Vec3` instead of dlib so cross builds need no Pi sysroot.
- Firmware main loop samples the body IMU and ADS7830 battery ADC every 200 ms; `GET /api/status` includes cached `imu` and `battery` readings and the page polls them five times per second.
- Process logs at `/var/log/doggy/doggy.log` (plog): roll on start and at the size cap, keep 10 files; gzip archives are `doggy-YYYY-MM-DD-HHMM.log.gz`; `/api` calls and failures are logged (successful `GET /api/status` is skipped so 5 Hz polling does not fill the log).
- Repo-root `./doggy` opens an SSH session as user `doggy` (`zssh` if present, else `ssh`); extra args are jump hosts; unknown-host prompts are disabled.
- `install-prereqs.sh` installs Raspberry Pi native build deps and Ubuntu aarch64 cross tools (Qt Creator and editors included by default; no Windows target).
- CMake presets `native-debug`, `native-release`, and `ubuntu-aarch64-cross`.
- `install.sh` copies a `.deb` to a remote host over SSH and installs it with `apt-get`.
- In-process web UI (cpp-httplib): Home button, servo table, sliders (100ms idle before send).
- `ServoBoard` is the PCA9685; `Servo` is one named channel with last commanded angle/PWM.
- DEB installs `doggy.service` to `/etc/systemd/system`, creates system user `doggy` if missing, and starts the UI on boot.
- Each build stamps version `1.0.0-YYYY-MM-DD-HHMM-<short-git-hash>` (binary, startup line, and DEB).
- `GET /api/status` and a page banner for hardware errors (I2C open failure no longer kills the process).
- DEB `postinst` enables `dtparam=i2c_arm=on` in the Pi boot config when missing and asks to reboot.

### Changed
- ⚠ Breaking: the UI default is `https://<pi>/` (443). `http://<pi>:8080` no longer serves the app. Port 80 only redirects. Override with `DOGGY_HTTPS_PORT` / `DOGGY_HTTP_PORT`.
- Shared helpers (`to_hex`, `hashes_equal`) live in `src/utils.cpp`; callers use those instead of local copies.
- Servo JSON `angle` is `null` when PWM is 0 (was `0`). The bundled page treats that as unknown.
- ⚠ Breaking: application sources moved to `src/`. Configure with the new CMake presets instead of listing files at the repo root.
- ⚠ Breaking: cpp-httplib is downloaded by CMake (no `third_party/cpp-httplib`). Configure needs network or `FETCHCONTENT_SOURCE_DIR_CPP_HTTPLIB`.
- ⚠ Breaking: installing the DEB enables and starts `doggy.service` as user `doggy` (stop a manual `./doggy` first, or `systemctl disable --now doggy`).
- Removed unused Arduino-style `gpioPin` helper (libgpiod v1, never called). Firmware talks to the dog over I2C only.

### Fixed
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
