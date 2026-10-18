# Handoff: YAML actions + `Stop_Fighting_by_Yielding` (v03)

Status: **implemented, built, deployed, not tested in game, not committed.** Baseline commit `c385cae`, on top of the uncommitted v02 work. `make dll` succeeded; Pyro compiled `Injury_Engine_Actions` and `Injury_Engine_Engine` (2 succeeded, 0 failed). No ESP change.

## Problem
The user wants all four LLM actions as SkyrimNet YAML actions (as in SkyrimNet_SexLab): `Torture_Target`, `Stop_Fighting`, `Start_Fighting`, and a new `Stop_Fighting_by_Yielding`. The new action means the speaker surrenders, and the target accepts or rejects the surrender through the decision model.

## What changed
- **YAML** (new): `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/actions/`
  - `Torture_Target.yaml` → `TortureTarget`. Params `target`, `type` `whip|pinch|slap|pull|punch`, `strikes` `1..10`. Not in combat.
  - `Stop_Fighting.yaml` → `StopFighting`. In combat.
  - `Stop_Fighting_by_Yielding.yaml` → `StopFightingByYielding`. In combat.
  - `Start_Fighting.yaml` → `StartFighting`. No eligibility rule.
  - All four use `questEditorId: Injury_Engine` and `scriptName: Injury_Engine_Actions`. Eligibility uses the native `is_in_combat` only.
- **Papyrus** `Injury_Engine_Actions.psc`:
  - `Setup` no longer registers anything (it keeps the mod event registration and `RestoreExpiredTruces`).
  - The global `*_IsEligible` / `*_Execute` functions became member functions with positional params. Each re-checks player, dead and target.
  - `ApplyTruce(speaker, target, accepted, yielded)`; `OnTruce` reads `TruceYields`.
  - Yield: the speaker gets `StopFighting` + `Calm` + `SheatheWeapon` at once, then `AskTruce(.., true)`. Accepted: the target stops and is calmed. Refused: the target is left alone. Event `injury_truce`, id `surrender_<s>_to_<t>`.
- **C++**:
  - `TruceDecision::Ask(speaker, target, yield)` picks template `injury_engine/accept_surrender` or `accept_truce`; `Answer.yielded`. Log prefix `surrender:` or `truce:`.
  - Natives: `AskTruce(Actor, Actor, Bool yield)` and the new `Int[] TruceYields()`.
- **Prompt** (new): `prompts/decisions/injury_engine/accept_surrender.prompt`.
- **Data**:
  - `whipping.json` `action.description` is removed; the text now lives in the YAML.
  - `guide.md`, `manifest.yaml` (`max_strikes` text) and `manifest.json` description are updated.
- **Docs**: actions.md (rewritten), CLAUDE.md, AGENTS.md map, README, papyrus.md, overlay.md, both changelogs, KNOWLEDGEBASE ("LLM actions are YAML, not Papyrus RegisterAction").

## Incident
A script truncated `docs/authors/actions.md` (a cp1252 write error). It was restored from this session's copy, then rewritten. `git diff` against HEAD looked as expected.

## Known limits
- The YAML type list and strike range are static. Torture types added by other mods are not offered.
- A refused yield leaves the speaker at Aggression 0, but it may still defend itself when hit.
- Unverified: that SkyrimNet loads YAML from this plugin, passes `strikes` as a String, and resolves `prompts/decisions/` here.
- SE only.

## Next step for the developer
1. MO2: check overwrite for a stale `Injury_Engine.dll` or `.pex`. **Restart the game**, since old Papyrus registrations live until then.
2. SkyrimNet webUI → Game Data Explorer → Refresh Actions. Each of the four actions should show once.
3. Turn verbose logging on, then:
   - `Torture_Target` out of combat (whip and punch): `TortureTarget:` lines in `Papyrus.0.log`. It should not be offered in combat.
   - The `Start_Fighting` and `Stop_Fighting` checks from v02.
   - Mid-fight `Stop_Fighting_by_Yielding`: the speaker sheathes and stops at once, and `surrender: send` / `surrender: ... answer` appear in `Injury_Engine.log`. Accept: the target stops, and `GetAV Aggression` is 0 on both. Refuse: the target keeps attacking.
   - No decision provider: the yield is refused, and the speaker still stands down.

## After it holds
Commit together with v02, leaving out `checkpoints/`. Put the in-game results in KNOWLEDGEBASE.
