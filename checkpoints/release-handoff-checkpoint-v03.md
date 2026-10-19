# Release handoff checkpoint v03 (2026-10-18): 0.1.2

## Status
- Packaged: `versions/Injury_Engine 0.1.2.7z` (1.85 MB, `make release`, all steps OK).
- Commits: `4885a13` (code-review v02 fix pass, version bump, changelogs, checkpoint) and `1999fd1` (rebuilt DLL + `info.json`). Not pushed, no tag, no GitHub Release.
- `check-version.py -v 0.1.2`: all sources match.
- Deployed `SKSE/Plugins/Injury_Engine.dll` is now the new build (2026-10-18 18:23). It had been stale since the review fixes.

## Problem
0.1.1 was packaged, then the review v02 fix pass ([reviews/checkpoint-v03.md](reviews/checkpoint-v03.md)) landed uncommitted. Release 0.1.2 ships it.

## Findings
- Delta since 0.1.1: Papyrus (Main, Actions, LashEffect), DLL (Classifier, Hooks, InjuryConfig, InjuryEngine, Overlay, TruceDecision, plugin), `overlay.html`. No ESP or injury JSON change.
- Docs (CLAUDE.md, KNOWLEDGEBASE.md, papyrus.md, overlay.md, actions.md) were already current.

## Current fix
- `CHANGELOG.md`, `CHANGELOG-developer.md`, `CHANGELOG-player.md` have a 0.1.2 section; `release-checkpoint.xml` has base 0.1.1 and next 0.1.2.

## Known limits
- **None of the 0.1.2 fixes are verified in game yet.** See the in-game checks in reviews/checkpoint-v03.md.
- Deferred items from that review (g_lock across health changes, ally override by equipped gear, real spells vs equipped weapon) are still open.
- `origin` still points at SkyrimNet_Whipped.

## Next step for the developer
1. Make sure MO2 overwrite has no older `Injury_Engine` DLL, `.pex` or overlay page.
2. Load a save. `Injury_Engine.log` should show the hook installs and no `classify rule skipped` / `has no region` warnings.
3. Run the in-game checks from reviews/checkpoint-v03.md (verbose logging on for the truce / melee ones).
4. Optionally install the 7z in a clean profile to test the FOMOD.

## After it holds
`git tag 0.1.2`, push, GitHub Release with the 7z attached and `CHANGELOG-player.md` as the body.
