# Review fixes, checkpoint 04 (2026-11-06)

Repo `Injury_Engine` / branch `main` / baseline `1f804ee` (checkpoint 01 commit). Code changed, DLL and Papyrus rebuilt. **Not committed**: checkpoints 02, 03 and 04 are all still in the working tree.

## Goal

Finish the review from [checkpoint-01.md](checkpoint-01.md): the low findings L1–L6 and L9, and the cleanup list. L7 and L8 were done in [checkpoint-03.md](checkpoint-03.md).

## Results

Fixed: **L1, L2, L3, L4, L5, L6, L9** and the whole cleanup list. **No review finding is open now.**

Builds:
- `make dll`: clean, wrote `SKSE/Plugins/Injury_Engine.dll` (2026-11-06 08:13, 1,680,384 bytes).
  - The first build failed because the probe removal had also cut out the `AddTarget` / `HandleScope` structs. They were restored from `HEAD` and the diff checked line by line.
- Papyrus: the new `make papyrus` (the same command as `compile: papyrus`) compiled all 5 scripts cleanly (`Scripts/*.pex`, 08:14).
  - A broken test script makes `papyrus.exe` exit 1, so `make release` now stops on a compile error.
- No native was added or removed (`Injury_Engine_Engine.psc` is unchanged this session), so the 65 registered natives still match.

Nothing has been tested in game (see the log finding below: no game run has used checkpoints 02–04 yet).

| # | Fix | Where | Confidence |
|---|---|---|---|
| L1 | `HealSend` finds the session again with `HealFind(target)` before every StorageUtil write that follows a yielding call (`PairVisible`, `ClaimNarration`, `HealMapText`). If the session is gone, it releases the claim (only if one is held) and returns. | `Injury_Engine_Main.psc` `HealSend` | 95% |
| L2 | The script variable `heal_short_method` is gone. Each session keeps its event method in the parallel list `KEY_HS_METH` (`injury_engine.hs_meth`), set by `HealOpen` (new `method` param) and the new `HealRefresh`. `NarrateSourceHeal` / `NarrateHealGroup` use a local `method`, and `HealSend` uses the session's own method. | `Injury_Engine_Main.psc` | 95% |
| L3 | `Classifier::IsTortureAttacker` returns false whenever either hand holds a `TESObjectWEAP` that is neither the padded whip nor a torture weapon. Spells and shields don't count. A whip lash from a dual-wielder is then calmed after the fact by `CombatSink`. | `src/Classifier.cpp`, `include/Classifier.h` | 90% |
| L4 | `Torture_Target`: a busy guard (`injury_engine.action_start` on the speaker, 60 s, a future stamp is stale), and a lent-whip marker (`whip_lent`, `whip_previous`, quest list `whip_lent_actors`). A lent whip is reused, never kept. `ReturnWhip` puts the previous weapon back. `ReturnLentWhips` (from `Setup`, every load) takes back whips from speakers whose action is no longer running. | `Injury_Engine_Actions.psc` | 85% |
| L5 | `OnTruce` sets `truces_again` on every event. The drainer loops while it is set, and `truces_draining = False` follows the loop test with no call in between, so an answer arriving at the end of a drain is still taken. | `Injury_Engine_Actions.psc` | 90% |
| L6 | The handled mark stores `{time, attacker}`. `TakeHandled(victim, attacker)` uses it only for the same attacker (a null attacker on either side still matches). | `src/Hooks.cpp` | 90% |
| L9 | New Makefile target `papyrus` (overridable `PAPYRUS` / `PAPYRUS_HEADERS`, same `-h` order as tasks.json). `release` runs it before `dll`. | `Makefile`, `release-guide.md`, `AGENTS.md` | 95% |

