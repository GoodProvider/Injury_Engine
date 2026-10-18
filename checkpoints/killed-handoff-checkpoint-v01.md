# Handoff v01: torture spells kill (Flames (torture) killed Angi)

Status: **cause found, fix built (DLL 2026-10-15 11:27), waiting for one play test.** Follows `killed-handoff-checkpoint.md` (v00).

## Problem
Flames (torture) kills Angi (000CAB2E), caster the player (Bob, 00000014). Torture must never kill: torture wounds take health only down to the floor (`body.floor`, 10%). Reproduced at 10:48, 11:01, 11:07, 11:10, 11:23.

## Cause (proved by the 11:10 probe run)
`Actor::HandleHealthDamage` (vfunc 0x104) is a **notification after the damage**, not the place where it's applied:
- every Flames (torture) tick reached it (attacker null "Someone", sometimes Bob), damage ~-0.13 to -0.19 per call (per frame);
- health was already lower on entry each time (148.74 → 138.40 → 128.73 …, one probe line per second);
- the original call itself took 0 (`game took:0.000`), so passing it a zeroed value blocked nothing. The spell did full damage (~7%/s) while torture points landed at 1–2/s.
- `ActorValueOwner::ModActorValue` (slot 6) never saw these ticks.

The same flaw hits everything that relied on zeroing in `HandleHealthDamage`: punches classified as torture, and real hits that should be reduced by "replace torture wounds first".

## Attempt 1 (11:18 build): failed, Angi died at 11:23
Hook on `ValueModifierEffect::ModifyActorValue` (vfunc 0x20 on `RE::VTABLE_ValueModifierEffect[0]` and `RE::VTABLE_DualValueModifierEffect[0]`; CommonLib `RE/V/ValueModifierEffect.h`, Dual overrides 0x20). The idea: skip `func` for Health ticks of a "(torture)" MGEF, call `OnDamage` with the effect's caster, apply the dual Stamina / Magicka drain ourselves. Installed (`ValueModifierEffect::ModifyActorValue hooks installed (plain and dual)`), but **no probe line ever came from it**. Either the engine doesn't call slot 0x20 virtually for these ticks, or the MGEF match (`Classifier::TortureSourceOfEffect`) fails there. Note: `Classifier::NoteEffect` (AddTarget hook, same `TortureSourceOf` matcher) has never logged `torture effect: victim ...` in any run either, so a matching problem is possible in both places. The hook stays in, unchanged in behaviour.

## Current fix (11:27 build, uncommitted)
- `SKSE_Source/src/Hooks.cpp` `HandleHealthDamage::thunk`: after the original call, `if (kept < amount) InjuryEngine::RestoreHealth(a_this, amount - kept);`. Torture ticks: all of it comes back, and `TakeHealth` (inside `OnDamage`) applies the floor-limited loss. Real hits: the share absorbed by torture wounds comes back. This should also fix punches and the replace-torture reduction.
- `InjuryEngine::RestoreHealth(actor, amount)` (new, `InjuryEngine.h/.cpp`): `RestoreActorValue(kHealth, amount)` inside `InternalScope`; no-op on dead actors.
- Diagnostics (info, always on, at most 1 line / second per slot; `ProbeDue(slot)`):
  - `probe HandleHealthDamage ... health:a -> b change:+x`: `b` is after our restore, so the change should be about 0 for torture ticks;
  - `probe effect hook[ (dual)] victim effect spell av value torture source`: any slot-0x20 call on an Injury_Engine.esp MGEF, before any filter;
  - `probe AddTarget victim effect item added torture source`: any Injury_Engine.esp MGEF landing.
- `Classifier::TortureSourceOfEffect` / `TortureSourceOfSpell` (new public helpers, `Classifier.h/.cpp`).
- `KNOWLEDGEBASE.md` top entry "HandleHealthDamage runs after the damage..." and the CLAUDE.md flow sentence describe this; both say pending confirmation.
- Earlier work in the same tree (keep): both-hands classify rule, `death ... under torture` line in `DeathSink`, the non-hostile spell / MGEF changes (0x813–0x818, 0x822–0x825), `GetFactionFightReaction` hook.

## Known limits of the current fix
- A tick that is lethal on its own has already killed before the notification (not expected for torture spells: about 0.15 health per frame).
- Misclassified ticks aren't restored beyond the torture share. Seen at every re-cast: the first tick after a pause over `kTortureWindow` (2 s), before any hit event, is classified `generic` (11:23:40 `health: ... generic ... amount:1.2`, then death 2 s later). With the restore every torture tick is given back, so this alone shouldn't kill, but it's still wrong.

## Next step (developer)
1. Load the save and cast Flames (torture) on Angi from full health for about 10 s. Stop for 3+ s, then cast again for 10 s. Then Frostbite and Sparks (torture), 10 s each.
2. Expected: health drops in small steps to about 10% and holds there. Angi lives. Stamina / Magicka drain for frost / shock as vanilla. Vanilla Flames still does `burning` damage.
3. Send `Injury_Engine.log`. Things to read:
   - `probe HandleHealthDamage ... change:` ≈ +0.000 on torture ticks: the restore works;
   - `probe effect hook` lines present? With `torture source:-1`, the matcher fails (fix `TortureSourceOf` / form resolution). With a source ≥ 0 but no `probe ValueModifierEffect`, the a_value / a_av filter skips them (check `av` and `value`). No lines at all: slot 0x20 isn't called for these effects (drop the hook);
   - `probe AddTarget` lines: same question for NoteEffect;
   - any `death ... under torture`.

## After it holds
- If the effect hook works, prefer it for spells (no damage lands at all, the caster is exact, and there are no `generic` first ticks). Keep the restore for melee.
- If it doesn't work, fix the re-cast `generic` tick another way (for example, an attacker-less tick within a few seconds of a torture effect on that victim counts as that torture source).
- Demote all `probe` lines to debug. Update KNOWLEDGEBASE (confirmed), CLAUDE.md Flow, and commit.
- Check a sword hit on a tortured NPC: health should fall only by the real share (the restore covers the replaced torture part).
