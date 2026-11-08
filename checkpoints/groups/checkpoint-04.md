# checkpoint-04 — race groups: engine + first three groups

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `main` · baseline `1f804ee` (working tree has many uncommitted
changes from earlier sessions) · 2026-11-07. Supersedes [checkpoint-03.md](checkpoint-03.md). Logs:
[logs/2026-11-07-17-07-42/](logs/2026-11-07-17-07-42/) (`Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`).

## Why this session

The user reported that in the last game a spider, a werewolf and draugr all got humanoid injury text. Cause: the
checkpoint-03 work had only been planned. There was no group lookup at all.
[Injury_Engine.log:10718-10754](logs/2026-11-07-17-07-42/Injury_Engine.log) (16:59:47) shows a "Wounded Frostbite
Spider" taking mauling on its "face", "right arm", "right leg and penis", narrated as "flesh that is already torn".

## Done

### Engine (`make dll` compiles cleanly; NOT deployed, see Not yet tested)

- `InjuryConfig.h/.cpp`:
  - `kMaxParts = 16` replaces `kCoreParts` / `kPartCount` / `kSexualEnd` / `kPartLungs`.
  - `Body` now has `partCount`, `coreCount`, `sexualCount`, `lungs` (-1 when none), `partSex`, `bleedout` and
    `disabledSources`.
  - Groups: `injuries/humanoid/` is always group 0. Every other folder with a `body.json` is a group.
  - Each source in a group is the humanoid JSON, plus the group's `common.json` (the `health` / `torture` block by
    model, minus the sources in `skip`), plus the group's `<source>.json`, all merged with merge_patch. The group's
    `sexual_parts.json` text merges on top.
  - Thread-local current group (`GroupScope` / `SetGroup`). `GetBody`, `Find`, `Str`, `Count`, `Number` and `Json`
    read it.
  - `races.json` loader and `GroupOf(Actor*)`. Lookup order: `actors`, `races` EditorIDs, `race_forms`,
    `race_contains`, `keywords`, `default_group`. Results are cached per race. A group with no folder falls back to
    humanoid (warned once).
  - `IsDisabled`, `Source::unreachable` (part_weights name only parts the group lacks: drowning without lungs;
    health sources only) and `DumpRaces`.
  - Legacy fallback: without `humanoid/body.json` the body comes from the whipping block plus `sexual_parts.json`.
    Without a `humanoid/` folder the root is read.
- `InjuryEngine.h/.cpp`:
  - `State.group` is set when the entry is made (`Track`) and never changes.
  - `GroupFor(actor)` returns the tracked group, else `GroupOf`. It takes `g_lock`, so it is called before the lock.
  - Every entry point takes a `GroupScope`: `Hit` / `HitPart`, `OnDamage`, `HealPart`, `HasOpenWounds`,
    `SyncHealth`, `StateJson`, `PartPointsJson`, `HitText`, `LevelText`, `DeathText`, `HealMapText`,
    `SourceHealText`, `SourceHealGroupText`, `ActionText`, `AttackLine`, the scene lines (per victim), the overlay
    and save / load (per actor).
  - `Current()` sets the state's group.
  - State-only loops run over `kMaxParts`; body loops run over `partCount`.
  - `PartFits` uses `partSex`. `CapacityTorture` counts unsexed parts plus the larger sex-specific total.
  - Disabled torture sources are ignored (`OnDamage` returns 0, `Hit` returns -1). Disabled or unreachable health
    sources pass through.
  - Torture bleedout only runs where the group's `bleedout` is true.
  - `TranslatePart` maps Papyrus part indices (humanoid, from `PartIndex`) into the victim's group by name, else by
    region, else random. No `.psc` change.
  - `static_assert(kMaxParts <= 16)`.
- Co-save v8: per actor, the group id string, then a `u8` part count, then arrays of that length. v7 and older load as
  humanoid with today's part counts. Unknown group: loads as humanoid. A body that shrank: parts past the end are
  dropped.
- `plugin.cpp`: `InjuryConfig::DumpRaces()` at kDataLoaded writes info-level `race dump:` lines, one per race.
- Overlay: each actor in `OverlayJson` carries its own `part_max`. The `index.html` `summarize` / `apply` use it.

### Data

- `git mv` of every source file and `sexual_parts.json` into `injuries/humanoid/` (`session.json` and `guide.md` stay
  at the root).
