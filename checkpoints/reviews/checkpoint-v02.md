# Code review checkpoint v02 (2026-10-18)

This is a second review of the same tree as [checkpoint-v01.md](checkpoint-v01.md), at plugin version 0.1.1 in `SKSE_Source/CMakeLists.txt`. Since v01, only docs have changed (CHANGELOG-player.md, z-plan.md, the review files). I reviewed against the source, not a running game. No code was changed.

Scope:
- Read in full: `Hooks.cpp`, `Classifier.cpp`, `TruceDecision.cpp`, `Overlay.cpp`, `plugin.cpp`.
- `InjuryEngine.cpp`: the state model, `OnDamage`, `TortureHit`, `SyncHealth` / bleedout, the death sink, and the co-save.
- `InjuryConfig.cpp`: `Load`, `ReadBody`, `ReadSexualParts`, `FinishSources`.
- The native wrappers in `Papyrus_Engine.cpp`.
- Papyrus: `Injury_Engine_Main.psc` (setup, scene, heal, sync loop, drains), `Injury_Engine_Actions.psc`, `Injury_Engine_LashEffect.psc`, `Injury_Engine_PlayerRef.psc`.
- SkyrimNet `PublicAPI.h`.

Every native in `Register_Engine_Functions` is registered without `callableFromTasklets`. Each native call therefore runs on the game thread, and the calling Papyrus thread yields while it runs. Calls on other objects (`actor.IsDead()`, `GetDistance`, ...) yield too. Other Papyrus threads on the Main quest can run at each of those points. Two of the new findings below come from that.

## Fix these first

1. The release DLL frees SkyrimNet's `std::string` with the static CRT (v01, confirmed). NPC `Stop_Fighting` and surrender can crash.
2. **New:** heal and death events can be dropped or narrated for the wrong actor. Two Papyrus threads drain the shared C++ result buffers at the same time.
3. Saved torture and heal scenes use real-time stamps. They never expire after a relaunch (v01, confirmed).
4. A recent Padded Whip hit event turns the next 0.6 s of damage into an ignore (v01, confirmed).
5. `ProcessHitData` classifies from equipped gear and recent events, not from `a_hit.weapon` (v01, confirmed).
6. A real hit that clears the last torture point drops the actor without `ReleaseDowned` (v01, confirmed).

## v01 findings, re-checked

| v01 finding | Status | Where |
|---|---|---|
| Release build frees SkyrimNet `std::string` with the wrong CRT | Confirmed. CMake forces `/MT` (`CMakeLists.txt:9-10`, again at `:51`). `PublicAPI.h:40` requires `/MD`. Also, `PublicSendCustomDecisionToLLM` takes a `std::function` by value, which crosses the same boundary. Linking `/MD` fixes both. | `TruceDecision.cpp:154` |
| Torture / heal sessions never expire after a relaunch | Confirmed. `Setup` does not clear the `KEY_SC_*` / `KEY_HS_*` lists. | `Injury_Engine_Main.psc:862`, `:1178` |
| Recent whip hit event swallows later damage | Confirmed. `hit.source == -1` is not `-2`, so `Classify` returns at once. | `Classifier.cpp:922-925` |
| `ProcessHitData` can fail to zero a punch or a whip | Confirmed. The thunk calls `Classify` and never reads `a_hit.weapon`. | `Hooks.cpp:350` |
| Queued combat stops ignore `ForgetCalm` / `ForgetTruces` | Confirmed. The `StopCombatLater` task and the truce task call `StopCombatNow` without re-checking. | `Classifier.cpp:516`, `:577` |
| Last torture point cleared by a hit never stands the actor up | Confirmed. Raw `g_actors.erase`, no `Erase`. | `InjuryEngine.cpp:1628-1630` |
| Reload during `Torture_Target` swallows that actor's lashes | Confirmed | `Injury_Engine_Actions.psc:86-120`, `Injury_Engine_LashEffect.psc:12` |
| Heal start lines dropped while SkyrimNet is replying | Confirmed. The removal test runs before the `pend` branch. | `Injury_Engine_Main.psc:1178-1181` |
| Late truce answer applied after the request was dropped | Confirmed | `TruceDecision.cpp:248-253`, `:288-297`, `:313-317`, `:365-371` |
| Separate PrismaUI invokes apply out of order and stick | Confirmed | `Overlay.cpp:57-66`, `:135-161` |
| Bad field type aborts config load outside the per-file catch | Confirmed. `FinishSources` and `ReadClassify` run after the per-file `try`. | `InjuryConfig.cpp:284-285`, `:419-424` |
| Looking away for one tick cancels a torture start | Confirmed | `Injury_Engine_Main.psc:825-827` |
| Only one truce partner is saved | **Corrected.** v01 said "the player is never written". The NPC's `KEY_TRUCE_WITH` does get the player as partner (`GuardTruce` writes it for each calmed actor in the pair). What is missing is any second partner, plus a record keyed on the player's side. The fix is unchanged. | `Injury_Engine_Actions.psc:245-260` |
| Ally override follows what is equipped, not the swing | Confirmed | `Hooks.cpp:327`, `Classifier.cpp:657-687` |
| Classify caches and `kIgnoreFriendlyHits` survive the wrong lifetime | Confirmed. `kPreLoadGame` calls only `ResetTruces`. | `plugin.cpp:67-69` |
| Real spells lose to an equipped weapon, or become generic | Holds as written (not re-tested beyond the hands loop) | `Classifier.cpp:932-983` |
| Sexual parts on counts lungs in the health capacity | Confirmed and **extended** (see below) | `InjuryEngine.cpp:243-246`, `:424-435` |
| `g_lock` held across health changes | Confirmed. `TortureHit` → `TakeHealth` runs under the lock, as does `HealPart`'s `RestoreActorValue`. | `InjuryEngine.cpp:1244`, `:1666` |
| Old external strikes narrate beside an open scene | Not re-read (`Strike`, `Injury_Engine_Main.psc:1592+`). Carried forward. | |
| Decision timeline reads `text` | Confirmed. `PublicAPI.h:208-213` documents the content in `data`. | `TruceDecision.cpp:173` |
| Death text and hit rolls share an unlocked RNG | Confirmed. The death sink calls `DeathText` outside `g_lock`. | `InjuryEngine.cpp:224-228`, `:2292` |
| Revert leaves two hit-flag arrays | Confirmed | `InjuryEngine.cpp:2900-2926` |
| One `thread_local` "already handled" slot | Confirmed | `Hooks.cpp:45-51` |
| `heal_tick_active` not cleared on load | Confirmed | `Injury_Engine_Main.psc:192-196` |
| `npc` / `combat` numbers treated as "don't care" | Not re-read. Carried forward. | |
| Config readers never take the lock `Load` takes | Not re-read. Carried forward. | |

