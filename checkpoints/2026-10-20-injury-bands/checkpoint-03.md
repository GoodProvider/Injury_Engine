Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `e887881` (Release 0.1.3) · committed this session

# Goal

Checkpoint 01/02 work: the Injury Ranges sliders, the real scale (1 point = 1% of max health), the missing-health
caps, and the checkpoint-02 fix (hits that land at or below 0 health are ignored). This session reviewed the second
in-game test (09:55–10:07) and committed the work.

# Decisions

Unchanged from [checkpoint-02.md](checkpoint-02.md).

# Results

Logs: [logs/2026-10-20-10-11-54/](logs/2026-10-20-10-11-54/) (`Injury_Engine.log`, `Papyrus.0.log`, `MANIFEST.txt`).
The user's summary: "verified".

Verified in the logs:
- **Post-death fix works.** 8 `victim already at … health, ignored` lines, each right before that victim's death
  record, and the record now names the hit that took health below zero:
  - Wolf 0009F361: ignored burning + piercing at -18.8 (Injury_Engine.log:1429, 1434) → `burning part:face` (1436).
  - Pelf 0001EBF6: three ignored hits at -23 (3258–3267) → `burning part:right leg` (3269).
  - Bemfola 00029602 (9782, 9787 → 9789) and Martio 00029600 at -55.3 (10047 → 10049).
- 11 death records (669–11576), all `text:yes`. Death narration seen both as DirectNarration (3643) and as a
  short-lived event (7246).
- No `[error]` or `[warning]` lines in `Injury_Engine.log`.
- Truce flow ran during the fight (Injury_Engine.log:3647).

Committed on `bands` (all bands work since `e887881`, plus the `bug-logs/` cleanup and this checkpoint folder;
`z-plan.md` left out as user scratch).

# Findings / risks

Carried over, no change:
- Spell streams count each tick as one attack, so points scatter over parts and rarely reach minor before death.
  Open question: should a beam stay on one part?
- In the log file, `➤` shows as `â¤` on some lines. Cosmetic.
- The External API `RealHit` scale changed (1 point = 1% of max health); not yet in a changelog.

# Not yet tested

- Torture, all of it: no torture hit in this session either (only `torture source:-1` AddTarget lines).

# Next

1. Torture test: whip, punch and Flames (torture). Check stages, overlay and bleedout match 0.1.3.
2. Hit a tortured part with a sword: the red overlay segment should grow and the torture points be replaced.
3. Snapshot the logs next session and review.
4. When torture passes: changelog entries (release skill) for the bands work and the `RealHit` scale change.
