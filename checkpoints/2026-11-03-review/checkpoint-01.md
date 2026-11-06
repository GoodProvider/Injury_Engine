# Full code review, checkpoint 01 (2026-11-03)

Repo `Injury_Engine` / branch `main` / baseline `d79da46` (0.1.7 plus the VR merge). No code changed, nothing built, nothing committed.

## Goal

Review the whole mod and record the findings for the fix sessions that follow.

## Results

- 4 high, 7 medium and 9 low findings, plus a cleanup list. No fixes were made.
- **Scope:** all C++ in `SKSE_Source/src` and `include`, all Papyrus, the overlay page, YAML actions, prompts, `manifest.yaml`, Makefile, CMake, Python scripts.
- **Partly covered:** injury JSON was checked for structure only (classify rules, `heal_events`, `falling.json` "both" blocks), not prose.
- **Not covered:** Spriggit / ESP records.
- **Method:** code reading only. Nothing was run in game.
- **Logs:** none needed, so this folder has no `logs/` snapshot.

Earlier review: [../reviews/checkpoint-02.md](../reviews/checkpoint-02.md). H4 is the `g_lock` item it left open.

Verdicts: **CONFIRMED** = the code path is certain from reading it. **PLAUSIBLE** = depends on engine behaviour not verified here. Nothing was reproduced in game.

Line numbers are for the `d79da46` sources. `psc` means `Scripts/Source/Injury_Engine_Main.psc`.

## High

### H1. Non-UTF-8 actor names can crash the game (CONFIRMED path)

`nlohmann::json::dump()` uses the strict error handler by default and throws `type_error.316` on invalid UTF-8. Nothing catches it, and there is no `JSON_NOEXCEPTION`.