### Extension: lungs also become a torture part

With sexual parts on, `ActiveParts()` returns 12, so index 11 (lungs) also passes the `wanted` test in `TortureHit`:

```1227:1228:SKSE_Source/src/InjuryEngine.cpp
            const bool wanted = part >= 0 && part < ActiveParts() && PartFits(female, part);
            index = wanted ? part : WeightedPart(candidates, count, *weight);
```

`PartIndex("lungs")` returns 11. With the setting on, a `HitPart` with part `"lungs"` (any External API call that passes a part name) puts **torture** points on the lungs. CLAUDE.md says lungs are "never torture". `OverlayJson` then gets a lungs slot, and `PartPointsJson` skips lungs, so the two disagree. v01's fix covers this too: when sexual parts are on, `ActiveParts()` returns `kPartLungs` (11). Lungs stay reachable only through `PartActive`'s explicit exception.

## New findings

### High

#### Sync-event and death buffers are drained by two Papyrus threads at once

`InjuryEngine::DrainPendingEvents`, `Injury_Engine_Main.OnUpdate`, `ProcessSyncEventBatch`, `NarrateDeaths`.

`TakeHits` and `TakeDeaths` keep their result arrays on an empty take ("an empty take must not wipe the arrays another Papyrus thread is still reading"). `DrainPendingEvents` does not. It clears the arrays on every call:

```936:941:SKSE_Source/src/InjuryEngine.cpp
        std::vector<std::int32_t> DrainPendingEvents()
        {
            std::vector<std::int32_t> codes;
            g_syncActors.clear();
            g_syncCasters.clear();
            for (const auto& e : g_events) {
```

`SyncHealth` and `TakeSyncEvents` both end in this function. `OnUpdate` reads the codes and the actors in three separate natives:

```1244:1247:Scripts/Source/Injury_Engine_Main.psc
    int[] codes = Injury_Engine_Engine.SyncHealth()
    if codes.Length > 0
        Actor[] targets = Injury_Engine_Engine.SyncActors()
        Actor[] casters = Injury_Engine_Engine.SyncCasters()
```

Each native call yields. `OnInjury` fires on every `NotifyPapyrus`: every hit, every healing effect added, every uncaused chunk. It runs `DrainInjuryQueues` → `ProcessSyncEventBatch` → `TakeSyncEvents`, and nothing like `hits_draining` keeps it out. If it lands between `SyncHealth()` and `SyncActors()`, it empties the arrays, or refills them with its own events. `OnUpdate` then either:

