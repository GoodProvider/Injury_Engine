# Handoff: surrender accepted but the fight resumed (v01)

Status: **implemented, built, deployed, not tested in game, not committed.** `make dll` OK. Pyro compiled Actions, Engine, Main (0 failed).

## Problem
Angi yielded to the player (`Stop_Fighting_by_Yielding`), the Yes / No box came up and was accepted, but she started fighting again.

## Findings (2026-10-16 08:36–08:42 logs)
- 08:36:47 `StartFighting` Angi → Bob. Bob punched her into torture bleedout.
- 08:41:21 the yield; `truce: 000CAB2E stops fighting` came **while she was downed**. 08:41:23 the player accepted. For a surrender to the player, `ApplyTruce` did nothing more to Angi.
- 08:41:30 Bob healed her → `downed Angi recovered`. 08:41:33 she punched Bob.
- Aggression 0 doesn't end hostility she already has toward the player. Nothing stopped combat again after that one stop.

## Current fix
- `Classifier::NoteTruce(a, b, untilGameDays)` (`g_truce`, both directions; not saved). The `TESCombatEvent` sink stops any combat start between the pair until the truce ends and logs `truce: X kept from fighting Y`.
- `Classifier::OnStoodUp`: called from the bleedout release in `InjuryEngine::UpdateBleedout`. A truce actor that gets up stands down again (`truce: X stood up, standing down again`).
- `StopCombatNow(id, clearAnger)`: truce stops also clear `kAngryWithPlayer` (logs `no longer angry with the player` when it was set).
- `ForgetCalm` (Start_Fighting) drops the guard.
- Papyrus: new native `NoteTruce`. In `ApplyTruce`, an accepted truce or surrender arms the guard, and an accepted surrender stops the speaker again. `Calm(a, other)` stores `injury_engine.truce_with`. `RestoreExpiredTruces(True)` from `Setup` re-arms active guards on load.
- Docs: CLAUDE.md, docs/authors/actions.md, KNOWLEDGEBASE.

## Known limits
- The guard is per pair. A third actor who joins the fight against the yielder isn't covered.
- Clearing `kAngryWithPlayer` is a raw flag reset. It's unverified whether the game sets it in this scenario.

## Next step for the developer
1. MO2: no stale `Injury_Engine.dll` / `.pex` in overwrite. Load a save and turn on verbose logging.
2. Repeat: have an NPC `Start_Fighting` you, punch it into bleedout, let it yield, click Yes, heal it.
3. Expect no more punches. Check `Injury_Engine.log` for `truce: … until game day`, then `stood up, standing down again` and/or `kept from fighting`.
4. Regression: `Start_Fighting` from that NPC afterwards still fights. A refused surrender still lets the target attack.
5. Save and reload during the truce. The log shows `truce: … until game day` again on load.

## After it holds
Commit (leave `checkpoints/` out). Mark the KNOWLEDGEBASE entry confirmed and note which log path caught it.
