# checkpoint-02: 0.2.0 committed and packaged

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · commit `074a31e` · 2026-11-07.
Supersedes [checkpoint-01.md](checkpoint-01.md). Logs: [logs/2026-11-07-23-07-11/](logs/2026-11-07-23-07-11/)
([Injury_Engine.log](logs/2026-11-07-23-07-11/Injury_Engine.log), [Papyrus.0.log](logs/2026-11-07-23-07-11/Papyrus.0.log),
[MANIFEST.txt](logs/2026-11-07-23-07-11/MANIFEST.txt)).
Not pushed, not tagged, not published.

## Goal

The developer verified checkpoint-01 in game and asked for `git add`, `git commit`, `make release`.

## Results

- **Commit `074a31e`** on `main`: 64 files. `z-plan.md` was deliberately left out (standing rule: `z-*` is never committed)
  and is still modified.
- **`make release SHELL=cmd.exe`** exited 0: `versions/Injury_Engine 0.2.0.7z`, 1,986,530 bytes.
- **Archive checks** (`7z l`, 196 entries), all pass:
  - Top level: `FOMOD`, `core`, `fight_actions`, `images`, `overlay`.
  - 19 group folders in `core/SKSE/Plugins/Injury_Engine/injuries/`.
  - `core` has `Injury_Engine.dll`, `Injury_Engine.esp`, `CHANGELOG-player.md`; `info.json` says 0.2.0.
  - The three fight action YAMLs and two decision prompts are only in `fight_actions`.
  - `overlay` has the original `manifest.yaml` and `whipping.json`; core's `whipping.json` has `"overlay_enabled": false`.
  - No `z-*` files.

## Findings

- Load check in the 23:07 snapshot of `Injury_Engine.log`: 18 `group <id> loaded` lines plus the base humanoid group,
  no `not loaded: humanoid`, no `has no sexual_parts`. The log has no level tags, so it can't be grepped for errors in general.
- `make release` left these tracked files modified after the commit:
  - `SKSE/Plugins/Injury_Engine/info.json`: the version moves to 0.2.0. This one matters: it ships.
  - `Scripts/*.pex` (all 5): same size, 3 bytes differ at offsets 14-16 of the header, which is the compile timestamp.
    No code change.
  - The DLL and ESP match the commit.

## Not yet tested (in game)

- Part names sent from scripts on a non-human body and VR were still listed as untested in checkpoint-01. The developer's
  "verified" did not say whether it covered them; the changelog says VR is untested.
- The packaged 7z itself has not been installed through the FOMOD.

## Next

1. Commit what `make release` left (`info.json`, the `.pex` files, this checkpoint) so the tag matches the shipped files.
2. Install `versions/Injury_Engine 0.2.0.7z` through the FOMOD in a clean MO2 profile, if wanted.
3. Check tag `0.1.6`: it points at `c16e0e2` ("Rebuild 0.1.5 artifacts"), one commit before `c6cabaf` ("Prepare 0.1.6").
4. Update `origin` if it should no longer be `SkyrimNet_Whipped` (release guide), then push.
5. Tag `0.2.0` and publish the GitHub Release (attach the 7z, body = `CHANGELOG-player.md`).
