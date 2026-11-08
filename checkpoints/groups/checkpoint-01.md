# checkpoint-01 — race groups

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `c16e0e2` (0.1.5 artifacts; working tree has
uncommitted 0.1.5 changes) · 2026-10-30 · planning only, no code or data changed.

## Goal

1. `SKSE/Plugins/Injury_Engine/races.json`: maps each race to a group.
2. `SKSE/Plugins/Injury_Engine/injuries/<group>/`: a group-specific copy of every current injury file, plus that group's body.
3. On a hit: look up the victim's group, land the damage on that group's body parts, use that group's text.
4. The body supports any number of parts (a dragon has 4 legs + 2 wings + tail + neck).

Source list: `../SkyrimNet_SexLab/SKSE/Plugins/SkyrimNet_SexLab/creatures.json` (the request said `creature.json`; the
file is `creatures.json`, 52 races), plus the playable races and the other vanilla / DLC races below.

## Decisions (proposed, confirm in session 02)

- **Lookup key:** race EditorID first (`TESRace::formEditorID` is kept in memory for races; verify in CommonLib), then
  `file + form id`, then keyword rules, then `default_group` (`humanoid`). Result cached per race FormID.
- **Keyword fallback for mod races:** `ActorTypeDwarven` → `construct`, `ActorTypeUndead` + `ActorTypeNPC` →
  `humanoid_undead`, `ActorTypeDragon` → `dragon`, `ActorTypeNPC` → `humanoid`, `ActorTypeAnimal` → `quadruped`,
  `ActorTypeDaedra` → `humanoid` (Dremora shape), anything else → `default_group`.
- **Reanimated corpses / thralls** keep their living race (`NordRace`), so race alone can't find them: an actor with an
  active `Reanimate` archetype effect counts as `humanoid_undead` (actor-level override before the race lookup).
- **Any number of parts:** a compile-time cap `kMaxParts = 32` (the biggest group below has 11) with a per-group runtime
  part count. Keeps the `std::array` code paths fast and simple. A group with more than 32 parts fails to load with an error line.
  The co-save stores each actor's group and part count.
- **Body per group:** a new `injuries/<group>/body.json` (parts list `{name, region, max, weight, real_max, flags}`,
  `regions`, overlay order, `health_link`, `lungs`, `bleedout`) replaces the "body block must be identical in every file"
  rule. Humanoid's `body.json` is built from today's body block + `sexual_parts.json` `parts`, so humanoid behaves exactly
  as now.
- **Hard-coded indices become part flags:** `lungs` (drowning target, no cap, outside capacity), `sexual` (only with
  `include_sexual_parts`), `sex: female|male` (pussy / penis), `core`. `ActiveParts()`, `PartFits`, `RealCaps`,
  `CapacityDamage`, `SourceReach`, `PickPart`, `WeightedPart` read the flags of the actor's body.
