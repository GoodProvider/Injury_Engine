# Handoff: narrations wait for SkyrimNet's reply (v01)

Status: **implemented, compiled, deployed, not tested in game, not committed.** Baseline `7763f6c`. Pyro compiled `Injury_Engine_Main` (1 succeeded, 0 failed). No DLL / ESP change.

## Problem
Too many narrations / events in a row with no SkyrimNet dialogue in between. Wanted pattern: narration (or event) → dialogue → narration → dialogue, unless the dialogue stays silent for a long time.

## Findings (2026-10-15 22:43–22:50 logs, Bob → Angi)
- The only gate was `GetSpeechQueueSize() == 0`, and the player path skipped it (`PurgeDialogue(true)` + direct). The queue is empty while the LLM is still generating, so the next line went out before the reply. The purge also dropped the queued reply to the previous line.
- Duplicate starts: 22:44:41 / 22:44:42 and twice at 22:47:32 gave two stacked replies at 22:47:33. `NarrateHits` (OnInjury) and `SceneTick` (OnUpdate) raced on the same `SC_PENDING` pair.
- `SceneTick` could send a switch, a start and an again in the same tick.

## Current fix (`Injury_Engine_Main.psc`)
- `ClaimNarration()` is the reply gate, a test-and-set claim (`narration_claim`). It opens when either:
  - an audio line ended ≥ `REPLY_MIN_DELAY` (1.5 s) after `last_direct_time` and the speech queue is empty (`GetTimeSinceLastAudioEnded`), or
  - there has been `reply_timeout` (new setting `injury_engine.narration.reply_timeout`, default 15 s) of silence.
- `DirectNarrate` stamps `last_direct_time` (through `NoteDirect`) and releases the claim. Unused claims call `ReleaseNarration`. A claim older than 2 s is taken over.
- Torture: start / switch lines wait while the gate is closed (the pair stays pending / flagged). `SceneTick` sends at most one line per tick (switch, else start, else again). `OptionalDirectNarrate` is removed.
- Heal: a held start line waits in the new parallel list `KEY_HS_PEND`, and `HealTick` sends it when the gate opens. A held again line is skipped.
- Real hits and deaths: gate closed → their existing short-lived events.
- External API narrations (`Strike`, `External_Injure`, `External_Heal`) are never held back, but they close the gate.
- Settings, docs and knowledgebase updated: `manifest.yaml`, `whipping.json` `defaults.reply_timeout`, README, docs/developers/papyrus.md, CLAUDE.md, KNOWLEDGEBASE.

## Known limits
- With TTS off, no audio ever ends, so lines come only every `reply_timeout`.
- Any NPC's line that ends after our narration counts as the reply (the gate is global, not per actor).
- With torture narration off, a start event can also wait for the gate. The per-hit short events still go out at once.

## Next step for the developer
1. MO2: no stale `Injury_Engine_Main.pex` in overwrite. Load a save. Turn on verbose logging. The dashboard shows "Wait for reply (sec)" = 15.
2. Whip Angi continuously for about 20 s, then switch to Flames (torture), then heal her.
3. `Injury_Engine.log`: `torture:direct` lines alternate with Angi's lines in `conversation_log.log`. There are no two directs before a reply, and no duplicate start in one second.
4. `Papyrus.0.log`: `[v] ClaimNarration ... -> replied` before each direct (or `-> silence` after about 15 s).
5. Heal right after a lash: the heal start line arrives after her reply to the lash.

## After it holds
Commit, leaving out `checkpoints/`. Add the changelog lines at release.
