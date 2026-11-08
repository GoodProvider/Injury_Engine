# Release 0.1.6 handoff checkpoint v01 (2026-10-26)

## Status
0.1.6 built and packaged: `versions/Injury_Engine 0.1.6.7z` (1.9 MB). Not committed, not tagged, not published.

## Problem
0.1.5 was committed (c16e0e2) but never tagged; new work (combat punches, DOM slap, healing cost) had been written into the 0.1.5 changelogs. The user chose to ship it as 0.1.6.

## Findings
- `check-version.py -v 0.1.6` passes (Makefile, manifest.json, manifest.yaml, CMakeLists.txt, version.rc).
- `info.json` 0.1.6, DLL FileVersion 0.1.6.0, `FOMOD/info.xml` 0.1.6.
- 7z has core (5 .pex, DLL, ESP, injuries JSON incl. `slapping.json`, `CHANGELOG-player.md`), fight_actions, overlay, FOMOD, images.
- No stale `punch_combat_health` in `docs/` (KNOWLEDGEBASE keeps it as history).

## Current fix
- Version bump in all sources.
- CHANGELOG.md / -developer / -player: new 0.1.6 sections; uncommitted bullets moved out of 0.1.5; CHANGELOG.md gained the healing-cost bullet (`g_healCredit`, cleared in `Load` / `Revert`); player bullets ≤ 100 chars.
- `release-checkpoint.xml`: base_tag 0.1.5, next_version 0.1.6.
- `compile: papyrus`, `make release`.

## Known limits
- 0.1.5 has no git tag; the 0.1.6 changelog links "since 0.1.5" to a tag that does not exist yet.
- `origin` still points at SkyrimNet_Whipped.

## Next step for the developer
1. Test the 0.1.6 7z install in MO2 (or this dev folder): in-combat punch = bludgeoning, DOM slap = slap torture, healing a tortured part.
2. Commit (version bump, changelogs, sources, DLL, .pex).
3. Optionally tag 0.1.5 at c16e0e2; tag 0.1.6 at the new commit.
4. GitHub Release 0.1.6: attach the 7z, body = `CHANGELOG-player.md` 0.1.6 section.

## After it holds
Set `release-checkpoint.xml` version_status to "0.1.6 tagged" and start the next delta from base_tag 0.1.6.
