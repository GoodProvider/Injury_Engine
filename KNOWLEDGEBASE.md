# KNOWLEDGEBASE

Quirks and lessons for SkyrimNet_Whipped. Read before changes; append a dated entry (newest first) after a session that found something non-obvious. Entries marked *(from SkyrimNet_SexLab)* were learned there and apply here too.

## Injury JSON shadowed by a stale copy in MO2 overwrite (2026-10-11)

MO2 overwrite wins over the mod folder, so an old `overwrite/SKSE/Plugins/SkyrimNet_Whipped/injuries/` (left by a tool run under MO2) silently replaces the repo JSON. Seen with files from before `classify` existed: whipping still worked (it needs no classify rule), but Sparks (torture) did plain damage with no hit or narration. Symptoms in `SkyrimNet_Whipped.log`: `generic.json missing` although generic.json is listed as loaded, no `Classifier: torture MGEF ...` lines, and now `injuries: no classify rules in ...`. Fix: move the folder out of overwrite.

## Torture spells: classify MGEF before resist health (2026-10-11)

`(torture)` spells list the torture MGEF first and a vanilla resist effect second. `Classifier::Init` resolves `effect_file` / `effect_id` to a runtime form id (`LookupForm`, else ESL `0xFE000000 | (smallFileCompileIndex << 12) | localId`). `MatchMagic` prefers that torture MGEF over resist-only health rules (`shocking`, …); vanilla Flames / Frostbite / Sparks stay health. `OnTortureDamage` lands one point on the first tick of `fire` / `frost` / `shock` beams. `Main.NarrateHits` sends `SceneSend("start", …)` immediately on the first registered hit of any torture source (method change uses `start` again, not `switch`).

## DirectNarration reenters Papyrus during SceneTick (2026-10-11)

`SkyrimNetApi.DirectNarration` can run other scripts (eligibility checks, and so on) before it returns. If `SceneTick` marks torture pairs as started only after `SceneSend`, a nested `OnUpdate` still sees `SC_PENDING` and sends the same `start` line twice. Claim state before `SceneSend` (`SC_RUNNING` for starts, clear `KEY_SC_SWITCH` for method changes) and ignore a nested `SceneTick` with `scene_tick_active`.

## Concentration torture beams use HandleHealthDamage per tick (2026-10-11)

Sparks / Flames / Frostbite `(torture)` are concentration spells with a projectile; each tick can arrive in `Actor::HandleHealthDamage`, not only in `ModActorValue`. Those ticks must **not** set `physicalMelee` (they land `hook.points_per_second` points per real second like other spell torture). When the hit form is a projectile or otherwise fails `ClassifyForm`, `Classifier` must prefer the live `(torture)` effect from `g_torture` over guessing unarmed `punching` on an out-of-combat NPC with empty hands.

## Never walk active effects from damage hooks (2026-10-10)

`ModActorValue` / `HandleHealthDamage` hooks run inside the engine's own active-effect update (a value-modifier effect applying damage). A nested `VisitActiveEffects` / `GetActiveEffectList` walk there crashes when effects are dispelled in the same update (`SkyrimSE+0FAEDF5`, `mov rax,[rcx]` reading 0x1, `MagicTarget::EffectVisitor` on the stack; seen right after removing a DD armbinder). Track effects as they land instead: `Classifier::NoteEffect` (AddTarget hook) and hit events feed `g_torture`, read back within `kTortureWindow`.

## Torture replaced the whip model; hooked torture needs in-game verification (2026-10-09)

