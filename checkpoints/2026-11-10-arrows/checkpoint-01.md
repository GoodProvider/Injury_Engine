Repo: Injury_Engine · branch `main` · baseline `ad0767d` (uncommitted work on top) · 2026-11-10

# Checkpoint 01: arrow injury source for every race group

## Goal

Player report (checkpoint-00): arrow hits are tracked as `piercing` but narrated as melee stabs ("Dwimm hits Draugr
with a sharp stabbing point hitting Draugr's right arm into flesh that is already split."). Identify arrow damage and
give it its own text for every creature type.

## Decisions

- One new health source `arrow` for bows and crossbows (neutral wording: arrow, shaft, fletching). `piercing` keeps
  daggers only.
- A bash with a bow / crossbow is `bludgeoning` (small DLL change, new classify key `bash`).
- Group `sexual_parts.json` files (undead, vampire, werewolf, insectoid) got no `text.arrow`: their `health` blocks
  are weapon-neutral (cuts, tears) and read fine for arrows. Humanoid `sexual_parts.json` has `text.arrow`.

## Findings

Logs: [logs/2026-11-10-17-52-54/](logs/2026-11-10-17-52-54/) (the player's verbose `Injury_Engine.log`, moved in;
see `MANIFEST.txt`).

- Arrows are already identifiable: the hit event's source is the bow. Every arrow hit logs
  `classify 00086AC8: hit event form 00013841 -> piercing`
  ([Injury_Engine.log:1879](logs/2026-11-10-17-52-54/Injury_Engine.log#L1879), also 2288, 2667, 2976, 3286).
  The cause was data only: `humanoid/piercing.json` claimed `OneHandDagger`, `Bow`, `Crossbow`.
- The real-hit line is `session.json` `hit.start` + `inline.start` + the level text
  ([Injury_Engine.log:394](logs/2026-11-10-17-52-54/Injury_Engine.log#L394)), not `narration.hit.lead`. `arrow.json`
  overrides `narration.session.hit.*` ("shoots an arrow into {target}", "shoots arrow after arrow into {target}",
  no attacker "is struck by an arrow") and `hit_noun` "arrow".
- Adding a source is save-safe: the co-save stores sources by string id; 21 of 32 sources used.

## Results (done, not yet tested in game)

- `injuries/humanoid/arrow.json` (new, "Arrow Wounds", classify `Bow` / `Crossbow`, `"bash": false`, priority 60):
  looks / feels / levels / session wording / death lines; body block and heal text copied from piercing.
- `injuries/humanoid/piercing.json`: `weapon_types` `["OneHandDagger"]`.
- `injuries/humanoid/bludgeoning.json`: `{ "priority": 60, "weapon_types": ["Bow", "Crossbow"], "bash": true }`.
- `injuries/humanoid/sexual_parts.json`: `text.arrow`.
- `injuries/<group>/arrow.json` for all 18 creature groups (levels, looks / feels for every region of the group's
  `body.json`, death lines per region; soil comes from each `common.json`). Checked by script: every region
  covered, 3 stages, no digits, no pronoun variables in narration / death lines.
- DLL (`make dll`, deployed to `SKSE/Plugins/Injury_Engine.dll`, 17:55):
  - `InjuryConfig.h` / `.cpp`: `ClassifyRule::bash` (tri-state, JSON `bash`).
  - `Classifier.cpp`: `HitSink` stores `bash` = `kBashAttack` or `projectile == 0` in `g_recent` and logs
    `projectile:<id> bash:<bool>`; `MatchWeapon` / `ClassifyForm` take `bash`; the hit-event branch passes it
    (log `hit event form X (bash) -> ...`); `ClassifyMelee` (melee hook) passes `true`; the equipped-hands
    fallback passes `false`.
- Docs: README source table, `injuries/guide.md` (source table, `bash` key), `docs/authors/custom-weapons.md`,
  CLAUDE.md, external `manifest.json`, CHANGELOG `Unreleased`, KNOWLEDGEBASE entry.
- No Papyrus or ESP change.

## Not yet tested

- Everything in game. Risk: if a real arrow's hit event has no projectile, it would log `bash:true` and land as
  bludgeoning; then drop the `projectile == 0` test and keep the bash flag only (Classifier.cpp `HitSink`).

## Next

1. Start the game with `Injury_Engine.log` verbose logging on (`injury_engine.debug.verbose_logging`).
2. Check the start of `Injury_Engine.log`: `injuries: loaded arrow.json (arrow, model health)` and no merge / parse
   errors for any group.
3. Shoot a human NPC with a bow: `hit event ... projectile:<non-zero> bash:false`, `classify ...: hit event form ...
   -> arrow`, narration "X shoots an arrow into Y hitting Y's ...".
4. Shoot a draugr, a wolf, a troll, a spriggan / atronach and a dragon: group-specific arrow lines.
5. Crossbow hit (Dawnguard): `-> arrow`. Dagger hit: still `-> piercing`.
6. Bow bash an NPC: `bash:true`, `-> bludgeoning` (also `classify ...: melee hit weapon ... -> bludgeoning`).
7. Kill something with an arrow: the death line uses the arrow text.
8. Copy `Injury_Engine.log` and `Papyrus.0.log` into `checkpoints/2026-11-10-arrows/logs/` and start the next
   session from this file (`checkpoints/2026-11-10-arrows/checkpoint-01.md`).
