Repo: Injury_Engine · branch `main` · baseline `ad0767d` (uncommitted work on top) · 2026-11-10

# Checkpoint 02: arrows landed as bludgeoning (bash detection fixed)

## Goal

Player report after checkpoint-01's test: only arrows were used, but every hit was narrated as a blunt weapon
("Bob hits Wolf with a heavy blunt weapon ..."). Arrow hits must land as `arrow`, a bow bash as `bludgeoning`.

## Findings

Logs: [logs/2026-11-10-22-03-12/](logs/2026-11-10-22-03-12/) (verbose `Injury_Engine.log`, `Papyrus.0.log`; see
`MANIFEST.txt`).

- `arrow.json` loads fine: [Injury_Engine.log:11](logs/2026-11-10-22-03-12/Injury_Engine.log#L11).
- An arrow that hits an **actor** goes through the melee `ProcessHitData` hook like a swing, and its `TESHitEvent`
  has `projectile` 0:
  [Injury_Engine.log:1683-1685](logs/2026-11-10-22-03-12/Injury_Engine.log#L1683)
  (`melee hit weapon 0003B562 ... -> bludgeoning`, `projectile:00000000 bash:true`, `hit event form 0003B562 (bash)
  -> bludgeoning`; also 2598, 2779, 2891, 3126 for the player, 2800 / 2932 / 3071 / 3160 for a bandit archer).
- Hit events that do carry the projectile (`projectile:0003BE11 bash:false`, lines 2313, 2449, 2565, 2667, 2761,
  2900, 3042, 3048) are arrows hitting world objects: no `HandleHealthDamage`, no wound follows.
- So checkpoint-01's two bash tests were both wrong: "no projectile" and "seen by the melee hook" are true for every
  arrow on an actor. The older log [2026-11-10-17-52-54/Injury_Engine.log:1879](logs/2026-11-10-17-52-54/Injury_Engine.log#L1879)
  shows the same path (melee hook line before the hit event) when arrows were still `piercing`.

## Results (built, not yet tested in game)

- `Classifier.cpp` `HitSink`: `bash` = `TESHitEvent::Flag::kBashAttack` only (projectile test dropped).
- `ClassifyMelee(victim, attacker, weapon, bash, unarmedForm)`: new `bash` param; `Hooks.cpp` `ProcessHitData`
  passes `a_hit.flags.any(HitData::Flag::kBash, kTimedBash)`. The log line now shows `(bash)` when set.
- `make dll` deployed `SKSE/Plugins/Injury_Engine.dll` (22:06).
- Docs: CLAUDE.md, KNOWLEDGEBASE (entry rewritten with the verified facts), `injuries/guide.md` `bash` key,
  CHANGELOG `Unreleased`, `InjuryConfig.h` comment. No JSON, Papyrus or ESP change.

## Not yet tested

- Arrow hits now `-> arrow` in both the melee hook line and the hit event line.
- Bow bash: relies on the game setting `HitData` kBash and `TESHitEvent` kBashAttack for a bow bash (expected, not
  yet seen in a log). If a bow bash lands as `arrow`, look at the `melee hit weapon` line: no `(bash)` means the
  flag is missing; then log `a_hit.flags` raw in `ProcessHitData` to find the right bit.

## Next

1. Start the game with verbose logging on (`injury_engine.debug.verbose_logging`).
2. Shoot a human NPC and a creature with a bow: `classify ...: melee hit weapon <bow> ... -> arrow` and
   `hit event form <bow> -> arrow` (no `(bash)`), narration "X shoots an arrow into Y ...".
3. Bow bash an NPC (block + attack): both lines show `(bash)` and `-> bludgeoning`.
4. Let a bandit archer shoot you: `-> arrow`; a bandit archer that bashes in melee range: `-> bludgeoning`.
5. Crossbow (Dawnguard): `-> arrow`. Dagger: still `-> piercing`.
6. Kill something with an arrow: the death line uses the arrow text.
7. Put `Injury_Engine.log` and `Papyrus.0.log` into `checkpoints/2026-11-10-arrows/logs/` and start the next
   session from this file (`checkpoints/2026-11-10-arrows/checkpoint-02.md`).
