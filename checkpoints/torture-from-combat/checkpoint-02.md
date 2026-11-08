# checkpoint-02: DOM slap is always slap torture

Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `c16e0e2` (uncommitted work on top, including [checkpoint-01.md](checkpoint-01.md))

## Goal
User: "DOM has a 'slap with hand' weapon, it is always slap torture."

## Findings
- DiaryOfMine.esm WEAP `DOMSlap` "Slap with hand", local `0x404724` (the ESM has 4 masters, so this is its own record). Its type is OneHandSword, so `MatchWeapon` made it real `slashing`.
- DOM also has `DOMWhip` "Padded whip" 0x31678F and `DOMCane` "Punishment Cane" 0x108EC3, both OneHandSword (still `slashing`; not changed).

## Changes
- New classify rule keys `weapon_file` / `weapon_id` (`InjuryConfig.h` / `.cpp`: `weaponFile`, `weaponId`, `hasWeapon`).
- `Classifier.cpp`: `g_ruleWeapons` is resolved in `CacheClassifyEffectForms`. A plugin that is not loaded gives an info line and leaves the rule inactive. Helpers `WeaponRuleSource` and `IsTortureWeaponId`. `MatchWeapon` checks the rule before the weapon type, with no combat check. `IsTortureAttacker` is true for a torture weapon in either hand. The hit sink calms on a torture weapon hit (`tortureWeapon` flag; `NoteTortureLocked` stays spells only).
- `slapping.json` classify: `{ "priority": 100, "weapon_file": "DiaryOfMine.esm", "weapon_id": "0x404724" }`.
- Docs: `injuries/guide.md`, CLAUDE.md Flow, KNOWLEDGEBASE "Other mods' torture weapons are weapon-type weapons", and all three changelogs under 0.1.5.
- Built: `make dll` → `SKSE/Plugins/Injury_Engine.dll` (16:21), no errors. No Papyrus or ESP change.

## Not yet tested
- In game.

## Next
1. Load with verbose logging. `Injury_Engine.log`: `Classifier: weapon DiaryOfMine.esm 0x404724 -> 04404724 (slapping)` (the runtime index depends on load order).
2. Slap an NPC out of combat with the DOM slap: `ProcessHitData torture ... source:slapping ... -> 0`, a slap bar, no health loss, no combat.
3. Slap a hostile in combat: still `slapping`, and health does not drop.
4. A sword or punch in combat: still `slashing` / `bludgeoning`.
5. Optional: add rules for the DOM whip / cane if they should be torture too.
6. Checkpoint-01 tests are still open as well. Commit when verified.
