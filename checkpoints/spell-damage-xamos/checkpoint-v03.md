# Spell damage (Xamos): real carry reads missing health from the damage modifier — checkpoint v03 (2026-10-18)

## Status
Built and deployed: `make dll` → `SKSE/Plugins/Injury_Engine.dll` (22:13). Needs in-game verification. Nothing committed.

## Problem
Retest of v02 (sword + vanilla Flames on Skadi FE04B83C): no wound, no bar change. `logs/01-Papyrus.0.log` (22:01): 299 `Injury_Engine_Injury` events (every `OnDamage` sends one, point or not), no hit on the follower narrated; big hits still wound (Bob → Angi 22:02:34). The 22:01 DLL log was lost (`logs/01-Injury_Engine.log` is the 21:44 session; the live log was rewritten by the 22:09 restart).

## Cause
21:44 log, same follower: `health:579.96` with permanent max 550 (a temporary +30 Health still active) while `damageMod` grew -0.0 → -0.3 per Flames tick. v02's clamp `maxHealth - health - amount` was negative → carry reset to 0 on every hit.

## Fix (`InjuryEngine.cpp`, `OnDamage`)
`missing = -GetActorValueModifier(kDamage, kHealth) - amount` (same read as `HealthDamaged` in Hooks.cpp). New debug line `carry X from the last hit, Y still missing -> Z`. Docs: KNOWLEDGEBASE real-carry entry, CLAUDE.md, CHANGELOG / -developer / -player Unreleased.

## Note
The overlay shows torture damage only; real damage on an untortured follower never makes a bar (only a red segment on an actor with torture damage).

## Next step for the developer
**Copy `Injury_Engine.log` before restarting the game.**
1. MO2: overwrite has no stale Injury_Engine.dll.
2. Verbose on. Iron Sword on an unwounded follower: hit 1 `no new point` + `kept for the next hit`; hit 2 `from the last hit ... still missing` then `health: ... slashing ... points:1`.
3. Same with Courage / Fortify Health active on the follower (the failing case).
4. Flames for a few seconds: burning points.
5. One hit, wait for full regen, one hit: `still missing 0.00`, no wound.
6. Fire torture first, then sword: red segment in the bar.
7. Regression: Courage ending `[buff] ... shrink:true`, no wound; a big hit wounds at once.

## After it holds
Commit Hooks.cpp, InjuryEngine.cpp, KNOWLEDGEBASE, CLAUDE.md, changelogs and the DLL, separately from the in-progress `healed` decorator work. Send the DLL to Xamos.