Whipping is now one of eight `model: "torture"` types (`"whip"` is still read as torture). Each (type, part) holds one deficit; a part counts only its **largest** type for the health hold and the overlay. Healing lowers every type on the part by one (slap 3, pinch 4, fire 1, heal 2 gives 1 / 2 / 0); real damage replacing torture lowers the maximum by one and clamps every type to it (convert 2 gives 2 / 2 / 1). A hit on a type that sits below the part's maximum costs no health. Co-save record 4; records 2 / 3 load as whipping (`deficit = damage > 0 ? healing_max + damage : healing_max - healing`). Unverified, check the `classify` / `torture damage` lines with verbose logging: (1) fire / frost / shock torture comes only from the "(torture)" spells (MGEF 0x813–0x815, copies of vanilla Flames / Frostbite / Sparks effects; matched by form, in or out of combat; vanilla spells are always real damage); (2) concentration spells may tick after the 0.6 s `TESHitEvent` window, so `Classifier` falls back to scanning the victim's active effects for a torture MGEF; (3) an out-of-combat punch is classified before combat starts (the attacker's `IsInCombat()` at the hit); (4) the hooks return 0 for torture, so the game applies no damage and `TakeHealth` applies the floor-limited torture loss instead. `kMaxSources` is 32 (8 torture + 8 health files already filled 16).

## PrismaUI view path (from SkyrimNet_SexLab, 2026-07-24)

`CreateView("SkyrimNet_Whipped/overlay.html")` loads from **`Data/PrismaUI/views/`**, not `SKSE/Plugins/`. `make release` copies `PrismaUI/` into the package. A missing file gives a valid-looking C++ path but no visible UI. Do not `Focus` a view that should not pause the game; the overlay is `Show`-only.

## Separate PrismaUI Invokes are not applied in order (from SkyrimNet_SexLab, 2026-10-03)

`PrismaUI->Invoke` is async; two Invokes can land in either order. A state change that must not interleave goes out as **one** Invoke calling one JS entry point (here: `whipUpdate(json)` with the whole state).

## `window.prompt` / `confirm` do nothing in PrismaUI (from SkyrimNet_SexLab, 2026-09-27)

Ultralight returns as if cancelled. Build text entry and confirmations in HTML.

## Papyrus default args are filled at the caller's compile (from SkyrimNet_SexLab)

Adding a parameter (even with a default) needs every caller recompiled, or the stale `.pex` logs `Expected N, got M` and the call returns None. Pass every argument in cross-script calls. See [docs/reference/papyrus-rules.md](docs/reference/papyrus-rules.md).

## ESL FormIDs are signed in Papyrus (from SkyrimNet_SexLab, 2026-09-15)

`0xFE057894` arrives as `-33195884`. C++ must `static_cast<uint32_t>` before `LookupByID`.

## SKSE native params must use engine types (from SkyrimNet_SexLab, 2026-07-25)

Natives take `RE::Actor*`, `RE::BSFixedString`, etc. Mismatched types fail to bind or crash.

## The .pex and the .dll deploy in opposite directions (from SkyrimNet_SexLab, 2026-09-23)

Pyro writes `.pex` into this repo's `Scripts/`; the DLL build copies into this repo's `SKSE/Plugins/`. Whatever the game loads comes from the MO2 mod folder — confirm the game is reading this folder (not an installed release copy) before concluding a fix "did not work".

## Action eligibility cannot use Papyrus decorators (from SkyrimNet_SexLab, 2026-09-14)

`CallDecoratorDirect` returns empty on a cache miss. Use inventory, factions, globals, or Papyrus eligibility functions (as `Whip_IsEligible` does).

## Papyrus runs while a focused PrismaUI view pauses the game (from SkyrimNet_SexLab, 2026-09-28)

Paused game time does not stop Papyrus. Not an issue for the Show-only overlay, but matters if a focused panel is ever added.

## RenderTemplate binds its variable as a string; SkyrimNet inja has no `lower` / `+` (2026-10-04)

`SkyrimNetApi.RenderTemplate(name, "heal", json)` sets `heal` to the JSON *string*: SkyrimNet.log shows `variable 'heal.kind' not found`, the template silently takes its last `else` branch, and the result passes the "Error" / `{{` checks. Load the template with `RenderTemplate` (logic inside `{% raw %}`), then render with `ParseString(source, "heal", json)`, which parses JSON. SkyrimNet's inja also lacks `lower()` and string `+` (`variable 'add' not found`); write joined phrases inline with `{% if %}` blocks, and send ints rather than Papyrus string literals (those get re-cased, e.g. `Start`, `SPELL`). Check `SkyrimNet.log` for `PromptEngineRender.cpp` "missing variable(s)" after any helper prompt change.

## inja array literals are parsed as JSON: no variables inside `[...]` (2026-10-04)

`join([heal.caster, "'s ", noun], "")` fails the whole render while parsing, not just its branch: `PromptEngineRender.cpp ... RenderTemplateString: Exception during rendering: [json.exception.parse_error.101] ... last read: '[h'`. Every heal narration fell back to the Papyrus sentence. Only constant arrays work; build phrases inline (`{% if named_caster %}{{ heal.caster }}'s{% else %}the{% endif %} {{ noun }}`).

## Health ratio must cover the full deficit, or the wounded level goes negative (2026-10-04)

`Deficit()` is damage **plus missing healing** per part, so its maximum is 6 × (20 + 11) = 186, not 6 × 20. With `/ 120`, a deficit of 180 gave `wounded:-35`; the regen take-back pushed health to -21.5 (bleedout), and when health came back (bleedout recovery jumped it to full) the whole deficit converted in one 0.2 s tick: "pain gone" and "fully healed" at once. `Ratio()` now divides by `kPartCount * kMaxDeficit`, and `ConvertHealth` clamps the wounded level to `kHealthFloor`.

## CommonLibSSE: `GetActiveEffectList()` is unreliable on VR; use `VisitActiveEffects` (2026-10-05, updated 2026-10-09)

In cross-runtime builds (SE+AE+VR, our default) `MagicTarget::GetActiveEffectList()` is non-virtual. On alandtse v11 (`62a1936`) it returned nullptr on VR, still compiled, and failed silently; on MinLL 4.39.5 VR gets a thread-local snapshot that is only valid until the next call. Walk active effects with `MagicTarget::VisitActiveEffects` (the engine's own walk, correct on every runtime).

## CommonLibSSE moved to MIT MinLL/CommonLibVR 4.39.5 via overlay port (2026-10-09)

`SKSE_Source/vcpkg-ports/commonlibsse-ng-fork` pins `MinLL/CommonLibVR` 4.39.5 (`550cc4f`, MIT; branched from v4.39.3, the last MIT CommonLibVR, upstream is GPL-3.0 from v5.0.0) and adds SE 1.7.99 / 1.7.104. It is a copy of `../SkyrimNet_SexLab/extern/vcpkg-ports/commonlibsse-ng-fork` (also used by Leashed and DOM); bump all copies together. Before this the plugin `add_subdirectory`'d SexLab's alandtse v11.0.0 (GPL-3.0, not compatible with the Skyrim EULA). The portfile fetches ValveSoftware/openvr into `extern/openvr` (the GitHub tarball leaves the submodule empty) and builds with `SKSE_SUPPORT_XBYAK=off` (we only use `write_vfunc`, no trampoline). `CMakeLists.txt` uses `find_package(CommonLibSSE CONFIG REQUIRED)` plus `find_package(directxtk)` (named in CommonLib's link interface). After switching, delete `SKSE_Source/build/release` (its cache still holds `CommonLibPath`). Plugin source needed no API changes.

## Injury data lives in `SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json`; the DLL reads them at kDataLoaded (2026-10-06)

`InjuryConfig::Load` looks next to the DLL (`<dll dir>/SkyrimNet_Whipped/injuries/`, falling back to `Data/SKSE/Plugins/SkyrimNet_Whipped/injuries/` under the working directory), so under MO2 the folder must be in the mod's `SKSE/Plugins/`. A JSON file that fails to parse is skipped and named in `SkyrimNet_Whipped.log` (`injuries: <file> skipped: ...`); a missing `whipping.json` disables whipping, a missing `generic.json` stops unclassifiable damage being tracked. Source indices follow load order (whipping first, then by file name) and are session-local: the co-save stores source **ids**, never indices. `injuries/guide.md` is not loaded. Edits need a game restart. `manifest.yaml` (dashboard schema) still has to mirror the `defaults` block of `whipping.json`.

## Real damage replaces whip damage; the damage hooks need in-game verification (2026-10-06)

Whip points hold health down by `deficit × 0.9 × max / 186`; real points are worth `max / 120` health each (6 parts × 20 = 100% of max). A real hit while whip points remain is cut by `min(damage, whip hold)` **inside the hook** (the original is called with the reduced amount), the same health worth of whip points is removed and real points added on the same part (one attack = one part, whipped parts first, spilling to another part only when full), so health only drops once all whip wounds are replaced and the 10% floor never blocks real damage. Unverified assumptions, turn on `whip.debug.verbose_logging` and check the `[hook] ...` lines in `SkyrimNet_Whipped.log`: (1) `Actor::HandleHealthDamage` (vfunc 0x104; the VR slot is not verified, so the hook is skipped on VR) receives the damage as a positive amount (the code uses `fabs` and keeps the sign); (2) melee damage reaches `HandleHealthDamage`, everything else `ActorValueOwner::ModActorValue(kDamage, Health)` (vfunc 6); (3) a nested `ModActorValue` call inside `HandleHealthDamage` is ignored through a thread-local scope, so a damage that reaches both is not counted twice; (4) `TESHitEvent` may fire after the damage, so `Classifier::Classify` falls back to the attacker's equipped weapon, then to unarmed (people bludgeoning, creatures mauling), then `generic`. Damage the engine does itself (`TakeHealth`, regen take-back) is wrapped in `InternalScope` and never classified.

## Papyrus: `key` is reserved, native strings come back case-folded risk (2026-10-06)

A Papyrus parameter named `key` fails to compile ("key is not a variable"); use `setting` or similar. Strings returned by natives are `BSFixedString` and Papyrus interns strings case-insensitively (the reason `kind` / `source` travel as numbers); narration text from `HitText` / `HealText` is long and unique enough that this has not shown up, but if a sentence ever comes back with odd casing, return it through a different channel (e.g. `std::string` registered as a plain string).

## CommonLibSSE vtable slot comments are hex (2026-10-06)

`// 104` next to `Actor::HandleHealthDamage` in `RE/A/Actor.h` means slot `0x104` (260). Hooking decimal `104` (`0x68`, `Unk_68`, called every movement update) crashed the game a second after loading a save, inside our thunk (`Classifier::Classify` read the bogus "attacker" argument), with a call stack through actor movement / `bhkCharacterProxy` and no `[hook]` log line. Always write vtable indices in hex (`0x104`, `0x6`, `0x1`), exactly as the CommonLib comment shows.

## SkyrimNet has no silent-narration call; events are the silent path (2026-10-06)

`SkyrimNetApi` offers `DirectNarration` (forces a reply) and `RegisterEvent` / `RegisterShortLivedEvent` (recorded, nobody is made to speak); there is no "silent" flag. Passive healing (game time, natural regen, `pain_gone.none` / `full.none`) uses `RegisterShortLivedEvent` so it is in the LLM's recent history (for `event_ttl`) without anyone remarking on it; non-whip hits are short-lived events too. Since 2026-10-06 every event this mod sends is a `RegisterShortLivedEvent` (`event_ttl`); permanent history comes only from DirectNarrations (summaries, fresh hits / heals, fully healed).

## Papyrus builds trace arguments even when the trace is gated (2026-10-06)

Papyrus has no lazy evaluation: `Verbose("f", "x:" + victim.GetDistance(a))` makes the native call and the string even with verbose logging off. Wrap any verbose trace whose message calls a native, measures distance or loops in `if verbose_logging` (see `docs/developers/papyrus.md`). The same holds in C++ for `SKSE::log::debug` arguments (spdlog drops the line but the arguments are evaluated), so keep debug lines off per-tick paths unless something changed.

## Names passed into natives arrive with another mod's casing (2026-10-09)

The case-folding risk above happened in the other direction: SkyrimNet_SexLab passed `"pussy"` to `External_Damage`, and `PartIndex` received `"Pussy"` (the BSFixedString pool had interned that spelling first). The case-sensitive compare failed, so the slap landed on a random part ("slaps Nina's pussy… Nina's right arm is slapped"). `InjuryEngine::PartIndex` and `InjuryConfig::IndexOf` (source ids) now compare with `InjuryConfig::IEquals`. Any native that matches a Papyrus string against names from JSON must use it.

## Torture scene phase strings arrive as Start / End (2026-10-11)

`Main.SceneTick` passes `"start"` and `"end"` into `Engine.SceneText`; Papyrus re-interns them as `"Start"` and `"End"` (same pool as `Start` / `SPELL` in the note above). `"again"` is unchanged. `InjuryEngine::SceneText` folds the phase to lowercase before comparing or building `session.json` paths (`hit.start`, `inline.end`, …). Without that, the first combined torture line was only the attacker name plus `narration.hit.levels` (e.g. `Bob , the small flame passed slowly…`).

## Sessions are exclusive per victim (2026-10-11)

A torture scene and a heal session used to run side by side, so the scene's periodic `again` lines followed the heal `start` line. Now a different activity silently closes the open session: `HealOpen` and non-torture `NarrateRealHits` call `ClearTortureScene`; torture and real hits call `ClearHealBatch`. `TakeHits` also carries classifier torture (punching, fire, frost, shock), so `NarrateRealHits` checks `IsTorture` before clearing the scene.

## RegisterForSingleUpdate replaces a pending update (2026-10-11)

Each hooked hit called `StartSync` → `RegisterForSingleUpdate(0.2)`, which resets the timer. A concentration torture beam postponed `OnUpdate` until the hits paused, so short-lived `torture_hit` events lagged seconds behind damage. `StartSync` now sets `sync_pending` and skips scheduling when an update is already waiting; `OnInjury` drains `TakeHits`, `TakeQueuedHealActors`, and `TakeSyncEvents` immediately so short-lived events land in the same frame as the hook.

## Removed Papyrus-to-DLL contract needs both rebuilt (stale .pex)

Removing a scene phase (the torture "end" line) from `InjuryEngine::SceneText` and `Main.psc` only works if `pyro` is rerun as well as `make dll`. A stale `.pex` still calls `SceneText("end", ...)`; the new DLL has no such branch, so the generic path rendered `"Bob , and it is too much. ..."` (empty `hit.end` / `inline.end` plus the level text) about 13 s after the last hit. `SceneText` now returns an empty string with a warning for any phase other than start / again / switch, and `SceneSend` skips empty lines. Symptom of a stale `.pex`: half-rendered scene lines; compare `Scripts/*.pex` timestamps with `Scripts/Source/*.psc`.

## Injury point log lines are throttled (skipped:N)

`LogInjuryPoint` writes at most one `torture:` / `health:` line per second per category + attacker + victim + source; hits in between only increment a counter, and the next logged line ends with `skipped:<n>`. The part is not in the key, so the shown part is just the first hit after the window. A final burst's count is only reported if another line for that key follows. Use `whip.debug.verbose_logging` debug lines (`hit`, `real hit`) when every hit matters.

- **Narration log format:** `SkyrimNet_Whipped.log` lines are `[hh:mm:ss] <category>:<direct|event|short>:<start|switch|update|finish> <text>`. Scene / heal phases map again->update, end->finish; one-shot lines default to `start`. `Engine.LogLine` takes (category, narrationType, sessionType, text), so DLL and .pex must be deployed together.

## TakeHits / HitVictims race across Papyrus threads (2026-10-11)

`TakeHits` fills a shared C++ buffer that `HitVictims` / `HitAttackers` / `HitUnarmed` read afterwards. Each hooked hit fires `SkyrimNet_Whipped_Injury`, so several `OnInjury` threads (plus `OnUpdate`) ran `NarrateRealHits` at once. One thread's `TakeHits` cleared the arrays (even when it took nothing) while another was between `TakeHits` and `HitVictims`, so whole batches died with `Array index N is out of range` at `NarrateRealHits`. The first shock batch, the one that sends the scene `start`, was lost this way. Now `TakeHits` returns early without clearing when the queue is empty, and `NarrateRealHits` is guarded by `hits_draining` and loops until `TakeHits` is empty. Any native pair of the form "take + read parallel arrays" needs the same guard.

## The attacker's equipped object can be a spell (2026-10-11)

`Classifier::Classify` falls back to the attacker's equipped objects once the hit event is older than `kHitWindow`. With "Sparks (torture)" equipped, that matched shock and set `physicalMelee = true`, so every spell tick (each frame) landed a full melee hit: ~45 points a second instead of `hook.points_per_second`, health to the floor in 3 s, and ~50 Papyrus events a second that delayed narration by several seconds. Only an equipped weapon (`IsWeapon()`) counts as melee now.

## Per-hit Papyrus narration lags behind fast hits (2026-10-11)

Every queued hit used to get its own `NarrateRealHits` → `NarrateHits` pass (HitText, scene StorageUtil, visibility, short-lived event). At about 2 hits a second (spell ticks, quick whipping) Papyrus fell behind and `torture:short` lines came 1 to 6 s after the hit, and several shorts with the same event id (same attacker, victim, method) landed in one second although SkyrimNet keeps only the last. Now the queue merges in C++: hits of one victim, attacker and source collapse into one entry (`count`, latest part and level) and `TakeHits` releases one per key per second; a real (health) hit also releases that victim's held torture hits. Scripted strikes (`OnLash`, `Torture`) go through `QueueHit` so they are gated the same way. A merged entry narrates as "again and again" (`count` > 1).