- `injuries/humanoid/body.json`: today's 12 parts in today's order, overlay order face, back, right arm, left arm,
  right leg, left leg.
- `SKSE/Plugins/Injury_Engine/races.json`, generated from the scratch script `races.py`:
  - exact EditorIDs (playable races and their vampires, Draugr, Skeleton, DragonPriest, werebeasts,
    DLC1VampireBeastRace, FrostbiteSpider*, Chaurus*, Dwarven*, DragonRace, DLC1UndeadDragonRace)
  - all 52 `creatures.json` forms as `race_forms`
  - 70 `race_contains` rules ("dwarven" before "spider", "werewolf" before "wolf", "dragonpriest" before "dragon")
  - keyword fallbacks
- Groups shipped:
  - `insectoid`: head, thorax, abdomen, four leg pairs; no lungs, no bleedout.
  - `humanoid_werewolf`: muzzle, arms, back, legs, tail (max 12, real max 50, weight 0.4), lungs, bleedout.
  - `humanoid_undead`: humanoid part names; no lungs, no bleedout; no soil on death.
  - Each has `body.json` (with the humanoid sexual set) and `common.json` (health and torture looks / feels / heal,
    hit levels, heal levels, death regions, soil; `skip: drowning`).
  - `insectoid` and `humanoid_werewolf` also ship part_weights-only files for pinching, pulling, punching, slapping
    and falling (torture block).
- Docs: `injuries/guide.md` "Groups", `CLAUDE.md` (Race groups paragraph, key paths), `AGENTS.md`, `README.md`,
  `KNOWLEDGEBASE.md` (16-part limit, thread group rule, critters, Durnehviir, thralls), `docs/reference/json-keys.md`,
  `Makefile` (the overlay FOMOD step now reads `injuries/humanoid/whipping.json`).

## Decisions (this session)

- `common.json` per group, not 19 full copies per group. It gives material-true text for every source at once (no
  blood on draugr or spiders). Per-type group files can override it later. Damage-type flavor still shows through
  each source's `narration.form` in hit lines.
- No Reanimate override for thralls: an active-effect scan in the damage hook is the known crash pattern. Thralls
  keep their living race's group.
- The race dump logs at info level always (about 150 lines once per game start), so the mapping can be checked
  without verbose logging.
- `disabled_sources` on a health source means "pass through untracked". On a torture source it means "ignored".

## Not yet tested

- **Nothing has run in game.** The DLL built, but the copy to `SKSE/Plugins/Injury_Engine.dll` failed: Skyrim was
  running (`Permission denied`). **The shipped DLL is the old one, and the old DLL cannot find the moved JSON
  files.** Run `make dll` with the game closed before the next launch.
- Untested: the co-save v7 → v8 load, the race mapping (EditorIDs are from memory, so check the `race dump:` lines),
  overlay bars for 7-part bodies, werewolf tail healing, drowning passing through on insectoid / undead, and
  `TranslatePart`.
- The sexual parts on these three groups still use humanoid prose (no group `sexual_parts.json` yet).
- The Papyrus log helper `Main.PartsText` loops 12 parts with humanoid names: log only.

## Next

1. Close Skyrim, run `make dll` (check that `SKSE/Plugins/Injury_Engine.dll` updates), then start the game.
2. Check `Injury_Engine.log`:
   - `injuries: group insectoid loaded`, `humanoid_werewolf` and `humanoid_undead` lines appear.
   - The `race dump:` lines map FrostbiteSpiderRace → insectoid, DraugrRace → humanoid_undead and
     WerewolfBeastRace → humanoid_werewolf.
   - Note any wrong mapping.
3. Load a save with wounds: humanoid hits, torture, healing and the overlay behave as before.
4. Fight a frostbite spider, draugr and a werewolf (or turn into one while unwounded):
   - Log parts read thorax / legs, face / back, muzzle / tail.
   - Narration talks about chitin and ichor, dry flesh and bone, fur and hide.
   - The bio prompt (`openrouter_input.log`) shows the group text.
5. Snapshot the logs into `checkpoints/groups/logs/<timestamp>/` and start checkpoint-05 from here.
6. Later sessions: fix `races.json` from the dump; per-type group files (slashing / piercing / bludgeoning / mauling
   / burning ...) and `sexual_parts.json` for the three groups; then `dragon`, `quadruped`, `beast_humanoid` and the
   other groups (body.json + common.json each); Durnehviir `actors` entry; changelogs at release.
