Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `f27c478` (Release 0.1.4) · uncommitted Papyrus + docs changes

# Goal

Make killing blows (and, where possible, real hits) reach SkyrimNet as DirectNarration in combat, rather than silent short-lived
events. Cause analysed in [checkpoints.md](checkpoints.md) from logs [logs/2026-10-20-11-36-51/](logs/2026-10-20-11-36-51/).

# Decisions

- A death that meets a closed reply gate waits **1 s** (user's choice), retried every 0.2 s tick, then becomes the `injury_death`
  event as before.
- The reply gate also opens when the target of the last DirectNarration is dead, because its reply never comes.
- No DLL change.

# Results (implemented, compiled, not tested in game)

All changes are in [Scripts/Source/Injury_Engine_Main.psc](../../Scripts/Source/Injury_Engine_Main.psc); `Scripts/Injury_Engine_Main.pex` rebuilt 11:45, clean.
- `DirectNarrate` → `NoteDirect(target)` stores `last_direct_target`. `ClaimNarration` adds `why = "target dead"`. In verbose mode it
  also logs `-> closed` (at most once a second, `closed_log_time`), with the target name.
- `NarrateDeathBatch`: mode / not visible → event (logged); gate closed → `DeathHold` (StorageUtil lists `KEY_DP_VIC/ATT/TXT/SRC/TIME`,
  `StartSync`). `DeathEvent` is the shared event path.
- `DeathTick` (from `OnUpdate` after `NarrateDeaths`): sends held deaths older than `DEATH_WAIT` as events; then, if the gate opens, sends the oldest
  held death as DirectNarration (one per tick). `OnUpdate` keeps looping while `KEY_DP_VIC` is non-empty. `ClearSessions` clears the lists on load.
- `NarrateHits` (health): verbose `no DirectNarration: mode | cooloff | not visible | gate`.
- Docs: CLAUDE.md Killing blows paragraph; KNOWLEDGEBASE entry "The reply gate starved death lines in combat".

# Findings carried over

- Hits still use the 6 s cooloff and the gate. Spell ticks often stay `below minor` (checkpoints.md finding 2).
- DirectNarration makes the victim reply, which sounds like a combat bark (checkpoints.md).
- 11:26:48 killer mismatch (Bob in the torture line vs Skadi's last hit) is unchecked.

# Not yet tested

- Everything above, in game.
- Bio looks / the healed line; torture stages, overlay, bleedout (carried over).

# Next

1. In MO2, make sure no stale `Injury_Engine_Main.pex` sits in overwrite. Start the game with verbose logging on and Direct
   Narration for injury and death.
2. Fight 2 bandits with a follower; kill both in sight.
3. In `Papyrus.0.log`, each death should show `DirectNarration ... ` (Injury_Engine.log `health:direct:death`), or
   `death pending` → `death timed out -> event` with the `ClaimNarration ... -> closed` reason. `ClaimNarration ... target dead` should appear
   after a hit narration on a victim who then died.
4. If deaths still time out, look at the logged `queue` / `audio_ended`: a wait longer than 1 s, or a forced send, may be needed.
   Optional next: narrate real hits only on a stage increase.
5. Snapshot the logs into `checkpoints/to-quite/logs/` and write `03-checkpoint.md`.