Cleanup:
- **Drowning diagnostics removed** (the user verified drowning in game): `UnderwaterProbe` and its calls, the pass-through `ModBaseActorValue` (0x05) / `SetActorValue` (0x07) hooks and their constants. `Throttle` and `ReadDrownSignals` stay, because they are still used.
- `HitText`: the unreachable `by_attacker` branch is gone.
- Papyrus: the unused `BatchKey` and `LashText` and the write-only `KEY_LAST_TORTURE` are removed. Its docs row is replaced by rows for the new keys.
- `LogInjuryPoint`: the throttle map drops keys idle for 10 s once it is larger than 512.
- Stale text:
  - `InjuryEngine.h` header: real points = 1% of max health, capped per part at `real_max`.
  - KB "no trampoline" now describes the `write_call` hooks.
  - KB drowning entry marks the probes removed.
- Already fixed earlier: the mojibake (now an escape) and `psc:563`.

Docs:
- `CLAUDE.md` Flow: the fight-reaction rule, the per-attacker handled mark, the `Torture_Target` busy guard and lent whip, and `KEY_HS_METH` / `HealFind`.
- `docs/developers/papyrus.md`: the StorageUtil table.
- `KNOWLEDGEBASE.md`: 4 new entries (the MO2 profile, heal session writes, `Torture_Target` guard, attacker-aware fight reaction and marks).

## Log finding: the game is not loading this folder

Snapshot: [logs/2026-11-06-08-10-59/](logs/2026-11-06-08-10-59/) ([MANIFEST.txt](logs/2026-11-06-08-10-59/MANIFEST.txt)).

- [Injury_Engine.log:36-37](logs/2026-11-06-08-10-59/Injury_Engine.log#L36-L37) (session 08:00) prints `Classifier: hit and combat event sinks registered` and `InjuryEngine: death event sink registered`. Those are the **pre-checkpoint-03** messages. The 07:30 session that was read in place showed the same.
- Cause: `C:\Skyrim\dev\profiles\SkyrimNet SexLab\modlist.txt:257-258` has `+Injury Engine` (an installed release, `C:\Skyrim\dev\mods\Injury Engine`, DLL 2026-10-26) **enabled** and `-Injury_Engine` (this repo) **disabled**.
- So nothing from checkpoints 02, 03 or 04 has run in game. The `[buff] … shrink:true` lines in those sessions come from the old build.
- The profile was not changed. It is a load-order file, so it needs the user's go-ahead.

## Not yet tested

Everything from checkpoints 02, 03 and 04. The user chose to treat 02 as verified, but no game run has loaded it.

## Next

1. **Fix the deploy:** in MO2 (profile `SkyrimNet SexLab`), disable `Injury Engine` (release copy) and enable `Injury_Engine` (this repo). Check that overwrite has no `Injury_Engine` DLL or `.pex`.
2. **Confirm the build loaded:** `Injury_Engine.log` must show `hit, combat and active effect event sinks registered`, `death and object load event sinks registered`, and **no** `probe hooks installed` line.
3. **In-game tests**, verbose logging on:
   1. The M1, M4, M5, M6 and M7 tests from [checkpoint-03.md](checkpoint-03.md#next) (Courage ending on a hurt follower, `External_Damage(..., False)`, the dashboard, whip then Healing Hands, leaving and re-entering a cell).
   2. **L4:**
      - Give a whip-less follower `Torture_Target` (whip, 10 strikes), save mid-action and load that save. After the load the follower must not keep a padded whip, and `Papyrus.0.log` shows `ReturnLentWhips: taking back …` if the stack was dropped.
      - A second `Torture_Target` on the same speaker during the first must log `already torturing, ignored`.
   3. **L3 / L6:** a follower with a padded whip in one hand and a sword in the other attacks a neutral NPC. The sword hit must do real damage (a `health:` line), and there must be no `fight reaction … torture hit, ally` line for it.
   4. **L1 / L2:** heal two actors with different sources at the same time (Healing Hands on one, a potion on the other). Each `healing_<method>_…` event id carries its own source.
   5. **Drowning:** drown briefly as the player. The drowning wound still lands on the lungs, with no `[probe]` lines.
4. **Commit** checkpoints 02–04 together (sources, DLL, `.pex`, docs, Makefile, this folder's `logs/`) once you're happy. Not done this session.
5. **Next session:** a fresh review pass, or a release (`release` skill), after the in-game results. It writes `checkpoints/2026-11-03-review/checkpoint-05.md`.
