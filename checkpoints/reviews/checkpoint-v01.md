# Code review checkpoint v01 (2026-10-18)

Review of the tree as it stands (plugin version 0.1.1 in `SKSE_Source/CMakeLists.txt`). Read against the source, not against a running game. No code was changed.

Scope: `SKSE_Source/src` and `include` (InjuryEngine, Hooks, Classifier, TruceDecision, Overlay, InjuryConfig, Papyrus_Engine, plugin), `Scripts/Source/*.psc`, and the SkyrimNet `PublicAPI.h` contract those files call. Papyrus native names, argument counts, and return types match the registrations in `Papyrus_Engine.cpp` (61 natives).

## Fix these first

1. Release DLL frees SkyrimNet `std::string` with the static CRT. NPC `Stop_Fighting` / surrender can crash.
2. A saved torture or heal scene uses real-time stamps. After the game process restarts those scenes never expire, and a held start line keeps narrating.
3. A recent Padded Whip hit makes the next 0.6 s of damage on that victim an ignore, so an off-hand spell or a drown tick takes real health.
4. `ProcessHitData` classifies from equipped gear and recent events, not from the swing. A punch while a torture spell is live is not zeroed and can kill.
5. Clearing the last torture point with a real hit drops the actor from the map and leaves them in bleedout.

## Critical

### Release build frees SkyrimNet's `std::string` with the wrong CRT

`TruceDecision::BuildTimeline`, called from `BuildContext` inside `Ask` for an NPC target.

`CrossDllStdStringSafe()` returns false only when `_DEBUG` is set. A release build then does:

```154:154:SKSE_Source/src/TruceDecision.cpp
                events = nlohmann::json::parse(PublicGetRecentEvents(target, kTimelineEvents, ""), nullptr, false);
```

`PublicGetRecentEvents` returns `std::string` by value (`SkyrimNet devkit/CppAPI/PublicAPI.h`). That header requires both DLLs to use the dynamic CRT (`/MD`). This plugin forces the static CRT:

```9:10:SKSE_Source/CMakeLists.txt
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
```

The temporary is destroyed in this DLL. An empty `"[]"` fits in the small-string buffer and survives. Any real event list is heap memory from SkyrimNet's CRT, freed with this DLL's heap. That is a game-thread crash or heap corruption on every NPC truce or surrender, after `Busy` has already marked the speaker in flight. The player Yes/No path never calls `BuildContext`. Debug builds skip the call, so the crash shows up only in the DLL the game loads.

Fix: stop consuming a SkyrimNet `std::string` in this DLL until the CRTs match. Linking `/MD` is what `PublicAPI.h` asks for. A `_DEBUG` check does not cover the release `/MT` build.

### Torture and heal sessions never expire after the process restarts

`Injury_Engine_Main.SceneTick`, `HealTick`, `OnUpdate`. `Setup` does not clear the scene lists.

`KEY_SC_LAST`, `KEY_SC_NARR`, `KEY_HS_LAST`, and `KEY_HS_NARR` are `Utility.GetCurrentRealTime()` stamps stored in StorageUtil, which is saved. That clock starts over when the game process starts. `ClaimNarration` already treats a backwards clock as an open gate:

```157:158:Scripts/Source/Injury_Engine_Main.psc
    if last_direct_time < 0.0 || since < 0.0 ; never narrated, or real time restarted after a load
        return true
```

Expiry does the opposite. `SceneTick` removes a pair only when `now - KEY_SC_LAST >= limit`, and `HealTick` removes a session only when `now - KEY_HS_LAST >= torture_maximum_break`. After a relaunch that difference is negative, so the test never passes. `OnUpdate` keeps registering itself while either list is non-empty.

A load during an open scene, after quitting the game: the 0.2 s loop never stops. A pair still pending, a pair with an unsent switch, or a heal start sitting in `KEY_HS_PEND` is sent at once. `SceneSend` / `HealSend` then stamp the narration time with the new clock, while the last-hit time stays in the previous session. `sinceHit` stays negative, which still counts as "within `torture_again_recent`", so an `again` line goes out every `torture_direct_narration_gap` for a scene that is not happening. A load inside the same process keeps the clock, so this is a quit-and-relaunch bug.

