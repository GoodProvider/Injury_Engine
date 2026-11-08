# checkpoint-02 — race groups

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `c16e0e2` (0.1.5 artifacts; working tree has
uncommitted 0.1.5 changes) · 2026-10-30 · planning only, no code or data changed. Supersedes
[checkpoint-01.md](checkpoint-01.md): the six open questions are answered and folded in below. No logs used (no
`logs/` folder).

## Goal

1. `SKSE/Plugins/Injury_Engine/races.json`: maps each race (and a few actors) to a group.
2. `SKSE/Plugins/Injury_Engine/injuries/<group>/`: a group-specific copy of every current injury file, plus that
   group's `body.json` and `sexual_parts.json`.
3. On a hit: look up the victim's group, land the damage on that group's body parts, use that group's text.
4. The body supports any number of parts (a dragon has 4 legs + 2 wings + tail + neck).

Source list: `../SkyrimNet_SexLab/SKSE/Plugins/SkyrimNet_SexLab/creatures.json` (52 races), plus the playable races
and the other vanilla / DLC races below.

## Answers (session 02)

| # | Question | Answer |
|---|----------|--------|
| 1 | Undead / spectral dragons | three groups: `dragon_undead` (Durnehviir), `dragon_skeletal` (skeletal dragons), `spectral_dragon` (Spectral Dragon) |
| 2 | Horker | `quadruped` (text says flippers where it fits) |
| 3 | Race change on a live actor | keep humanoid for now: no remap (see Decisions) |
| 3b | Werebeast / vampire lord | groups `humanoid_werewolf` and `humanoid_vampire`; `winged_humanoid` is dropped |
| 4 | Sexual parts | every group (each `body.json` defines its own), still behind `include_sexual_parts` |
| 5 | Torture on non-humanoids | can be turned off per group (`body.json` `disabled_sources`) |
| 5b | A disabled torture hit | ignored: no wound and no health loss |
| 6 | Arvak's skeleton horse | `quadruped_undead` |

## Decisions

- **Lookup order:** (1) actor override: an active `Reanimate` archetype effect → `humanoid_undead` (thralls keep
  their living race); (2) `races.json` `actors` (actor base EditorID or `file + form` → group; needed because
  Durnehviir most likely shares `DLC1UndeadDragonRace` with the skeletal dragons); (3) race EditorID
  (`TESRace::formEditorID` is kept in memory for races; verify in CommonLib); (4) race `file + form`; (5) keyword
  rules; (6) `default_group` (`humanoid`). Race results cached per race FormID.
- **Keyword fallback for mod races:** `ActorTypeDwarven` → `construct`, `ActorTypeUndead` + `ActorTypeNPC` →
  `humanoid_undead`, `ActorTypeDragon` → `dragon`, `ActorTypeNPC` → `humanoid`, `ActorTypeAnimal` → `quadruped`,
  `ActorTypeDaedra` → `humanoid` (Dremora shape), anything else → `default_group`.
- **Race change (werewolf / vampire lord transform, both ways):** no remap. The group is picked when the actor's
  `State` is created and stays with that state until it is forgotten (all wounds healed, death cleanup, `Forget`).
  A player who transforms while wounded keeps the humanoid body and text; one who transforms unwounded and is then
  hit gets `humanoid_werewolf` / `humanoid_vampire`. Remap by part name / region stays a later idea.
- **Any number of parts:** compile-time cap `kMaxParts = 32` with a per-group runtime part count (biggest group:
  `dragon` 11 core parts + its sexual parts + lungs, well below 32). A group with more than 32 parts fails to load
  with an error line. The co-save stores each actor's group and part count.
- **Body per group:** `injuries/<group>/body.json` (parts list `{name, region, max, weight, real_max, flags, sex}`,
  `regions`, overlay order, `health_link`, `lungs`, `bleedout`, `disabled_sources`) replaces the "body block must be
  identical in every file" rule. Humanoid's `body.json` is built from today's body block + `sexual_parts.json` `parts`,
  so humanoid behaves exactly as now.
- **Hard-coded indices become part flags:** `lungs` (drowning target, no cap, outside capacity), `sexual` (only with
  `include_sexual_parts`), `sex: female|male` (pussy / penis today), `core`. `ActiveParts()`, `PartFits`, `RealCaps`,
  `CapacityDamage`, `SourceReach`, `PickPart`, `WeightedPart` read the flags of the actor's body.
