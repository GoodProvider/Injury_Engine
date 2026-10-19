# Spell damage (Xamos): buff ending read as a wound — checkpoint v01 (2026-10-18)

## Status
Fix built (`make dll` → `SKSE/Plugins/Injury_Engine.dll`, 21:42). Not committed. Needs in-game verification.

## Problem
Xamos (0.1.2, including 07ad3b3): "just cast Courage on her and walked for a minute or two". Camilla Valerius got a severe generic wound on face and both arms ("legs begin to give"), then "fully healed" a moment later.

## Findings (`bug-logs/spell-damage-xamos/02-Injury_Engine-02.log`)
- Fall part fixed: `drop:647 ... max_drop:100 -> not falling` (stale fall height rejected).
- 00:24:52: `HandleHealthDamage victim:00013488 attacker:0 damage:-30`, during a refresh of the mod Courage effect `851E56CE` (cast by the player about every second, Ocato's Recital).
- No `[buff]` and no `recover: buff change` lines (verbose was on): this buff ending reaches neither the ValueModifierEffect 0x20 hook nor the vtable `ModActorValue(kTemporary)`. It arrives only as an attacker-less HandleHealthDamage.
- 00:24:54: `uncaused ... sum:30 stuck:30 -> landed`. `top` was health before the drop (115); health afterwards was 85/85, which is full.

## Current fix
- `Hooks.cpp`: `HealthDamaged(actor)` = `-GetActorValueModifier(kDamage, kHealth)`. An attacker-less HandleHealthDamage whose loss is more than `HealthDamaged + 0.5` is max health shrinking. `BuffChange(..., shrink=true)` marks it, logs `[buff] ... damageMod:.. shrink:true` and passes it through before Classify. The `[hook] HandleHealthDamage` debug line now has `damageMod`.
- `InjuryEngine.cpp` `ResolveUncaused`: `stuck = clamp(min(top, maxHealth) - health, 0, sum)`.
- KNOWLEDGEBASE (the 2026-10-18 entry's "Unverified" bullet is now answered), CLAUDE.md, the CHANGELOG / -player / -developer "Unreleased" sections.

## Known limits
- Assumes the damage modifier is already applied when HandleHealthDamage runs (KB: "HandleHealthDamage runs after the damage"). If a real attacker-less source ever reports before applying it, that source would be passed through unwounded. Watch for `shrink:true` on real falls or drowning.
- An overkill uncaused hit whose modifier clamps passes through (no wound, no narration).

## Next step for the developer
1. Deploy is done (DLL in place). In MO2, check that overwrite has no stale Injury_Engine.dll.
2. Turn verbose logging on. Cast Courage (or any Fortify Health) on a follower at full health and wait for it to refresh or wear off. Repeat at partial health.
3. Expect in `Injury_Engine.log`: `[buff] HandleHealthDamage ... damageMod:0.0 ... shrink:true`, no `uncaused ... landed` and no `health: ... generic`. No narration in game.
4. Regression: console on an NPC `<ref>.damageav health 30` should still log `uncaused ... landed` (damageMod at least 30). A real fall still logs `landing`. Drowning still classifies.
5. Send the DLL to Xamos for the same Courage / Ocato test.

## After it holds
Commit (Hooks.cpp, InjuryEngine.cpp ResolveUncaused line, docs, DLL) separately from the in-progress `healed` decorator work in the same tree. Drop the "needs in-game verification" tag from the KB heading.
