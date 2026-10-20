Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `e887881` (Release 0.1.3) · uncommitted

# Goal

Checkpoint 01 work: the Injury Ranges sliders, the real scale (1 point = 1% of max health) and the missing-health
caps. This session reviewed the logs from the user's first in-game test (09:43–09:47) and fixed one bug they showed.

# Decisions

Unchanged from [checkpoint-01.md](checkpoint-01.md). One new rule: a health-model hit that lands while the victim is
already at or below 0 health is ignored. These are hits that arrive before `IsDead()` turns true.

# Results

Logs: [logs/2026-10-20-09-54-07/](logs/2026-10-20-09-54-07/) (`Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`).
The user's summary: "appeared to work".

Verified in the logs:
- Ranges load from the dashboard: `[v] injury ranges minor 25 major 75 severe 90` (Papyrus.0.log:2312).
- Below-minor gating works: 220 `below minor, not narrated` lines (first one at Injury_Engine.log ~line 980). Small
  ticks carry (`no new point (carry …)`, 335 lines). Hits narrate at levels 0..3 (Papyrus `NarrateRealHits` lines).
- `FitRealToHealth` trims work. On the player (00000014) during regen, 1 point at a time from 09:47:11. On an
  overkilled bandit: `fit 0001EBF0: 157 real points trimmed` (Injury_Engine.log:3008).
- The old co-save loads: `co-save loaded 1 actors (record version 6)`. Actor 00019DDF was saved again with 11 real
  points and was not trimmed. There is no log line showing its value before the rescale.
- Death narration: DirectNarration when in sight, otherwise a short-lived event. A hit on a dead victim is skipped
  ("the death line covers it").
- No `[error]` or `[warning]` lines in `Injury_Engine.log`.

Fixed and built (`make dll` OK, not yet tested in game):
- **Hits after death took the killing blow.** Feduzdal (0001EBF0), Injury_Engine.log:2990–3014:
  - A burning hit took health 5 → -25. This was the real kill.
  - A piercing hit then landed at -55 → -115 and made 172 points.
  - Last came a 0.1 nipple sliver. It overwrote `g_lastReal`, so the death record says `piercing part:nipples`.
  - Fix: in [InjuryEngine.cpp](../../SKSE_Source/src/InjuryEngine.cpp), `OnDamage` (health model) returns early
    when health before the hit is ≤ 0. Health before = `GetActorValue(kHealth) + amount` when `applied`
    (HandleHealthDamage), otherwise the current value. New debug line: `victim already at … health, ignored`.
  - KNOWLEDGEBASE entry "Hits keep landing for a moment at or below zero health, before IsDead".

# Findings / risks

- Spell streams (Flames on a wolf) count each tick as one attack, so the points scatter over random parts
  (Injury_Engine.log ~981–1053) and rarely reach minor before the death. This is the existing "one location per
  attack" rule. With the bands it just means less narration. No change made. Worth deciding whether a beam should
  stay on one part.
- The level text index is "stage before the point" (as noted in checkpoint 01). In-game result: a part crossing into
  minor narrates the fresh-wound line, e.g. "a clean, hot line of pain" (Papyrus.0.log:11979).
- In the log file, `➤` shows as `â¤` on some lines (two string encodings in the code). Cosmetic only.
- Still open from checkpoint 01: the External API `RealHit` scale changed, and the dashboard's YAML has not been parsed
  by a tool (it did load in game).

# Not yet tested

- The post-death fix, in game.
- Torture, all of it: none happened in this session (checkpoint-01 step 6).

# Next

1. Deploy: MO2 already loads this folder. The new DLL is in `SKSE/Plugins/Injury_Engine.dll`. Keep verbose logging on.
2. Kill a bandit with several fast hits or a spell. Look for `already at … health, ignored` in `Injury_Engine.log`.
   The `death … part:` line should name the hit that took health below zero.
3. Torture: whip, punch and Flames (torture). Check stages, overlay and bleedout match 0.1.3. Then hit a tortured part
   with a sword: the red bar should grow.
4. Snapshot the logs next session and review.
