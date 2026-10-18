# Handoff: `Stop_Fighting` / `Start_Fighting` LLM actions (v02)

Status: **implemented, built, deployed, not tested in game, not committed.** Baseline commit `c385cae`. `make dll` and Pyro both succeeded (3 scripts compiled, 0 failed). No ESP change.

## Problem
The user asked for an LLM action that lets an NPC stop fighting (gated on being in combat, with the target accepting or refusing through SkyrimNet's decision model) and one that starts a fight with a target. The v01 plan is the spec.

## What changed
- **C++**
  - New `SKSE_Source/src/TruceDecision.cpp`, `include/TruceDecision.h`: `Init` (FindFunctions, kDataLoaded), `Reset` (load / new game), `Ask`, `Take`. Template `injury_engine/accept_truce`, needs API v12+ and a release build. Refusal: no API, `no_decisions_route` (latched until the next load, warned once), a request already in flight for the speaker, a request that was not queued, or a failure. The answer is `"true"` from a `noul` question. Logs: `truce: send ...` and `truce: ... answer accepted|refused` in `Injury_Engine.log`.
  - Context: names, uuids, `relationship` (target → speaker), health words, `*_is_follower`, `target_fights_speaker`, `timeline` (the target's events).
  - `Classifier::ForgetCalm(a, b)` and `Classifier::StopFighting(actor)`. The shared stop body is now `StopCombatNow`, which `StopCombatLater` also uses.
  - Natives: `AskTruce`, `TakeTruces`, `TruceSpeakers`, `TruceTargets`, `TruceAccepted`, `ForgetCalm`, `StopFighting`.
  - `CMakeLists.txt` includes the devkit `CppAPI`.
- **Papyrus**
  - `Injury_Engine_Actions.psc`: both actions, `OnTruce` (mod event `Injury_Engine_Truce`), `ApplyTruce`, `Calm`, `RestoreAggression`, `RestoreExpiredTruces`, `GetActions()`.
  - `Main.OnUpdateGameTime` calls `RestoreExpiredTruces`.
  - `Injury_Engine_Engine.psc`: the native declarations.
- **Prompt:** `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/prompts/decisions/injury_engine/accept_truce.prompt`.
- **Docs:** actions.md, papyrus.md, overlay.md, CLAUDE.md Flow, README features, both changelogs, KNOWLEDGEBASE.

## Deviations from the v01 plan
- `TruceAccepted` returns `Int[]` (1/0), not `Bool[]`. A `std::vector<bool>` return is not safe to marshal through CommonLib.
- Expiry has no game-time registration of its own in Actions. Main and Actions share quest 0x800, and the plan's "update registrations are per script" was unverified. `Main.OnUpdateGameTime` already fires every `hours_per_point` (default 1 game hour) and now calls `RestoreExpiredTruces`. Setup (every load) calls it too. An expiry can therefore land up to `hours_per_point` late.
- The saved Aggression is the **base** value (`GetBaseActorValue`), restored with `SetActorValue`.

## Known limits
- `Start_Fighting` against an ally or follower may not stick (risk carried over from v01).
- Unverified: whether SkyrimNet resolves `external/<id>/prompts/decisions/` for this plugin the way it does for SexLab.
- A truce answer that arrives after a reload still pushes a mod event. `ApplyTruce` only re-checks that the actors are alive.
- SE only.

## Next step for the developer
1. MO2: make sure overwrite has no stale `Injury_Engine.dll` or `.pex`.
2. Load a save. In the SkyrimNet webUI, go to Game Data Explorer → Refresh Actions.
3. Turn verbose logging on, then test:
   - `Start_Fighting` on a bandit.
   - Torture an NPC, then within 3 s use `Start_Fighting` between the pair. The fight should stay, with no `calm: victim ... stops fighting` line.
   - Out of combat, `Stop_Fighting` should not be offered.
   - Mid-fight `Stop_Fighting`: check `truce: send` and the answer line. On accept, `GetAV Aggression` on the NPC should be 0 and the fight should not restart. On refuse, the fight may resume.
   - Without a decision provider, the warning should appear once and the light path should run.
   - Wait 24 game hours, or save, wait past the expiry and reload. Aggression should be restored (a `RestoreAggression` line in `Papyrus.0.log`).

## After it holds
Commit, leaving out `checkpoints/`. Add the in-game results to the KNOWLEDGEBASE entry "Stop_Fighting / Start_Fighting and the torture calm".
