# checkpoint-05 — race groups verified and committed; races.json fixes; `quadruped` group

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `9ea757b` (checkpoint-04 work) · 2026-11-07.
Supersedes [checkpoint-04.md](checkpoint-04.md). Logs: [logs/2026-11-07-17-52-18/](logs/2026-11-07-17-52-18/) (the
user's snapshot after the checkpoint-04 test: `Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`; no new snapshot
this session, no loose files to move).

## Goal

The user verified checkpoint-04 in game. This session commits that work, fixes the race mapping from the race dump,
and adds the next group, `quadruped`.

## Verified (checkpoint-04, in game)

- [Injury_Engine.log:30-36](logs/2026-11-07-17-52-18/Injury_Engine.log): `humanoid_undead` (11 parts), `humanoid_werewolf`
  (13, lungs) and `insectoid` (12) loaded. races.json: 45 races, 52 forms, 70 contains rules.
- The race dump (208 lines) maps Draugr / Skeleton / DragonPriest → humanoid_undead, Frostbite spiders / Chaurus →
  insectoid and Werewolf / Werebear → humanoid_werewolf. The user confirmed the behavior in game.
- Committed as `9ea757b` (`Race groups: per-race injury bodies and text ...`). That commit includes the outstanding
  checkpoint folders and log snapshots. `z-plan.md` was left out (scratch).

## Done this session

### races.json (wrong mappings in the dump)

- Removed `race_contains` "cat". It matched `ccBGSSSE040_GoblinRaceDuplicate` ("dupli**cat**e") → quadruped.
- `BretonRaceChildVampire` added to `races.humanoid`. It had reached humanoid_undead through the ActorTypeUndead +
  ActorTypeNPC keyword rule.
- New contains rules: `wight` and `bonecolossus` → humanoid_undead (`ccBGSSSE067_WightRace` and
  `ccVSVSSE003_BoneColossusRace` had fallen to the default), and `elytra` → insectoid (`ccBGSSSE025_ElytraRace` /
  `_PET` had gone to quadruped by keyword).
- These rules were checked against the dump's EditorIDs with a script. The new texts match only the intended races.

### Group `injuries/quadruped/` (data only; no DLL or Papyrus change)

- `body.json`:
  - Parts: head (0.8), back, left / right foreleg, left / right hind leg (0.8), tail (max 8, real_max 30,
    weight 0.3), lungs.
  - `bleedout: false`. Many creatures have no bleedout graph.
  - **No sexual parts.** This is a deliberate departure from checkpoint-03 decision 10: no sexual torture text on
    animals.
- `common.json`:
  - Health and torture looks / feels per region (head, body, leg, tail), hit levels, heal levels, death regions and
    soil. `skip: drowning`.
  - The words ("coat", "hide") fit fur and bare hide alike, so one text serves wolf, bear, horse, deer, sabre cat,
    skeever, mammoth and horker.
  - Narration uses `{name}` only. No numbers.
- `falling.json`: torture part_weights only (legs and back heavy).
- Docs: `injuries/guide.md` (shipped folders, plus a warning that `race_contains` is a substring test).
  `KNOWLEDGEBASE.md` (heading now reads verified, and a new entry on the substring / keyword pitfalls).
- All new and edited JSON parses (Python `json.load`).

## Decisions

- Quadruped has no sexual parts. Groups with zero sexual parts are supported: the counts come from `count_if`
  (`InjuryConfig.cpp:340-341`), and the missing-`sexual_parts.json` warning only fires when `sexualCount > 0` (`:701`).
- No per-type files except `falling.json`. A torture source's humanoid part_weights names that do not exist here
  (face, arms) are ignored, so those parts keep the body weights (`FinishSources`, `InjuryConfig.cpp:446-490`).
- The cosmetic `name` labels on `race_forms` (0xA5601 is ChaurusReaper, 0x104F45 is WhiteStag, ...) are left alone.
  The groups they map to are correct.

## Not yet tested

- Nothing from this session has run in game.
- `ccBGSSSE040_GoblinRaceDuplicate` no longer has a contains rule. It falls to the keyword rules or the default,
  which should be humanoid like `ccBGSSSE040_GoblinRace`. Check it in the dump.
- Quadruped hits, overlay bars for a 7-part animal body, a tail on small animals (hare, skeever), drowning a horse
  (humanoid drowning text, lungs part), and torture on an animal.
- Still open from checkpoint-04:
  - the co-save v7 → v8 load on an older save with wounds
  - werewolf tail healing
  - `TranslatePart`
  - the sexual parts of insectoid / werewolf / undead, which still use humanoid prose

## Next

1. Start the game (no build needed: JSON only). In `Injury_Engine.log` check:
   - `injuries: group quadruped loaded (8 parts: 7 core, 0 sexual, lungs yes; 1 own files; disabled 0)`
   - race dump: WolfRace / BearBrownRace / HorseRace → `quadruped (race_form)` with no `[not loaded]`;
     GoblinRaceDuplicate → humanoid; BretonRaceChildVampire → humanoid (race); WightRace / BoneColossusRace →
     humanoid_undead; ElytraRace → insectoid.
2. Fight a wolf or bear:
   - the log parts read head / back / foreleg / hind leg / tail
   - the narration talks about coat and hide
   - `openrouter_input.log` shows the quadruped looks in the bio
3. Optional: whip or drown a horse (torture and lungs on an animal).
4. Snapshot the logs into `checkpoints/groups/logs/<timestamp>/` and start checkpoint-06.
5. Later groups, one per session: `dragon` (dragon / undead dragon; keep `dragon_skeletal` / `spectral_dragon`
   separate), `beast_humanoid` (trolls, lurker), `construct`, `elemental`, `spirit`, `plant`, then `bird`, `fish`,
   `crustacean`, `tentacled`, `quadruped_undead`, `humanoid_vampire`. After those: per-type group files and the
   sexual-part prose for the humanoid-shaped groups, and changelogs at release.
