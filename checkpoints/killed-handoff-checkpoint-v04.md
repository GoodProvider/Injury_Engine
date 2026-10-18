# Handoff v04: torture spells reach max torture in about 15 s

Status: **Built (DLL 2026-10-15), uncommitted; play test needed.** Follows `killed-handoff-checkpoint-v03.md`.

## Problem
Holding a "(torture)" spell took far longer than 10 s to reach max torture. Target: about 15 s.

## Cause
- `hook.points_per_second` was 2: 120 points (6 parts x 20) took at least 60 s.
- `TortureHit` picked a random part per point, even a full one, so the last parts filled very slowly.

## Done
- `InjuryEngine.cpp` `TortureHit`: random picks skip `PartFull` parts while any other part can take a point.
- `fire.json` / `frost.json` / `shock.json`: `hook.points_per_second` 2 -> 8 (120 / 8 = 15 s).
- `guide.md` note; KNOWLEDGEBASE entry "Random torture picks skip full parts".
- Still uncommitted from v03: `actual:` in `torture:` / `health:` log lines.

## Next step (developer)
1. Load the save and hold Flames (torture) on an unhurt NPC; time it until the overlay bar is full / health stops near 10%.
2. Expected: about 15 s (about 19 s with sexual parts on); the NPC lives; `actual:` never below 10% of max.
3. Repeat with Frostbite / Sparks (torture) and watch the Stamina / Magicka drain.
4. Send `Injury_Engine.log` if the time is off.

## After it holds
Commit v03 + v04 changes; mark floor and drain checks confirmed in KNOWLEDGEBASE.
