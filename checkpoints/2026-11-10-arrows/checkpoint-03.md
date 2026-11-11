Repo: Injury_Engine · branch `main` · baseline `ad0767d` (arrow work committed on top in this session) · 2026-11-10

# Checkpoint 03: arrows verified; `<unused6226>` in chat is an LLM provider fault

## Goal

1. Confirm checkpoint-02's arrow fix in game.
2. Player report: `unused6226` showed up in SkyrimNet's chat. Find out whether Injury_Engine caused it.

## Results

- Arrows verified in game by the player. Log: [Injury_Engine.log:1661](logs/2026-11-10-22-28-49/Injury_Engine.log#L1661)
  `classify 0009F361: melee hit weapon 0003B562 attacker 00000014 -> arrow`; 48 `-> arrow` lines in the session.
- The arrow work (all groups' `arrow.json`, Classifier / Hooks / InjuryConfig, DLL, .pex, docs) is committed.
- No code change for `<unused6226>`: it is not caused by Injury_Engine.

## Findings

Logs: [logs/2026-11-10-22-28-49/](logs/2026-11-10-22-28-49/) (`MANIFEST.txt`; includes SkyrimNet conversation and
OpenRouter request / reply logs).

- [conversation_log.log:3506](logs/2026-11-10-22-28-49/conversation_log.log#L3506) and
  [:3512](logs/2026-11-10-22-28-49/conversation_log.log#L3512): two NPC lines made only of
  `<pad><unused6226><unused6226>...` (Bargo 22:19:08, Aghurz 22:24:00).
- Both are SkyrimNet `[combat] GenerateStreaming` replies:
  [openrouter_output.log:14162](logs/2026-11-10-22-28-49/openrouter_output.log#L14162),
  [:14245](logs/2026-11-10-22-28-49/openrouter_output.log#L14245), each slow (20 s / 26 s). No older output log has
  any `<unusedN>`.
- Both requests went to `google/gemma-4-31b-it`:
  [openrouter_input.log:7412](logs/2026-11-10-22-28-49/openrouter_input.log#L7412),
  [:8504](logs/2026-11-10-22-28-49/openrouter_input.log#L8504). The good combat requests around them went to
  `google/gemma-4-26b-a4b-it`; [:8019](logs/2026-11-10-22-28-49/openrouter_input.log#L8019) (26b) is 5 s before
  8504 with nearly the same prompt and answered fine.
- `<pad>` / `<unusedNNNN>` are Gemma tokenizer special tokens: the provider serving 31b decoded padding instead of
  text. 31b ran ~150 requests on 11-08..11-10 without this, so it is new and provider-side.
- Injury_Engine's prompt content in the bad request is clean (arrow hit narration, `### Arrow Wounds` bio; no
  `{placeholder}` left, no odd characters). `Injury_Engine.log` has no `unused`.
- SkyrimNet logs don't name the serving provider. Model rotation and provider order:
  `C:\Skyrim\dev\overwrite\SKSE\Plugins\SkyrimNet\config\OpenRouter.yaml` (lines 2, 94, 110, 176), order
  `friendli, venice, primeintellect, modal, chutes, ...`, `ignore: [atlas-cloud]`.

## Not yet tested

- Bow bash -> `bludgeoning` (no `(bash)` line in this session's log).
- Crossbow -> `arrow`; death line with arrow text.

## Next

1. On the OpenRouter Activity page, open the gemma-4-31b-it requests at 22:18:46 and 22:23:34 and note the
   provider; add it to `ignore` in `OpenRouter.yaml` (or remove `google/gemma-4-31b-it` from the model lists for
   now). This is SkyrimNet config, not this mod.
2. Bow bash an NPC (block + attack): `melee hit weapon ... (bash) -> bludgeoning`.
3. Crossbow shot -> `arrow`; kill something with an arrow -> arrow death line.
4. Optional text nit: "shoots an arrow into X hitting X's right leg" would read better with a comma before
   "hitting" (`humanoid/arrow.json` narration).
5. Snapshot logs into `checkpoints/2026-11-10-arrows/logs/` and start from this file.
