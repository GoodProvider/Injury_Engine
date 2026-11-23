# Checkpoint 02: released as 0.2.2 (not verified in game)

Repo `Injury_Engine` · branch `main` · baseline `1353cbd` (tag 0.2.1)

## Done
- The UBE / custom vampire race fix from [checkpoint-01](checkpoint-01.md) ships as 0.2.2 at the maintainer's request,
  labelled as not verified in game in all three changelogs and `release-checkpoint.xml`.
- Version sources bumped to 0.2.2 (`check-version.py` passes). Commit "Prepare 0.2.2", then `make release` →
  `versions/Injury_Engine 0.2.2.7z`, then commit "Release 0.2.2". Not tagged, not pushed.

## Not yet tested
- In game at all, with or without UBE.

## Next
1. In-game check, steps 1–2 of checkpoint-01 (`1 strip prefixes` in `Injury_Engine.log`; `00UBE_BretonRaceVampire ->
   humanoid (race (prefix 00ube_))` with UBE enabled).
2. Reply to the reporter (the shipped races.json fixes it; their manual list can stay or go).
3. Tag `0.2.2`, push, and create the GitHub Release (attach the 7z, body = `CHANGELOG-player.md`).
