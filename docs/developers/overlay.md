# SKSE plugin + PrismaUI overlay

C++ plugin `SkyrimNet_Whipped.dll` (injury engine, damage classification and healing hooks, Papyrus natives) and the PrismaUI whipping overlay.

Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md). JSON: [../reference/json-keys.md](../reference/json-keys.md#overlay-whipupdate).

## Paths

| Path | Role |
|------|------|
| `SKSE_Source/src/plugin.cpp` | Entry, logger (`SKSE\SkyrimNet_Whipped.log`), messaging, co-save registration |
| `SKSE_Source/src/InjuryEngine.cpp`, `include/InjuryEngine.h` | Per-actor, per-part, per-source injury (whip and health models), real-damage replacement of whip points, health link, co-save v2, narration text, `StateJson`, `OverlayJson` |
| `SKSE_Source/src/InjuryConfig.cpp`, `include/InjuryConfig.h` | Loads `SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json` at kDataLoaded (nlohmann-json), dotted-path lookup, `{token}` rendering |
| `SKSE_Source/src/Classifier.cpp`, `include/Classifier.h` | TESHitEvent sink + damage classification (slashing, piercing, bludgeoning, burning, freezing, shocking, mauling, generic) |
| `SKSE/Plugins/SkyrimNet_Whipped/injuries/` | Injury data and text per source; `guide.md` is the agent-facing editing guide |
| `SKSE_Source/src/Papyrus_Engine.cpp` | Natives bound to `SkyrimNet_Whipped_Engine.psc` |
| `SKSE_Source/src/Hooks.cpp` | Vtable hooks: `MagicTarget::AddTarget` (healing sources), `Actor::HandleHealthDamage` (SE / AE) and `ActorValueOwner::ModActorValue` (damage intake, classify, `InjuryEngine::OnDamage`, may reduce the damage) |
| `SKSE_Source/src/Overlay.cpp`, `include/Overlay.h` | PrismaUI view + push |
| `SKSE_Source/lib/PrismaUI/` | Vendored PrismaUI API header |
| `SKSE/Plugins/SkyrimNet_Whipped.dll` | Built plugin (copied by the build) |
| `PrismaUI/views/SkyrimNet_Whipped/overlay.html` | Overlay page under `Data/PrismaUI/views/` |

## Build

`make dll` → `cmake --preset release && cmake --build --preset build-release` in `SKSE_Source/`. Needs `VCPKG_ROOT`. CommonLibSSE is MinLL/CommonLibVR 4.39.5 (MIT) from the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork` (`vcpkg-configuration.json` `overlay-ports`, `find_package(CommonLibSSE)`); a clean configure builds it, ~6 min. Do not edit `SKSE_Source/build/` or vendored `lib/`.

## Natives

Declared in `Scripts/Source/SkyrimNet_Whipped_Engine.psc`, registered in `Papyrus_Engine.cpp`. Native parameters must use engine types (`RE::Actor*`, `RE::BSFixedString`, …). A signature change means rebuilding the DLL and recompiling the `.psc` and every caller.

## Overlay

- Setting `whip.overlay.enabled` ("Show whipping overlay").
- `Overlay::Init` on `kDataLoaded`: PrismaUI API, `CreateView("SkyrimNet_Whipped/overlay.html")`. PrismaUI is a listed requirement; if it is missing anyway → no overlay, everything else works.
- The natives `Hit`, `SyncHealth`, `TakeHealedActors`, `TakeHits` and `Forget` call `Overlay::Notify`, which pushes `OverlayJson` to JS `whipUpdate(json)` only when it differs from the last push.
- The view is shown, never focused (no pause, no cursor).
- Page: `#WhippedPanel` with a white title ("Whip Damage", from `whipping.json` `overlay.title`) above `.actor-whipped-damage-bars`: one name-only bar per whipped actor, most recently hit first; red damage fill or yellow healing fill. Real damage replacing whip damage grows a darker red segment (`overlay.colors.replaced`) from the left while the whip fill shrinks; an actor with no whip damage left is dropped and the panel hides when nobody has whip damage; the whole overlay fades 3 s after the last change. Numbers, thresholds and colors come from the JSON in every push (the page has no copies).

## Lifecycle

- Plugin load: `Install_Hooks()` (healing-source and damage hooks), co-save callbacks.
- `kDataLoaded`: `InjuryConfig::Load()` (injuries/*.json), `Classifier::Init()` (hit sink), overlay view. The co-save loads after this, so source ids resolve.
- Load / new game: co-save read; Papyrus `PlayerRef.OnPlayerLoadGame` → `Main.Setup` → `ApplyPluginConfig`.
- Dashboard save → `SkyrimNet_OnPluginConfigSaved` → `Main.OnPluginConfigSaved` → `ApplyPluginConfig` (settings pushed to the DLL).

## Damage hooks (verify in game)

`HandleHealthDamage` (Actor vfunc 0x104, SE / AE; skipped on VR, slot unverified) sees melee / ranged hit damage, `ModActorValue` (ActorValueOwner vfunc 6) every other health damage. A thread-local scope stops the nested `ModActorValue` call inside `HandleHealthDamage` from counting twice, and `InjuryEngine::IsInternalDamage()` hides the engine's own `DamageActorValue` calls. The first 40 intercepted damages are logged as `[hook] ...` in `SkyrimNet_Whipped.log` (victim, attacker, damage, classified source): use them to confirm the sign of `HandleHealthDamage`'s damage argument, that melee goes through the hook you expect, and the classifier result.