- reads `targets[i]` past the end. That is a Papyrus array error, and the target comes back as `None`. `NarrateSourceHeal` returns at `target == None`, so heal start, pain gone, fully healed or heal stopped is lost. A lost heal start also means no heal session opens.
- pairs its own codes with the other drain's actors. The line then names the wrong actor.

This happens in normal play: a heal spell on someone who is being hit, or healing during combat.

`NarrateDeaths` has the same shape with no guard. It is called from `OnUpdate` and from `DrainInjuryQueues`. Two non-empty `TakeDeaths` calls can interleave: thread A's `DeathVictims()` / `DeathAttackers()` / `DeathSources()` then return thread B's entries, so A's texts are narrated against B's victims. It takes two killing blows in quick succession (an AoE, a group fight).

Fix: return each drain as one native result. For example, a JSON string or parallel arrays from one call, or FormID buffers that a read native resolves, the way `TakeTruces` → `TruceSpeakers` already does. Until then, make `DrainPendingEvents` leave the arrays alone when `g_events` is empty. Also add `sync_draining` / `deaths_draining` flags that follow the `hits_draining` pattern, so `ProcessSyncEventBatch` and `NarrateDeaths` return while another thread is draining.

### Medium

#### Scene and heal list indices go stale across yielding calls

`Injury_Engine_Main.SceneTick`, `SceneSend`, `HealTick`.

The scene and heal sessions are parallel StorageUtil lists, addressed by index. `ClearTortureScene` and `ClearHealBatch` remove entries from other threads: `NarrateRealHits`, `NarrateDeaths`, `HealOpen`, `OnLash`, `Torture`. Several loops keep an index across a call that yields:

- In the `SceneTick` visibility loop (`:819-829`), `i` is held across `vic.IsDead()` and `PairVisible` (`Is3DLoaded`, `GetDistance`). After a removal below `i`, `IntListSet(KEY_SC_STATE, i, ...)` writes to the next pair. That can mark a visible pending pair `SC_HIDDEN`, which is the v01 start-cancel bug by another route. It can also promote a hidden pair.
- In `SceneSend` (`:744-791`), `picked[]` is held across `SceneLine` → `Injury_Engine_Engine.SceneText`. The loop that follows clears `KEY_SC_CAUSE`, stamps `KEY_SC_NARR`, and takes the speaker and target from `picked[0]`. All of these can hit the wrong pair. The line then goes out with another pair's actors, or with `None`.
- In `HealTick` (`:1172-1186`), `i` is held across `IsHealing` and `HealSend`. `HealSend` re-finds its own index after the claim, but `HealTick`'s next `HealRemove(i)` or `KEY_HS_LAST` stamp can still land on a shifted entry.

The code already re-finds the index after `ClaimNarration` ("the lists may have changed during the claim"). The other yields in these loops are just as open.

Fix: re-find by (attacker, victim) or by target after each yielding call before writing. Or keep a scene / heal generation counter, bump it in `SceneRemove` / `HealRemove`, and abandon the pass when it changed.

#### Hot-path form lookups by plugin name

`Classifier::EffectMatchesRule`, `MatchWeapon`, `IsIgnoredWeapon`.

`EffectMatchesRule` calls `LookupForm(rule.effectFile, rule.effectId)` first. That is `TESDataHandler::LookupForm(id, name)`, a linear walk of the file list with a name compare. Only after that does it try the `g_classifyEffectForms` cache, and the key for that cache is a freshly built `std::string` (`EffectRuleKey`). The call chain is:

- `ModifyEffectActorValue` → `TortureSourceOfEffect` / `TortureSourceOfSpell`, on every negative Health tick of every value-modifier effect on every actor: poison, fire, frost, any damage-over-time.
- `AddTarget` → `NoteEffect`, for every effect applied.
- `HitSink` for every hit event (`TortureIndexOf` per spell effect, then `IsIgnoredWeapon`).
- `MatchWeapon` and `IsWhipAttacker`, which look up the ignore weapon the same way on every classify and every fight-reaction call.

With a few hundred plugins and several effect rules, that is thousands of string compares per frame in a busy fight, inside the engine's effect update.

Fix: in `Classifier::Init`, resolve the ignore weapon(s) and every rule's effect to `RE::FormID` once (the cache already does the ESL math). Then compare IDs. Drop the per-call `LookupForm` and the key string.

### Low

#### Missing or short `sexual_parts.json` leaves region indices out of range

`InjuryConfig::ReadBody` / `ReadSexualParts`, read in `InjuryEngine::StateJson` and `DeathText`.

Shipped `whipping.json` lists 4 regions (`head`, `body`, `arm`, `leg`), so `ReadBody` appends `lungs` as region 4 and `regionName` has 5 entries. The default `partRegion` for parts 6–10 is `{4, 5, 6, 7, 8}`. `ReadSexualParts` fixes those indices only when `sexual_parts.json` exists and its `parts` array has exactly 5 entries. If the file is missing, fails to parse, or has a different count, parts 7–10 keep region indices 5–8. With sexual parts on, a wound there then reaches:

