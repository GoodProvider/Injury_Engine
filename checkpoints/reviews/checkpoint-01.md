# Code review checkpoint 01 (2026-10-24)

Repo `Injury_Engine`, branch `main`, baseline `f27c478` (0.1.4) .. `HEAD` `2eabf24`. Review only: no code, build or deploy changes were made. Logs: [logs/2026-10-24-21-46-53/](logs/2026-10-24-21-46-53/) ([MANIFEST.txt](logs/2026-10-24-21-46-53/MANIFEST.txt)). The newest crash log (2026-10-18) predates this work, so it was not copied.

## Goal

Review the four commits since 0.1.4: killing-blow narration (`1240b3e`), SexLab / OStim bleedout release (`095aed1`), food heal source (`8b87c24`), grouped heal line (`2eabf24`). Also check that the uncommitted doc edits (CHANGELOG "Ambient healing no longer narrates", `docs/developers/papyrus.md` "Outbound mod events") match the code.

## Results

The docs match the code. `Seen` has the ambient self-heal skip and the 60 s unhurt gap, and `NotifyTorture` / `NotifyHealStart` exist in [Injury_Engine_Main.psc](../../Scripts/Source/Injury_Engine_Main.psc). Every torture path reaches `NotifyTorture` once: the narrated ones through `NarrateInjury` → `NarrateHits`, the silent ones directly. The `SendBleedout` hold logic (`down == start`) is correct for both directions. The SexLab / OStim release is sound.

The live log shows the grouped heal line working: "Bob casts Grand Healing around Skadi and Bob" ([Papyrus.0.log:1921](logs/2026-10-24-21-46-53/Papyrus.0.log)).

## Findings (most severe first)

