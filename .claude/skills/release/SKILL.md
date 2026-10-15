---
name: release
description: >-
  Align Injury_Engine release docs and version metadata for next_version.
  Use when preparing a release, writing CHANGELOG.md, CHANGELOG-developer.md or CHANGELOG-player.md,
  bumping the version, updating release-checkpoint.xml, or documenting the
  delta since the last tag. Does not tag, publish, or run make release unless asked.
---

# Release documentation

Facts (version sources, freshness matrix, packaging): [release-guide.md](../../../release-guide.md). State: [release-checkpoint.xml](../../../release-checkpoint.xml). The guide wins on facts; this file wins on procedure.

## Cold start

1. Read `release-guide.md`, then `release-checkpoint.xml`.
2. Take `base_tag` / `next_version` from the checkpoint (fallback: `git describe --tags`).
3. Run `python3 python_scripts/check-version.py -v <next_version>`. **If sources disagree, stop and ask. Do not invent a version.**
4. Diff `base_tag...HEAD` (no base: whole tree) plus uncommitted files that will ship. Ignore the guide's noise list.

## Hard stops

- No git tag, GitHub Release, `make release`, or 7z unless asked.
- No feature work or refactors.
- No invented changelog bullets or README claims.
- No commit or push unless asked.
- SE ≠ VR; do not promise VR behavior.
- Before any game/script/ESP change: state confidence ≥ 90% and assumptions (AGENTS.md).

## Workflow

1. Establish the delta (scripts, prompts, action, settings, DLL/overlay, ESP via `Spriggit/`, docs). Report preflight only: are `Scripts/*.pex`, the DLL and the ESP newer than their sources?
2. Ask if version, ship set, or deliberate omissions are unclear; do not draft until answered.
3. Write `CHANGELOG.md` (themes and title format in the guide).
4. Write `CHANGELOG-developer.md` (releases URL first line, SkyrimNet requirement, 5–12 plain bullets) and `CHANGELOG-player.md` (only player-facing bullets from `CHANGELOG.md`, each 100 characters or less).
5. Align docs per the freshness matrix.
6. If bumping, change all sources in the guide's table together, then re-run `check-version.py`.
7. Update `release-checkpoint.xml` (`updated`, `base_tag`, `next_version`, `version_status`).
8. Hand-off: files touched, leftover mismatches, preflight notes, maintainer ship order (guide, Packaging).

Done when changelogs cover `next_version`, matrix docs match the delta, `check-version.py` passes, and the checkpoint is current.
