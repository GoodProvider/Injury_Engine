# Review fixes, checkpoint 03 (2026-11-05)

Repo `Injury_Engine` / branch `main` / baseline `1f804ee` (checkpoint 01 commit). Code changed, DLL and Papyrus rebuilt. **Not committed**: this session's changes and checkpoint 02's are both still in the working tree.

## Goal

Treat checkpoint 02's fixes (H1–H4, M2, M3) as verified (user decision) and fix the rest of the medium findings from [checkpoint-01.md](checkpoint-01.md): M1 and M4–M7. Two cheap low findings (L7, L8) went in too.

## Results

Fixed: **M1, M4, M5, M6, M7, L7, L8**. Still open: **L1–L6, L9** and most of the cleanup list.

Builds:
- `make dll`: clean, wrote `SKSE/Plugins/Injury_Engine.dll` (2026-11-05 18:34, 1,683,968 bytes).
- `compile: papyrus`: clean, all 5 scripts (`Scripts/*.pex`, 18:34).
- Registered natives match `Injury_Engine_Engine.psc` exactly (65, including the new `RealHitQuiet`).
- `whipping.json` and `manifest.yaml` still parse.

Logs: the live `Injury_Engine.log` (13 KB, 18:19) was a short session with no `[buff]` or `uncaused` lines, so no snapshot was taken and this folder has no `logs/`.

Nothing has been tested in game.

| # | Fix | Where | Confidence |
|---|---|---|---|
| M1 | New `EffectSink` (`TESActiveEffectApplyRemoveEvent`). The apply event queues an SKSE task. The task finds the effect with `VisitEffects` and, if it is a max-health buff, records it in `g_buffEffects`. A max-health buff is: Health primary AV, not detrimental, magnitude > 0, and either PeakValueModifier or a Recover-flagged ValueModifier / DualValueModifier. The task is needed because the event can arrive inside the effect update, where walking the list crashes. On removal, a recorded buff calls `NoteBuffChange` and logs `[buff] effect <id> ended on <actor>` (info). `ResetSession` clears the record. | `src/Classifier.cpp` | 75% |
| M4 | `OnDamage(..., narrate)` and `RealHit(..., narrate)`. New native `RealHitQuiet` (wound lands, no `HitEntry`; a killing blow is still narrated). `External_Damage(narrate=False)` uses it. With `narrate`, the `cause` event has its own id `cause_<type>_<attacker>_to_<victim>`, so it no longer overwrites the hit's event. | `InjuryEngine.cpp/.h`, `Papyrus_Engine.cpp`, `Injury_Engine_Engine.psc`, `Injury_Engine_Main.psc` | 90% |
| M5 | Removed `narration.cooldown` and `narration.torture_narration_window_size` from the manifest, Main.psc (properties, reads, `KEY_LAST_NARRATION`, `NarrationAllowed`, `GateText` part) and the `whipping.json` defaults. Rewrote the stale manifest text: the mode descriptions (reply gate instead of "speech queue free"), the healing description (no "11 points"), Healing Hands (no "pain gone"), the overlay (one bar per actor) and the health cooloff. The range fields now have `min: 1`. Fixed the stale `psc:563` comment. | `manifest.yaml`, `Injury_Engine_Main.psc`, `whipping.json`, README, `docs/developers/papyrus.md`, `docs/reference/papyrus-rules.md` | 95% |
| M6 | `SyncHealth` stores each still-wounded actor's `HealEventsSource` in `g_healEventSource` (not erased with the State, cleared on load / revert). `SourceHealText` uses it for `kFullyHealed` when the State is gone, so whipping's and each torture type's `full.*` text is used. `kPainGone` is no longer pushed, and the `g_pained` map is gone. The enum value and the Papyrus branch stay. | `src/InjuryEngine.cpp`, json-keys / guide / actions docs | 85% |
| M7 | `TrackedCount` counts only loaded actors (plus events, hits, uncaused). In `SyncHealth`, unloaded entries get game-time healing (`Advance` + `ClearTortureIfDone`) and are erased silently when healed or dead. New `LoadSink` (`TESObjectLoadedEvent`, registered in `InitDeaths`) calls `NotifyPapyrus` when a tracked actor loads, which restarts the loop. | `src/InjuryEngine.cpp` | 90% |
| L7 | The death sink keeps the attacker's `NiPointer` alive | `src/InjuryEngine.cpp` | 99% |
| L8 | `NoteEffect` records real magic only for effects whose primary or secondary AV is Health | `src/Classifier.cpp` | 95% |