- Sites: [InjuryEngine.cpp:2410](../../SKSE_Source/src/InjuryEngine.cpp#L2410) (`StateJson`, every bio / feels render), [:2472](../../SKSE_Source/src/InjuryEngine.cpp#L2472) (`OverlayJson`, on `Hit`, `SyncHealth`, `TakeHits`), [:2493](../../SKSE_Source/src/InjuryEngine.cpp#L2493) (`PartPointsJson`), [TruceDecision.cpp:231](../../SKSE_Source/src/TruceDecision.cpp#L231).
- `NameOf` passes `GetDisplayFullName()` bytes through unchanged. A CP-1252 / CP-1251 name (accented custom name, localized game) would throw out of a native on the game thread.
- Fix: one helper calling `dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)`, used at all four sites.

### H2. Fortify Health is counted as healing (CONFIRMED logic; the death outcome is PLAUSIBLE)

`ConvertHealth` computes `excess = GetActorValue(Health) - WoundedLevel(GetPermanentActorValue(Health))` ([InjuryEngine.cpp:1086-1092](../../SKSE_Source/src/InjuryEngine.cpp#L1086-L1092)). Current health includes temporary buffs and the permanent max does not (the KB logged 580/550).

- Real wounds only: `fullSpeed` spends the buff on points, so a buff worth 30% of max health erases up to 30 real points while that health is still missing.
- Tortured: the excess counts as regen and is taken back with `DamageActorValue` ([:1151-1153](../../SKSE_Source/src/InjuryEngine.cpp#L1151-L1153)), so the buff is eaten every sync. That deepens the damage modifier, so when the buff ends health can fall below the floor and possibly to 0 or below. That breaks "torture can't kill".
- Fix: compare what is missing instead, `excess = (maxHealth - WoundedLevel) - HealthDamaged`, where `HealthDamaged` is minus the Health `kDamage` modifier.

### H3. The committed DLL does not contain the VR hook fix (CONFIRMED)

`SKSE/Plugins/Injury_Engine.dll` was last committed in `663938e`. `Hooks.cpp` changed after that in `279caa4` (slot 0x106). On VR the shipped DLL still hooks 0x104, which is `StartPowerAttackCoolDown`, using the `HandleHealthDamage` signature.

Fix: `make dll` and commit the DLL before any VR use or release.

### H4. `g_lock` is still held across engine health changes (PLAUSIBLE freeze; open since the earlier reviews)

`DamageActorValue` / `RestoreActorValue` run under `g_lock` at [InjuryEngine.cpp:1153](../../SKSE_Source/src/InjuryEngine.cpp#L1153) (`ConvertHealth`), [:1172](../../SKSE_Source/src/InjuryEngine.cpp#L1172) (`TakeHealth`, from `TortureHit`) and [:1972](../../SKSE_Source/src/InjuryEngine.cpp#L1972) (`HealPart`). `SyncHealth` holds the lock for its whole loop, while worker threads wait for it in the damage hooks. The KB names these as the next freeze suspects, and this breaks its own rule about engine calls under `g_lock`.

Fix: collect the health changes under the lock and apply them after unlocking, keeping `InternalScope`.

## Medium

### M1. A buff ending on a wounded actor lands a phantom wound (CONFIRMED logic)

[Hooks.cpp:222](../../SKSE_Source/src/Hooks.cpp#L222) treats a loss as a max-health shrink only when `HealthDamaged + 0.5 < -a_damage`. Once the damage modifier already holds at least the buff size, the test is false. With no recent buff mark, the loss goes to `NoteUncaused` and can land as `generic`. So the Courage / Ocato's fix from the KB only covers near-unhurt actors.

Possible fix: compare the damage modifier before and after, or watch for the permanent + temporary max dropping.

### M2. The latest hit event is used without checking who caused it (PLAUSIBLE)

[Classifier.cpp:1015-1037](../../SKSE_Source/src/Classifier.cpp#L1015-L1037) takes `g_recent[victim]` for any attacker. If A's Torture Flames event is under 0.6 s old and B's sword hit arrives before its own event, the hit becomes `fire`, `OnDamage` returns 0 and `RestoreHealth` gives back all of it: a real hit causes no wound and no health loss.

Fix: when `attacker` is known and the event's `cause` differs, ignore the event.

### M3. A Papyrus event per real damage call (CONFIRMED)

`OnDamage` calls `NotifyPapyrus()` unconditionally ([InjuryEngine.cpp:1938](../../SKSE_Source/src/InjuryEngine.cpp#L1938)), even when no point landed. `NoteHealingSource` does the same ([:2149](../../SKSE_Source/src/InjuryEngine.cpp#L2149)), including skipped ambient self-heals. Each call starts an `OnInjury` thread that runs several natives. Damage-over-time in a big fight could repeat the narration lag and backlog the KB recorded for torture.

Fix: notify only when a hit, event or new State was queued.

### M4. `External_Damage(narrate = False)` is not silent for real damage (CONFIRMED)

[docs/developers/papyrus.md:54](../../docs/developers/papyrus.md#L54) and [psc:1784](../../Scripts/Source/Injury_Engine_Main.psc#L1784) promise silence, but `RealHit` → `OnDamage` → `PushHit` still narrates through `NarrateRealHits`. With `narrate = True`, the `cause` event ([psc:1804](../../Scripts/Source/Injury_Engine_Main.psc#L1804)) uses the same id as the hit's own short-lived event, so one overwrites the other.

Fix: a no-narrate flag on `RealHit`, and a distinct or merged event id.

### M5. Dashboard settings that do nothing, and stale descriptions (CONFIRMED)

- `torture_narration_window_size` is read ([psc:438](../../Scripts/Source/Injury_Engine_Main.psc#L438)) and only logged.
- `narration.cooldown` only feeds `NarrationAllowed`, which only `GateText` (a verbose log) uses, and `KEY_LAST_NARRATION` is never written. The manifest still describes it as an active healing gate ([manifest.yaml:135](../../SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml#L135)).
- Stale manifest text: `:185` (the old 11 healing points), `:233` (per-part red / yellow bars), the "speech queue free" wording at `:12-45`, and `min: 0` on ranges that `SetInjuryRanges` clamps to 1.

Fix: remove the two settings and rewrite the text.

### M6. Heal-end lines use the wrong text, and "pain gone" is effectively dead (CONFIRMED)

- `kFullyHealed` is pushed and the actor erased ([InjuryEngine.cpp:2293-2296](../../SKSE_Source/src/InjuryEngine.cpp#L2293-L2296), [:918-921](../../SKSE_Source/src/InjuryEngine.cpp#L918-L921)) before Papyrus asks for the text, so `SourceHealText` gets no State and `HealEventsSource(nullptr)` returns `generic`. `whipping.json`'s `heal_events.full.*` ("whip wounds") and each torture type's own text never reach a fully healed victim.
- After `ClearTortureIfDone`, "no open damage" and "no injury" are the same state, so `kPainGone` ("...but the healing continues") is immediately followed by `kFullyHealed` in the same pass ([:2285-2297](../../SKSE_Source/src/InjuryEngine.cpp#L2285-L2297)), with the same event id.

Fix: keep the `heal_events` source with the event, and either drop `kPainGone` or give it a real threshold.

### M7. The 5 Hz sync loop never stops while a wounded actor is unloaded (CONFIRMED)

`TrackedCount` counts every `g_actors` entry ([InjuryEngine.cpp:2340](../../SKSE_Source/src/InjuryEngine.cpp#L2340)), but `SyncHealth` skips actors without 3D ([:2241](../../SKSE_Source/src/InjuryEngine.cpp#L2241)), so they never heal or clear. One wounded NPC left in another cell keeps `OnUpdate` running at 5 Hz ([psc:1568](../../Scripts/Source/Injury_Engine_Main.psc#L1568)) and stays in the co-save.

Fix: count only loaded actors for the loop (restart on load or `OnUpdateGameTime`) and prune stale entries.

## Low

| # | Location | Issue | Verdict |
|---|---|---|---|
| L1 | `psc:1286`, `:1298-1303` | `HealSend` writes through index `i` after yielding calls (`PairVisible`, `ClaimNarration`, `HealMapText`), against the KB rule to find the session again | CONFIRMED |
| L2 | `psc:122`, `:1316`, `:1483`, `:1739` | One `heal_short_method` is shared by all sessions, so an `again` event can carry another actor's method in its id | CONFIRMED |
| L3 | `Classifier.cpp:719-745`, `Hooks.cpp:420` | The fight-reaction hook checks either hand: a real sword hit with the whip in the other hand reads as an ally hit | CONFIRMED |
| L4 | `Injury_Engine_Actions.psc:83-139` | A load mid-action leaves the temporary whip equipped; two overlapping actions on one speaker share the whip and flag | CONFIRMED |
| L5 | `Injury_Engine_Actions.psc:182-199` | Lost wake-up: an answer arriving between the last empty `TakeTruces` and clearing the flag waits for the next truce answer (no `OnUpdate` retry) | PLAUSIBLE |
| L6 | `Hooks.cpp:46-74`, `:250` | If no zero-damage `HandleHealthDamage` consumes the handled mark, another attacker's melee hit within 100 ms is fully restored | PLAUSIBLE |
| L7 | `InjuryEngine.cpp:2622` | `rec.attacker.get().get()` keeps a raw pointer after the temporary `NiPointer` is gone | CONFIRMED, low risk |
| L8 | `Classifier.cpp:952` | `NoteEffect` records any resist-tagged effect as real health-damage magic, with no check that its AV is Health | CONFIRMED |
| L9 | `Makefile:20-35` | `make release` does not compile Papyrus; `release-guide.md:72` documents this as a manual prerequisite, but nothing enforces it | CONFIRMED |

## Cleanup

- Diagnostics the KB says to remove once the drowning path is known: hooks 0x05 / 0x07 and the always-on `[probe]` (`Hooks.cpp:518-522`).
- Mojibake in a log string (`InjuryEngine.cpp:1919`).
- Unreachable code in `HitText` (`InjuryEngine.cpp:2510-2512`).
- Unused `BatchKey`, `LashText`; write-only `KEY_LAST_TORTURE`.
- `LogInjuryPoint`'s throttle map is never pruned (`InjuryEngine.cpp:1304`).
- Stale comments and docs: `psc:563` ("never narrated"), the KB's "no trampoline" (the hooks now use `write_call`), `InjuryEngine.h:17-18` (the old real-point scale).

## Checked and clean

- All 63 registered natives are declared in `Injury_Engine_Engine.psc`, and every declared native is registered.
- Hit code fields round-trip across C++ and Papyrus (mask, source, level, fresh).
- The decision answer check `"true"` / `"yes"` matches the `PublicAPI.h` noul contract.
- `falling.json`'s classify rule is only in its `health` block, so a fall is not read as torture.
- Co-save read and write layouts match for the current version.
- The `OnDamage` part loop has progress and round guards; the VisitEffects `g_lock` fix is in place.
- The Take + read drain flags are present in Papyrus.

## Not yet tested

Nothing here has been tested. No finding has been reproduced in game and no fix has been written.

## Next

Suggested fix order: H3 (rebuild), H1, H2, M1, H4, M2, M3, then M4-M7.

Assumptions to verify in game:

1. H1: rename an actor or use a non-ASCII name, then render a bio and open the overlay.
2. H2: cast Courage on (a) a real-wounded follower and (b) a tortured follower at the floor. Watch the wounds, the take-back, and health when the buff ends.
3. M1: Courage ending on a wounded follower; look for `uncaused ... landed`.
4. M2: two attackers on one victim, Torture Flames plus a sword, with verbose logging.
5. M3: count `OnInjury` lines in `Papyrus.0.log` during damage-over-time in a large fight.

Per [AGENTS.md](../../AGENTS.md), state confidence before each game / script change, and deploy (`make dll`, `compile: papyrus`) before stopping.

Next session writes `checkpoints/2026-11-03-review/checkpoint-02.md`.
