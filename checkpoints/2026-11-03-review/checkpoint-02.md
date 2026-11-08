# Review fixes, checkpoint 02 (2026-11-05)

Repo `Injury_Engine` / branch `main` / baseline `1f804ee` (checkpoint 01 commit). Code changed and DLL rebuilt; **not committed**.

## Goal

Fix the review findings from [checkpoint-01.md](checkpoint-01.md) that are C++ only and can be fixed from reading the code.

## Results

Fixed: **H1, H2, H3, H4, M2, M3**, plus one cleanup item (the mojibake log string). Deferred: **M1**, **M4-M7**, all L items, and the rest of the cleanup list.

`make dll` built clean and wrote `SKSE/Plugins/Injury_Engine.dll` (2026-11-05 18:15, 1,675,264 bytes). No Papyrus, JSON or ESP changes. No logs were needed, so there is no `logs/` snapshot.

Nothing has been tested in game.

| # | Fix | Where | Confidence |
|---|---|---|---|
| H1 | `InjuryConfig::Dump` calls `dump(-1, ' ', false, error_handler_t::replace)`, so invalid UTF-8 becomes U+FFFD instead of throwing. All 5 `.dump()` sites use it (`StateJson` x2, `OverlayJson`, `PartPointsJson`, the truce decision context). | `include/InjuryConfig.h`, `src/InjuryEngine.cpp`, `src/TruceDecision.cpp` | 95% |
| H2 | `EffectiveHealth = min(health, max - kDamage modifier) + pending`. It is used for the `ConvertHealth` excess and for the `TakeHealth` floor and loss, so a Fortify Health / Courage buff is not read as healing and gives torture no room. | `src/InjuryEngine.cpp` (`EffectiveHealth`, `ConvertHealth`, `TakeHealth`) | 85% |
| H3 | DLL rebuilt from the current sources, including the VR slot 0x106 change. Committing it is still open. | `SKSE/Plugins/Injury_Engine.dll` | done (build only) |
| H4 | `HealthBatch` (RAII, thread_local, nests) + `ChangeHealth`. Health changes under `g_lock` are queued and applied under `InternalScope` after the unlock. The batch is open in `SyncHealth` (after `ResolveUncaused`), `HitPoints`, the locked block of `OnTortureDamage` (it still applies before that function returns to the hook) and `HealPart`. Reads made while changes are pending add `PendingHealth(id)`: `EffectiveHealth`, the `FitRealToHealth` missing value, `UpdateBleedout` and the torture log `healthAfter`. | `src/InjuryEngine.cpp` | 85% |
| M2 | If the recent hit event's `cause` is an Actor other than this call's `attacker`, the event is ignored and classification uses the attacker's hands. Debug line: `event from another attacker, ignored`. | `src/Classifier.cpp` (`Classify`, before the `if (sourceId)` block) | 85% |
| M3 | Real-damage `OnDamage` notifies Papyrus only when at least one of these is true: a hit was pushed, a new State was kept, torture was replaced, or a heal session was dropped. `NoteHealingSource` notifies only when `Seen` opened a session or queued an event, so refreshes and skipped ambient self-heals send nothing. | `src/InjuryEngine.cpp` | 90% |
| cleanup | Mojibake `â¤` in the `health:` info line replaced with `->` | `src/InjuryEngine.cpp` | — |

Docs updated:
- `KNOWLEDGEBASE.md`: two new entries, the HealthBatch rule and Fortify Health.
- `CLAUDE.md` Flow: one sentence on the batch and `EffectiveHealth`.

### Reasoning notes

- **H4 ordering:** the lock is declared after the batch, so it is released first and the queued changes land right after. In `OnTortureDamage`, `TakeHealth` used to change health before the hook computed its `before` value, and it still does.
- **H2 debuffs:** a negative temporary modifier lowers health below `max - damaged`. The `min` keeps the old behaviour for it, so torture still can't go below the real floor.
- **M3 loop:** the sync loop keeps running for an open heal session (`KEY_HS_TGT` in `Main.OnUpdate`, psc:1568) and for tracked actors. A dropped refresh event therefore never stalls `again` lines. Death narration has its own `NotifyPapyrus` in the death sink.

## Still open (from checkpoint 01)

- **M1** (a buff ending on a wounded actor gives a phantom `generic` wound). `HandleHealthDamage` runs after the damage, so comparing the damage modifier before and after inside the hook doesn't work. A likely fix is to cache the Health temporary modifier per actor and treat a drop in it as a shrink. Run test 3 below before designing it.
- **M4-M7** (Papyrus + manifest): the `External_Damage` silent flag, the dead settings and stale manifest text, the heal-end text source and `kPainGone`, and the 5 Hz loop for unloaded actors.
- **L1-L9** and the rest of the cleanup list are unchanged.

## Not yet tested

Nothing here has been run in game.

## Next

1. **Deploy check:** MO2 must load this folder's `SKSE/Plugins/Injury_Engine.dll` (18:15 build), with no stale copy in overwrite.
2. In-game tests, with verbose logging on:
   1. **H1:** give an actor a non-ASCII name (`setdisplayname`, accented character), wound them, then open the bio / dialogue and the overlay. The game must not crash.
   2. **H2:**
      - Courage on a real-wounded follower: the real points stay and the `convert` lines spend nothing from the buff.
      - Courage on a tortured follower at the floor: no take-back of the buff, and health stays at or above the floor when it ends.
   3. **M1 (data for the fix):** Courage ending on a wounded follower. Look for `uncaused ... landed` or a `generic` wound.
   4. **H4:** Torture Flames plus a sword fight nearby. No freeze; the `take health` and `convert` lines look as before.
   5. **M2:** Torture Flames from A and a sword from B on one victim. B's hits log `event from another attacker, ignored` and land as `slashing`.
   6. **M3:** damage-over-time in a big fight. Far fewer `OnInjury` lines in `Papyrus.0.log`.
3. Commit the source and the DLL together once you're happy (not done this session).
4. Next session: M1 (with the test 3 log), then M4-M7. It writes `checkpoints/2026-11-03-review/checkpoint-03.md`.
