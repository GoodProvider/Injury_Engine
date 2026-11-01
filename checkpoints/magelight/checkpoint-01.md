# Magelight checkpoint 01: move the overlay from PrismaUI to Magelight UI

Repo: `c:\Skyrim\dev\mods\Injury_Engine`, branch `main`, baseline `c6cabaf` (0.1.6 prepared, not tagged; latest tag
0.1.5). Host source read: `../SkyrimNet_SexLab/extern/MageLightUI` at `05d05d8` (Magelight 1.7.1, MIT). Logs: none
snapshotted, nothing has run yet.

Path note: `docs/DISTRIBUTION.md`, `docs/CPP.md`, `Magelight.cpp`, `MagelightManifest.cpp` and `MagelightUI_API.h` below
are in the host repo (its `docs/`, `src/` and `api/`), not this one. Also there: `docs/PRISMAUI_MIGRATION.md`, the
host's call-by-call mapping.

**Status: nothing is implemented.** This file is self-contained: start the port here. Read `AGENTS.md` and `CLAUDE.md`
as usual (deploy rule, `z-*` files off limits, never hand-edit the ESP). No ESP change is needed.

## Goal

Replace PrismaUI with Magelight UI as the overlay's web host, and remove PrismaUI from the mod. The bars must look and
behave as they do today. The page's JS API (`injuryEngineUpdate(state, seq, reset)`) and `InjuryEngine::OverlayJson`
do not change.

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Dependency | Require Magelight as a separate download, do not bundle | `docs/DISTRIBUTION.md` Option A; bundling makes us a distributor of Ultralight under its EULA |
| Coexistence | No PrismaUI code path or fallback | The hosts coexist in one process, so other mods keep working; a single path is simpler |
| Minimum host | 1.7.1 (`PackVersion(1, 7, 1)`) | The only version installed, so the only one that can be tested. The calls used exist since 0.16. Lowering it later is a one-line change |
| API layer | Raw `MagelightUI_API.h` only, not the `MagelightUI_Mod.h` wrapper | We need no per-view close slots. The wrapper's destructor calls `UnregisterMod` during static destruction at DLL detach, and we also get `onLog` and `modVersion` |
| View | `Layer::Hud`, `fullscreen`, `clickThrough`, `startVisible = false` | Same as today: never focused, no cursor, no pause. The page positions itself by percent, so it stays fullscreen |
| Page location | `Magelight/Injury_Engine/views/overlay/index.html` (`git mv`) | Doc layout. The folder name equals the `modId` |
| Manifest | None | The DLL owns the view. `Manifest::LoadAll` skips a folder with no `manifest.json` (`MagelightManifest.cpp:451`), so the folder is safe |
| DOM ready | Do not gate on it; remove `g_domReady` and the DOM-ready callback | See finding 1 |
| `seq` | Keep in the page and the C++ | Host calls are now in order, so it is only a guard, but keeping it leaves the page API unchanged |

Confidence (AGENTS.md scale): 88%, proceed with caveats. The API contract and threading are read from host source. In-game
rendering, Hud behaviour around menus and load screens, and VR are unverified.

## Findings

1. **DOM-ready must not call `AddTask`.** The DOM-ready callback runs on the render thread, inside Present or on
   Magelight's page thread. Today's Prisma callback calls `tasks->AddTask(...)`, which can hang on VR
   (`MagelightUI_API.h:32-44`). A hidden view's DOM ready can also come about a second after a load, and the docs say
   not to gate on it. Calls made earlier are queued and delivered after it (`docs/CPP.md`, "When pages load"), so the
   gate and the callback can both go.
2. **`InvokeJS` is any-thread and FIFO.** Each view has one outbound queue (`Magelight.cpp:1087`, `InvokeJS` at
   `:11643`). The queue has a cap (`kOutboundCap`) and drops new calls when full, logging once. The coalescing in
   `Push` (one pending payload, one queued task) must stay so it cannot fill while a page loads.
3. **`IsViewValid` is true right after creation.** It only checks the view registry (`Magelight.cpp:11494`), so a view
   created at kDataLoaded is usable before the renderer exists. `CreateViewEx` works then too.
4. **Hidden pages pause (host 1.5.0+).** CSS transitions stop; timers and `InvokeJS` keep running. The page drives its
   logic with `setTimeout` / `setInterval`, not `transitionend`, so its logic is safe. A fade interrupted by a hide
   finishes on the next show. Check this in game.
