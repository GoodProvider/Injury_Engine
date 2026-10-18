# Handoff v03: torture spells kill (Flames (torture) killed Angi)

Status: **Fixed and committed (7a0ad18). Log line extended (DLL 2026-10-15 12:14, uncommitted); two checks left for a play test.** Follows `killed-handoff-checkpoint-v02.md`.

## Problem
Flames (torture) killed Angi (000CAB2E), caster the player (Bob, 00000014). Torture must never kill: torture wounds take health down to the floor (`body.floor`, 10%) and no further.

## Findings from the 12:03 run (DLL 11:52)
- `probe effect hook ... av:-1 member:24` for all three torture MGEFs, so the `this->actorValue` resolution is right.
- After the first tick every `probe (Dual)ValueModifierEffect` line reads `change:+0.000`: ticks are blocked before they land. Health fell only in our own `TakeHealth` steps (99% to 80%).
- The attacker is `Bob (00000014)`. No HandleHealthDamage probe for torture ticks, no `generic` on a re-cast, no deaths.
- The sword logged `slashing` and vanilla Flames logged `burning`.

## Done since v02
- Commit 7a0ad18: the whole fix set (effect hook 0x20, HandleHealthDamage restore, both-hands classify, non-hostile MGEFs 0x822–0x825, fight-reaction hook). The probes are now verbose-only `[hook] ...` debug lines and `ProbeDue` is removed. KNOWLEDGEBASE has a kNone AV entry and marks the HandleHealthDamage entry confirmed; CLAUDE.md Flow is updated.
- **Uncommitted (12:14 DLL):** `InjuryEngine.cpp` `LogInjuryPoint`: `torture:` / `health:` lines now include `actual:<current>/<max>`, the victim's live game health read when the line is written. `health:<pct>` is the engine's value for that point (estimated per point for multi-point real hits).

## Known limits
- The 10% floor hold was not reached in the 12:03 run (v01 proved the restore floor).
- The dual Stamina / Magicka drain is our own `ModActorValue` call; its amount has not been checked against vanilla.

## Next step (developer)
1. Load the save. Hold Flames (torture) on Angi until health sits at about 10% for ~10 s. Then cast Frostbite and Sparks (torture) and watch the Stamina / Magicka bars.
2. Expected: Angi lives, `actual:` stays at or above 10% of max on every `torture:` line, and the secondary bars drain at about the vanilla rate.
3. Send `Injury_Engine.log`. Compare `health:` with `actual:` on each line; a gap means the engine's health model and the game disagree.

## After it holds
- Commit the `actual:` log change. Mark the floor and drain checks confirmed in KNOWLEDGEBASE.