Fix: in `Setup`, drop every torture pair and heal session. Also treat `now < last` as expired in `SceneTick` and `HealTick`.

## High

### A recent whip hit event swallows later damage

`Classifier::Classify`, the `g_recent` block.

A Padded Whip `TESHitEvent` stores the whip form. `MatchWeapon` returns source `-1` for the ignore rule. `-1 != -2`, so Classify returns ignore immediately and never reaches the both-hands rule, the live torture effect, or drowning. The window is `kHitWindow` (0.6 s), and each new lash restarts it.

For that window, a `HandleHealthDamage` that has no newer hit event of its own is the whip. `source < 0` keeps the full amount and records no wound. A flames tick, a staff bolt, or a drowning tick takes real health and can kill someone the torture floor was supposed to hold. The comment above the both-hands loop describes this exact case ("a Padded Whip in one hand must not swallow the beam"), and that loop is skipped whenever `g_recent` already matched.

Fix: treat `-1` from a stored hit event as "this form is not the answer" and fall through. Return ignore only when this call's own form is the whip.

### `ProcessHitData` can fail to zero a punch or a whip

`Hooks.cpp` `ProcessHitData::thunk`. It has `HitData::weapon` for the swing that connected and does not read it. It calls `Classify`.

A fist `TESHitEvent` often has source 0, and `if (sourceId)` skips that record. With both hands empty, a live `g_torture` entry (`kTortureWindow`, 2 s, any caster) wins: Classify returns the torture spell with `physicalMelee` false. The punch is not zeroed and `OnDamage` is not called here. Health is taken before `HandleHealthDamage` can give it back. That is the kill path this hook was added for: a power-attack punch while Flames (torture) is still on the victim.

The same Classify result misses a whip swing when the hit event is not in `g_recent` yet and the other hand's weapon or a live torture effect wins. A left-hand whip with a sword in the right hand is then the sword, and the weapon hit is not zeroed.

Fix: in this thunk, classify `a_hit.weapon` (and the left-hand flag) first. Zero a whip or a torture unarmed hit from that form. A live spell effect must not replace the melee result.

### Queued combat stops ignore `ForgetCalm` and `ForgetTruces`

`Classifier::StopCombatLater` and the truce `AddTask` in `CombatSink::ProcessEvent`.

The task always calls `StopCombatNow`. It does not check that `g_calm` or the truce is still there. `ForgetCalm` / `ForgetTruces` only erase the maps. Every combat start inside the 3 s calm window queues another stop.

`Start_Fighting` calls `ForgetCalm` and then `StartCombat`. A stop queued earlier in that window still runs and calls `StopCombat` on the new fight. The same applies to a truce stop after `ForgetTruces`.

Fix: stamp a generation (or re-read `g_calm` / `g_truce` under the lock) inside the task, and bump it in `ForgetCalm` and `ForgetTruces`.

### Last torture point cleared by a hit never stands the actor up

`InjuryEngine::OnDamage`, contrasted with `Erase`.

When a real hit removes the last torture and does not finish a real-damage point, `OnDamage` drops the actor with a raw map erase:

```1628:1630:SKSE_Source/src/InjuryEngine.cpp
            if (!AnyInjury(st)) {
                g_actors.erase(id);
            }
```

`Erase` is what calls `ReleaseDowned` (clears `g_downed` / `g_collapsePending` and queues `BleedOutStop`). Time-heal and `SyncHealth` use `Erase`. This path does not.

One replacement is often not enough carry for a real point (`wound_share` is 0.9, so one replaced point adds `0.9 * ratioR`). That is normal after torture has ticked down to a last point while health is still under `bleedout_below`. Healing points does not raise health, and regen is mostly taken back while any torture remains.

