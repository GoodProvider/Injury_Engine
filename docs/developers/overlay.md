# SKSE plugin + PrismaUI overlay

C++ plugin `Injury_Engine.dll` (injury engine, damage classification and healing hooks, Papyrus natives) and the PrismaUI torture overlay.

Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md). JSON: [../reference/json-keys.md](../reference/json-keys.md#overlay-injuryengineupdate).

## Paths

| Path | Role |
|------|------|
| `SKSE_Source/src/plugin.cpp` | Entry, logger (`SKSE\Injury_Engine.log`), messaging, co-save registration |
| `SKSE_Source/src/InjuryEngine.cpp`, `include/InjuryEngine.h` | Per-actor, per-part, per-source injury (torture and health models), real-damage replacement of torture points, health link, co-save, narration text, `StateJson`, `OverlayJson` |
| `SKSE_Source/src/InjuryConfig.cpp`, `include/InjuryConfig.h` | Loads `SKSE/Plugins/Injury_Engine/injuries/*.json` at kDataLoaded (nlohmann-json), dotted-path lookup, `{token}` rendering |
| `SKSE_Source/src/Classifier.cpp`, `include/Classifier.h` | TESHitEvent sink. Which source a hit becomes is the `classify` array in `injuries/*.json` |
| `SKSE/Plugins/Injury_Engine/injuries/` | Injury data and text per source; `guide.md` is the agent-facing editing guide |
| `SKSE_Source/src/Papyrus_Engine.cpp` | Natives bound to `Injury_Engine_Engine.psc` |
| `SKSE_Source/src/Hooks.cpp` | Vtable hooks: `MagicTarget::AddTarget` (healing sources), `Actor::HandleHealthDamage` (SE / AE) and `ActorValueOwner::ModActorValue` (damage intake, classify, `InjuryEngine::OnDamage`, may reduce the damage) |
| `SKSE_Source/src/Overlay.cpp`, `include/Overlay.h` | PrismaUI view + push |
| `SKSE_Source/src/TruceDecision.cpp`, `include/TruceDecision.h` | `Stop_Fighting` / `Stop_Fighting_by_Yielding`: asks SkyrimNet's decision model (`PublicSendCustomDecisionToLLM`, API v12+; template `accept_truce` or `accept_surrender`) whether the target accepts; answers queued for Papyrus (`TakeTruces`, mod event `Injury_Engine_Truce`). The only file that includes `PublicAPI.h` (it defines its globals) |
| `SKSE_Source/lib/PrismaUI/` | Vendored PrismaUI API header |
| `SKSE/Plugins/Injury_Engine.dll` | Built plugin (copied by the build) |
| `PrismaUI/views/Injury_Engine/overlay.html` | Overlay page under `Data/PrismaUI/views/` |

## Build

`make dll` → `cmake --preset release && cmake --build --preset build-release` in `SKSE_Source/`. Needs `VCPKG_ROOT`. CommonLibSSE is MinLL/CommonLibVR 4.39.5 (MIT) from the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork` (`vcpkg-configuration.json` `overlay-ports`, `find_package(CommonLibSSE)`); a clean configure builds it, ~6 min. Do not edit `SKSE_Source/build/` or vendored `lib/`. The SkyrimNet public API header comes from the devkit next to this repo: `../../SkyrimNet devkit/CppAPI` (`target_include_directories` in `CMakeLists.txt`, as in SkyrimNet_SexLab). The truce decision needs SkyrimNet API v12+ at run time and a release build (`_DEBUG` builds never touch SkyrimNet's `std::string` results); without it every `Stop_Fighting` offer is refused.

## Natives

Declared in `Scripts/Source/Injury_Engine_Engine.psc`, registered in `Papyrus_Engine.cpp`. Native parameters must use engine types (`RE::Actor*`, `RE::BSFixedString`, …). A signature change means rebuilding the DLL and recompiling the `.psc` and every caller.

## Logging

`Injury_Engine.log` (`plugin.cpp` `InitLog`) starts at `info`. The native `SetVerboseLogging` (setting `injury_engine.debug.verbose_logging`, pushed by `ApplyPluginConfig`) switches the spdlog level to `debug`, so `SKSE::log::debug` lines appear: hook damage, classifier decisions, health taken / converted, taken hits, sync events, overlay pushes, co-save contents. Anything logged before the first `ApplyPluginConfig` (plugin load, `kDataLoaded`, co-save load) is at info level only. Arguments of `debug` calls are still evaluated when the level is `info`, so keep them cheap on hot paths (`ConvertHealth` logs only when points were healed).

## Overlay

- Setting `injury_engine.overlay.enabled` ("Show torture overlay").
- `Overlay::Init` on `kDataLoaded`: PrismaUI API, `CreateView("Injury_Engine/overlay.html")`. PrismaUI is a listed requirement; if it is missing anyway → no overlay, everything else works.
- The natives `Hit`, `SyncHealth`, `TakeHits` and `Forget` call `Overlay::Notify`, which pushes `OverlayJson` to JS `injuryEngineUpdate(json)` only when it differs from the last push.
- The view is shown, never focused (no pause, no cursor).
- Page: `#InjuryEnginePanel` with a white title ("Torture Damage", from `whipping.json` `overlay.title`) above `.actor-injuryengine-damage-bars`: one name-only bar per tortured actor, most recently hit first; the bar shows the actor's latest torture type only (`OverlaySource`: open type hit most recently, transient `g_lastTorture`; most points after a load): width = that type's points out of the parts it reaches (`SourceReach`, shared with `TortureHit`), filled in that type's `overlay_colors` by average stage, light blue (`overlay.colors.healing`) while a healing source works on the actor (heal session). Real damage replacing torture damage grows a red segment (`overlay.colors.replaced`) from the left while the torture fill shrinks; an actor with no torture damage left is dropped and the panel hides when nobody has torture damage; the whole overlay fades 3 s after the last change. Numbers, thresholds and colors come from the JSON in every push (the page has no copies).

## Lifecycle

- Plugin load: `Install_Hooks()` (healing-source and damage hooks), co-save callbacks.
- `kDataLoaded`: `InjuryConfig::Load()` (injuries/*.json), `Classifier::Init()` (hit sink), overlay view. The co-save loads after this, so source ids resolve.
- Load / new game: co-save read; Papyrus `PlayerRef.OnPlayerLoadGame` → `Main.Setup` → `ApplyPluginConfig`.
- Dashboard save → `SkyrimNet_OnPluginConfigSaved` → `Main.OnPluginConfigSaved` → `ApplyPluginConfig` (settings pushed to the DLL).

## Damage hooks (verify in game)

`HandleHealthDamage` (Actor vfunc 0x104, SE / AE; skipped on VR, slot unverified) sees melee / ranged hit damage, `ModActorValue` (ActorValueOwner vfunc 6) every other health damage. A thread-local scope stops the nested `ModActorValue` call inside `HandleHealthDamage` from counting twice, and `InjuryEngine::IsInternalDamage()` hides the engine's own `DamageActorValue` calls. With verbose logging on, every intercepted damage is logged as `[hook] ...` in `Injury_Engine.log` (victim, attacker, damage, classified source), followed by the classifier's `classify ...` line: use them to confirm the sign of `HandleHealthDamage`'s damage argument, that melee goes through the hook you expect, and the classifier result.
