Repo: Injury_Engine · branch `main` · baseline `d743ab5` (uncommitted work on top) · 2026-11-10

# Checkpoint 01: draining, poisoning, clawing and blood magic sources (built, not yet tested in game)

## Goal

From [checkpoint-00](checkpoint-00.md), give vampire attacks their own wound types:
- Vampiric Drain, Drain Health and Absorb Health read as life being drained away, not a blow.
- Vampire Lord claws read as claws swiping, not a werewolf chewing.
- Poison reads as poison seeping in, not fists.
- Blood magic gets a hook for spells from other mods.

Every race group gets its own text for each.

Decisions (player, plan session):
- Poison covers all ResistPoison Health damage: spells, weapon poisons and creature venom.
- Blood magic is a source matched by JSON rules. There are no new ESP spells, and thrall creation is left out.
- Every group gets an override file per new source.

## Findings (logs/2026-11-10-22-41-18)

- **Drain on a wolf:**
  - [Injury_Engine.log:394](logs/2026-11-10-22-41-18/Injury_Engine.log#L394): hit event, spell `06000C2E`.
  - [Injury_Engine.log:395](logs/2026-11-10-22-41-18/Injury_Engine.log#L395): `AddTarget … effect:000F5B57`. The drain's MGEF is Skyrim.esm `0x0F5B57`.
  - The spell matched no rule, so `Classify` fell to the Iron Mace (`0x13982`) in the other hand: 437× `classify 0009F361: attacker 00000014 equipped 00013982 -> bludgeoning`.
  - The wolf died as `bludgeoning`.
  - [Papyrus.0.log:1142](logs/2026-11-10-22-41-18/Papyrus.0.log#L1142): `OnSpellCast 06000C2E`.
- **Root cause:** `MatchMagic` knew only torture MGEFs and resist Fire / Frost / Shock. Anything else returned -2, which left the result to the hands (weapon, fists, or generic).
- **Vampire Lord:** `DLC1VampireBeastRace` has no `ActorTypeNPC`, so its fists matched `mauling` (`npc: false`).
- **Absorb Health** is an archetype (`kAbsorb` = 4), not a flag. `AbsorbEffect` has its own vtable.

## Results (changed, built)

- **C++** ([Classifier.cpp](../../SKSE_Source/src/Classifier.cpp), [Hooks.cpp](../../SKSE_Source/src/Hooks.cpp), [InjuryConfig.cpp/.h](../../SKSE_Source/src/InjuryConfig.cpp), [Classifier.h](../../SKSE_Source/include/Classifier.h)):
  - New classify keys `absorb`, `effect_keywords`, `attacker_races`, and `resist: ResistPoison`.
  - `HealthSourceOf` applies the magic rules in priority order: effect form, absorb, keyword, resist. An unmatched Health damage effect becomes `generic`.
  - `MatchMagic` prefers a named source over generic.
  - `Classifier::MagicScope` is set around every non-torture Health tick in `ModifyEffectActorValue`, and `Classify` honours it first.
  - A new hook on `VTABLE_AbsorbEffect` slot 0x20 (`ModifyEffectActorValue<false, 1>`).
  - `UnarmedMatches` checks `attacker_races`.
  - `NoteEffect` writes an info line, `health effect: … archetype resist detrimental hostile keywords -> source`, once per victim and MGEF every 10 s.
- **JSON:**
  - Four humanoid sources:
    - [draining.json](../../SKSE/Plugins/Injury_Engine/injuries/humanoid/draining.json): effect rule `0x0F5B57` at priority 45, plus `absorb` at 44.
    - [poisoning.json](../../SKSE/Plugins/Injury_Engine/injuries/humanoid/poisoning.json): `ResistPoison`, priority 42.
    - [clawing.json](../../SKSE/Plugins/Injury_Engine/injuries/humanoid/clawing.json): unarmed with `DLC1VampireBeastRace`, priority 75.
    - [blood_magic.json](../../SKSE/Plugins/Injury_Engine/injuries/humanoid/blood_magic.json): keywords `Injury_Engine_BloodMagic` / `MagicBloodMagic`, priority 46.
  - `text.<id>` blocks for all four in `humanoid/sexual_parts.json`.
  - 72 group overrides (`injuries/<group>/{draining,poisoning,clawing,blood_magic}.json`, 18 groups). A script checked their region keys against each `body.json`. No digits in any text.
- **Docs:** `injuries/guide.md` (source table and rule keys), `docs/authors/custom-weapons.md` (new "Spells from other mods" section), README, CHANGELOG Unreleased, CLAUDE.md classification paragraph, KNOWLEDGEBASE entry.
- **Build:** `make dll` succeeded, and `SKSE/Plugins/Injury_Engine.dll` was written at 22:44. No Papyrus or ESP changes.

## Not yet tested

- Whether the HandleHealthDamage damage runs nested inside the `ModifyActorValue` hook, so that `MagicScope` applies. Look for `classify …: effect tick by … -> draining`.
- Whether `AbsorbEffect`'s target tick goes through vtable slot 0x20.
- Whether MGEF `0x0F5B57` is archetype Absorb (the `health effect:` line will show it). The form rule covers it either way.
- Vampire Lord claws, the Vampire Lord drain, poison spells, weapon poisons and spider spit.
- Any regression in burning / freezing / shocking, torture spells, the whip and fists.

## Next

1. Deploy: the DLL and JSON are already in the mod folder. Check that MO2 overwrite has no stale `Injury_Engine.dll`.
2. In game, with verbose logging on:
   - Vampiric Drain on a wolf with a mace in the other hand. Expect `-> draining` and draining narration and death text.
   - An Absorb Health staff or enchantment.
   - Transform into a Vampire Lord: claws on an NPC (expect `-> clawing`), and its Drain Life.
   - A poison spell or poisoned weapon on a Riekling (expect `poisoning`).
   - A Flames hit, to confirm it still reads `burning`.
3. Capture `Injury_Engine.log` and `Papyrus.0.log`. From the `health effect:` lines, note the MGEF ids, archetype and keywords for the Vampire Lord drain and the poison bolt. If absorb is not set, add `effect_file` rules.
4. If ticks still classify by the other hand's weapon, the nested-call assumption is wrong. Fall back to preferring `RecentRealMagic` in the hands loop.
5. Commit once verified.