The actor stays in the bleedout idle after health recovers. `SyncHealth` never visits them, because they are no longer in `g_actors`. `g_collapsePending` also sticks, so a later scene line can say they collapse again. Turning torture bleedout off still releases everyone left in `g_downed`.

Fix: call `Erase(id)` there. Queue a fully-healed event only if that line is wanted.

### A reload during `Torture_Target` swallows that actor's real lashes

`Injury_Engine_Actions.TortureTarget` and `Injury_Engine_LashEffect.OnEffectStart`.

The flag is set before the waits and cleared only after them (`KEY_IN_ACTION` on the speaker, plus a 0.5 s tail so a late enchantment hit is ignored). Papyrus drops latent stacks on load, so those `Wait`s never reach `UnsetIntValue`. StorageUtil still has the int. The enchantment then returns forever:

```12:14:Scripts/Source/Injury_Engine_LashEffect.psc
    if akCaster != None && StorageUtil.GetIntValue(akCaster, main.KEY_IN_ACTION, 0) > 0
        return
    endif
```

Save or reload while an NPC is mid `Torture_Target` (about a second of setup plus ~1.2 s per strike). Later real Padded Whip hits by that NPC play and create no whip wounds. A new scripted action still calls `Lash` directly, so it looks like only the LLM whip works.

Fix: store a real-time stamp with the flag. In `OnEffectStart`, ignore the flag and clear it when the stamp is older than a few seconds, or when `now < stamp` (clock restarted).

### Heal start lines are dropped while SkyrimNet is still replying

`Injury_Engine_Main.HealTick`, contrasted with `SceneTick`.

A torture start or switch that is still waiting gets `torture_maximum_break + reply_timeout` before it is dropped. A heal start held in `KEY_HS_PEND` does not. `HealTick` deletes the session as soon as the last healing observation is `torture_maximum_break` old (default 3 s), and that check runs before the pending send.

A short heal (potion, brief spell) that starts while someone is still talking stores the start line and then discards it after 3 seconds. `reply_timeout` defaults to 15 seconds, so the reply gate often opens after the line is already gone. While the source is still active, `KEY_HS_LAST` is refreshed and the line survives. The loss is the heal that ends before the reply.

Fix: if `KEY_HS_PEND` is non-empty, wait `torture_maximum_break + reply_timeout` before `HealRemove`, the same rule as `SceneWaiting`.

### A late truce answer is applied after the request was dropped

`TruceDecision::OnAnswer`, `PlayerChoice::Run`, `Done`, `Busy`, `Reset`.

`Done` erases `g_inFlight` only when the target still matches. Both answer paths then always `Push`. `Busy` reuses the slot after 60 seconds ("request never answered; dropped") and sends a new request. `Reset` (`kNewGame` / `kPostLoadGame`) clears the map and `g_answers` with no generation, while the callback already captured FormIDs and will `AddTask` later.

The old Yes/No box or LLM callback still enqueues an accept or refuse. Papyrus applies it: combat stop and Aggression 0 for a request that timed out, was replaced, or belonged to the previous save. The hop onto the game thread is correct. The missing check is whether this reply is still the live request.

Fix: stamp each request. Bump the stamp in `Reset` and when `Busy` replaces a slot. `OnAnswer` and the message-box task should `Push` only when that stamp is still current.

### Separate PrismaUI invokes can apply out of order and then stick

`Overlay::Notify`, `Invoke`, `Reset`.

Each change queues its own task that calls `g_prisma->Invoke("injuryEngineUpdate(...)")`. `Reset` queues a different call, `whipReset()`. `g_lastPush` is updated when the task is queued, not when JS runs. PrismaUI applies separate invokes in either order (known for this dependency).

A newer bar can be painted and then overwritten by an older one, or `whipReset` from a load can wipe the next update. `Notify` then sees `json == g_lastPush` and will not send again, so the panel stays wrong until some later, different state.

Fix: keep one pending payload and have a single in-flight task send whatever is current at send time. Express reset as that same `injuryEngineUpdate` with an empty actor list.

