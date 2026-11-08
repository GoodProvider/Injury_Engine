Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `568b85c` (Injury bands commit) · not committed

# Goal

The user saw a SkyrimNet parsing error at the end of the last play (session ending 10:14). This session found the cause and
cleaned up a related stale prompt override that it turned up.

# Decisions

Unchanged from [checkpoint-02.md](checkpoint-02.md). No repo code changed.

# Results

Logs: [logs/2026-10-20-10-18-40/](logs/2026-10-20-10-18-40/) (`SkyrimNet.log`, `Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`).

- **The parse error comes from the provider, not Injury_Engine.** SkyrimNet.log:4624 has `MemoryGenerator.cpp:479 Failed to parse LLM response as
  JSON ... last read: 'B'`. The raw response was just `Bob's` (4625). The memory model `qwen/qwen3-235b-a22b-2507` first returned an empty
  completion (4612–4613), and the failover slot sent the one-word reply. SkyrimNet skipped that memory segment (4627–4628).
- **Found next to it: a stale prompt override (this one is ours).** `overwrite/SKSE/Plugins/SkyrimNet/overlay/prompts/submodules/character_bio/0420_injury_looks.prompt`
  is a 2026-10-09 SkyrimNet prompt editor save from before the rename. It called `skyrimnet_whipped_injuries(actorUUID)` and shadowed the repo
  prompt, so every bio render failed: SkyrimNet.log:4545 `variable 'skyrimnet_whipped_injuries' not found`, then `Failed to render bio template`.
  As a result, wound looks and the 0.1.3 healed line never reached the LLM.
- Moved to `C:\Skyrim\dev\overwrite-backup-2026-10-20\` (same relative paths): that overlay prompt, `Data/Scripts/SkyrimNet_Whipped_Actions.pex`,
  `SkyrimNet_Whipped_Main.pex`, `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/` and `SKSE/Plugins/SkyrimNet_Whipped/`. Overwrite now has
  no `*whipped*` files.
- KNOWLEDGEBASE: new entry "A SkyrimNet prompt editor override shadows the plugin prompt (2026-10-20)", plus a pointer in the rename section.
- Truce decision at 10:02:01 (`openrouter_output.log`) parsed fine (`accept: noul 0.2`, refused).

# Findings / risks

Carried over from [checkpoint-03.md](checkpoint-03.md):
- Spell streams count each tick as one attack, so points scatter over parts and rarely reach minor before death.
- `➤` shows as `â¤` in the log (cosmetic).
- The External API `RealHit` scale changed and is not yet in a changelog.

New:
- Every earlier test of the bio "looks" / healed line ran with the stale override in place, so none of them counts as tested.
- If MemoryGenerator parse errors keep coming, the fix is the memory model / slots in the SkyrimNet dashboard (the user's call).

# Not yet tested

- Bio looks and the healed line in the prompt, now that the override is gone.
- Torture, all of it (carried over).

# Next

1. In game: wound a follower (any real hit), talk to them. In `SkyrimNet.log`, check there are no `skyrimnet_whipped_injuries` / `Failed to render bio template`
   lines. In `openrouter_input.log`, check the bio carries the injury looks. After healing, it should say "<name> is completely healed."
2. Torture test: whip, punch and Flames (torture). Check stages, overlay and bleedout match 0.1.3.
3. Hit a tortured part with a sword: the red overlay segment should grow.
4. Snapshot the logs next session and review.
5. When torture passes: changelog entries (release skill) for the bands work and the `RealHit` scale change.