Docs updated:
- `CLAUDE.md` Flow: buff removal event, `RealHitQuiet` and the cause id, the loaded-only loop and `LoadSink`, `kPainGone` dropped and the heal-end source, the removed settings.
- `KNOWLEDGEBASE.md`: two new entries (buff ending on a wounded actor; the loop runs for loaded actors only).

### Reasoning notes

- **M1 ordering:**
  - Mark before the attacker-less `HandleHealthDamage(-x)`: `BuffChange`'s 1 s window passes the loss through.
  - Mark after it: the loss is held as uncaused, and `ResolveUncaused` drops it because the mark is within `kUncausedHold + 1` s.
  - Not covered: buffs already running when a save loads (the record is not saved), and the one-frame gap before the recording task runs. The old shrink test still covers near-unhurt actors there.
- **M6:** at the moment of full healing, `ClearTortureIfDone` has already cleared the torture cells, so even a live State would give `generic`. That is why the source is taken from the previous sync (0.2 s earlier). An actor that heals fully while unloaded is erased silently (M7), so there is no line to word.
- **M7:** removing an Auto property from Main.psc can log a "property not found" warning once on an old save. It is harmless.

## Still open

- **L1–L6, L9** (see checkpoint 01). L1 / L2 (Papyrus heal session indexes, shared `heal_short_method`) are next in value.
- Cleanup:
  - the drowning diagnostics (`[probe]`, hooks 0x05 / 0x07)
  - unreachable code in `HitText`
  - unused `BatchKey` / `LashText` / `KEY_LAST_TORTURE`
  - the unpruned `LogInjuryPoint` throttle map
  - the KB's "no trampoline" line
  - `InjuryEngine.h:17-18`

## Not yet tested

Nothing from checkpoints 02 or 03 has been run in game. The user chose to treat 02 as verified.

## Next

1. **Deploy check:** MO2 loads this folder's `SKSE/Plugins/Injury_Engine.dll` (18:34) and `Scripts/*.pex` (18:34), with no stale copies in overwrite.
2. In-game tests, verbose logging on:
   1. **M1:** cast Courage on a follower who is missing at least half their health, and let it run out. Expect `[buff] effect ... ended on ...` in `Injury_Engine.log`, and either `[buff] HandleHealthDamage ...` or `uncaused ... dropped (buff change)`. There must be no `uncaused ... landed` and no `generic` wound. If no `[buff] effect` line appears, check for `[buff] effect ... recorded` (debug) after the cast. If that is missing too, the apply event or the effect test did not match: note the MGEF id.
   2. **M4:** call `External_Damage(None, follower, "", "", "slashing", 10, False)` from a test script or the console. The wound must land (bio / log `health:` line) with no hit narration and no `injury_hit` event. With a cause and `narrate = True`, two events must appear (`cause_slashing_...` and `health_slashing_...`).
   3. **M6:** whip a follower, then heal them fully with Healing Hands. The fully healed line must use whipping's `heal_events.full.*` text ("whip wounds"), with no "pain gone ... healing continues" line before it.
   4. **M7:** wound an NPC, then leave the cell. `Papyrus.0.log` (verbose) must show `nothing tracked, sync loop stops`. Come back: `tracked actor ... loaded, sync restarts` (DLL debug), and the loop runs again.
   5. **M5:** the dashboard no longer shows "Torture narration window" or "Narration cooldown", and the settings load and save without errors.
3. Commit checkpoints 02 and 03 together (sources, DLL, `.pex`, docs) once you're happy. Not done this session.
4. Next session: L1 / L2 (Papyrus), then L3–L6 and the cleanup list. It writes `checkpoints/2026-11-03-review/checkpoint-04.md`.