- **Sexual parts in every group:** each group's `body.json` lists its sexual parts (flag `sexual`, optional `sex`)
  and `injuries/<group>/sexual_parts.json` holds their text (`text.<source id>` blocks merged into that group's
  source text, as today). Which parts each creature group gets is decided while authoring that group's data.
- **Disabled torture sources:** `body.json` `disabled_sources: [ids]`. A disabled torture source on the victim's group
  is ignored: `Classifier` returns the same ignore result as a padded-whip hit, the `ProcessHitData` hook zeroes the
  melee damage, the `ValueModifierEffect::ModifyActorValue` hook blocks the "Torture" spell tick, the
  `HandleHealthDamage` restore gives back anything else; `Engine.Hit` / `QueueHit` from `Main.Torture`,
  `External_Torture`, `Torture_Target` and the whip's `OnLash` make no wound. Only torture sources can be disabled.
  First proposal (confirm while authoring): `construct`, `elemental`, `spirit`, `spectral_dragon` disable pinching,
  slapping, pulling.
- **Sources stay global:** a source id is one index in every group; `classify` rules are read from the `humanoid`
  files only. Each group file supplies text, stages, `part_weights` (by that group's part names), `death`, `narration`.
- **Missing group file:** for a source that is not disabled, falls back to the `humanoid` file with a warning in
  `Injury_Engine.log` (development aid; every group ships every enabled file).
- **No lungs in a group** (undead, constructs, elementals, spirits, fish, insects): drowning damage there is not a
  wound (passes through).
- **Bleedout:** `body.json` `bleedout`; torture bleedout only runs where it is true.
- **Root folder after the move:** `injuries/session.json` and `injuries/guide.md` stay shared at the root; every source
  file and `sexual_parts.json` lives in `injuries/<group>/`.

## Groups and their bodies

Every group also gets sexual parts (defined per group, behind `include_sexual_parts`); the column lists core parts.

| Group | Shape / material | Core parts (region) | Lungs | Bleedout |
|-------|------------------|---------------------|-------|----------|
| `humanoid` | bleeding biped | face (head), left arm, right arm (arm), back (body), left leg, right leg (leg); sexual: nipples, pussy, penis, lips, ass hole; lungs | yes | yes |
| `humanoid_undead` | dry / rotting / bone biped | face, left arm, right arm, back, left leg, right leg | no | no |
| `humanoid_werewolf` | furred were-beast biped with claws | muzzle (head), left arm, right arm, back, left leg, right leg (tail: open question) | yes | yes |
| `humanoid_vampire` | grey-skinned winged biped (Vampire Lord) | face, left arm, right arm, back, left wing, right wing (wing), left leg, right leg | yes | yes |
| `beast_humanoid` | hide biped with claws (trolls, Lurker) | muzzle (head), left arm, right arm, back, left leg, right leg | yes | yes |
| `plant` | wood / bark biped | face, left arm, right arm, trunk (body), left leg, right leg | no | no |
| `dragon` | four legs + two wings, scaled, bleeds | head, neck (head), back (body), left wing, right wing (wing), left foreleg, right foreleg, left hind leg, right hind leg (leg), tail (tail); lungs | yes | no |
| `dragon_undead` | rotting flesh over bone (Durnehviir) | as `dragon` | no | no |
| `dragon_skeletal` | bare bone | as `dragon` | no | no |
| `spectral_dragon` | ghostly light | as `dragon` | no | no |
| `quadruped` | four-legged animal (Horker: flippers in the text) | head, back (body), left foreleg, right foreleg, left hind leg, right hind leg (leg), tail (tail); lungs | yes | no |
| `quadruped_undead` | bone horse (Arvak) | as `quadruped` | no | no |
| `bird` | two legs + two wings | head, body, left wing, right wing (wing), left leg, right leg (leg), tail; lungs | yes | no |
| `fish` | finned swimmer | head, body, left fin, right fin (fin), tail | no (gills) | no |
| `insectoid` | six / eight legs, chitin | head, thorax, abdomen (body), left front legs, right front legs, left hind legs, right hind legs (leg) | no | no |
| `crustacean` | shell, claws, legs | eyes (head), shell (body), left claw, right claw (claw), left legs, right legs (leg) | no | no |
| `tentacled` | floating, tentacles | head, body, left tentacles, right tentacles (tentacle) | no | no |
| `construct` | Dwemer metal | head (sensor), chassis (body), left arm, right arm (arm), legs / base (leg) | no | no |
| `elemental` | fire / ice / stone / ash | head, core (body), left arm, right arm (arm), lower body (leg) | no | no |
| `spirit` | intangible light / mist | head, body, tail | no | no |
| `none` | no injuries tracked | — | — | — |

`humanoid` keeps its 12 parts in today's index order (co-save compatible).

## Race table

### From creatures.json (all 52)

| Name | Plugin | Form | Group | Note |
|------|--------|------|-------|------|
| Bear | Skyrim.esm | 0x131E7 | quadruped | |
| Cave Bear | Skyrim.esm | 0x131E8 | quadruped | |
| Snow Bear | Skyrim.esm | 0x131E9 | quadruped | |
| Chaurus | Skyrim.esm | 0xA5601 | insectoid | |
| ChaurusFlyer (Chaurus Hunter) | Dawnguard.esm | 0x51FB | insectoid | wings treated as legs + body |
| chicken | Skyrim.esm | 0xA919D | bird | |
| Deathhound | Dawnguard.esm | 0x3D02 | quadruped | vampire hound, bleeds |
| Deer | Skyrim.esm | 0x104F45 | quadruped | |
| Dog | Dawnguard.esm | 0x18B33 | quadruped | |
| Dragon Priest | Dragonborn.esm | 0x3911A | humanoid_undead | |
| Dragon Race | Skyrim.esm | 0x1052A3 | dragon | verify this is `DragonRace`, not a variant |
| draugr | Dragonborn.esm | 0x1B637 | humanoid_undead | DLC2 draugr race |
| Dwarven Centurion | Skyrim.esm | 0x131F1 | construct | |
| Dwarven Sphere | Skyrim.esm | 0x131F2 | construct | wheel: "legs / base" |
| Dwarven Spider | Skyrim.esm | 0x131F3 | construct | |
| falmer | Skyrim.esm | 0x131F4 | humanoid | bleeding biped |
| Flame Atronach | Skyrim.esm | 0x131F5 | elemental | |
| Fox | Skyrim.esm | 0x109C7C | quadruped | |
| Frost Atronach | Skyrim.esm | 0x131F6 | elemental | |
| Frostbite Spider | Skyrim.esm | 0x4E507 | insectoid | |
| gargoyle | Dawnguard.esm | 0x10D00 | elemental | stone; small wings not modeled |
| Giant | Skyrim.esm | 0x131F9 | humanoid | |
| goat | Skyrim.esm | 0x131FA | quadruped | |
| Hagraven | Skyrim.esm | 0x131FB | humanoid | |
| highlandcow | Skyrim.esm | 0x4E785 | quadruped | |
| HMDaedra (Seeker) | Dragonborn.esm | 0x1DCB9 | tentacled | |
| Horker | Skyrim.esm | 0x131FC | quadruped | flippers in the text |
| Horse | Skyrim.esm | 0x131FD | quadruped | |
| Ice Wraith | Skyrim.esm | 0x131FE | spirit | serpent shape: head / body / tail |
| Lurker | Dragonborn.esm | 0x14495 | beast_humanoid | |
| Mammoth | Skyrim.esm | 0x131FF | quadruped | trunk / tusks not modeled |
| mudcrab | Skyrim.esm | 0xBA545 | crustacean | |
| Netch | Dragonborn.esm | 0x1FEB8 | tentacled | |
| rabbit | Skyrim.esm | 0x6DC99 | quadruped | |
| Sabre Cat | Dawnguard.esm | 0xD0B6 | quadruped | DLC1 (glowing) sabre cat |
| Snowy Sabre Cat | Skyrim.esm | 0x13202 | quadruped | |
| Scrib | Dragonborn.esm | 0x1B658 | insectoid | |
| skeever | Skyrim.esm | 0xC3EDF | quadruped | |
| Skeleton | Skyrim.esm | 0xB7998 | humanoid_undead | |
| SlaughterFish | Skyrim.esm | 0x13203 | fish | |
| Snow Troll | Dawnguard.esm | 0x117F4 | beast_humanoid | DLC1 armored frost troll |
| Troll | Dawnguard.esm | 0x117F5 | beast_humanoid | DLC1 armored troll |
| Spectral Dragon | Dragonborn.esm | 0x1F98F | spectral_dragon | |
| Spriggan | Skyrim.esm | 0xF3903 | plant | |
| Storm Atronach | Dragonborn.esm | 0x27BFC | elemental | |
| The Forgemaster | Dawnguard.esm | 0x15C34 | construct | |
| Vampire Lord | Dawnguard.esm | 0x283A | humanoid_vampire | |
| Werebear | Dragonborn.esm | 0x1E17B | humanoid_werewolf | assumption, open question |
| Werewolf | Skyrim.esm | 0xCDD84 | humanoid_werewolf | |
| Wisp | Skyrim.esm | 0x13208 | spirit | |
| Witchlight | Dawnguard.esm | 0x2AE0 | spirit | |
| wolf | Skyrim.esm | 0x1320A | quadruped | |

### Playable races (Skyrim.esm, all `humanoid`)

| EditorID | Form | Vampire variant (EditorID) |
|----------|------|----------------------------|
| ArgonianRace | 0x13740 | ArgonianRaceVampire |
| BretonRace | 0x13741 | BretonRaceVampire |
| DarkElfRace | 0x13742 | DarkElfRaceVampire |
| HighElfRace | 0x13743 | HighElfRaceVampire |
| ImperialRace | 0x13744 | ImperialRaceVampire |
| KhajiitRace | 0x13745 | KhajiitRaceVampire |
| NordRace | 0x13746 | NordRaceVampire |
| OrcRace | 0x13747 | OrcRaceVampire |
| RedguardRace | 0x13748 | RedguardRaceVampire |
| WoodElfRace | 0x13749 | WoodElfRaceVampire |

Vampires (not Vampire Lords) bleed, so they stay `humanoid`. Vampire form IDs to be read from the race dump.

### Other races (EditorIDs from memory, verify each against the race dump)

| Group | Races |
|-------|-------|
| humanoid | ElderRace, ElderRaceVampire, DremoraRace, DLC2GhostFrostGiantRace (Karstaag), DLC2RieklingRace, AfflictedRace (if it exists), child races (`*ChildRace`, if present) |
| humanoid_undead | DraugrRace, DraugrMagicRace, SkeletonRace variants (SkeletonNecroRace, DLC1 Soul Cairn Boneman / Mistman / Wrathman / Keeper races), DragonPriestRace (Skyrim.esm), reanimated thralls (effect rule, not race) |
| humanoid_werewolf | WerewolfBeastRace, DLC2WerebearBeastRace |
| humanoid_vampire | DLC1VampireBeastRace |
| beast_humanoid | TrollRace, TrollFrostRace (Skyrim.esm) |
| plant | SprigganMatronRace, SprigganEarthMotherRace, DLC2SprigganBurntRace |
| dragon | DragonRace, Miraak's dragons (DragonRace) |
| dragon_skeletal | DLC1UndeadDragonRace (skeletal dragons) |
| dragon_undead | Durnehviir: `races.json` `actors` override (actor base), since he likely uses DLC1UndeadDragonRace |
| quadruped | ElkRace, DeerRace, GoatDomesticsRace, CowRace, DogRace, DogCompanionRace (Meeko), DLC1HuskyBareRace / DLC1HuskyArmoredRace, DLC1DeathHoundCompanionRace, SabreCatRace, HareRace, SkeeverWhiteRace, DLC2BoarRace (Bristleback) |
| quadruped_undead | Arvak's skeleton horse race (EditorID from the dump) |
| bird | DLC2CliffRacerRace, HawkRace (if an actor) |
| fish | SlaughterfishRace variants |
| insectoid | FrostbiteSpiderRaceGiant / large variants, ChaurusReaperRace, DLC2AshHopperRace |
| crustacean | Solstheim mudcrab variants |
| construct | DLC2DwarvenBallistaRace |
| elemental | DLC2AshSpawnRace, Ash Guardian race |
| spirit | WispMotherRace / WispShadeRace (if separate) |
| none | critters are activators, not actors (butterflies, moths, dragonflies, torchbugs, salmon): nothing to map |

## Engine changes (for the implementation sessions)

1. **InjuryConfig** (`SKSE_Source/include/InjuryConfig.h`, `src/InjuryConfig.cpp`): `Body` becomes
   `std::array<..., kMaxParts>` + `partCount` with part flags and `sex`; new `Group { id, Body, disabledSources,
   per-source json }`; loader walks `injuries/<group>/` dirs, reads `body.json`, merges that group's
   `sexual_parts.json`. `GetBody(group)`, `Str(group, source, path)`, `Count`, `Number`, `Json` take a group;
   `IsDisabled(group, source)`. `races.json` loader (`actors`, `races`, `keywords`, `default_group`) +
   `GroupOf(RE::Actor*)` (reanimate override, actor override, race cache, keyword rules, default).
2. **Classifier / hooks:** a torture source disabled on the victim's group returns the ignore result (padded-whip path):
   `ProcessHitData` zeroes, `ValueModifierEffect` hook blocks, `HandleHealthDamage` restores; the hit sink's calm still
   runs (the hit is still non-hostile).
3. **InjuryEngine** (`src/InjuryEngine.cpp`): `State` gets a `group` index set at creation and never changed; every
   `kPartCount` loop uses the state's part count; `kPartLungs` / `kPartPussy` / `kPartPenis` / `kCoreParts` /
   `kSexualEnd` become flag lookups; text (`HitText`, `HealMapText`, `SourceHealText`, `DeathText`, decorator looks /
   feels, `OverlayJson`) reads the state's group; `Hit` / `QueueHit` / `OnDamage` drop disabled sources.
4. **Co-save version 8:** per actor the group id (string, so group order can change) and the part count; version 7 and
   older load as `humanoid` with today's 12 parts.
5. **Papyrus / API:** part names from Papyrus (`External_Torture`, `Torture_Target`) resolve in the victim's group; an
   unknown part name picks a random part. No `.psc` signature change expected.
6. **Race dump:** at kDataLoaded with verbose logging, log every `TESRace` (EditorID, file, form, keywords, group
   picked) and every `actors` override hit to `Injury_Engine.log`; also log once per unknown race that fell to a
   keyword or the default.
7. **Docs:** `injuries/guide.md` (groups, `body.json`, `disabled_sources`, per-group files and sexual parts),
   `CLAUDE.md` flow, `KNOWLEDGEBASE.md` (race EditorIDs, critters are activators, Durnehviir override),
   `docs/reference/json-keys.md`, changelogs.

## Data work (per group, one or more sessions each)

Copy `injuries/*.json` into `injuries/humanoid/` unchanged (behavior identical), then for every other group copy the
humanoid set and rewrite per `guide.md`: `body.json` (core and sexual parts, `disabled_sources`), `part_weights` by
the group's part names, `text.looks` / `text.feels` per the group's regions (`head`, `body`, `arm`, `leg`, `wing`,
`tail`, `fin`, `claw`, `tentacle`), `death.regions` per region, `sexual_parts.json`, material-true prose (no blood for
constructs / elementals / spirits / skeletons / spectral dragon: dents, cracks, sparks, oil, sap, chips, bone splinters,
guttering light). Disabled sources ship no file. No numbers in narration. About 20 files × 20 groups: one group per
session.

## Findings

- `creatures.json` is the real file name (not `creature.json`), 52 entries, `form_` as `__formData|<plugin>|<hex>`.
- The body is fixed in two places that must stay in sync: `InjuryConfig.h:16-19` and `InjuryEngine.h:33-34`
  (`kPartCount = 12`, `kPartLungs = 11`); sexual part indices are hard-coded at `InjuryEngine.cpp:320-321`; co-save
  part count by version at `InjuryEngine.cpp:3321` (line numbers from session 01, recheck).
- `guide.md:97-99`: the body block must be identical in every file and only the first loaded file (whipping) is read.
  This is the rule `body.json` per group replaces.
- No logs reviewed (planning only).

## Open questions

1. Werebear: `humanoid_werewolf` (assumed) or `beast_humanoid` / its own group?
2. Does `humanoid_werewolf` get a tail part?
3. `disabled_sources` defaults: is "construct, elemental, spirit, spectral_dragon disable pinching, slapping, pulling"
   right? Any other group / source (whipping a Dwarven Sphere, Torture Frost on a Frost Atronach)?
4. Which sexual parts each creature group gets (answer per group while authoring its data).

## Not yet tested

Nothing: no code or data written. Race EditorIDs, Durnehviir's race and Arvak's race, and the form IDs not taken from
`creatures.json` are unverified.

## Next

1. Answer open questions 1–3 (4 can wait for data sessions).
2. Session 03: engine work (InjuryConfig groups + `body.json` + `disabled_sources` + `races.json` loader with actor
   overrides + race dump, Classifier ignore for disabled sources, InjuryEngine part count / flags / group per state,
   co-save v8), `injuries/humanoid/` = today's files unchanged, `make dll`, in-game check that humanoid behavior is
   unchanged and the race dump lists every race with its group.
3. Fix `races.json` from the dump, then author group data one group per session (`dragon`, `quadruped` and
   `humanoid_undead` first: the most common enemies).
