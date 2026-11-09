Repo: Injury_Engine / branch main / baseline 074a31e (uncommitted edits on top)

# Custom weapon docs

## Goal

Answer a user's question: "I can write the json file, but how is it called by Injury_engine to prompt the correct descriptions?" Add docs, linked from README.md.

## Results

- New [docs/authors/custom-weapons.md](../../docs/authors/custom-weapons.md): how the file gets called (no registration; its `classify` rule claims the weapon), step by step, `classify` recipes, key-to-text table, creature groups and sexual parts, log checks, limits.
- [injuries/guide.md](../../SKSE/Plugins/Injury_Engine/injuries/guide.md): corrected the other-mods path to `injuries/humanoid/<id>.json`; added a short weapon note.
- [README.md](../../README.md): links in "Extensible" and "Editing and adding injury types"; path corrected.
- [AGENTS.md](../../AGENTS.md): documentation map row.

Docs only: no DLL, Papyrus, JSON data or ESP change, so nothing to build or deploy. Nothing committed.

## Verified

- Behavior read from `InjuryConfig.cpp` (`Load`, `LoadHumanoidSources`, `LoadGroup`, `ReadClassify`, `FormIdOf`) and `Classifier.cpp` (`MatchWeapon`, `WeaponRuleSource`, `CacheClassifyEffectForms`, `ClassifyMelee`).
- All six JSON snippets in the doc parse. Links and guide.md anchors resolve.

## Not yet tested

- No custom weapon was run in game for this doc.
- Enchantment damage on a named weapon: the doc says it may classify as burning / freezing / shocking (untested).
- `weapon_id` rule in game: KNOWLEDGEBASE lists Diary of Mine's slap as unconfirmed (2026-10-26).

## Findings

**Likely bug, other repo:** `../SkyrimNet_Leashed/SKSE/Plugins/Injury_Engine/injuries/leash_dragging.json` sits at the `injuries/` root. `InjuryConfig::Load` reads sources from `injuries/humanoid/` whenever that folder exists, so this file is not loaded and `External_Injure("leash_dragging")` should find no source. Confirm with `injuries: loaded` lines in `Injury_Engine.log`. Fix: move it to `injuries/humanoid/` in the Leashed repo.

`docs/` is not obviously part of the release archive (the Makefile doesn't mention it), so guide.md carries the key rules for players, and its pointer names the source repository.

## Next

1. Optional test: copy `slashing.json` to `humanoid/test_blade.json`, add a `weapon_id` rule for a known weapon, confirm `Classifier: weapon ... (test_blade)` in `Injury_Engine.log`, and hit a weak NPC until health is well down. Delete the file afterward.
2. Check the Leashed finding above and move its file if confirmed.
3. Commit the docs when happy.
