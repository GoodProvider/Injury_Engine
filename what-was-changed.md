# What was changed (code review follow-up)

Implements the three **Fix these** items from the main vs `origin/main` code review plan.

## 1. Truce: duplicate `Ask` while a decision is in flight

**File:** `SKSE_Source/src/TruceDecision.cpp`  
**Behavior:** If `Stop_Fighting` / surrender is called again for the same speaker before the player box or LLM answer returns, the second call is ignored. It no longer pushes a synthetic **refused** answer, which had caused `ApplyTruce` to run twice (refuse, then accept).

**Log:** `already waiting for an answer; duplicate ignored`

**Deploy:** `make dll` succeeded; `SKSE/Plugins/Injury_Engine.dll` updated.

## 2. Bleedout: stand up when torture is gone

**File:** `SKSE_Source/src/InjuryEngine.cpp` (`UpdateBleedout`)  
**Behavior:** A downed actor is released from torture bleedout when they no longer have torture points (`!HasTorture(st)`), not only when health rises above `bleedout_below` (15%). Fixes the case where health stayed at the 10% floor after passive torture heal while bleedout kept them down and blocked regen past 15%.

**Log:** recovery line now includes `(torture:true/false)`.

**Deploy:** same DLL build as above.

## 3. Torture scene: pending start/switch vs reply gate

**File:** `Scripts/Source/Injury_Engine_Main.psc`  
**Behavior:**

- `SceneMatches`: `torture_maximum_break` (3 s) applies only to **again** selection, not to pending **start** or flagged **switch** pairs waiting on `ClaimNarration`.
- `SceneTick` removal: pairs are dropped after the break only if they are not `SC_PENDING` and do not have an unsent switch (`KEY_SC_SWITCH == 0`).

Comments at the torture-scene header were updated to match.

**Deploy:** Papyrus source only in this session. **`pyro` was not on PATH** here — run **`pyro skyrimse.ppj`** (or VS Code **compile: pyro**) so `Scripts/Injury_Engine_Main.pex` matches before testing in MO2.

## Not in this pass

Lower-urgency review items (truce generation counter on load, `kIgnoreFriendlyHits` on unload, passive health sync when torture points decay) were left unchanged.

## Suggested in-game checks

1. Double `Stop_Fighting` before answer → one `ApplyTruce` in `Papyrus.0.log`.
2. Torture to bleedout, let torture heal off (real wounds may remain) → actor gets up without waiting for health &gt; 15%.
3. Single whip with slow LLM reply → combined **start** direct still fires after the gate opens (not dropped at 3 s).
