# Checkpoint 01: UBE races get draugr wound text

Repo `Injury_Engine` · branch `main` · baseline `1353cbd` · uncommitted

## Goal
A player on UBE (`UBE_AllRace.esp`) got humanoid_undead (draugr) wound text. Race dump line from the report:
`UBE_AllRace.esp 1B005735 00UBE_BretonRaceVampire -> humanoid_undead (keywords)`. Unknown races should resolve to
humanoid; a UBE race should resolve like the vanilla race it copies (drop `00UBE_`).

## Findings
`../UBE 2.0/UBE_AllRace.esp` RACE records, parsed read-only (KWDA):

| Races | Keywords | Old result |
|-------|----------|------------|
| `00UBE_<Race>` (8) | ActorTypeNPC, RaceCompatibility keyword | humanoid (keywords) |
| `00UBE_<Race>Vampire` (8) | ActorTypeNPC 0x13794, ActorTypeUndead 0x13796, Vampire 0xA82BB, RaceCompatibility keyword | **humanoid_undead** (keywords) |
| `00UBE_CustomRace01/02` | ActorTypeNPC | humanoid (keywords) |

The `ActorTypeUndead + ActorTypeNPC` keyword rule caught the vampire copies. Vanilla vampire races escape it only through
exact `races` entries (same issue as KNOWLEDGEBASE `BretonRaceChildVampire`). The reporter's guess, humanoid_vampire, is
wrong: that group is the Vampire Lord beast form. Their manual `races` list does work.

Logs: [logs/2026-11-20-08-56-19/Injury_Engine.log](logs/2026-11-20-08-56-19/Injury_Engine.log) (dev setup, no UBE; the
baseline race dump, to compare against after the change).

## Changes
- [races.json](../../SKSE/Plugins/Injury_Engine/races.json): new `race_strip_prefixes: ["00UBE_"]`; new keyword rule
  `Vampire + ActorTypeNPC -> humanoid` placed before the undead rule (covers any custom playable vampire race).
- [InjuryConfig.cpp](../../SKSE_Source/src/InjuryConfig.cpp) `LoadRaces` / `RaceGroupName`: reads `race_strip_prefixes`;
  after a failed exact match it tries the EditorID with each matching prefix removed against `races` (rule logged as
  `race (prefix 00ube_)`). The summary line now counts strip prefixes.
- Docs: `injuries/guide.md` (Groups), CLAUDE.md (Race groups), KNOWLEDGEBASE.md (race_contains / vampire entry),
  CHANGELOG.md (Unreleased / Fixes).
- `make dll` built and deployed `SKSE/Plugins/Injury_Engine.dll`.

## Not yet tested
- In game at all. Not tested with UBE installed.

## Next
1. Start the game (dev setup). Check `Injury_Engine.log`: `injuries: races.json: ... 1 strip prefixes ...`. Diff the
   `race dump:` lines against the snapshot: only vampire races that came from `(keywords)` may change, and they should
   now give humanoid.
2. Optional: enable `../UBE 2.0` in MO2. Expect `00UBE_BretonRaceVampire -> humanoid (race (prefix 00ube_))` and
   `00UBE_CustomRace01 -> humanoid (keywords)`. Wound a UBE vampire character and confirm the wound text is humanoid.
3. Reply to the reporter: the shipped races.json fixes it (the keyword rule works with the 0.2.1 DLL too), and their
   manual list can stay or go.
4. Commit when verified.
