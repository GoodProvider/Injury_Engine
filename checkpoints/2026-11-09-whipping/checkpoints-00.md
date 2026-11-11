# Slave "whipped for no reason": checkpoints-00

Repo: Injury_Engine (no code changes) · branch main · baseline `ad0767d`. The fix is in `C:\Skyrim\dev\mods\SkyrimNet_DOM`, branch main, baseline `09de906`, uncommitted.

## Goal

In the last play sessions the slave Coralyn kept getting "whipped for no reason". Find out why and stop it.

## Results

No whip was used. The whipping was only in the LLM's text, and it came from DOM prompts, not from Injury_Engine.

Changed in `SkyrimNet_DOM`:

- `Scripts/Source/SkyrimNet_DOM_API.psc`: new `KEY_PAIN_METHOD` (`skyrimnet_dom.pain_method`), `MarkPainMethod`, `PainMethod` (falls back to `"pain"`). `MarkPainActivity` takes the source. `EndStalePainPunishment` clears the key when no punishment runs and after it ends one.
- `Scripts/Source/SkyrimNet_DOM_Events.psc`, `OnInjuryTorture`: stores the method before `StartPunishingByIndex` (its notification saves the actor JSON) and on every batch.
- `Scripts/Source/SkyrimNet_DOM_Decorators.psc`: exports `punishment.method`, which also reaches the saved JSON through `Get_Actor_Info`.
- `prompts/submodules/character_bio/0055_dom_physical_activity.prompt` and `user_final_instructions/0551_dom_instructions.prompt`: "is being punished with <method> for <reason>". Removed "Include this information in your response" and "Ignore dialogue and narration, if they disagree with activities just described".
- `KNOWLEDGEBASE.md`, `docs/authors/punishment.md`: documented.

Not done: the plan's step 3 (removing the "for no reason" widget text). `StartPunishingByIndex` passes the reason to `DOM_Actor.CorrectBehaviour`, which acts on any reason above 0 (equip changes, spoken answers, re-queued reasons); index 0 returns early. So the widget still briefly shows "with pain for no reason" before the real reason is applied.

## Findings

Logs: [logs/2026-11-09-14-09-45/](logs/2026-11-09-14-09-45/) (copies; see `MANIFEST.txt`).

- Only slaps landed. `Papyrus.0.log` L4147 and L4150 (09:10:55); `Papyrus.1.log` L7016 (08:32:57) and L7971 (08:37:48); `Injury_Engine.log` L789-790. No `OnLash` or `TortureTarget` in `Papyrus.0` to `.3`.
- `OnInjuryTorture` starts DOM's pain punishment for any torture source, setting `mind.whipping_active`. That flag means "pain punishment", not "whip".
- Both prompts turned the flag into "is being whipped". `openrouter_input.log` carries it in 5 generation requests after one slap: L1744, 2381, 3038, 3713, 4360 (bio copies L1649, 2281, 2938, 3612, 4282).
- Resulting lines: `conversation_log.log` L2899, 2902, 2909, 2912 (and L2846, 2851 from the earlier session).
- The state outlives the slap. The 20 s timeout counts game time: `EndStalePainPunishment` logged `elapsed:21.358967s` at 09:12:13 for the 09:10:55 slap (78 s real), `Papyrus.0.log` L4624. While time is paused, prompts read `SkyrimNet_DOM/actors/<name>.json`, saved with the flag on. DOM closed the punishment at 09:13:12 (L4625), and the 09:13:14 request was the last with the line.
- The widget text: `Papyrus.0.log` L4148, "Start punishment of a weeping Coralyn with pain for no reason". Reason 0 goes in first; `ApplyTortureReason` names it a second later (L4154).
- The whip dialogue of 2026-11-08 (`conversation_log.log` L2495-2750) is older than the Papyrus logs kept, so it is unconfirmed whether a real whip was used then.

## Build state

- The three `.pex` files (`API`, `Events`, `Decorators`) were rebuilt at 14:10:55 and contain the new names (`skyrimnet_dom.pain_method`, `MarkPainMethod`, `PainMethod`; byte search).
- That build was not this session's. This session's `papyrus.exe` run failed with 958 header errors and wrote nothing; the DOM repo is Pyro only (`AGENTS.md`). Something else in that repo compiled at 14:10:55, most likely another session or a task of yours.
- The DOM repo has other uncommitted work being changed at the same time (`Actions`, `Menu`, `InfoPanel`, `Obedience`, YAML actions, docs). Commit with care: `Decorators.psc`, `0551`, `KNOWLEDGEBASE.md` also contain changes from that work.

## Not yet tested

Nothing has been run in game. Untested: the new wording, the JSON path while paused, the `existsIn` fallback, clearing the method after a punishment.

The prompt no longer overrides narration, so an actual whipping relies on Injury_Engine's own events for intensity. Check it still reads strong enough.

## Next

1. In MO2 (profile `SkyrimNet SexLab`, `SkyrimNet_DOM` enabled), launch and load the save.
2. Slap a DOM slave from the punish panel. In `openrouter_input.log` expect "is being punished with slapping for ..." and no "whipped".
3. Open a menu or otherwise pause during the punishment and talk to the slave: same wording.
4. Lash with the padded whip: "punished with whipping", and the reaction should still feel strong.
5. Wait for the punishment to end: the line leaves new prompts.
6. Punish through DOM's own route: "punished with pain".
7. If you actually saw whip swings on screen, report it with the logs; that would be a separate bug.
8. Commit the `SkyrimNet_DOM` changes when the other work there is settled.
