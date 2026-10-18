# Handoff v06: punches start no combat, fill faster

Status: **Built, not committed.** Follows v05.

## Result of v05 test (12:36 run)
- Punches were `punching` torture but only reached ~29% in 30 s. Then vanilla combat started, the player was in combat, and the following punches were real `bludgeoning` (Angi went down to 10/150).

## Changes
- `Classifier::IsTortureAttacker` (whip, or empty hands whose unarmed rule is a torture source). The `FightReaction` hook uses it, so a punch on a neutral NPC reads as ally + ignore friendly hits, the same as a lash.
- `punching.json` `hook.points_per_hit` 2 -> 4.

## Next step (developer)
Turn verbose logging on.
1. Punch an unhurt NPC out of combat. Expect `[hook] fight reaction ... torture hit, ally`, no combat, only `punching` lines, health at ~10% in about 15-20 s, NPC alive.
2. Whip still works the same.
3. Send `Injury_Engine.log` if combat starts anyway.