### A bad field type aborts config load outside the per-file catch

`InjuryConfig::Load`, `ReadClassify`, `FinishSources`.

Parse errors are caught per file. After that loop, `FinishSources` and `ReadClassify` call `json::value` with no try. nlohmann's `value` throws `type_error.302` when the key exists and the type differs (`rule.value("priority", 0)`, `rule.value("unarmed", false)`, `s.json.value("/hook/points_per_second"...)`, and the other `rule.value` calls). `FormIdOf`'s `get<std::uint32_t>()` throws on a negative id.

One file in `SKSE/Plugins/Injury_Engine/injuries/` with `"priority": "high"` or `"unarmed": 1` unwinds out of `Load`. `plugin.cpp` `kDataLoaded` then never reaches `Classifier::Init`, `InitDeaths`, `TruceDecision::Init`, or `Overlay::Init`. Shipped files use the types these calls expect. The hole is any other file in that folder.

`overlay.order` is a related crash, later: `ReadBody` stores `get<int>()` with no range check, and `OverlayJson` indexes `partMax[overlayOrder[i]]`. A value of `99` is an access violation on the next overlay push. Shipped `whipping.json` uses `0, 3, 2, 1, 5, 4`.

Fix: catch per rule and per source, log, and skip that rule or source. Accept an overlay index only inside `0 .. kPartCount - 1`.

## Medium

### Looking away for one tick cancels a torture start

`Injury_Engine_Main.SceneTick` visibility pass.

Every non-hidden pair, including `SC_PENDING`, becomes `SC_HIDDEN` when the pair is out of sight for that 0.2 s tick. Coming back promotes `SC_HIDDEN` straight to `SC_RUNNING`. A pending start was owed a line. After this it is not, and it is no longer `SceneWaiting`, so it expires at 3 seconds instead of 3 plus `reply_timeout`.

The hit landed in sight, the start is waiting on the reply gate, and one tick of "too far" or 3D unloaded (a turn, a doorway, a fade) cancels the Direct Narration. The short-lived `torture_hit` event is already registered.

Fix: leave `SC_PENDING` alone in that loop. In `SceneMatches` for `SC_SEL_START`, require the pair to be visible right now. A look-away then pauses the start.

### Only one truce partner is saved

`Injury_Engine_Actions.GuardTruce`, `RestoreExpiredTruces`.

`NoteTruce` is pair-keyed and stacks. Papyrus stores one partner in `KEY_TRUCE_WITH`. The player is never written. On load only that one form is armed again.

Lydia's truce with the player is armed. She later accepts a truce with someone else. Her saved partner becomes that someone else. This session still has both DLL guards. After reload only the second pair is noted. She can enter combat with the player again. Her Aggression stays 0, so she will not start it, and the player's attacks are no longer stopped. `StartFighting` has the mirror hole: `ForgetTruces` drops every DLL guard, and a third partner still stored on someone else's `KEY_TRUCE_WITH` comes back on the next load.

Fix: save every partner and `NoteTruce` each one on load. When `ForgetTruces` runs, clear those saved pairs too.

### Ally override follows what is equipped, not the swing

`FightReaction::thunk` via `IsTortureAttacker` / `IsWhipAttacker`.

The hooked call does not receive the weapon. `IsWhipAttacker` is true if either hand holds the padded whip. `IsTortureAttacker` is also true for empty hands whenever the unarmed torture rule matches, including in combat when `punch_combat_health` is off. Every neutral reaction at that site becomes ally, and `SuppressFriendHits` runs.

Sword in one hand and the whip in the other: the sword hit does not start combat, and the victim gets `kIgnoreFriendlyHits`. An unarmed fighter already in combat gets the same treatment against a neutral bystander.

Fix: override the reaction only when the hit being resolved is the whip or a torture punch. A "last swing" recorded from `ProcessHitData` is enough.

### Classify caches and `kIgnoreFriendlyHits` survive the wrong lifetime

