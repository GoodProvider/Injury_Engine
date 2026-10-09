# KNOWLEDGEBASE

Quirks and lessons for SkyrimNet_Whipped. Read before changes; append a dated entry (newest first) after a session that found something non-obvious. Entries marked *(from SkyrimNet_SexLab)* were learned there and apply here too.

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

Whip points hold health down by `deficit × 0.9 × max / 186`; real points are worth `max / 120` health each (6 parts × 20 = 100% of max). A real hit while whip points remain is cut by `min(damage, whip hold)` **inside the hook** (the original is called with the reduced amount), the same health worth of whip points is removed and real points added, so health only drops once all whip wounds are replaced and the 10% floor never blocks real damage. Unverified assumptions, check the first `[hook] ...` lines in `SkyrimNet_Whipped.log`: (1) `Actor::HandleHealthDamage` (vfunc 0x104; the VR slot is not verified, so the hook is skipped on VR) receives the damage as a positive amount (the code uses `fabs` and keeps the sign); (2) melee damage reaches `HandleHealthDamage`, everything else `ActorValueOwner::ModActorValue(kDamage, Health)` (vfunc 6); (3) a nested `ModActorValue` call inside `HandleHealthDamage` is ignored through a thread-local scope, so a damage that reaches both is not counted twice; (4) `TESHitEvent` may fire after the damage, so `Classifier::Classify` falls back to the attacker's equipped weapon, then to unarmed (people bludgeoning, creatures mauling), then `generic`. Damage the engine does itself (`TakeHealth`, regen take-back) is wrapped in `InternalScope` and never classified.

## Papyrus: `key` is reserved, native strings come back case-folded risk (2026-10-06)

A Papyrus parameter named `key` fails to compile ("key is not a variable"); use `setting` or similar. Strings returned by natives are `BSFixedString` and Papyrus interns strings case-insensitively (the reason `kind` / `source` travel as numbers); narration text from `HitText` / `HealText` is long and unique enough that this has not shown up, but if a sentence ever comes back with odd casing, return it through a different channel (e.g. `std::string` registered as a plain string).

## CommonLibSSE vtable slot comments are hex (2026-10-06)

`// 104` next to `Actor::HandleHealthDamage` in `RE/A/Actor.h` means slot `0x104` (260). Hooking decimal `104` (`0x68`, `Unk_68`, called every movement update) crashed the game a second after loading a save, inside our thunk (`Classifier::Classify` read the bogus "attacker" argument), with a call stack through actor movement / `bhkCharacterProxy` and no `[hook]` log line. Always write vtable indices in hex (`0x104`, `0x6`, `0x1`), exactly as the CommonLib comment shows.
