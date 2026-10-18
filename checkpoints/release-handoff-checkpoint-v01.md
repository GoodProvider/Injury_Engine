# Release 0.1.0 handoff checkpoint v01 (2026-10-16)

## Done
- Changelogs (`CHANGELOG.md`, `-developer`, `-player`): Unreleased + old whip-only 0.1.0 folded into one 0.1.0 describing the current tree. Links use `GoodProvider/Injury_Engine`.
- Docs aligned: `docs/developers/papyrus.md` (External_Damage, External_PartPoints, `injury_engine.truce_with`), `CLAUDE.md`, `FOMOD_source/info.xml` description, `release-guide.md` (URL, `SHELL=cmd.exe` note), `release-checkpoint.xml`.
- `check-version.py -v 0.1.0` passes. Papyrus recompiled. `make release SHELL=cmd.exe` → `versions/Injury_Engine 0.1.0.7z` (960 KiB, 61 files, FOMOD + core incl. DLL, ESP, scripts, injuries JSON, YAML actions, prompts, overlay, CHANGELOG-player.md).
- Commits `1f220bc`, `59bf0c8` on main (not pushed).

## Left for the maintainer
1. Optional smoke test: install the 7z in MO2 (disable the dev folder), load a save, check the overlay and a Torture_Target.
2. Rename the GitHub repo to `Injury_Engine` (origin is still `SkyrimNet_Whipped`) and update `origin`.
3. `git push`, `git tag 0.1.0`, `git push origin 0.1.0`.
4. GitHub Release `0.1.0`: attach the 7z, body = `CHANGELOG-player.md`.