`ReleaseFriendHits` (timed path, from `TakeHits`) and `plugin.cpp` `kPreLoadGame` (`ResetTruces` only).

The friend-hit flag is cleared on save, and otherwise only when `TakeHits` runs and the entry is older than 3 s. `TakeHits` runs from the Papyrus sync loop, which stops when nothing is tracked. `g_calm`, `g_torture`, `g_recent`, `g_realMagic`, and `g_friendFlag` are not cleared on load or revert. Those windows use `steady_clock`, so they stay valid across a quick load in the same process.

A reaction that never creates a wound (the lash enchantment did not apply, the hit was blocked) leaves `kIgnoreFriendlyHits` on until the next save. After a quick load, a combat start for that FormID within 3 s is still stopped, and damage within 2 s can still be classified as the previous session's torture effect.

Fix: clear the flag maps and the hit, effect, and calm maps on `kPreLoadGame` / `kNewGame`. If a flag is set and the sync loop is not running, schedule the 3 s release from `SuppressFriendHits`.

### Real spells lose to an equipped weapon, or become generic

`Classify` hands loop and `RecentRealMagic`.

`ClassifyForm` accepts a weapon or a `MagicItem`. A projectile form fails. `g_realMagic` is written once in `NoteEffect`, expires in 0.6 s, and is read only when both hands are empty and the caster matches. The hands loop then takes the first non-ignore match. The right hand is checked first, so a sword or staff there beats a spell in the left. A null attacker never consults `g_realMagic`.

Destruction from a staff is bludgeoning melee. An off-hand spell while a weapon is out is a weapon wound, with `physicalMelee` set, so the tick uses melee hit rules. A burn tick after the caster switches weapons, or a concentration tick with a null attacker and no fresh hit event, becomes generic script damage.

Fix: if the hit form is not a weapon, prefer `g_realMagic` or a non-weapon hand over an equipped weapon, and refresh that record on each damage tick. Keep the 0.6 s window as a fallback after the effect ends.

### Sexual parts on counts lungs in the health capacity

`InjuryEngine::ActiveParts` and `CapacityDamage`.

`ActiveParts()` returns `kPartCount` (12) when sexual parts are enabled. Index 11 is lungs. `CapacityDamage` sums `partMax[0..11]` and then subtracts one genital, so lungs' `partMax` (filled from `damage_max`, 20, and never overwritten by `ReadLungs`) sits inside the "100% of max health" budget. The same bound drives `OverlayJson`: `overlayOrder` defaults to `{0,3,2,1,5,4,6,7,8,9,10,11}`, so the bar list grows a lungs slot. `partMax` for lungs is otherwise unused, and drowning is supposed to sit outside the capacity.

With the shipped numbers that is 168 instead of 148 (core 120 + nipples 8 + one genital 8 + lips 6 + ass 6). Every wound is a smaller share of health than the files describe, and a fully filled hittable body never reaches 100% of the capacity. The setting defaults off.

Fix: when sexual parts are on, `ActiveParts()` should return `kPartLungs` (11). Keep the existing `part == kPartLungs` exception in `PartActive` so drowning still lands.

### `g_lock` is still held across health changes

`TakeHealth` (from `TortureHit` under the lock), `ConvertHealth`, `HealPart`, and `SyncHealth` (then `UpdateBleedout` → `Classifier::OnStoodUp`).

`g_lock` is a non-recursive `std::mutex`. Those functions call `DamageActorValue` / `RestoreActorValue` while it is held. `SyncHealth` also holds it across `LookupByID` and actor-value reads for every tracked actor.

The plugin's own damage hooks are safe on this path: `HandleHealthDamage` and `ModActorValue` see `IsInternalDamage()` and return before taking `g_lock`. What remains is any other same-thread callback from those actor-value calls into something that locks `g_lock` again, or an engine lock that a worker already inside `OnDamage` is waiting on. That is the freeze class left after the `VisitEffects` fix. It is not removed in the current code.

