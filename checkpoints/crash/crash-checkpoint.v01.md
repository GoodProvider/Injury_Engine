# Handoff: game froze during combat (v01)

Status: **implemented, built, deployed, not tested in game, not committed.** `make dll` OK (`SKSE/Plugins/Injury_Engine.dll`, 08:57). No Papyrus or ESP changes. The truce-guard work from `checkpoints/truce-guard-handoff-checkpoint-v01.md` is still uncommitted in the same tree.

## Problem
The game froze (hung, no crash log) during the Bob / Angi fight on 2026-10-16 at 08:51:02.

## Findings (08:50–08:51 logs)
- Angi and Bob traded torture punches until 08:50:49. At 08:51:00 Bob switched to Flames (torture) on Angi.
- 08:51:01: `downed Angi (000CAB2E) at 17.7/150.0: bleedout`. 08:51:02: one more `fire` tick on her. Both `Injury_Engine.log` and `Papyrus.0.log` end there.
- `SkyrimNet.log` (its own threads) kept writing until 08:51:17. The process was alive, but the game thread was blocked: a deadlock, not a crash.
- No truce or stand-up lines were logged, so the new truce code did not run.

## Cause (confidence ~75%)
An ABBA lock order between `InjuryEngine::g_lock` and the engine's per-actor effect list:
- The 1 s healing scan task (`SyncHealth` → SKSE task) held `g_lock` and walked Angi's effects (`MagicTarget::VisitEffects`).
- A Flames (torture) tick runs inside the engine's effect update and waits for `g_lock` in the `ModifyEffectActorValue` hook (`OnDamage` → `OnTortureDamage`).

This was the only place where `g_lock` was held across an engine call that takes engine locks. Bleedout was probably only the moment it happened. Gap: it's unproven which thread runs effect ticks, and verbose logging was off, so there is no `scan effects` line to confirm it.

## Fix (`SKSE_Source/src/InjuryEngine.cpp`)
- `ScanActiveEffects` replaced by `CollectHealingEffects(actor)`. It walks the effects **without** `g_lock` and copies `{caster handle, item}` of each healing effect.
- The `SyncHealth` scan task checks the actor, collects, then locks `g_lock`, looks the actor up again in `g_actors` and calls `Seen` for each entry.
- `NoteHealingSource` calls `NotifyPapyrus()` (mod event `SendEvent`) after releasing `g_lock`.
- KNOWLEDGEBASE: new top entry "Freeze: g_lock held across VisitEffects".

## Next steps for the developer
1. MO2: no stale `Injury_Engine.dll` in overwrite. Turn on verbose logging (`injury_engine.debug.verbose_logging`).
2. Repeat: punch an NPC down, then hold Flames (torture) on them past bleedout for 30–60 s. Also heal them with Healing Hands while the fire continues.
3. Expect no freeze. `Injury_Engine.log` should show `scan effects <id>` about once a second, the fire ticks continuing, and `heal start` when healing begins.
4. Regression: a concentration heal still opens and ends a heal session (`heal start` / `heal stopped`).
5. If it freezes again, save the last ~30 lines of `Injury_Engine.log` (verbose on). Next suspects: `ConvertHealth` / `TakeHealth` changing health while holding `g_lock`.

## After it holds
Commit (leave `checkpoints/` out) together with or after the truce-guard work. Mark the KNOWLEDGEBASE entry confirmed.
