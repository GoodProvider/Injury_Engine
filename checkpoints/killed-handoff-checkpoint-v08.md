# Handoff v08: punches zeroed before they land; torture bleedout at 15%

Status: **Built (DLL 13:14 + Papyrus), not committed.** Follows v07.

## Result of v07 test (12:57 run)
- Setting worked: every punch was `punching`, also in combat.
- Angi was still punched to death: last punch was a power attack (`amount:20`) at 5/150, lethal before the
  `HandleHealthDamage` restore could run. Health had already slipped below the 10% floor (21.9 → 14 → 5) with no
  torture points that could explain it (verbose was off, path unknown).

## Changes
- `Hooks.cpp`: call hook `ProcessHitData` (SE 37673+0x3C0 / AE 38627+0x4A8, `E8` check, VR skipped). Torture melee
  hit → `OnDamage` with `HitData.totalDamage`, then damage zeroed. `HandleHealthDamage` for the same hit only restores.
- Torture bleedout (`InjuryEngine.cpp`, `SyncHealth` → `UpdateBleedout`): torture and health ≤ 15% (`health_link.bleedout_below`)
  → `BleedOutStart`; re-sent once a second if graph `IsBleedingOut` is false; health > 15% / forget / death /
  setting off → `BleedOutStop`.
- Next scene line (`SceneText`) for a newly downed victim ends with `session.json` `collapse`: "{target} collapses." (once).
- Setting `injury_engine.body.torture_bleedout` (default on), native `SetTortureBleedout`, `ApplyPluginConfig`.
- `bleedout_below: 0.15` added to the body block of every injury JSON. Docs: CLAUDE.md, README, guide.md, KNOWLEDGEBASE.

## Next step (developer), verbose logging on
1. `Injury_Engine.log` at load: `ProcessHitData hook installed` (not "skipped: no call").
2. Punch Angi down, with power attacks and after she fights back: `[hook] ProcessHitData torture ... -> 0` per
   punch, `actual:` never below 15/150, she lives.
3. At 15% she collapses (`downed ... bleedout`), and the next `torture:direct` line ends "Angi collapses.". She stays down.
4. Heal her above 15%: she gets up (`downed ... recovered`).
5. Setting off while she is down: she gets up. Sword on an untortured NPC: normal damage.
If step 1 shows "skipped", the offset is wrong for this runtime: send the log line.