Fix: copy the actor id and the amount, release `g_lock`, then change health under `InternalScope` only. Do the same for `OnStoodUp`.

### Old external strikes narrate beside an open scene

`Injury_Engine_Main.Strike` (`External_Strike`, `External_Torture`).

`Strike` always `DirectNarrate`s and never enters the scene. `External_Damage` goes through `NarrateHits`, which joins the scene and uses `ClaimNarration`. `Strike` does neither, and it does not clear an open scene.

A caller still on `External_Strike` / `External_Torture` forces a line immediately, even mid-reply, and an open torture scene for that victim can still speak `again` on top of it.

Fix: route `Strike` through `NarrateHits` so it uses the same gate and the same scene as `External_Damage`.

### Decision timeline reads `text`, which these events do not have

`TruceDecision::BuildTimeline`.

The loop keeps a line only when `e.value("text", ...)` is non-empty. `PublicGetRecentEvents` puts the line in `data` (a dialogue object `{"dialogue": "..."}`, or a JSON string), not in a top-level `text` field. The `try` covers only `parse`. A present but non-string `type` or `originatingActorName` (nlohmann throws when the key exists and the type differs) leaves the speaker in flight for 60 seconds.

Once the CRT crash above is fixed, NPC decisions still go out with `"timeline": []`.

Fix: read `data` for `dialogue` / `line` / `text`, and use `is_string()` so a bad field skips the event.

## Low

### Death text and hit rolls share an unlocked RNG

`InjuryEngine::Rng`. `DeathText` calls it with no lock. `WeightedPart` / `RandomIndex` use it on the damage threads while `g_lock` is held. `std::mt19937` is not thread-safe. `TESDeathEvent` and Papyrus `DeathText` run off those threads. In practice that is a bad part or soil roll. It can also crash.

Fix: lock around `Rng()`, or give the death path its own generator.

### Revert leaves two hit-flag arrays from the previous session

`InjuryEngine::Revert` clears `g_hitVictims` and `g_hitAttackers` and leaves `g_hitUnarmed` and `g_hitCounts`. `Load` does not clear them either. An empty `TakeHits` deliberately does not wipe those arrays. After loading a save, `HitUnarmed` / `HitCount` can still return the previous session's values until the next non-empty `TakeHits`.

Fix: clear both arrays in `Revert`.

### One `thread_local` "already handled" slot

`HandledByProcessHit` stores one victim per thread for 100 ms. `HandleHealthDamage` on another thread does not see it. A second hit on that victim within 100 ms on the same thread can also match. The punch's points are applied in `ProcessHitData` and again in `HandleHealthDamage`. Health stays put. The torture wound is doubled. A torture tick that shares that 100 ms is restored and not counted.

Fix: key the guard by victim plus a per-hit token.

### `heal_tick_active` is not cleared on load

`Injury_Engine_Main.Setup` clears `scene_tick_active`, `hits_draining`, and `sync_pending`. `heal_tick_active` is an `Auto` property, so it is saved. `HealTick` returns immediately while it is true. It is only set for the body of `HealTick`, which does not `Wait`, so a normal save should not catch it. It is the one flag a bad save cannot recover from.

Fix: set `heal_tick_active = False` in `Setup`.

### `npc` / `combat` numbers are treated as "don't care"

`InjuryConfig::TriBool`. Only a JSON bool becomes 0 or 1. A present `1` or `0` returns -1 with no log, so `"npc": 1` does not restrict the rule to NPCs.

Fix: if the key is present and is not a bool, log and skip the rule.

### Config readers never take the lock `Load` takes

`InjuryConfig::Load` locks, then `g_sources.clear()`. `Find` returns `const Source*` into that vector. The other readers do not lock. Today `Load` runs only from `kDataLoaded`, so this does not fire in game. A second `Load` while a native is inside `Find` is a data race and a dangling source pointer.

## Checked and holding

