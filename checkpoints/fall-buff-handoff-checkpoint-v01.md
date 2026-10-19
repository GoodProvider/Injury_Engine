# Fall / buff handoff checkpoint v01 (2026-10-18)

## Status
Built and deployed (`make dll` → `SKSE/Plugins/Injury_Engine.dll`, 18:40, no warnings). Not yet tested in game. No Papyrus, JSON or ESP changes.

## Problem
Xamos: a follower (Camilla Valerius) sneaking along is narrated as falling ("Camilla Valerius stumbles.") whenever the player's Ocato's Recital buffs (Courage, Nightfall / Shadow Mage) wear off; Courage is the suspect. The logs in `bug-logs/spell-damage/` have no Injury_Engine lines.

## Findings
- No-cause Health damage (no attacker, no hit event) goes to `ClassifyDrown`, then `ClassifyFall`. A fall is not held as uncaused, so it wounds at once.
- `fallStartHeight` is stale between falls: the local `Injury_Engine.log` shows `fallTime:0.00 drop:-12613` for a standing actor. A brief `fallTime > 0` on a step, plus an old start height 300+ units up, passed the old test.
- A buff ending is a plausible source of that no-cause Health loss. The exact engine path is unknown.

## Current fix
- `Classifier.cpp` `ClassifyFall`: also needs `fallTime >= 0.3 s` and `drop <= 686 × max(gravity, 1) × t² + 100`. Rejections are logged at debug with a reason; accepted landings log `landing <id>: fallTime drop ...` at info.
- `Classifier::NoteBuffChange` / `RecentBuffChange` (map `g_buff`, cleared in `ResetSession`).
- `Hooks.cpp`:
  - `ModifyEffectActorValue`: a non-torture Recover Health effect with a negative value calls `NoteBuffChange` and runs inside `BuffScope`.
  - `ModActorValue`: a negative `kTemporary` Health change calls `NoteBuffChange`.
  - In both damage hooks, attacker-less damage under `BuffScope` or within 1 s of a mark passes through, with an always-on throttled `[buff] ... not a wound` line.
  - `Throttle` helper shared with `UnderwaterProbe`.
- `InjuryEngine.cpp` `ResolveUncaused`: drops a held chunk with a mark within 2.5 s (`dropped (buff change)`).
- Docs: KNOWLEDGEBASE entry, CLAUDE.md, CHANGELOG.md, CHANGELOG-player.md (CHANGELOG-developer.md is left to the release skill).

## Known limits
- Unverified that a Fortify Health ending reaches any of the marking paths. If none fires, the fall fix still stops the "falling" label, but the loss may still land as a `generic` wound when it sticks (≥ 5 % of max health).
- A real no-cause loss within 1 s of a buff ending is ignored.

## Next step for the developer
1. Enable the mod build in MO2 and turn on verbose logging (`injury_engine.debug.verbose_logging`).
2. Cast Courage on a follower, let it expire (or recast it with Ocato's Recital), and stand or sneak nearby. Expect no narration. In `Injury_Engine.log`, look for `[buff]` and `recover: buff change`, and for no `landing` and no `uncaused ... landed`.
3. Jump from about 4 m or more: expect `landing ...` and a fall narration as before.
4. Walk down stairs and slopes: expect no `landing` lines.
5. Send the DLL to Xamos and ask for `Injury_Engine.log` from an Ocato's Recital repro.

## After it holds
- If the log shows which path a fortify ending takes, record it in KNOWLEDGEBASE and remove the unused marking path.
- Include it in the next release notes (release skill).
