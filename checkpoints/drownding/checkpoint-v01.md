# Drowning handoff checkpoint v01 (2026-10-17)

## Status
Built and deployed, **not tested in game yet**. `SKSE/Plugins/Injury_Engine.dll` rebuilt (`make dll`), Papyrus recompiled (`compile: papyrus`), `injuries/drowning.json` added.

## Problem
Vanilla drowning (~20 s under water, then ~20 health / s) arrived with no hit form and no attacker, so it was classified `generic` (or `falling` when `fallTime > 0`) and landed on random outer parts.

## Findings
- CommonLib: `Actor::BOOL_FLAGS::kUnderwater` (in `boolFlags`; `kInWater` / `kSwimming` are `BOOL_BITS`), `underWaterTimer` (runtime data), `GetSubmergedLevel(z, cell)`, `ActorValue::kWaterBreathing`.
- Parts were a fixed 11-slot array. Lungs were appended as slot 11, so the old indices and the co-save prefix stay the same.

## Current fix
- New part `lungs` (`kPartLungs = 11`, `kPartCount` 12): body weight 0, no cap (`PartFull` false), outside `CapacityDamage()`, stage = lung points / capacity (share of max health) against `lungs.medium` / `lungs.severe` (0.25 / 0.5, read from drowning.json). Never torture or overlay (`ActiveParts()` unchanged).
- `PickPart` uses the source's `partWeight` and only parts weighted > 0. Nothing changes for existing health sources, whose weights equal the body's.
- Decorator (`GetStateJson`) includes the lungs; `PartPointsJson` (torture) skips them; `DeathText` uses `death.regions.lungs`.
- `Classifier::ClassifyDrown` (rule `"drowning": true`, priority 40): no hit form, no attacker, `kUnderwater` or submerged >= 0.85, WaterBreathing 0. It runs before `ClassifyFall`, and a debug line logs every signal.
- Co-save v6 (12 parts); v5 loads with 11 parts.
- Papyrus `PartsText` loop 11 → 12.
- Docs: guide.md, CLAUDE.md, README, KNOWLEDGEBASE.

## Known limits
- Which hook catches vanilla drowning (HandleHealthDamage or ModActorValue) is not confirmed; both are hooked.
- `kUnderwater` on NPCs is not confirmed (submerged level is the fallback). VR not verified.
- Toggling sexual parts changes the capacity, so recorded lung points shift stage slightly.

## Next step for the developer
1. Turn on verbose logging. Swim under water past 20 s as an unhurt player.
2. `Injury_Engine.log`: expect `classify ...: underwater:... submerged:...`, then `classify ...: drowning -> drowning` and `damage ... drowning` points on `lungs`. Note which of `underwater` / `submerged` is true.
3. Surface. The decorator should show only "### Near Drowning", the overlay should stay hidden, and the lungs should heal as health regenerates. Cast Healing and check for the drowning heal text. Save, reload, and check the lungs are still there.
4. Drown a follower and look for the drowning death narration.
5. Confirm that a fall, a weapon hit and bathing produce no drowning.
6. Load an old save: the log should say `record version 5` and the wounds should be intact.

## After it holds
Commit (not `checkpoints/`), then run the release skill to write the changelog entry.
