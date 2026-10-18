# Handoff: player Yes / No box for truce and surrender (v04)

Status: **implemented, built, deployed, not tested in game, not committed.** On top of the uncommitted v02 / v03 work (baseline `c385cae`). `make dll` succeeded; Pyro compiled `Injury_Engine_Actions` (1 succeeded, 0 failed). No ESP / JSON change.

## Problem
With the player as target, `Stop_Fighting` and `Stop_Fighting_by_Yielding` were accepted at once. The player should decide: "{name} wishes to stop fighting. Do you accept?" / "{name} wishes to yield to you. Do you accept?", buttons Yes / No, nothing else.

## What changed
- `TruceDecision.cpp`: `Ask` checks `target->IsPlayerRef()` before the API checks and shows a vanilla message box (`ShowPlayerBox`, `PlayerChoice` callback). The answer goes through `Push` → mod event `Injury_Engine_Truce` → `OnTruce` → `ApplyTruce`, the same path as the decision model. Esc = No. One box per speaker in flight (`g_inFlight`). VR: refused. Log: `truce|surrender: asking the player about X`, `truce|surrender: player accepted|refused X`.
- `Injury_Engine_Actions.psc`: `StopFighting` / `StopFightingByYielding` always call `AskTruce` (the player branch is gone).
- Docs: CLAUDE.md, docs/authors/actions.md, README, both changelogs, KNOWLEDGEBASE.

## Next step for the developer
1. MO2: no stale `Injury_Engine.dll` / `.pex` in overwrite. Restart the game.
2. Fight an NPC, get it to `Stop_Fighting` you → box. Yes: both stop, NPC `GetAV Aggression` 0. No: the NPC stands down, you are free.
3. Get an NPC to `Stop_Fighting_by_Yielding` you → "X wishes to yield to you. Do you accept?". Check Yes, No and Esc.
4. `Injury_Engine.log` shows the `asking the player` / `player accepted|refused` lines; NPC→NPC still logs `truce: send`.

## After it holds
Commit together with v02 / v03, leaving out `checkpoints/`.
