# Handoff: torture spells kill (Flames (torture) killed Angi)

Status: **open, diagnosing.** A diagnostic DLL (built 2026-10-15 11:09) is deployed and waiting for one play test. Nothing is fixed yet. One classifier change (both hands) is in, but it did not fix this.

## Problem (user's words)
> in the last play test, the flames (torture) spell killed angi

Torture must never kill: torture wounds take health only down to the floor (`body.floor`, 10% of max), and the hooks are meant to block the spell's real damage. The user reproduced it three times (10:48, 11:01, 11:07). Victim: Angi (000CAB2E), caster: the player (Bob, 00000014).

## Evidence so far (verbose logging was OFF in every run)
Logs: `Injury_Engine.log` and `Papyrus.0.log` (paths in AGENTS.md).

- **10:48 and 11:01:** whipped to ~10.7%, then Flames (torture) → ~9.2% → ~4% → dead in 2–3 s. Health was already under the floor at the first fire tick the plugin recognised. Neither log has a `health:` line or a `death ... by` (killing blow) line.
- **11:07 (no whip, from full health):** Flames (torture) alone. The `torture: Someone ➤ Angi | fire ...` lines show health 99.2 → 92.4 → 82.4 → ... → 0.1 → dead. That's 7–10% of health per second, while torture points land at only ~1–2 a second (each worth ~1% via `TakeHealth`). The new always-on line read: `death Angi (000CAB2E) under torture, killer Someone (00000000), last real hit: generic 16.6s ago`.
- **Classification works.** Every tick in the 11:07 run was classified `fire`. Attacker "Someone" means the ticks came through the `ModActorValue` hook, which has no attacker. Apart from one `generic` tick (below), no `HandleHealthDamage` tick was ever logged as torture.
- **Key conclusion:** `OnTortureDamage` returns 0, so the hook passes a zeroed tick to the game, yet health falls at the spell's full rate. Zeroing the value in our hook does **not** stop the damage. KNOWLEDGEBASE already listed this as unverified: "the hooks return 0 for torture, so the game applies no damage".
- **Side finding:** the 11:06:47 line `health: ... generic ... amount:1.2` is the first tick after a re-cast. It arrived before any hit event and before `g_torture` was set, so it counted as real damage. `Classifier::NoteEffect` (AddTarget hook) never logged `torture effect: victim ... source fire` in any run, so the AddTarget hook never saw 0x813 land, or never matched it.
- **Ruled out:** the whip in the other hand returning `-1` (first theory; no whip in the 11:07 run). The Hostile flag removal (0x813–0x815) was new in all three runs, so a link to it isn't proven either way.

## Changes in the working tree from this session (uncommitted, mixed with the earlier "torture is not aggression" work)
- `SKSE_Source/src/Classifier.cpp`, `Classify` attacker branch: checks both hands. A "(torture)" spell matching the victim's live torture effect wins; then any real match; the Padded Whip's `-1` is used only when nothing else matches and no torture effect is live. Keep it: it's correct, it just wasn't this bug. `HitSink` now has a debug line per hit event.
- `SKSE_Source/src/Hooks.cpp`:
  - debug lines: `[hook] AddTarget ...`; `HandleHealthDamage` internal pass-through and health before / after; `ModActorValue` calls skipped while nested or internal. A local `Verbose()` helper gates them (spdlog level).
  - **`ProbeTorture` (info, always on, at most one line per hook per second):** for torture-classified ticks, logs `probe <hook> victim attacker damage kept health a -> b game took:x`, where `game took` is the health the original game call removed after we passed `kept`. In `ModActorValue` the torture path now calls `func` itself (to measure) and returns.
- `SKSE_Source/src/InjuryEngine.cpp` `DeathSink`: info line `death <victim> under torture, killer ..., last real hit: ...` when an actor dies with torture points.
- `KNOWLEDGEBASE.md`: entry "Padded Whip in one hand let a torture spell in the other kill (2026-10-15)", already marked **Unconfirmed**. Rewrite it once the real cause is known.
- `CLAUDE.md`: a clause on the both-hands classify rule (still accurate).
- Plan file: `C:\Users\bhuff\.claude\plans\in-the-last-play-wise-glacier.md`.

## Next step (waiting on the developer)
1. Cast Flames (torture) on Angi for ~5 s from full health. No whip needed, and she doesn't need to die.
2. Read the `probe` lines in `Injury_Engine.log`:
   - **`game took` ≈ 0 but health still falls between lines:** the damage is applied by a path we don't hook.
   - **`game took` ≈ the tick even though `kept` is 0:** the zeroed value is ignored, so the damage was applied before or outside our hooked call.
   - **Only `ModActorValue` probes, no `HandleHealthDamage` probes:** HandleHealthDamage doesn't see spell ticks (or sees them as non-torture).

## Planned fix (likely, whatever the probe says)
Data-level, so a torture spell can't take health at all:
- Change MGEFs 0x813–0x815 (`Spriggit/Injury_Engine/MagicEffects/Injury_Engine_Torture{Fire,Frost,Shock} - 00081x_...json`) from `ActorValue: Health` to a harmless dummy actor value. Candidates: an unused Oblivion leftover such as Fame / Infamy; research first. Frost / Shock keep their Stamina / Magicka secondary. Keep `Detrimental` (`IsHealing` treats a non-detrimental health modifier as healing; irrelevant after the change, but harmless). Then `make esp`.
- In the `ModActorValue` hook, treat `kDamage` on that AV as torture ticks: classify with the existing logic, and **drop the tick if it isn't a torture source** (never turn it into real damage). Zero the AV change. `TakeHealth` keeps the floor-limited health loss.
- Fix the first-tick problem as part of this: a dummy-AV tick can never become `generic`.
- Unknowns to verify in game: does a ValueModifier on the dummy AV tick through the same vtable `ModActorValue` (slot 6) call? Does AI / SkyrimNet still cast the spells?
- **Wider risk:** if the probe shows zeroing doesn't work in `HandleHealthDamage` either, the real-damage "replace torture wounds" reduction (KNOWLEDGEBASE line ~95, also unverified) is broken for weapon hits too. Check a sword hit on a tortured NPC with the probe extended to real hits.

## Deploy / verify after the fix
`make esp`, `make dll`. In game: Flames, Frostbite and Sparks (torture) for 10+ s on a whipped NPC. Health holds at 10%, the NPC lives, and there are no `death ... under torture` lines. Then remove or demote `ProbeTorture` to debug, and update KNOWLEDGEBASE and CLAUDE.md (Flow: how torture spell ticks are detected).