- **`OnDamage` / `PickPart`.** A full tortured part is picked only when the remainder can replace a torture point. The real-point pass passes `canReplace == false`. The outer loop stops at 1000 rounds. A `PickPart` of -1 ends the outer loop. This is the 2026-10-17 freeze fix, and it is in the current source.
- **Heal-effect scan.** `CollectHealingEffects` runs before `g_lock`. `Seen` runs after the lock. `NotifyPapyrus` runs after unlock. `Overlay::Notify` is called from Papyrus after the native returns.
- **`HandleHealthDamage` return.** Damage is already applied before that hook. Torture returns 0 and the hook gives back `amount - kept`. `IsInternalDamage` is thread-local. Punches that `ProcessHitData` does zero are restored if any health still landed. A hit that is already lethal stays fatal. That limit is the documented one. The punch hole above is the case the zeroing misses.
- **Hook installs.** `ProcessHitData` is `RelocationID(37673, 38627)` plus `VariantOffset(0x3C0, 0x4A8)`, and `GetFactionFightReaction` is `(37672, 38626)` plus `(0x187, 0x182)`. Both require opcode `0xE8`, and both return on VR before the address is used. `HandleHealthDamage` slot `0x104` is not installed on VR. `ModifyActorValue` slot `0x20` is installed on VR with a log line that the slot is unverified.
- **Torture health ticks.** The original is not called. The actor value used is `a_av != kNone ? a_av : this->actorValue`. `GetCasterActor()` is the attacker. Dual Stamina / Magicka is applied with `ModActorValue` and the original is not called, so the secondary drain is not applied twice. Health is excluded from that secondary drain.
- **Lock order.** Hooks drop `Classifier::g_lock` before `OnDamage`. The nest is `SyncHealth` holding `InjuryEngine::g_lock` and then `OnStoodUp` taking `Classifier::g_lock`. Nothing holds the classifier lock and then waits for the injury lock.
- **Co-save.** Version 6 writes 12 parts. Load uses 6 / 11 / 12 for v2 / v3–v5 / v6 and resolves form IDs. The first `TESDeathEvent` consumes `g_lastReal`.
- **Drowning order**, when the recent-hit early return does not fire: drowning is checked before falling, only with no hit form and no attacker, `kUnderwater` or submerged `>= 0.85`, water breathing above 0 suppresses it.
- **Papyrus scene gate**, inside one process. A start or switch is marked sent before `SceneSend`. `scene_tick_active` makes a nested `OnUpdate` return. `ClaimNarration` is test-and-set. One line per tick: switch, else start, else again. Deaths are drained before hits. A dead victim's hit is not narrated.
- **Truce apply**, for a single partner in one session. A duplicate `Ask` for the same speaker and target is ignored. A second target while one is in flight is refused at once. Refused `StopFighting` stops the speaker and does not change Aggression. Accepted truce stops both and calms each NPC once. `StartFighting` restores both, `ForgetTruces`, `ForgetCalm`, then `StartCombat`.
- **`Torture_Target` double count**, in a run that is not saved mid-action. `KEY_IN_ACTION` is set before the swings and held through the 0.5 s tail. `OnEffectStart` sees it and does not call `OnLash`.

## In-game checks, when these are fixed

1. NPC `Stop_Fighting` against another NPC, with recent dialogue on the target. Expected: no crash, and the decision context contains dialogue lines.
2. Quit to desktop during a whip scene or a heal, relaunch, load. Expected: no `again` line, and the 0.2 s loop stops once nothing is tracked.
3. Whip in one hand, Flames (torture) in the other, on a victim who is also underwater. Expected: torture points, health held at the floor, no kill from the beam.
4. Punch a victim who still has Flames (torture) on them, power attack. Expected: torture points, health unchanged.
5. Torture to bleedout, let it tick to one point, then one real hit that replaces that point and leaves no real wound. Expected: the actor stands up.
6. Save and reload during `Torture_Target`, then hit the same NPC with the Padded Whip by hand. Expected: a whip wound.
7. Accept two truces on one NPC (player, then another NPC), save, reload, attack them. Expected: both pairs stay stopped.
