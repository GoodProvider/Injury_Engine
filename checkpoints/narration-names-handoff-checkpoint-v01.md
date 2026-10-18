# Narration names: handoff checkpoint v01 (2026-10-17)

## Status
Built and deployed: `make dll` → `SKSE/Plugins/Injury_Engine.dll`; papyrus.exe → `Scripts/*.pex`; injury JSON edited in place. Not committed. Unconfirmed in game.

## Problem
Player report: a follower answers DirectNarrations about NPCs hurt in fights the party isn't in as if the player were hurt ("someone was hit in the leg").

## Findings
- Lead clauses already named both actors; the level / death / heal sentences after them used `{he}` / `{his}` / `{him}` / `{reflexive}`.
- `NameOf` returned "Someone" for an actor with no display name.
- `SourceHealText` switched to pronouns on self-heals.
- Real-damage hit narrations had no sight check (torture and deaths had `PairVisible`).

## Current fix
- Injury JSON `narration.*` / `death.*`: 217 strings now use `{name}` in place of pronoun placeholders (`text.*` looks / feels unchanged). Reflexive lines were reworded by hand in `burning`, `piercing`, `punching` and `slapping`.
- `InjuryEngine.cpp`: `NarrationVars` (pronoun placeholders render as the name) on every narration / event path; `ActorVars` stays only for `GetStateJson`. `SourceHealText` always uses names. `NameOf` falls back to base name, then race, then "an unknown attacker" / "an unknown person".
- `Injury_Engine_Main.psc` `NarrateHits` (health): DirectNarration needs `PairVisible`.
- Docs: guide.md Placeholders, docs/authors/prompts.md "Names, not pronouns", CLAUDE.md, KNOWLEDGEBASE, both changelogs (0.1.1).

## Known limits
- Repeated names read heavily ("Angi's nerves ... makes Angi flinch"), but that was requested.
- External API cause text (e.g. SkyrimNet_Leashed "Bob yanks Nina off her feet") is written by other mods and may still use pronouns.
- `TruceDecision.cpp` keeps its own "Someone" fallback (decision prompts, not narration).

## Next step for the developer
1. Start the game with MO2 loading this folder.
2. Let two NPCs fight within view: `Injury_Engine.log` `health:direct` lines name both actors with no she / he / her.
3. Move far from a fight (beyond `injury_engine.narration.max_distance`): only `health:short` lines, no `health:direct`.
4. Torture an NPC: start / again lines name torturer and victim.
5. Heal yourself with Healing Hands: "<name> casts ... around <name>".
6. Kill an NPC: the death line uses names only.
7. In `conversation_log.log`, the follower no longer answers as if the player were hit.

## After it holds
Commit with the 0.1.1 changes; drop "Unconfirmed in game" from the KNOWLEDGEBASE entry.
