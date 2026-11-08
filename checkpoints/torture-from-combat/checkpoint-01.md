# checkpoint-01: no torture from combat punches

Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `c16e0e2` (uncommitted work on top)

## Goal
Nosimo (see [checkpoint-00.md](checkpoint-00.md)) took torture damage in combat while fighting mages at Fellglow Keep. Healing in combat then made health jump while the torture healed.

## Findings (code only, no user log)
- No user log was supplied. The cause is inferred, not confirmed.
- Likely path: vanilla mages that run out of magicka fight with their fists. `ClassifyMelee` / `Classify` → `MatchUnarmed` → `UnarmedMatches` let the out-of-combat `punching` rule ([punching.json:12](../../SKSE/Plugins/Injury_Engine/injuries/punching.json#L12)) match in combat unless `injury_engine.body.punch_combat_health` was on, and it was off by default.
- Second, less likely path: a spell-distribution mod giving NPC mages the "Torture" spells (0x816–0x818). Those stay torture by design.

## Decisions (user, 2026-10-26)
- In combat, all damage is real, always, except the labeled torture sources (Torture Flames / Frostbite / Sparks, padded whip), which stay torture in or out of combat.

## Changes
- [Classifier.cpp](../../SKSE_Source/src/Classifier.cpp) `UnarmedMatches`: a `combat: false` rule never matches in combat. In-combat fists now fall to the `bludgeoning` unarmed rule ([bludgeoning.json:9](../../SKSE/Plugins/Injury_Engine/injuries/bludgeoning.json#L9)). Removed `g_punchCombatHealth` / `SetPunchCombatHealth` from the C++ (Classifier.h, Papyrus_Engine.cpp), the Papyrus native (`Injury_Engine_Engine.psc`), the `Main.ApplyPluginConfig` call, `manifest.yaml`, and README.
- Docs: KNOWLEDGEBASE "Torture punches and combat", CLAUDE.md Flow, and all three changelogs under 0.1.5 (untagged).
- Side effect: a `Start_Fighting` brawl is now real bludgeoning and can kill.
- Built: `make dll` → `SKSE/Plugins/Injury_Engine.dll` (09:07), `compile: papyrus` → `Scripts/*.pex` (09:08), both with no errors.

## Not yet tested
- All of it, in game.

## Next
1. In game, verbose logging on: fight an unarmed NPC or a mage out of magicka. `Injury_Engine.log` should show `melee hit weapon 00000000 ... -> bludgeoning`, with no torture bar.
2. `Torture_Target` punch out of combat: still `punching`, no combat started.
3. Torture Flames in combat: still `fire` torture.
4. `Start_Fighting` brawl: punches are `bludgeoning`.
5. Ask Nosimo for `Injury_Engine.log` (verbose) if torture still shows in combat with the new build. That would point to the spell-distribution path.
6. Commit when verified. The working tree also holds earlier unrelated edits (`InjuryEngine.cpp`, `CHANGELOG-player.md` healing lines).
