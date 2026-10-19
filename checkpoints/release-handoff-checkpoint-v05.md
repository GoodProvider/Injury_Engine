# Release handoff checkpoint v05 (2026-10-18): 0.1.3

## Status
- Every version source is now 0.1.3 (`check-version.py -v 0.1.3` passes). The changelogs cover 0.1.3.
- `make release` built `versions/Injury_Engine 0.1.3.7z`. The 7z holds the rebuilt DLL (version resource 0.1.3), `session.json` with `healed`, the recompiled .pex files and `CHANGELOG-player.md` (0.1.3).
- Commits: 080059f (bump + changelogs) and a follow-up commit with the rebuilt DLL, `info.json` and .pex files. Not pushed, no tag, no GitHub Release.

## Contents (since 0.1.2, d430eef)
- Buff endings that only show as an attacker-less `HandleHealthDamage` (Courage re-applied by Ocato's Recital) pass through as a buff change.
- Small real hits add up (`g_realCarry`).
- `healed` bio line.

## Tested
- The developer tested Courage in game: no wounds or damage.
- Not yet verified in game: the small-hit carry (`spell-damage-xamos/checkpoint-v03.md` steps 2–6) and the healed bio line (`healed-bio-handoff-checkpoint-v01.md`).

## Notes
- The 0.1.3 player bullets were shortened to 100 characters or less, as the release guide requires.
- `origin` still points at SkyrimNet_Whipped.

## Next step for the developer
1. Optional: install the 7z in a clean profile to check the FOMOD.
2. `git tag 0.1.3`, then push.
3. Make a GitHub Release `0.1.3`: attach `versions/Injury_Engine 0.1.3.7z` and use `CHANGELOG-player.md` as the body. Send the DLL / 7z to Xamos.
