# Healed bio line: handoff checkpoint v01

## Status
Built and deployed (DLL rebuilt into `SKSE/Plugins/Injury_Engine.dll`; JSON and prompt edited in place). Not yet tested in game.

## Problem
Followers kept talking about wounds after an actor had fully healed. A healed actor is erased from `g_actors`, so `StateJson` returned `{"sources":[]}` and the character bio said nothing. Only the old injury lines in the dialogue history were left.

## Findings
- `InjuryEngine::Current` erases a fully healed (or dead) actor and queues `kFullyHealed`. After that the decorator has nothing to show.
- The user chose to show the line for every unhurt living actor, including ones that were never hurt. It is not limited to recently healed actors.

## Current fix
- `InjuryEngine::StateJson`: when `sources` is empty and the actor is alive, the JSON gets `healed`. The text is `session.json` `healed` (default "{name} is completely healed."), rendered with `NarrationVars`.
- `character_bio/0420_injury_looks.prompt`: a new `{% else if existsIn(injuries, "healed") ... %}` branch prints that line.
- Docs updated: json-keys.md, prompts.md, guide.md, CLAUDE.md. Unreleased entries added to all three changelogs.

## Known limits
- Wounds on parts that are switched off are hidden from prompts, so an actor whose only wounds are there also reads as "completely healed".
- The feels prompt (final instructions) is unchanged.

## Next step for the developer
1. Restart Skyrim. MO2 must load this folder, and overwrite must not hold a stale DLL.
2. Hurt a follower (whip or weapon), then heal them fully with Healing Hands or potions until "fully healed".
3. Talk to the follower or let them comment.
4. Grep `C:\Skyrim\dev\overwrite\SKSE\Plugins\SkyrimNet\logs\openrouter_input.log` for `is completely healed.`

## After it holds
- The line is in the healed actor's bio, and in the bios of unhurt NPCs.
- A wounded actor's bio shows the wound list and no healed line.
- Followers stop describing the healed wounds.
