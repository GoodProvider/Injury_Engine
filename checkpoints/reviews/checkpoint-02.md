# Code review checkpoint 02 (2026-10-24)

Fixes for the findings in [checkpoint-01.md](checkpoint-01.md). Built and deployed (`make dll`, `compile: papyrus`), **not tested in game**, not committed.

## Done

| # | Result |
|---|---|
| 1 | **Rejected, no code change.** `TrackedCount()` counts `g_events`, so a held heal start keeps the `OnUpdate` loop alive. KNOWLEDGEBASE entry added. |
| 2 | `g_healStart` (`item`, `unhurt`, `time`) is recorded in `Seen`; `SourceHealGroupText` uses `unhurt` from it (AND over targets), not the state at narration time. |
| 3 | `Engine.HealItem(Actor)` native; `Main.SameHealCast` groups on same caster + source + item; no caster never groups. |
| 4 | `NarrateHealGroup` returns Bool: no text → False and the targets fall back to the per-target path; closed gate → the group line waits in `KEY_HS_PEND` of the first target (`HealTick` sends it). |
| 5 | `ClaimNarration` "target dead" needs `since >= DEAD_TARGET_GRACE` (3 s). |
| 6 | `g_healItem` replaced by `g_healStart`, pruned by age (30 s) in `Seen`. It is not erased in `Erase`: the group line is worded after the hold, when the actor may already be forgotten. `g_healFood` is unchanged on purpose (documented). |
| 7 | `NarrateSyncCodes` returns early when `count == 0`. |
| 8 | Dead `LookupByEditorID` OStim fallback removed. |
| 9 | `papyrus.md`, the `NotifyHealStart` comment in `Main.psc`, CHANGELOG, KNOWLEDGEBASE updated. |

## To test in game

1. Eat food while unhurt, 5 times: heal start and eat / drink line each time.
2. Group heal on one wounded and one healthy NPC: "wounds" wording, not "no wounds to mend", even if the wounded one heals fully within the hold.
3. Two NPCs drink different potions in the same second: two lines, not one group line.
4. Group heal while a narration awaits its reply: the line arrives after the reply (check `ClaimNarration` verbose lines), not dropped.
5. Look for array warnings in `Papyrus.0.log`.

A held group line is sent by `HealTick` as `lead + HealMapText(first)`, so it may carry the first target's heal map after the group text. Check the wording in step 4.

## Still open

The v03 deferred `g_lock` pass (held across health changes, ally override, real spells vs equipped weapon, config readers without `Load`'s lock).

Next session: `checkpoints/reviews/checkpoint-03.md`.
