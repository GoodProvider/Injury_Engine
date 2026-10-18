# Handoff v05: spell timing confirmed; next, test the whip and punching

Status: **Committed (947a576).** No new build: the DLL from 2026-10-15 12:21 in `SKSE/Plugins/` is current. Follows `killed-handoff-checkpoint-v04.md`.

## Result of v04 test (12:23 run)
- Flames (torture): from full health to the 10% floor in about 15 s; the floor held at 15.0/150 while casting went on. Confirmed.
- A sword hit at the floor killed Angi (`last real hit: none`). A hit that is lethal by itself kills before the hook runs. Accepted as a known limit (see KNOWLEDGEBASE).

## Next step (developer): whip and punch
Settings: `points_per_hit` 2 for whipping and punching.
1. **Whip**: lash an unhurt NPC who is not in combat with the Padded Whip.
   Expect: no combat and no vanilla bark; a `torture: ... whipping` line per lash; the overlay bar grows; narration start / again lines; health stops at about 10% and the NPC lives.
2. **Punch**: unarmed and out of combat, punch an unhurt NPC.
   Expect: `punching` torture lines (not `bludgeoning`), no combat, and the same floor.
   Also check: a punch while you are in combat should be real `bludgeoning` damage.
3. **Mixed**: whip, then punch the same NPC. Expect a `switch` narration line.
4. Optional: Frostbite / Sparks (torture) for the Stamina / Magicka drain.
5. Send `Injury_Engine.log` and `Papyrus.0.log` if anything is off (verbose logging gives the `[hook]` lines).
