# Changelog — doggy

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) · Versioning: [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Guiding principle:** A changelog is written for humans — your users and teammates — not for machines.  
> Document *what changed and why it matters*, not what files you touched.

---

## [Unreleased]

> Work merged but not yet shipped. Move entries to a versioned section on release.

### Added
- `scripts/install-prereqs.sh` installs Raspberry Pi native build deps and Ubuntu aarch64 cross tools (Qt Creator and editors included by default; no Windows target).
- CMake presets `native-debug`, `native-release`, and `ubuntu-aarch64-cross` (cross requires a Pi sysroot).

### Changed
- ⚠ Breaking: application sources moved to `src/`. Configure with the new CMake presets instead of listing files at the repo root.
- Removed unused Arduino-style `gpioPin` helper (libgpiod v1, never called). Firmware talks to the dog over I2C only.

### Fixed
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
