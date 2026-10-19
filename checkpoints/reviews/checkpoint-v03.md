# Code review checkpoint v03 (2026-10-18)

## Status

Fix pass for the findings in [checkpoint-v02.md](checkpoint-v02.md), which includes the re-checked v01 list. Nothing is verified in game yet.

- **Papyrus:** compiled with `compile: papyrus` (5 scripts, no errors). The `.pex` files are in `Scripts/`.
- **DLL:** `make dll` compiled and linked with no errors. The copy into `SKSE/Plugins/Injury_Engine.dll` failed with "Permission denied", because Skyrim was running and held the old DLL. The new build is at `SKSE_Source/build/release/Release/Injury_Engine.dll`. **The deployed DLL is still the old one.**
- **Overlay page:** `PrismaUI/views/Injury_Engine/overlay.html` changed. The new DLL and the new page must ship together.

The DLL and the `.pex` files must be deployed together. The scripts now call into the scene and sync contracts the new DLL has.

## Fixed

| Finding (v01 / v02) | Fix | Where |
|---|---|---|
| Sync-event and death buffers drained by two Papyrus threads (v02 High) | `sync_draining` / `deaths_draining` flags, as with `hits_draining`. Drains loop until empty. `OnUpdate` skips its `SyncHealth` for that tick while another thread drains. `DrainPendingEvents` no longer clears the arrays on an empty take. | `Injury_Engine_Main.psc` `ProcessSyncEventBatch`, `NarrateSyncCodes`, `NarrateDeaths`, `OnUpdate`. `InjuryEngine.cpp` `DrainPendingEvents` |
| Raw `Actor*` result buffers (v02 Low) | Hit, sync and death buffers hold FormIDs, resolved in the read native (`ResolveActors`) | `InjuryEngine.cpp` |
| Saved torture / heal scenes never expire after a relaunch (v01) | `Setup` → `ClearSessions` drops every pair and session. `SceneTick` and `HealTick` treat a future stamp as expired. | `Injury_Engine_Main.psc` |
| `heal_tick_active` not cleared on load (v01 Low) | Cleared in `Setup` | same |
| Recent whip hit event swallows later damage (v01) | A stored whip event sets `whipEvent` and falls through to the hands loop, the live torture effect and drowning. Ignore only when nothing else matches. | `Classifier.cpp` `Classify` |
| `ProcessHitData` classifies from gear, not the swing (v01) | New `Classifier::ClassifyMelee(victim, attacker, a_hit.weapon)`: whip → -1, fists → unarmed rule, weapon → its rule. No spell effect or older event can replace it. | `Hooks.cpp` `ProcessHitData`, `Classifier.cpp` |
| One `thread_local` "already handled" slot (v01 Low) | Per-victim map under a mutex, used once (`MarkHandled` / `TakeHandled`). Also used by a zero-damage `HandleHealthDamage`. | `Hooks.cpp` |
| Last torture point cleared by a hit never stands the actor up (v01) | `Erase(id)` instead of `g_actors.erase` | `InjuryEngine.cpp` `OnDamage` |
| Queued combat stops ignore `ForgetCalm` / `ForgetTruces` (v01) | Both tasks re-check `g_calm` / `InTruceLocked` under the lock before stopping | `Classifier.cpp` `StopCombatLater`, `CombatSink` |
| Classify caches and `kIgnoreFriendlyHits` survive load (v01) | `Classifier::ResetSession` on kPreLoadGame / kNewGame clears truces, `g_recent`, `g_torture`, `g_realMagic`, `g_quiet`, `g_calm` and `g_combatStart`, and releases every friend flag. `SuppressFriendHits` starts a 1 s background release timer, for when the sync loop is not running. | `Classifier.cpp`, `plugin.cpp` |
| Reload during `Torture_Target` swallows real lashes (v01) | `injury_engine.in_action_time` is refreshed on each strike. `LashEffect` clears a flag that is older than 3 s, or stamped in the future. | `Injury_Engine_Actions.psc`, `Injury_Engine_LashEffect.psc`, `Main.psc` (`KEY_IN_ACTION_TIME`, `IN_ACTION_STALE`) |
| Heal start lines dropped while SkyrimNet replies (v01) | A session with `KEY_HS_PEND` set lives `torture_maximum_break + reply_timeout` | `HealTick` |
| Looking away for one tick cancels a torture start (v01) | New state `SC_WAITING_HIDDEN`. A pending pair out of sight waits, then is pending again in sight (`SceneSetVisible`). It counts as `SceneWaiting`. | `Main.psc` |
| Scene / heal indices go stale across yields (v02 Medium) | `SceneIndexOf(att, vic, hint)` after the yielding calls in the `SceneTick` visibility loop. `SceneSend` captures the pairs' forms before `SceneText` and finds them again afterwards. `HealTick` re-finds with `HealFind` after `IsHealing` / `IsDead`. | `Main.psc` |
| Only one truce partner is saved (v01, corrected in v02) | Per-NPC lists `injury_engine.truce_partners` / `truce_partner_until`, each partner with its own end. `RestoreExpiredTruces` prunes them and notes each again. `Start_Fighting` removes both fighters from every list. A legacy `truce_with` is migrated. | `Injury_Engine_Actions.psc` |
| Late truce answer applied after the request was dropped (v01) | Each request has a ticket (`Waiting::ticket`). The LLM callback and the message-box task push only while `Current(speaker, ticket)` holds. `Reset` and a `Busy` replacement invalidate the ticket. | `TruceDecision.cpp` |
| Decision timeline reads `text` (v01) | Reads `data` (an object with `dialogue` / `line` / `text` / `narration`, or a JSON string), falling back to `text`. Every field is read `is_string()`-safe. | `TruceDecision.cpp` `BuildTimeline` |
| Separate PrismaUI invokes apply out of order and stick (v01) | One pending payload and one queued task. `injuryEngineUpdate(state, seq, reset)`: the page ignores a smaller `seq`. Reset is the same call with `reset` true (`whipReset` stays as the JS helper). | `Overlay.cpp`, `overlay.html` |
| Bad field type aborts config load (v01) | try / catch per classify rule and per source in `FinishSources`. `TriBool` throws on a non-bool, so that rule is skipped. `overlay.order` is range-checked. | `InjuryConfig.cpp` |
| `npc` / `combat` numbers treated as "don't care" (v01 Low) | See above: logged, and the rule is skipped | same |
| Missing / short `sexual_parts.json` leaves region indices out of range (v02 Low) | The `region` default is read only when the key is absent. `CheckRegions` remaps any bad index to `body` and logs a warning. | same |
| Lungs counted in capacity, and lungs as a torture part (v01, extended in v02) | `ActiveParts()` returns `kPartLungs` (11) with sexual parts on. `PartActive` keeps the explicit lungs exception for drowning. | `InjuryEngine.cpp` |
| Death text and hit rolls share an unlocked RNG (v01 Low) | Every draw goes through `RandomIndex` / `RandomFloat` under `g_rngLock` | `InjuryEngine.cpp` |
| Revert leaves two hit-flag arrays (v01 Low) | `g_hitUnarmed` and `g_hitCounts` are cleared in `Revert` | same |
| Hot-path form lookups by plugin name (v02 Medium) | `CacheClassifyEffectForms` resolves effect and ignore-weapon forms to FormIDs once (`g_ruleEffect`, `g_ignoreWeapons`). `EffectMatchesRule`, `MatchWeapon` and `IsIgnoredWeapon` compare ids. | `Classifier.cpp` |
| Two `AllocTrampoline(14)` calls (v02 Low) | One `AllocTrampoline(28)` in `Install_Hooks` (not on VR) | `Hooks.cpp` |
| Drowning diagnostics always on (v02 Low) | **Partly.** The drowning repro from `checkpoints/drownding/checkpoint-v02.md` is still pending, so the probe hooks stay. `UnderwaterProbe` now returns on cheap flags (`kUnderwater`, `kInWater`, `kSwimming`, `underWaterTimer`) before `GetSubmergedLevel`, the throttle key, or the log. | `Hooks.cpp` |
| Old external strikes narrate beside an open scene (v01) | `Strike` routes landed hits through `NarrateHits` (scene + reply gate, the caller's text as the cause). When nothing landed, the caller's text still goes out direct. | `Main.psc` `Strike` |

## Not fixed

- **CRT (`/MT` vs SkyrimNet `/MD`), v01 Critical: rejected.** The Universal CRT's `malloc` / `free` use the process heap (`GetProcessHeap`) in both `/MT` and `/MD` release builds. A SkyrimNet `std::string` freed in this DLL therefore goes back to the heap it came from. `std::string`'s layout is fixed across VS 2015+. SkyrimNet_SexLab ships `/MT` and makes the same calls in release. Only the debug heap differs, and `CrossDllStdStringSafe` already skips those calls in debug builds. Switching to `/MD` would need a new vcpkg triplet, a full CommonLib rebuild, and the VC redist on every player's machine. If an NPC `Stop_Fighting` ever crashes in `TruceDecision`, revisit this.
- **`g_lock` held across health changes (v01 Medium): deferred.** It is the remaining freeze class. The fix (copy the id and amount, unlock, then change health under `InternalScope`) touches `TortureHit` / `TakeHealth`, `ConvertHealth`, `HealPart` and `SyncHealth`, and needs its own pass with verbose logs.
- **Ally override follows what is equipped, not the swing (v01 Medium): deferred.** `FightReaction` gets no weapon. A "last swing" from `ProcessHitData` would need the order of the two call sites (37672 vs 37673) confirmed in game first.
- **Real spells lose to an equipped weapon, or become generic (v01 Medium): deferred.** Preferring `g_realMagic` over an equipped weapon risks misreading real sword hits inside the 0.6 s window. It needs a per-tick refresh from the effect hook.
- **Config readers never take `Load`'s lock (v01 Low): not changed.** `Load` runs only at kDataLoaded.

## Next step for the developer

1. Close Skyrim. Run `make dll` from the repo root: it only copies the finished build into `SKSE/Plugins/`. Check that `SKSE/Plugins/Injury_Engine.dll` has a 2026-10-18 timestamp.
2. The `.pex` files (`Scripts/`) and `overlay.html` are already in place. Confirm that MO2 overwrite has no older `Injury_Engine` DLL, `.pex` or overlay page.
3. Load a save. `Injury_Engine.log` should show the hook installs, `Classifier: torture MGEF ...` lines, and no `classify rule skipped` / `part ... has no region` warnings with the shipped JSON.
4. Run the in-game checks below. Turn verbose logging on for 3, 4 and 8.

## In-game checks

1. NPC `Stop_Fighting` against another NPC, with recent dialogue. Expected: no crash. The `truce: send` log line has a non-empty `timeline`.
2. Quit to desktop during a whip scene or a heal, relaunch, load. Expected: no `again` line, and the 0.2 s loop stops once nothing is tracked.
3. Whip in one hand, Flames (torture) in the other, on a victim who is also underwater. Expected: torture points, health held at the floor, no kill. The log has `padded whip hit event` lines followed by a torture or drowning classify, not ignore.
4. Punch a victim who still has Flames (torture) on them, with a power attack. Expected: a `ProcessHitData torture ... punching` line and health unchanged.
5. Torture to bleedout, let it tick down to one point, then one real hit that replaces it. Expected: the actor stands up.
6. Save and reload during `Torture_Target`, then whip the same NPC by hand. Expected: a whip wound.
7. Accept two truces on one NPC (the player, then another NPC), save, reload, attack. Expected: both pairs stay stopped, and two `truce: ... until game day` lines on load.
8. Heal an NPC with Healing Hands while another NPC hits them, then repeat with a potion mid-fight. Expected: each heal event names the right actor, and no `Array index` errors from `OnUpdate` in `Papyrus.0.log`.
9. Kill two NPCs at once (a fireball into a group). Expected: each death line names its own victim and attacker.
10. With sexual parts on, `External_Torture` with part `"lungs"`. Expected: the torture lands on another part, and the overlay has no lungs slot.
11. Rename `sexual_parts.json`, turn sexual parts on, wound a sexual part. Expected: no crash, and a `part ... has no region` warning.
12. Whip someone, then quick-load at once. Expected: the old bar does not reappear, and new bars show normally.
13. Hit a neutral NPC once with the Padded Whip and then do nothing else. Expected: `calm: victim ... counts friendly hits again` within about 4 s, even with no wound.

## After it holds

- Run the drowning repro (`checkpoints/drownding/checkpoint-v02.md`). Then remove the AV 0x05 / 0x07 probe hooks and gate `UnderwaterProbe` on verbose logging.
- Take the deferred `g_lock` pass next. It is the last known freeze path.
- Add a CHANGELOG entry for 0.1.2 with the `release` skill.
