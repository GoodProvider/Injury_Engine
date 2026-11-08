# checkpoint-06 — quadruped and races.json fixes verified; what is left before 0.1.7

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` (3 ahead of origin) · baseline `bec9514` · 2026-11-07.
Supersedes [checkpoint-05.md](checkpoint-05.md). Logs: [logs/2026-11-07-19-32-38/](logs/2026-11-07-19-32-38/)
(`Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`).

## Verified (in game, user + log)

- [Injury_Engine.log:36](logs/2026-11-07-19-32-38/Injury_Engine.log):
  `group quadruped loaded (8 parts: 7 core, 0 sexual, lungs yes; 2 own files; disabled 0)`. The count of "2 own
  files" is expected: `falling.json` patches both halves (`falling` torture + `falling_real` health).
- Race dump:
  - `GoblinRaceDuplicate` → humanoid (default), line 44
  - `BretonRaceChildVampire` → humanoid (race), line 51
  - `ElytraRace` / `_PET` → insectoid, lines 119-120
  - `BoneColossusRace` → humanoid_undead, line 138
  - `HorseRace` → quadruped, line 155
  - `WightRace` → humanoid_undead, line 230
  - `WolfRace` → quadruped, line 239
- A wolf was tracked as quadruped at line 2955 (19:20:33). The user confirmed the behavior in game.
- Line 1067: `group bird is not loaded` (chicken). Expected: the group is not shipped yet.

## Release state (0.1.7)

- Every version source is already 0.1.7. Tag `0.1.6` exists; 0.1.7 is not tagged, packaged or pushed.
- `CHANGELOG*.md` 0.1.7 only covers the Magelight UI move. Everything else since 0.1.6 is missing (see Next).
- `make release` already reads `injuries/humanoid/whipping.json` (Makefile:48-49). `races.json` and the group folders
  ship with `SKSE/` in `core/`.

## Next (before release)

See the final message of this session. In short:
1. Write the changelogs (all three) for the 0.1.7 delta.
2. Run the outstanding in-game checks (co-save v7 → v8, werewolf tail healing, overlay on Magelight).
3. Update `release-checkpoint.xml`, run `check-version.py`, then `make release`, check the tag 0.1.6 history, tag,
   push and publish.
