# Drowning handoff checkpoint v02 (2026-10-17)

## Status
Diagnostic build deployed (`make dll`, 22:06). Waiting for one in-game repro.

## Problem
First test: the player nearly drowned, no chat message, and no drowning / health / uncaused line in `Injury_Engine.log`.
The source had also lost `ClassifyDrown` after the v01 build (called at Classifier.cpp but not defined).

## Done
- Restored `DrowningRule` / `ClassifyDrown` (Classifier.cpp). Added public `Classifier::DrownSignals` / `ReadDrownSignals`.
- Hooks.cpp: added `UnderwaterProbe` (info level, throttled 1 s, only while wet) in `HandleHealthDamage` (no attacker), AV 0x06 (all Health decreases, skipped ones included), and new pass-through hooks on AV 0x05 / 0x07.
- InjuryEngine.cpp `OnDamage`: info line when `PickPart` finds no part.

## Next
Repro as the player, under water past ~20 s, verbose on if you can. Read the `[probe]` lines:
seen + `health:` drowning on lungs but no chat → Papyrus narration path; seen but generic → signals; seen but `inHandle`/internal → skip; none → drowning bypasses the AV vfuncs (RE or a watcher fallback).
Then remove the 0x05 / 0x07 probe hooks.