- **Sources stay global:** a source id (`slashing`, `whipping`, ...) is one index in every group; `classify` rules are read
  from the `humanoid` files only (classification does not depend on the victim). Each group file supplies text, stages,
  `part_weights` (by that group's part names), `death`, `narration`.
- **Missing group file:** falls back to the `humanoid` file with a warning in `Injury_Engine.log` (development aid only;
  every group ships every file).
- **No lungs in a group** (undead, constructs, elementals, spirits, fish, insects): drowning damage there is not a wound
  (passes through; these races don't drown in game anyway).
- **Bleedout:** `body.json` `bleedout` (true for humanoid groups, false for most creatures: no bleedout behavior). Torture
  bleedout only runs where it is true.
- **Sexual parts:** humanoid only for now (`sexual_parts.json` moves to `injuries/humanoid/`).
- **Root folder after the move:** `injuries/session.json` and `injuries/guide.md` stay shared at the root; every source
  file lives in `injuries/<group>/`.
- **Race change on a live actor** (werewolf / vampire lord transform, both ways): wounds move part by part, first by
  part name, then by region (`arm` → `arm`), else to a random part of the new body. Needs confirming.

## Groups and their bodies

| Group | Shape / material | Parts (region) | Lungs | Bleedout |
|-------|------------------|----------------|-------|----------|
| `humanoid` | bleeding biped | face (head), left arm, right arm (arm), back (body), left leg, right leg (leg); nipples, pussy, penis, lips, ass hole (sexual); lungs | yes | yes |
| `humanoid_undead` | dry / rotting / bone biped | face, left arm, right arm, back, left leg, right leg | no | no |
| `beast_humanoid` | furred / hide biped with claws | muzzle (head), left arm, right arm, back, left leg, right leg | yes | yes |
| `winged_humanoid` | biped with wings | face, left arm, right arm, back, left wing, right wing (wing), left leg, right leg | yes | yes |
| `plant` | wood / bark biped | face, left arm, right arm, trunk (body), left leg, right leg | no | no |
| `dragon` | four legs + two wings, scaled | head, neck (head), back (body), left wing, right wing (wing), left foreleg, right foreleg, left hind leg, right hind leg (leg), tail (tail); lungs | yes | no |
| `quadruped` | four-legged animal | head, back (body), left foreleg, right foreleg, left hind leg, right hind leg (leg), tail (tail); lungs | yes | no |
| `bird` | two legs + two wings | head, body, left wing, right wing (wing), left leg, right leg (leg), tail; lungs | yes | no |
| `fish` | finned swimmer | head, body, left fin, right fin (fin), tail | no (gills) | no |
| `insectoid` | six / eight legs, chitin | head, thorax, abdomen (body), left front legs, right front legs, left hind legs, right hind legs (leg) | no | no |
| `crustacean` | shell, claws, legs | eyes (head), shell (body), left claw, right claw (claw), left legs, right legs (leg) | no | no |
| `tentacled` | floating, tentacles | head, body, left tentacles, right tentacles (tentacle) | no | no |
| `construct` | Dwemer metal | head (sensor), chassis (body), left arm, right arm (arm), legs / base (leg) | no | no |
| `elemental` | fire / ice / stone / ash | head, core (body), left arm, right arm (arm), lower body (leg) | no | no |
| `spirit` | intangible light / mist | head, body, tail | no | no |
| `none` | no injuries tracked (critters) | — | — | — |

Largest body: `dragon` with 11 parts (no sexual parts). `humanoid` keeps 12 (index order unchanged, co-save compatible).

## Race table

### From creatures.json (all 52)

| Name | Plugin | Form | Group | Note |
|------|--------|------|-------|------|
| Bear | Skyrim.esm | 0x131E7 | quadruped | |
| Cave Bear | Skyrim.esm | 0x131E8 | quadruped | |
| Snow Bear | Skyrim.esm | 0x131E9 | quadruped | |
| Chaurus | Skyrim.esm | 0xA5601 | insectoid | |
| ChaurusFlyer (Chaurus Hunter) | Dawnguard.esm | 0x51FB | insectoid | has wings, treated as legs + body |
| chicken | Skyrim.esm | 0xA919D | bird | |
| Deathhound | Dawnguard.esm | 0x3D02 | quadruped | vampire hound, bleeds |
| Deer | Skyrim.esm | 0x104F45 | quadruped | |
| Dog | Dawnguard.esm | 0x18B33 | quadruped | |
| Dragon Priest | Dragonborn.esm | 0x3911A | humanoid_undead | |
| Dragon Race | Skyrim.esm | 0x1052A3 | dragon | verify this is `DragonRace`, not a variant |
| draugr | Dragonborn.esm | 0x1B637 | humanoid_undead | DLC2 draugr race |
| Dwarven Centurion | Skyrim.esm | 0x131F1 | construct | |
| Dwarven Sphere | Skyrim.esm | 0x131F2 | construct | wheel, no legs: "legs / base" |
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
| Horker | Skyrim.esm | 0x131FC | quadruped | flippers as legs (open question) |
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
| Spectral Dragon | Dragonborn.esm | 0x1F98F | dragon | text says scales and blood (open question) |
| Spriggan | Skyrim.esm | 0xF3903 | plant | |
| Storm Atronach | Dragonborn.esm | 0x27BFC | elemental | |
| The Forgemaster | Dawnguard.esm | 0x15C34 | construct | |
| Vampire Lord | Dawnguard.esm | 0x283A | winged_humanoid | |
| Werebear | Dragonborn.esm | 0x1E17B | beast_humanoid | |
| Werewolf | Skyrim.esm | 0xCDD84 | beast_humanoid | |
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

Vampires bleed, so they stay `humanoid`. Vampire form IDs to be read from the game (race dump below).

### Other races (EditorIDs from memory, verify each against the race dump)

| Group | Races |
|-------|-------|
| humanoid | ElderRace, ElderRaceVampire, DremoraRace, GiantRace (if not the creatures.json entry), DLC2GhostFrostGiantRace (Karstaag), DLC2RieklingRace, AfflictedRace (if it exists; else Breton), child races (`*ChildRace`, if present) |
| humanoid_undead | DraugrRace (Skyrim.esm), DraugrMagicRace, SkeletonRace variants (SkeletonNecroRace, DLC1 Soul Cairn Boneman / Mistman / Wrathman / Keeper races), DragonPriestRace (Skyrim.esm), reanimated thralls / zombies (effect rule, not race) |
| beast_humanoid | TrollRace, TrollFrostRace (Skyrim.esm), DLC2WerebearBeastRace, WerewolfBeastRace |
| plant | SprigganMatronRace, SprigganEarthMotherRace, DLC2SprigganBurntRace |
| dragon | DragonRace, DLC1UndeadDragonRace (Durnehviir, skeletal dragons; open question), Miraak's dragons (DragonRace) |
| quadruped | ElkRace, DeerRace, GoatDomesticsRace, CowRace, DogRace, DogCompanionRace (Meeko), DLC1HuskyBareRace / DLC1HuskyArmoredRace, DLC1DeathHoundCompanionRace, SabreCatRace, BearBrownRace / BearBlackRace / BearSnowRace (if these are the creatures.json ones, no change), HareRace, SkeeverWhiteRace, DLC2BoarRace (Bristleback), Arvak's skeleton horse race (open question: undead quadruped) |
| bird | DLC2CliffRacerRace, HawkRace (if an actor) |
| fish | SlaughterfishRace variants |
| insectoid | FrostbiteSpiderRaceGiant / large variants, ChaurusReaperRace, DLC2AshHopperRace |
| crustacean | Solstheim mudcrab variants |
| construct | DLC2DwarvenBallistaRace |
| elemental | DLC2AshSpawnRace, Ash Guardian race |
| spirit | WispMotherRace / WispShadeRace (if separate), Wispmother |
| none | critters are activators, not actors (butterflies, moths, dragonflies, torchbugs, salmon): nothing to map |

## Engine changes (for the implementation sessions)

1. **InjuryConfig** (`SKSE_Source/include/InjuryConfig.h`, `src/InjuryConfig.cpp`): `Body` becomes vectors (or
   `std::array<..., kMaxParts>` + `partCount`) with part flags; new `Group { id, Body, per-source json }`; loader walks
   `injuries/<group>/` dirs, reads `body.json`, merges `sexual_parts.json` only in humanoid. `GetBody(group)`,
   `Str(group, source, path)`, `Count`, `Number`, `Json` take a group. `races.json` loader +
   `GroupOf(RE::Actor*)` (cache by race FormID, reanimate override, keyword rules, default).
2. **InjuryEngine** (`src/InjuryEngine.cpp`, about 3300 lines): `State` gets a `group` index; every `kPartCount` loop uses
   the actor's part count; `kPartLungs` / `kPartPussy` / `kPartPenis` / `kCoreParts` / `kSexualEnd` become flag lookups;
   text (`HitText`, `HealMapText`, `SourceHealText`, `DeathText`, decorator looks / feels, `OverlayJson`) reads the
   actor's group. Group change → part remap (Decisions).
3. **Co-save version 8:** per actor the group id (string, so group order can change) and the part count; version 7 and
   older load as `humanoid` with today's 12 parts.
4. **Papyrus / API:** part names passed from Papyrus (`External_Torture`, `Torture_Target`) resolve in the victim's
   group; an unknown part name picks a random part. No `.psc` signature change expected.
5. **Race dump:** at kDataLoaded with verbose logging, log every `TESRace` (EditorID, file, form, keywords, group
   picked) to `Injury_Engine.log`; also log once per unknown race that fell to a keyword or the default. This verifies
   the table.
6. **Docs:** `injuries/guide.md` (groups, `body.json`, per-group files), `CLAUDE.md` flow, `KNOWLEDGEBASE.md`
   (race EditorIDs, critters are activators), `docs/reference/json-keys.md`, changelogs.

## Data work (per group, one or more sessions each)

Copy `injuries/*.json` into `injuries/humanoid/` unchanged (behavior identical), then for every other group copy the
humanoid set and rewrite per `guide.md`: `part_weights` by the group's part names, `text.looks` / `text.feels` per the
group's regions (`head`, `body`, `arm`, `leg`, `wing`, `tail`, `fin`, `claw`, `tentacle`), `death.regions` per region,
material-true prose (no blood for constructs / elementals / spirits / skeletons: dents, cracks, sparks, oil, sap, chips,
guttering light). No numbers in narration. About 20 files × 15 groups = 300 files: author the data one group per session.

## Findings

- `creatures.json` is the real file name (not `creature.json`), 52 entries, `form_` as `__formData|<plugin>|<hex>`.
- The body is fixed in two places that must stay in sync: `InjuryConfig.h:16-19` and `InjuryEngine.h:33-34`
  (`kPartCount = 12`, `kPartLungs = 11`); sexual part indices are hard-coded at `InjuryEngine.cpp:320-321`; co-save
  part count by version at `InjuryEngine.cpp:3321`.
- `guide.md:97-99`: the body block must be identical in every file and only the first loaded file (whipping) is read.
  This is the rule `body.json` per group replaces.
- No logs reviewed (planning only).

## Open questions

1. Undead dragons (Durnehviir, skeletal) and Spectral Dragon: own `dragon_undead` group, or `dragon` with neutral prose?
2. Horker: `quadruped` with flipper wording in the text, or its own group?
3. Race change (werewolf / vampire lord): remap wounds by part name / region (proposed), keep a separate state per
   group, or clear?
4. Sexual parts for `beast_humanoid` / `winged_humanoid` (werewolf, vampire lord)?
5. Torture on non-humanoid groups: all torture types for all groups (as asked), or disable some per group (pinching a
   Dwarven Sphere)?
6. Arvak's skeleton horse: `quadruped` or a `quadruped_undead` group?

## Not yet tested

Nothing: no code or data written. Race EditorIDs and the form IDs not taken from `creatures.json` are unverified.

## Next

1. Answer the open questions.
2. Session 02: engine work (InjuryConfig groups + `body.json` + `races.json` loader + race dump, InjuryEngine part
   count / flags / group, co-save v8), `injuries/humanoid/` = today's files unchanged, `make dll`, in-game check that
   humanoid behavior is unchanged and the race dump lists every race with its group.
3. Fix `races.json` from the dump, then author group data one group per session (`dragon`, `quadruped` and
   `humanoid_undead` first: the most common enemies).
