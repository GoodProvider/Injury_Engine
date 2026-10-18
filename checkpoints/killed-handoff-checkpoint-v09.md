# Handoff v09: Padded Whip weapon hits zeroed before they land

Status: **Built (DLL 13:41), not committed.** Follows v08. No Papyrus, JSON or ESP changes.

## Result of v08 test
- Punching looked good (developer report). Moving on to the whip.

## Changes
- `Hooks.cpp` `ProcessHitData`: a Padded Whip hit (Classifier ignore result `-1` + `physicalMelee`) gets
  `totalDamage` / `physicalDamage` / `bonusHealthDamageMult` zeroed. No `OnDamage`, because the lash ENCH →
  `Main.OnLash` → `Engine.Hit("whipping")` brings the torture. Log: `[hook] ProcessHitData padded whip ... -> 0`.
- `HandleHealthDamage`: for that hit (`HandledByProcessHit`) `kept = 0`, so any leftover damage is given back.
- Why: the whip's weapon damage (base 1, multiplied by perks / power / sneak attacks) used to land untouched below
  the torture floor and could kill a tortured victim.
- Docs: CLAUDE.md flow, KNOWLEDGEBASE entry, Hooks.cpp header.

## Next step (developer), verbose logging on
1. `Injury_Engine.log` at load: `ProcessHitData hook installed`.
2. Whip a neutral NPC: each lash logs `ProcessHitData padded whip ... -> 0`, **and the lash still lands**
   (`OnLash` / whipping hit in the logs, overlay bar grows, narration). She doesn't fight back.
   If lashes stop landing, the ENCH needs the damage: report it, and the zeroing gets rolled back to restore-only.
3. Keep whipping, with power and sneak attacks: at 15% she collapses (`downed ... bleedout`), the next
   `torture:direct` line ends "<name> collapses.", and `actual:` never reaches 0. She lives.
4. Heal her above 15%: she gets up. Turn the setting off while she is down: she gets up.
5. Sword on an untortured NPC: normal damage.
