# Release 0.1.1 handoff checkpoint v02 (2026-10-17)

## Done
- Version 0.1.0 → 0.1.1 in Makefile, manifest.json, manifest.yaml, CMakeLists.txt, version.rc; `check-version.py -v 0.1.1` passes; DLL FileVersion 0.1.1.0.
- Changelogs: 0.1.1 section on top of `CHANGELOG.md`, `CHANGELOG-developer.md`, `CHANGELOG-player.md` (FOMOD options for fight actions and overlay, installer image). The installer-options bullet was moved out of the player 0.1.0 section into 0.1.1.
- `release-checkpoint.xml`: base_tag 0.1.0 (untagged), next_version 0.1.1.
- Papyrus compiled (papyrus.exe, 5 files). `make release SHELL=cmd.exe` → `versions/Injury_Engine 0.1.1.7z` (1834 KiB, 64 files).
- Verified 7z: core has only `Torture_Target.yaml`, overlay default false in core `manifest.yaml` / `whipping.json`; `fight_actions/` has 3 YAMLs + 2 decision prompts; `overlay/` has originals (true); FOMOD 0.1.1; staging folders removed.
- Nothing committed (test build).

## Known nit
- core keeps an empty `prompts/decisions/injury_engine/` folder after the fight prompts are moved out (harmless).

## Left for the maintainer
1. Test in MO2 (disable the dev folder): install with both options, then with both off.
2. If good: commit, tag, GitHub Release (body = `CHANGELOG-player.md`).
