# Handoff v02: torture spells kill (Flames (torture) killed Angi)

Status: **v01 restore confirmed; effect-hook AV fix built (DLL 2026-10-15 11:52), waiting for one play test.** Follows `killed-handoff-checkpoint-v01.md`.

## Problem
Flames (torture) killed Angi (000CAB2E), caster the player (Bob, 00000014). Torture must never kill: torture wounds take health only down to the floor (`body.floor`, 10%). Cause (v01): `Actor::HandleHealthDamage` is a notification after the damage, so zeroing the value there blocked nothing.

## Findings from the 11:38 run (DLL 11:27)
- **Angi lived.** No `death` lines; lowest logged health 38.5%. Flames, Frostbite and Sparks (torture) all became torture points.
- **The restore works:** every `probe HandleHealthDamage` reads `change:+<damage>`, so each tick comes back in full and `TakeHealth` applies the floor-limited loss.
- **The slot 0x20 hook (`ValueModifierEffect::ModifyActorValue`) is called** for all three torture MGEFs, plain and dual, and the matcher works (`torture source:5` fire, `7` frost, `15` shock). But `a_actorValue` is **always kNone** (`av:-1`). The effect's real value is the member `ValueModifierEffect::actorValue` (0x90, set by `SetActorValue`, slot 0x19). The `a_av != kHealth` filter therefore passed every tick through, and nothing was blocked there.
- `probe AddTarget` sees the torture MGEFs with the right source, so `Classifier::NoteEffect` matching is fine (the v01 worry is resolved).
- Still wrong: on a re-cast the first tick is classed `generic` (11:40:53, Sparks, amount 1.2, real damage, so it is not restored). The attacker is always null ("Someone") on the HandleHealthDamage path.

## Current fix (11:52 build, uncommitted)
- `SKSE_Source/src/Hooks.cpp` `ModifyEffectActorValue::thunk`: the effective AV is `a_av`, or `this->actorValue` when `a_av` is kNone. Only negative Health changes are blocked (`a_value >= 0` passes through). Probe line now reads `av:<arg> member:<this->actorValue>`.
- Unchanged: a blocked tick skips `func`, calls `OnDamage` with `GetCasterActor()` (the real caster), MarkQuiet / NoteCalm, and the dual Stamina / Magicka drain is applied by us (`mgef->data.secondaryAV` × `secondAVWeight`).
- The v01 HandleHealthDamage restore stays as the safety net (punches, real hits replacing torture, any tick the effect hook misses). A blocked tick never changes health, so it should not reach HandleHealthDamage, and nothing is counted twice.
- Everything else from v01 is kept (RestoreHealth, `TortureSourceOfEffect` / `TortureSourceOfSpell`, both-hands rule, `death ... under torture`, non-hostile spells / MGEFs, `GetFactionFightReaction` hook).

## Known limits
- If `member` is not 24 (kHealth) for these effects, the hook still blocks nothing. The restore keeps the actor alive anyway, and the re-cast `generic` tick stays.
- The dual secondary drain is our own `ModActorValue` call, so its amount has to match vanilla (check Stamina / Magicka drain by eye).

## Next step (developer)
1. Load the same save. Cast Flames (torture) on Angi for about 10 s, stop for 3+ s, cast again for 10 s. Do the same with Frostbite (torture) and Sparks (torture). Then one sword hit on the tortured Angi, and a short burst of vanilla Flames.
2. Expected: health drops in small steps toward 10% and holds. Angi lives. Frost / shock still drain Stamina / Magicka. Vanilla Flames logs `burning`.
3. Send `Injury_Engine.log`. Things to read:
   - `probe effect hook ... av:-1 member:24`: the member holds Health. Any other member value means the resolution is wrong for that effect;
   - `probe ValueModifierEffect` / `probe DualValueModifierEffect` lines with `change:` about +0.000: the tick was blocked before landing;
   - torture lines with attacker `Bob (00000014)` instead of `Someone`;
   - **no** `probe HandleHealthDamage` lines for torture ticks, and **no** `generic` line on a re-cast;
   - the sword hit: health falls only by the real share;
   - any `death ... under torture`.

## After it holds
- Demote all `probe` lines (`ProbeTorture`, `probe effect hook`, `probe AddTarget`, `probe HandleHealthDamage`) to `debug`, and drop `ProbeDue` if unused.
- KNOWLEDGEBASE.md: mark the HandleHealthDamage entry confirmed; add "`ValueModifierEffect::ModifyActorValue` (0x20) is called with `a_actorValue` = kNone; read `this->actorValue`".
- CLAUDE.md Flow: replace "in game it has not seen them yet" with the working description (spells blocked at the effect, restore for the rest).
- Commit with a multi-line body.