```2058:2058:SKSE_Source/src/InjuryEngine.cpp
                const std::string& region = body.regionName[body.partRegion[i]];
```

and `DeathText` at `:2219`. Both read past the end of `regionName`, which is undefined behavior and likely a crash in the decorator or the death sink. Part 6 silently uses the `lungs` region text.

Line 227 (`e.value("region", b.regionName[b.partRegion[i]])`) evaluates the same out-of-range default eagerly. It only works with the shipped file because each iteration appends a region before the next default is read.

Fix: after loading, clamp or remap any `partRegion[i] >= regionName.size()` (log it), and compute the line 227 default only when the key is absent.

#### Raw `Actor*` result buffers

`g_hitAttackers`, `g_syncCasters`, `g_deathVictims`, `g_deathAttackers`.

These buffers store `handle.get().get()`. The `NiPointer` is released at the end of that expression, and the raw pointer waits for a later native (`HitAttackers`, `SyncCasters`, `DeathVictims`, ...). If a summon or other temporary actor is deleted between the take and the read, the VM is handed a dangling pointer. The truce buffers already do this right: they store FormIDs and `LookupByID` them inside the read native.

Fix: store FormIDs or handles, and resolve them in the read native.

#### Two `AllocTrampoline(14)` calls

`Hooks.cpp` `InstallFightReaction` (`:408`) and `InstallProcessHitData` (`:391`).

Each install calls `SKSE::AllocTrampoline(14)`. If SKSE's branch pool cannot serve the second request, CommonLib falls back to `Trampoline::create`. That replaces the shared trampoline and frees the previous buffer, so the first `write_call` would jump into freed memory. This is latent with a normal SKSE install.

Fix: one `SKSE::AllocTrampoline(28)` in `Install_Hooks` before both installs.

#### Drowning diagnostics are always on

`Hooks.cpp` `UnderwaterProbe`, `ModBaseActorValue`, `SetActorValue`.

`UnderwaterProbe` logs at info level once a second per hook and actor whenever an actor is wet, and the info level flushes every line. Every attacker-less `HandleHealthDamage` and every negative Health `ModActorValue` also pays for `ReadDrownSignals`. The `ModBaseActorValue` and `SetActorValue` vtable hooks exist only for this probe. Drowning has shipped, so these are diagnosis leftovers.

Fix: gate the probe on verbose logging, or remove it along with the two probe-only hooks.

## Checked and holding (new in v02)

- **Game-thread natives.** Natives are registered without `callableFromTasklets`, so `TruceDecision`'s "game thread only" `g_inFlight` / `g_noRoute` hold for `Ask` and `Reset`. Callbacks hop to the game thread with `AddTask`.
- **Truce buffers.** `TakeTruces` copies the answers, and `TruceSpeakers` / `TruceTargets` resolve FormIDs at read time. This is the pattern the result buffers above should follow.
- **Drain flags.** `hits_draining` and `truces_draining` are test-and-set with no call in between, so they are atomic under the script lock.
- **`ClaimNarration` after a relaunch.** A future `last_direct_time` or a stale `claim_time` is treated as open or taken over.
- **Hit code range.** `fresh*1e8 + source*1e6 + mask*10 + level` fits int32 for `kMaxSources` 32 and a 12-bit mask. `MaskOf` keeps 5 digits.
- **`TortureHit` candidates.** There is always at least one: when a type reaches no active part, the `SourceReach` fallback uses every part that fits.
- **Game-time poll.** `hours_per_point` has a dashboard minimum of 0.1, so `RegisterForSingleUpdateGameTime` cannot spin, and the DLL clamps it again at 0.05.
- **Co-save v6.** It writes 12 `lastHours` and 12 cells per source. Load drops unknown source ids with a warning.
- Everything in v01's "Checked and holding" list still matches the source.

## In-game checks, when these are fixed

1–7: as in v01.

8. Heal an NPC with Healing Hands while another NPC hits them, and repeat with a potion mid-fight. Expected: each heal start, fully healed and heal stopped event names the right actor, and `Papyrus.0.log` has no array index errors from `OnUpdate`.
9. Kill two NPCs within a moment of each other (fireball into a group). Expected: each death line names its own victim and attacker.
10. With sexual parts on, call the External API with part `"lungs"` (`External_Torture` / `External_Damage`). Expected: the torture lands on another part, never the lungs, and the overlay has no lungs slot.
11. Temporarily rename `sexual_parts.json` and turn sexual parts on, then wound a sexual part. Expected: no crash in the decorator, and a warning in `Injury_Engine.log`.
