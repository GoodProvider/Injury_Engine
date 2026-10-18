# Handoff v07: punches stay torture in combat (setting)

Status: **Built (DLL + Papyrus), not committed.** Follows v06.

## Result of v06 test (12:45 run)
- Punches were `punching` torture with 4 points per hit. Angi went down to 5/150 in about 27 s. Bob kept punching at the floor (3.3%).
- 12:46:37: Angi hit back, so combat started. Bob's next punches were real `bludgeoning`, and Angi died at 12:46:44.
- Health held at about 3% (5/150), below the documented 10% floor. Not investigated yet.

## Changes
- New dashboard setting `injury_engine.body.punch_combat_health` ("Punching during combat is health not torture", default **off**).
- Off: `Classifier::UnarmedMatches` ignores `combat:false` on a torture unarmed rule (punching), so punches stay torture in combat. On: same as before.
- `Classifier::SetPunchCombatHealth` and the native `Injury_Engine_Engine.SetPunchCombatHealth`. Applied in `ApplyPluginConfig`.

## Next step (developer)
Turn verbose logging on.
1. Punch Angi until they fight back, then keep punching. Every line should be `punching` and the NPC should live.
2. Turn the setting on and repeat. Punches in combat should be `bludgeoning`.
3. Check the floor: does health stop at 10% or at about 3%?
