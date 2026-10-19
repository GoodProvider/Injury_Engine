# Spell damage (Xamos): Courage verified; small real hits now add up — checkpoint v02 (2026-10-18)

## Status
Courage fix (v01) verified in game. New fix (real carry) built and deployed: `make dll` → `SKSE/Plugins/Injury_Engine.dll` (21:57). Needs in-game verification. Nothing committed.

## Problem
Developer test (`bug-logs/spell-damage-xamos/03-Injury_Engine.log`, Skadi FE04B83C, max health 550, sexual parts on): Courage on Skadi, walked around, let it run out; attacked with a sword ("no damage"); fire (torture) worked.

## Findings
- Courage: 21:45:42 `[buff] HandleHealthDamage ... value:-30 ... damageMod:0.0 shrink:true`, 21:47:01 `value:-25 ... health:580.0/550.0 ... shrink:true`. No `uncaused ... landed`, no wound. Fixed.
- Fire torture: ticks blocked at 0x20, points land, start narration sent. The internal `ModActorValue -30` at 21:47:45 is `SyncHealth` pulling health from 573 (over max after Courage ended) to 550 − 2 × 3.34. Fine.
- Sword: 4 Iron Sword hits (00012EB7), 3.0 health each, each `no new point (carry 2.96)`. Vanilla Flames (00012FCD) 0.04 per tick: `carry 0.04` 366 times. One real point is 550 / 165 ≈ 3.33 health. `OnDamage` erased the unwounded actor's fresh State (with its carry) after every hit, so no hit below one point could ever start a wound.

## Current fix (`SKSE_Source/src/InjuryEngine.cpp`)
- `g_realCarry` (FormID → float, not saved): in `OnDamage`, when `!AnyInjury(st)` erases the State, its `carry` is stored there (map cleared above 512 entries); debug `carry … kept for the next hit`.
- Next hit on an actor without a State: `st.carry = clamp(maxHealth - health - amount, 0, saved)`, entry removed. Regen / healing drain it through the clamp.
- `Erase`, co-save load and `Revert` clear `g_realCarry`.
- Docs: KNOWLEDGEBASE (new entry; Courage entry marked verified), CLAUDE.md (health paragraph), CHANGELOG / -player / -developer Unreleased.

## Known limits
- The clamp assumes health is already lowered (HandleHealthDamage). On the `ModActorValue` path it undercounts (safe: no invented wound).
- An actor hit, then wounded by torture before the next real hit, keeps a stale `g_realCarry` entry until erased; harmless because of the clamp.

## Next step for the developer
1. Deploy is done (DLL 21:57). In MO2, check that overwrite has no stale Injury_Engine.dll.
2. Verbose on. Iron sword on an unwounded follower: hit 1 `no new point (carry 2.96)` + `carry 2.96 kept for the next hit`; hit 2 `health: ... slashing ... points:1` and a hit narration.
3. Hit once, let health regenerate to full, hit once: no wound.
4. Vanilla Flames on a follower: a burning point once a point's worth of health is missing.
5. Regression: Courage still `shrink:true`, no wound; fire torture unchanged; a big single hit still wounds at once.

## After it holds
Commit Hooks.cpp, InjuryEngine.cpp (ResolveUncaused + real carry), KNOWLEDGEBASE, CLAUDE.md, changelogs and the DLL, separately from the in-progress `healed` decorator work (session.json, 0420_injury_looks.prompt, json-keys.md, prompts.md, guide.md and their CHANGELOG / CLAUDE lines). Send the DLL to Xamos.
