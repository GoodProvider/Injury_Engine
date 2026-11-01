# Magelight checkpoint 02: port done and built, in-game test pending

Repo: `c:\Skyrim\dev\mods\Injury_Engine`, branch `main`, baseline `c6cabaf` plus uncommitted changes. Version 0.1.7. Host
source read: `../SkyrimNet_SexLab/extern/MageLightUI` at `05d05d8` (Magelight 1.7.1, MIT). Logs: none snapshotted, nothing
has run in game yet.

**Status: the port is implemented, the DLL and Papyrus are built, and nothing has been tested in game.** Nothing is
committed, because the user has not asked for that. This file supersedes `checkpoint-01.md`, whose Findings still hold.

## Goal

Replace PrismaUI with Magelight UI as the overlay's web host and remove PrismaUI from the mod. The bars must look and
behave as before. `injuryEngineUpdate(state, seq, reset)` and `InjuryEngine::OverlayJson` do not change.

## Decisions

All decisions in `checkpoint-01.md` stand. The user answered its open questions:

| Question | Answer |
|---|---|
| Which release? | **0.1.7**. Version sources are bumped and all three changelogs have a 0.1.7 entry |
| Minimum host 1.7.1? | **Accepted** (`PackVersion(1, 7, 1)`) |

Confidence (AGENTS.md scale): 88%, proceed with caveats. In-game rendering, Hud behaviour around menus and load screens,
and VR are unverified.

## What changed

- **Files:** `PrismaUI/views/Injury_Engine/overlay.html` moved to `Magelight/Injury_Engine/views/overlay/index.html`
  (only two comments changed). `SKSE_Source/lib/PrismaUI` is gone. `SKSE_Source/lib/MagelightUI/MagelightUI_API.h` is an
  unchanged copy of the host header and is untracked until committed (`git add` it).
- **`SKSE_Source/src/Overlay.cpp`:** only host calls changed. `g_api` / `g_mod` / `g_view` replace `g_prisma` / `g_view` /
  `g_domReady`. New `PagePath` and a logging-only `OnEvent`. `Init` uses `RequestApi4`, `RegisterMod`, `CreateViewEx`.
  The DOM-ready gate and its `AddTask` callback are gone. The API header is included last because it pulls in
  `<windows.h>`. `ModDesc.modVersion` and `onLog` are not set: the first would be another hand-kept version source, and
  `Magelight.log` already records the rest.
- **Packaging:** `Makefile` stages `Magelight`; FOMOD option text says Magelight UI.
- **Version 0.1.7:** `Makefile`, `manifest.json`, `manifest.yaml`, `CMakeLists.txt`, `version.rc`.
- **Docs:** `AGENTS.md` (with a `Magelight.log` row), `CLAUDE.md`, `README.md`, `docs/developers/overlay.md`,
  `release-guide.md`, `KNOWLEDGEBASE.md` (entries rewritten, one added), `release-checkpoint.xml`, changelogs.

## Results (verified outside the game)

- `make dll` succeeded with no errors: `SKSE/Plugins/Injury_Engine.dll`, file version 0.1.7.0.
- Papyrus compile succeeded (5 files, exit 0). Only a comment in `Injury_Engine_Engine.psc` changed, but all `.pex` files
  were rewritten, so git shows them as modified.
- `check-version.py -v 0.1.7` passes.
- The DLL holds no `PrismaUI` string and does contain `Magelight.dll`.
- `Grep Prisma` outside `checkpoints/`, past changelog entries and `injury-engine-handoff.md` finds only deliberate
  mentions: `README.md:63` ("no longer used"), `release-checkpoint.xml` (status) and two history notes in
  `KNOWLEDGEBASE.md` (lines 111 and 119).
- Installed host: `Magelight.dll` file version 1.7.1, and `Magelight.log` says `Magelight v1.7.1`. MO2's `version=16.0.0.0`
  in `meta.ini` is the Nexus file index, not the host version. `modid=193303` confirms the Nexus id in the README.
- No stale `Injury_Engine.dll` in `C:\Skyrim\dev\overwrite\SKSE\Plugins`.
- `make release` was **not** run, so the 7z layout is unchecked.

## Findings

1. **Tag `0.1.6` is on the wrong side of the 0.1.6 commit.** It points at `c16e0e2` ("Rebuild 0.1.5 artifacts"). `c6cabaf`
   ("Prepare 0.1.6", with the 0.1.6 code and changelog) is one commit after it, on local `main` only, ahead of
   `origin/main`. So `0.1.6..HEAD` also contains all of 0.1.6's changes. The 0.1.7 changelogs cover the port only, on the
   assumption that 0.1.6 ships or shipped from `c6cabaf`. The user decides whether to move the tag. Nothing here touched it.
2. **Test profile confirmed.** In `profiles/SkyrimNet SexLab/modlist.txt`, `Injury_Engine` is disabled (line 255),
   `Magelight UI` is enabled and `Prisma UI - Next-Gen Web UI Framework` is enabled (line 295), so the profile also tests
   coexistence.

## Not yet tested (user, in game, flat SE/AE)

Setup: in MO2 profile `SkyrimNet SexLab`, enable `Injury_Engine` (the user does this; load-order edits need confirmation).
Keep Magelight UI enabled.

1. Log: `Injury_Engine.log` shows `Overlay: view created` and `Overlay: DomReady`; `Magelight.log` lists view `overlay`
   under mod `Injury_Engine` with no errors.
2. Torture an NPC (whip, then another type): the bar appears at the top right with title and name, in the type's color,
   and fades 3 s after the last change.
3. Heal: light blue. Real damage on a tortured actor: a red segment grows. All torture replaced: bar and panel go away.
4. No cursor, no input capture and no pause while bars show. Camera and controls work normally.
5. Open the pause menu, console and a dialogue: the overlay hides. Close them: it returns, and a mid-fade state settles.
6. Load another save with bars showing: old bars are gone and no stuck state remains. Repeat with a new game.
7. Toggle "Show torture overlay" off and on in the dashboard: hides and empties, then returns with current state.
8. Compare label sharpness and weight with the old look (Ultralight 2.0 text gamma). Check frame time during a long
   torture scene.
9. Coexistence: another PrismaUI mod in the profile keeps working.
10. Failure path: disable Magelight UI; the game runs, the log warns and the rest of the mod works.

VR is untested and stays out of scope (AGENTS: SE is not VR).

## Open questions

1. Should tag `0.1.6` move to `c6cabaf` (finding 1)? Answer this before tagging 0.1.7, since the 0.1.7 changelog says
   "since 0.1.6".

## Next

1. User: run "Not yet tested" and capture `Injury_Engine.log`, `Magelight.log`, `Papyrus.0.log`.
2. User: resume from this file with the results. Fix anything found and write `checkpoint-03.md`.
3. After the tests pass, and only when asked: commit (remember `git add SKSE_Source/lib/MagelightUI Magelight`), run
   `make release`, and check that `7z l` lists `core/Magelight/Injury_Engine/views/overlay/index.html`.