| # | Sev | Where | Finding |
|---|---|---|---|
| 1 | Medium | [InjuryEngine.cpp:1149-1160](../../SKSE_Source/src/InjuryEngine.cpp#L1149) with [Main.psc:1540](../../Scripts/Source/Injury_Engine_Main.psc#L1540) | **A held heal start can be stranded.** `NoteHealingSource` queues the event and fires `Injury_Engine_Injury`; `OnInjury` drains at once, so the event is held for `kHealGroupWindow` (0.35 s). The next tick is 0.2 s later (the event is still held), and `OnUpdate` keeps looping only while `TrackedCount() > 0`, a torture scene or a heal session exists, or a death is held. For an unhurt, untracked target (the player eating, a heal on a healthy NPC) none is true, so the loop ends with the event still in `g_events`. It is taken only at the next unrelated `StartSync`. A tick that lands after 0.35 s (Papyrus jitter) still works, so the loss is intermittent. Fix: keep the loop alive while the DLL holds events (a `HasPendingEvents()` native, or `StartSync` when a take returns empty but `held` is non-empty). |
| 2 | Low-Med | [InjuryEngine.cpp:2795-2830](../../SKSE_Source/src/InjuryEngine.cpp#L2795) | **Group line reads state after the hold, not at heal start.** `unhurt` is computed in `SourceHealGroupText` from `g_actors` at narration time, 0.35+ s later. In the log, Skadi had `points:2` at heal start ([Injury_Engine.log:368](logs/2026-10-24-21-46-53/Injury_Engine.log)) but healed fully within the hold (kinds 1 and 2 at 21:42:16), so the line said "With no wounds to mend" for a wounded actor. Fix: record `unhurt` per target in `Seen` (the `Event` / `Healer` struct) and use it for the group. |
| 3 | Low | [Main.psc:1414, 1426](../../Scripts/Source/Injury_Engine_Main.psc#L1414) | Grouping keys on `casters[j] == casters[i]` and source only. Two heal starts with no caster (a potion with no known drinker, ability effects) or two different spells from one caster in the same window merge into one line; the item name comes from the first target only (`g_healItem`). Add the item form to the key, or skip grouping when the caster is `None`. |
| 4 | Low | [Main.psc:1450-1500](../../Scripts/Source/Injury_Engine_Main.psc#L1450) | `NarrateHealGroup` differs from the single-target path: a closed gate drops the direct line (the event carries it), whereas `HealSend` holds a start line in `KEY_HS_PEND` for `HealTick`. The group also marks every target `done` before checking `msg == ""`, so a missing `heal_group` key in `session.json` silences all those targets, including their sessions and `NotifyHealStart`. Consider falling back to the per-target path when `msg == ""`. |
| 5 | Low | [Main.psc:190](../../Scripts/Source/Injury_Engine_Main.psc#L190) | The `target dead` clause in `ClaimNarration` has no age limit and runs after a death line, whose `DirectNarrate` target is the dead victim. The next claim (torture, heal, another death) then passes at once, even if SkyrimNet has not answered the death line. If the attacker is the speaker that replies, the gate is bypassed for any pair. Only clear this if in-game checks show double-talk after kills: check that the attacker reply really is the one awaited. |
| 6 | Low | [InjuryEngine.cpp:140-142](../../SKSE_Source/src/InjuryEngine.cpp#L140) | `g_healFood` and `g_healItem` are never erased per actor (only in `Revert` / `Load` clear). One entry per ever-healed actor, so slow growth. `g_unhurtHealAt` is erased in `Forget`; do the same for `g_healItem`. |
| 7 | Low | [Main.psc:1406](../../Scripts/Source/Injury_Engine_Main.psc#L1406) | `Utility.CreateBoolArray(count)` with `count == 0` (a take with codes but no resolved targets) logs a Papyrus array warning and returns `None`. The loops do not index it, so there is no crash. Return early when `count == 0`. |
| 8 | Info | [InjuryEngine.cpp:2606-2610](../../SKSE_Source/src/InjuryEngine.cpp#L2606) | `LookupByEditorID` for `OStimActorCountFaction` is a no-op at runtime without an editor-ID cache (there is none on stock SKSE), and there is none for SexLab. The form-ID lookups work (log: `SexLab:found OStim:found`). Harmless; the fallback is dead code unless powerofthree's Tweaks is installed. |
| 9 | Info | [docs/developers/papyrus.md:76](../../docs/developers/papyrus.md#L76) | The "Outbound mod events" row says `NotifyHealStart` comes from `NarrateSourceHeal`. `NarrateHealGroup` also sends it (once per target), and the `source` argument can now be 5 (food); the Main.psc comment says "1 spell, 2 staff, 3 potion". Update both. |

Checked and fine: the `DrainPendingEvents` blocked-actor ordering (later events of a held actor stay behind it); the `held < 0.0` and `now >= e.time` guards for a real-time reset; `DeathTick` iterating downward while `DeathHold` only appends; `DrinkSound` (thread-safe static init; empty result falls back to "eat"); the `InAnimationScene` check in both `UpdateBleedout` and the queued task; SE / VR (no new hook, only form lookups).

## v03 deferred items (2026-10-18 review, file removed from the working tree)

Not re-reviewed in depth; no commit since 0.1.4 touches them, so all still stand: `g_lock` held across health changes (freeze class), ally override following what is equipped, real spells losing to an equipped weapon, config readers without `Load`'s lock, CRT `/MT` (rejected).

## Not yet tested

Nothing from the four commits is verified beyond the single group-heal run in the log. In particular untested: SexLab / OStim scene release, food eat / drink wording, death hold / retry, ambient self-heal skip with a regen mod.

## Next

1. Fix #1 (and #2 together: both come from the hold window). Rebuild: `make dll`, then Papyrus `compile: papyrus` if `Main.psc` changes; deploy DLL and `.pex` together.
2. In-game after the fix: (a) the player eats food while unhurt: expect a `heal start` and the eat line, repeat 5 times to catch the tick timing; (b) cast a group heal on one wounded and one healthy NPC: expect "wounds" wording, not "no wounds to mend"; (c) two NPCs drink different potions in the same second: expect two lines, not one group line.
3. Then the deferred `g_lock` pass from v03.
4. Update the `papyrus.md` row (#9) and propose a KNOWLEDGEBASE entry: "events held in the DLL need a live sync loop".

Start the next session from `checkpoints/reviews/checkpoint-02.md` (written by the session that does the fixes).
