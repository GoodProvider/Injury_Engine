# Review v00 — code-review follow-up (`what-was-changed.md`)

Date: 2026-10-15. Reviewed the uncommitted diff in `SKSE_Source/src/TruceDecision.cpp`, `SKSE_Source/src/InjuryEngine.cpp` and `Scripts/Source/Injury_Engine_Main.psc` against the surrounding code. Not committed.

## Verdict per item

### 1. Truce: duplicate `Ask` while a decision is in flight — OK
- Both in-flight branches (player box and LLM decision) now `return` without `Push`, so the first answer is the only `Injury_Engine_Truce` result and `ApplyTruce` runs once.
- `g_inFlight` is still erased on every answer path, on failure and on load (`TruceDecision.cpp` lines 240 / 266 / 319 / 340 / 375), so a speaker can't get stuck.
- Follow-up: the `Injury_Engine_Actions.psc` `OnTruce` comment still said "one per AskTruce". **Fixed:** it now says "one per answered AskTruce; a repeat while one is waiting is ignored".

### 2. Bleedout: stand up when torture is gone — OK
- `UpdateBleedout` now also releases a downed actor when `!HasTorture(st)`. Entering bleedout still requires `HasTorture(st)`, so the actor can't go down and stand up in a loop at low health.
- When torture reaches zero, the actor gets up even below `bleedout_below` (15%). That is the intended fix: the 10% floor no longer holds them down. No change.

### 3. Torture scene: pending start / switch vs the reply gate — fixed further
The idea is right (a start line held by `ClaimNarration` must not be dropped at the 3 s break), but the patch added two bugs:

- **Leak of hidden switch pairs.** A pair with `KEY_SC_SWITCH == 1` that goes out of sight becomes `SC_HIDDEN`. `SceneMatches(SC_SEL_SWITCH)` only selects `SC_RUNNING`, and the new removal rule kept every pair with the switch flag set. So the pair was never sent and never dropped. If it came back into sight, it sent a switch line that was long out of date.
- **Unbounded wait, dead victims.** Pending starts and switches had no time limit. Other narrations (death, real hits, heal) reset `last_direct_time`, so a held torture "start" could go out much later, even after the victim's death line. Nothing dropped pairs whose victim had died.

**Fix (`Injury_Engine_Main.psc`):**
- New `SceneWaiting(i)`: `SC_PENDING`, or `SC_RUNNING` with the switch flag set, meaning an unsent start or switch line.
- `SceneTick` removal: a waiting pair drops after `torture_maximum_break + reply_timeout` since its last hit (default 3 + 15 s). Every other pair, hidden ones included, drops after `torture_maximum_break` as before.
- The `SceneTick` visibility loop treats a `None` or dead victim as not visible (`SC_HIDDEN`). So a dead victim gets no start, switch or again line, and its pair drops at the break.
- The comments on the torture-scene header and on `SceneMatches` are updated to match.
- The claim logic is unchanged: every path in `SceneTick` still sends or calls `ReleaseNarration`.

## Build / deploy
- Papyrus: Pyro succeeded with 2 of 2 scripts compiled and 0 failed. `Scripts/Injury_Engine_Main.pex` (23:59:36) and `Scripts/Injury_Engine_Actions.pex` (23:59:35) are current.
- DLL: `SKSE/Plugins/Injury_Engine.dll` (23:42:41) was built after both `.cpp` edits. This pass made no C++ changes, so it was not rebuilt.
- Nothing else needs deploying (no JSON or ESP changes).

## Left as before (lower urgency, from the original review)
- Truce generation counter across a load (an answer from before the load arriving after it).
- `kIgnoreFriendlyHits` release when the actor unloads.
- Passive health sync when torture points decay.

## In-game checks
1. Call `Stop_Fighting` twice before the answer comes back. `Papyrus.0.log` should show one `ApplyTruce`, and `Injury_Engine.log` should show `duplicate ignored`.
2. Torture an NPC into bleedout, then let the torture heal off (real wounds may remain). The actor should get up without waiting for health above 15%. The log line ends in `(torture:false)`.
3. A single whip with a slow LLM reply: the combined **start** line should still go out once the gate opens, not be dropped at 3 s.
4. Turn on verbose logging. Whip, switch to another type, then walk out of narration range. The pair should be dropped about 3 s after the last hit, with no switch line when you come back.
5. Whip an NPC, then kill them with a real hit while the reply is still pending. You should get the death line, and no torture start line after it.
