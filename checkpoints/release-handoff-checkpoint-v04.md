# Release handoff checkpoint v04 (2026-10-18): 0.1.2 with the fall / buff fix

## Status
- The fall / buff-change fix (`07ad3b3`, DLL only) was added to 0.1.2. The version stays 0.1.2 because 0.1.2 was never tagged or published.
- `versions/Injury_Engine 0.1.2.7z` was rebuilt with `make release`.
- `check-version.py -v 0.1.2`: all sources match.
- Not pushed, no tag, no GitHub Release.

## Problem
0.1.2 was packaged at 18:23 (v03). The fall / buff fix landed after that as "Unreleased".

## Current fix
- `CHANGELOG.md`: the Unreleased bullets moved into 0.1.2 under `### Wounds / healing`.
- `CHANGELOG-player.md`: the two fixes are now at the top of 0.1.2 `### Fixes`.
- `CHANGELOG-developer.md`: new bullet "Buffs and steps are not falls." (10 bullets).
- `release-checkpoint.xml`: `version_status` updated.

## Known limits
- The developer tested the fall / stairs fix in game. Still untested in game: the buff-ending path (see `fall-buff-handoff-checkpoint-v01.md`) and the review fixes (see `reviews/checkpoint-v03.md`).
- `origin` still points at SkyrimNet_Whipped.

## Next step for the developer
1. Optionally test the buff-ending path: cast Courage on a follower, wait for it to end, and look for a `[buff] ... not a wound` line in `Injury_Engine.log`. There should be no fall narration.
2. Optionally install the 7z in a clean profile to test the FOMOD.
3. `git tag 0.1.2`, push, and make a GitHub Release with the 7z attached and `CHANGELOG-player.md` as the body.
