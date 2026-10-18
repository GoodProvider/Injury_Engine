# FOMOD options handoff checkpoint v01 (2026-10-17)

## Done
- `FOMOD_source/ModuleConfig.xml`: module image `images\injury-engine.jpg`; step "Optional", SelectAny group "Options" with two Recommended (pre-checked) plugins:
  - "Add fight/stop/yield actions" → folder `fight_actions` (Start_Fighting / Stop_Fighting / Stop_Fighting_by_Yielding YAMLs + `prompts/decisions/injury_engine/accept_truce|accept_surrender.prompt`).
  - "Show torture overlay" → folder `overlay` (priority 1): original `manifest.yaml` + `whipping.json` (overlay default true).
- `python_scripts/fomod-overlay-off.py`: flips `injury_engine.overlay.enabled` `defaultValue` and `defaults.overlay_enabled` to false in core's staged copies (keeps line endings; exits 1 unless exactly one match each).
- `Makefile` `release`: stages `fight_actions/` (moved out of core) and `overlay/`, runs the script, packs `FOMOD core images fight_actions overlay`, cleans up. `.gitignore`: `fight_actions`, `overlay`.
- Docs: README Install, release-guide `make release` line, CHANGELOG-player Requirements bullet.
- Test build: `make release SHELL=cmd.exe RELEASE_FILE=<scratch>/test.7z` succeeded; archive checked (core default false, overlay/ true, fight files only in fight_actions/, ModuleConfig parses).

## Left for the maintainer
1. Install the 7z in MO2: image shows, both boxes pre-checked; install once with each unchecked and check `actions/` and the dashboard default.
2. `versions/Injury_Engine 0.1.0.7z` was NOT rebuilt; run `make release SHELL=cmd.exe` when ready.
3. Commit (not done).

## Caveat
A dashboard value already saved in SkyrimNet `settings.yaml` (overwrite) overrides the installer's default.
