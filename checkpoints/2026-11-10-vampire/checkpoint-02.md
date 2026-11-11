Repo: Injury_Engine · branch `main` · baseline `d743ab5` · 2026-11-11

# Checkpoint 02: vampire / spell sources verified in game, released 0.2.1

## Goal

From [checkpoint-01](checkpoint-01.md): commit the `draining`, `poisoning`, `clawing` and `blood_magic` sources and ship them as 0.2.1.

## Results

- The player verified the checkpoint-01 build in game.
- The log snapshot from checkpoint-01 had been written to `SKSE_Source/src/checkpoints/…`. It was moved to [logs/2026-11-10-22-41-18](logs/2026-11-10-22-41-18/), where checkpoint-01's links point.
- Committed the feature work, then the 0.2.1 release prep: all version sources bumped, `CHANGELOG.md` Unreleased became 0.2.1 (arrows plus the vampire / spell sources), developer and player changelogs, `release-checkpoint.xml`.
- `make release` builds `versions/Injury_Engine 0.2.1.7z`.

## Next

1. Maintainer: git tag `0.2.1`, push, GitHub Release with the 7z (body = `CHANGELOG-player.md`).
