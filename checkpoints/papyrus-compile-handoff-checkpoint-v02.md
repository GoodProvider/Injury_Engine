# Papyrus compile switch: handoff checkpoint v02

Status: **compiled, deployed, not tested in game, not committed.** `compile: papyrus` run as defined in `.vscode/tasks.json`: 5 files, 0 errors, 0 warnings, exit 0. Nothing needed fixing.

## Problem
Run the new task and fix whatever it reports.

## Findings
- The compiler prints its search order: `Scripts/Source`, SKSE64, SkyrimNet, PapyrusUtil, vanilla. This confirms the last `-h` is searched first, so the task's order is right.
- Every Function / Event in the five `.psc` files is in its `.pex` (checked with `papyrus.exe read`); the extras are `GetState` / `GotoState`.
- `-original` (the CK compiler wrapper) fails here with "The system cannot find the path specified", so a byte-level comparison with the CK compiler was not possible. Not needed for the task.

## Current fix
None needed. `Scripts/*.pex` rebuilt 09:57 from the current sources.

## Known limits
- Not tested in game with papyrus-compiler `.pex` files (smaller than the CK builds, e.g. Main 48662 bytes).

## Next step for the developer
1. Load a save in MO2 with this folder enabled.
2. Try a whip lash, a torture punch, a `Torture_Target` action, a truce / yield, and a heal.
3. Check `Papyrus.0.log` for errors from `Injury_Engine_*` scripts.

## After it holds
Commit `.vscode/tasks.json`, the docs and the rebuilt `.pex`.
