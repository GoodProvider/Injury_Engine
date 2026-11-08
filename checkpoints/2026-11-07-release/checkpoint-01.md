# checkpoint-01: 0.2.0 prepared (creature groups, group sexual text, Durnehviir, PartsText, release docs)

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `daa7091` · 2026-11-07.
Supersedes [checkpoint-00.md](checkpoint-00.md). Logs: [logs/2026-11-07-19-43-12/](logs/2026-11-07-19-43-12/)
([Injury_Engine.log](logs/2026-11-07-19-43-12/Injury_Engine.log), [MANIFEST.txt](logs/2026-11-07-19-43-12/MANIFEST.txt)).
Nothing is committed, packaged or tagged.

## Goal

Close the gaps listed in checkpoint-00, then prepare release 0.2.0 (0.1.7 was never released, so it folds into 0.2.0).

## Results

### Your checkpoint-00 answers

| Item | Status |
|------|--------|
| Old save from before race groups | ignored (your call) |
| Werewolf tail wounds heal | verified by you |
| Script part names land on the right part of a non-human body | **still untested** |
| Overlay on Magelight UI, 7-part bars | verified by you |
| VR | untested in game; the changelog says so |

### Gaps fixed

- **Remaining creatures.** 14 new group folders, each with `body.json` and `common.json`:
  - Dragons: `dragon` (lungs), `dragon_undead` (Durnehviir), `dragon_skeletal`, `spectral_dragon`. All four use the
    same 11 parts: head, neck, back, belly, two wings, four legs, tail.
  - `construct`, `elemental`, `spirit`, `plant`, `beast_humanoid` (lungs), `tentacled`, `crustacean`, `fish`,
    `bird` (lungs).
  - `humanoid_vampire`: humanoid parts plus wings, sexual parts and lungs.
  - No group has torture bleedout. Groups without lungs let drowning pass through.
  - Every race group that `races.json` names now has a folder, so no `[not loaded: humanoid]` should remain in the race dump.
  - The text was written with a generator that checks it: no digits, only known `{tokens}`, no pronouns in
    narration or death text, and 3 levels per region.
- **Sexual parts with their own text.** New `sexual_parts.json` files in `insectoid`, `humanoid_werewolf`,
  `humanoid_undead` and `humanoid_vampire`.
  - Small C++ change: `MergeSexualText` ([InjuryConfig.cpp:402](../../SKSE_Source/src/InjuryConfig.cpp#L402)) now also
    reads a `health` / `torture` block by the source's model, for any source without its own `text.<id>` entry.
  - So each group needs 75 lines, not about 700.
- **Durnehviir.** I read Dawnguard.esm directly: `DLC1Durnehviir` is 0x0030D8 and `DLC1DurnehviirSummon` is 0x00C71A,
  both on `DLC1UndeadDragonRace` (0x0117DE).
  - Both IDs are now `races.json` `actors` entries for `dragon_undead`.
  - The same check showed that Skyrim.esm 0x1052A3 is `UndeadDragonRace`. It used to map to the living `dragon` group
    ([Injury_Engine.log](logs/2026-11-07-19-43-12/Injury_Engine.log), the line
    `UndeadDragonRace -> dragon (race_form)`). It now maps to `dragon_skeletal`.
  - I also relabeled the race form Dragonborn.esm 0x1B637 from "draugr" to `DLC2AshSpawnRace`. Its group is unchanged
    (`humanoid_undead`).
- **`Main.PartsText`.** It now takes the victim and loops over 16 parts. New native
  `PartNameFor(actor, part)` ([InjuryEngine.cpp](../../SKSE_Source/src/InjuryEngine.cpp) next to `PartName`, registered
  in [Papyrus_Engine.cpp](../../SKSE_Source/src/Papyrus_Engine.cpp)). All callers (`PartsOf` and the `Damage` /
  `TortureText` / `Strike` traces) pass the victim.

### Release 0.2.0

- Version sources (Makefile, manifest.json, manifest.yaml, CMakeLists.txt, version.rc) are 0.2.0, and
  `check-version.py -v 0.2.0` passes.
- Changelogs:
  - `CHANGELOG.md` 0.2.0 (since 0.1.6) replaces the 0.1.7 section, which is folded in.
  - `CHANGELOG-developer.md` has 9 bullets.
  - `CHANGELOG-player.md`: every bullet is 100 characters or less.
- Docs: `injuries/guide.md` (Groups, the `sexual_parts.json` model blocks, `PartNameFor`), `CLAUDE.md` (shipped groups,
  Durnehviir, PartsText), `KNOWLEDGEBASE.md` (Durnehviir verified, the 0x1052A3 mislabel, sexual fallback), `README.md`.
- `release-checkpoint.xml`: `next_version` 0.2.0, status updated.
- Builds: `make dll` is clean (`SKSE/Plugins/Injury_Engine.dll` 19:57, version 0.2.0, contains `PartNameFor`).
  `make papyrus` compiled all 5 scripts (19:56). Every JSON file parses.

## Not yet tested (in game)

- The new groups load. Expect `group <id> loaded (...)` lines for all 19 groups, no
  `has no sexual_parts.json` warning, and the `race dump:` lines without `[not loaded: humanoid]`.
- A dragon, a skeletal dragon, Durnehviir (Soul Cairn or the summon), a troll, a spriggan, a Dwarven spider, an
  atronach, a wisp, a mudcrab and a chicken each get wounds with their own part names and text.
- Sexual-part text on a werewolf, draugr, spider or vampire lord (setting `include_sexual_parts` on).
- `PartsText` in `Papyrus.0.log` (verbose logging) names non-human parts.
- Carried over: part names sent from scripts (`External_Torture`, `Torture_Target`) on a non-human body. VR.

## Next

1. Deploy check: MO2 loads this folder, with no stale `Injury_Engine` DLL or `.pex` in overwrite.
2. Start the game with verbose logging on and check the `Injury_Engine.log` load lines (see above).
3. Fight a few of the creatures above. Torture one with `Torture_Target` and with the Torture Flames spell.
4. Capture `Injury_Engine.log` and `Papyrus.0.log`.
5. If everything is fine: commit, then `make release SHELL=cmd.exe`, inspect `7z l`, check where tag 0.1.6 points,
   tag `0.2.0`, and publish (body = `CHANGELOG-player.md`).