5. **Paths must be absolute and UTF-8.** Build the path from `GetModuleFileNameW` (exe folder + `Data\Magelight\...`).
   Prisma's path was relative to `Data/PrismaUI/views/`. In MO2's virtual filesystem the exe-relative path still
   resolves to this repo's `Magelight/` folder.
6. **Text may look slightly different.** Magelight runs Ultralight 2.0 with text gamma 2.2; Prisma's 1.4 used 1.8.
   Compare the bar labels. Do not change a global setting for this: `fontGamma` in `Magelight.json` belongs to the
   player.
7. **Include order.** `MagelightUI_API.h` includes `<windows.h>`, so it must come after CommonLib. `src/PCH.h` already
   does that; keep the include last in `Overlay.cpp`.
8. **Test profile.** Only MO2 profile `SkyrimNet SexLab` has Magelight UI enabled, and Injury_Engine is **disabled**
   there (`modlist.txt:255`). No other profile lists Injury_Engine. Prisma is enabled there too, which is useful: it
   tests coexistence. Installed host: `mods/Magelight UI`, 1.7.1.

## Work to do

### 1. Files

- `git mv PrismaUI/views/Injury_Engine/overlay.html Magelight/Injury_Engine/views/overlay/index.html`, then remove the
  empty `PrismaUI/` tree.
- `git rm -r SKSE_Source/lib/PrismaUI`.
- Copy `../SkyrimNet_SexLab/extern/MageLightUI/api/MagelightUI_API.h` to `SKSE_Source/lib/MagelightUI/` (MIT; keep its
  header comment unchanged).
- `SKSE_Source/CMakeLists.txt:31`: include `lib/MagelightUI` instead of `lib/PrismaUI`.

### 2. `SKSE_Source/src/Overlay.cpp`

Only the host calls change. `BlockingMenuOpen`, `MenuSink`, `Push`'s coalescing, `SetEnabled`, `Notify` and `Reset`
keep their logic. Replace `g_prisma` / `g_view` / `g_domReady` with:

```cpp
const MAGELIGHT_API::MagelightApi4* g_api = nullptr;
MAGELIGHT_API::ModId g_mod = 0;
MAGELIGHT_API::ViewId g_view = 0;

bool Ready() { return g_api && g_view && g_api->IsViewValid(g_view); }
```

Other replacements:

- `UpdateVisibility`: `g_api->ShowView(g_view, show)`.
- `Push`: `g_api->InvokeJS(g_view, script.c_str())`.

`Init`, inside the existing `call_once`:

```cpp
g_api = MAGELIGHT_API::RequestApi4();
if (!g_api) { warn "Overlay: Magelight UI not installed, overlay disabled"; return; }

MAGELIGHT_API::ModDesc mod{};
mod.size = sizeof(mod);
mod.modId = "Injury_Engine";
mod.displayName = "Injury Engine";
mod.minHostVersion = MAGELIGHT_API::PackVersion(1, 7, 1);
mod.onEvent = OnEvent;   // default GameThread
if (g_api->RegisterMod(&mod, &g_mod) != Result::Ok) {
    warn with the running g_api->hostVersion (too old, slug taken, ...); g_api = nullptr; return;
}

MAGELIGHT_API::ViewDesc view{};
view.size = sizeof(view);
view.name = "overlay";
const std::string page = PagePath();
view.htmlPath = page.c_str();
view.fullscreen = true;
view.clickThrough = true;
view.startVisible = false;
view.layer = MAGELIGHT_API::Layer::Hud;
if (g_api->CreateViewEx(g_mod, &view, &g_view) != Result::Ok) {
    error with g_api->GetLastErrorMessage(g_mod); g_api = nullptr; return;
}
g_shown = false;
// add the MenuSink exactly as today
```

- `PagePath()`: copy the `GetModuleFileNameW` + `u8string` code from
  `../SkyrimNet_SexLab/extern/MageLightUI/examples/Magelight.CppPanel/src/plugin.cpp:21-29`, with folders `Data`,
  `Magelight`, `Injury_Engine`, `views`, `overlay`, `index.html`.
- `OnEvent(const EventData*, void*)`: logging only. `ViewDomReady` logs `Overlay: DomReady`, `ViewLoadFailed` logs
  `detail` as an error, `RenderDead` logs a warning, and `ConsoleMessage` with `x == 2` logs `detail` as an error.
- The first `UpdateVisibility` is not lost by removing the DOM-ready callback: `SetEnabled` (from `ApplyPluginConfig`)
  and `MenuSink` both trigger it, and calls before the page loads are queued.
- `Overlay.h`: update the comments (Magelight path; `Whipping` wording may stay).

### 3. Page (`index.html`)

Change only the two comments about invokes landing out of order (line 14, "invokes are not applied in order", and line
240, "Separate PrismaUI invokes"). No logic change.

### 4. Packaging

- `Makefile:35`: `'PrismaUI'` becomes `'Magelight'` in the `Copy-Item` list.
- `FOMOD_source/ModuleConfig.xml:24`: "(PrismaUI)" becomes "(Magelight UI)".

### 5. Text and docs (current state only; leave past changelog entries and `injury-engine-handoff.md` as history)

| File | Change |
|---|---|
| `SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml:233` | "Needs Magelight UI." |
| `Scripts/Source/Injury_Engine_Engine.psc:167` | Comment only: Magelight UI |
| `README.md:63` | Requirement: **Magelight UI** 1.7.1+ (Nexus mod 193303) |
| `AGENTS.md:19,34,65,101` | Overview, key path, docs map, gotchas line. Add `Magelight.log` (`...\my games\Skyrim Special Edition\SKSE\Magelight.log`) to the log table |
| `CLAUDE.md:50` | Overlay paragraph: Magelight view path, API header, Hud view |
| `docs/developers/overlay.md` | Title, paths table, Overlay section (Init, `seq` note: `InvokeJS` is FIFO, `seq` kept as a guard) |
| `release-guide.md:74` | Stage `Magelight`; Magelight UI is the documented requirement |
| `KNOWLEDGEBASE.md:105-115,137` | Rewrite the Prisma entries for Magelight: page path; `InvokeJS` order; `window.prompt` is an Ultralight limit. Add an entry for findings 1, 4 and 6 |
| Changelogs | Per the `release` skill. The user decides which release (open question 1). The new requirement must be in `CHANGELOG-player.md` |

Afterwards `Grep` for `Prisma` outside `checkpoints/`, past changelog entries and `injury-engine-handoff.md`: expect no
hits. Leave `z-*` files alone.

### 6. Build and deploy

1. `make dll`, which must succeed and write `SKSE/Plugins/Injury_Engine.dll`.
2. Run the `compile: papyrus` line from `.vscode/tasks.json`. Only a comment changed, but this keeps `Scripts/` current.
3. Do not run `make release` unless asked. If asked, check that `7z l` lists `core/Magelight/Injury_Engine/views/overlay/index.html`.

Do not commit unless the user asks.

## Not yet tested (user, in game, flat SE/AE)

Setup: in MO2 profile `SkyrimNet SexLab`, enable `Injury_Engine` (the user does this; load-order edits need confirmation).
Keep Magelight UI enabled and confirm `overwrite` has no stale `Injury_Engine` DLL.

1. Log: `Injury_Engine.log` shows `Overlay: view created` and `Overlay: DomReady`; `Magelight.log` line 1 says 1.7.1,
   with view `overlay` under mod `Injury_Engine` and no errors.
2. Torture an NPC (whip, then another type): the bar appears at the top right with title and name, in the type's color,
   and fades 3 s after the last change.
3. Heal: light blue. Real damage on a tortured actor: a red segment grows. All torture replaced: bar and panel go away.
4. No cursor, no input capture and no pause while bars show. Camera and controls work normally.
5. Open the pause menu, console and a dialogue: the overlay hides. Close them: it returns, and a mid-fade state settles.
6. Load another save with bars showing: old bars are gone and no stuck state remains. Repeat with a new game.
7. Toggle "Show torture overlay" off and on in the dashboard: hides and empties, then returns with current state.
8. Compare label sharpness and weight with the old look (finding 6). Check frame time during a long torture scene.
9. Coexistence: another PrismaUI mod in the profile keeps working.
10. Failure path: disable Magelight UI; the game runs, the log warns and the rest of the mod works.

VR is untested and stays out of scope (AGENTS: SE is not VR). Host defaults apply there.

## Open questions

1. Does this ship in 0.1.6 (prepared, untagged) or the next version? A new required dependency is a player-visible
   change. The port does not bump versions on its own; ask before writing changelogs.
2. Should the minimum host be lowered from 1.7.1? Only worth it if users commonly carry an older bundled copy, for
   example through SeverActions.

## Next

1. Fresh session: do "Work to do" sections 1-6, then write `checkpoints/magelight/checkpoint-02.md` (what changed,
   build result, what is still untested).
2. User: run "Not yet tested", then resume from `checkpoint-02.md`. Logs to capture: `Injury_Engine.log`,
   `Magelight.log`, `Papyrus.0.log`.
