Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `e887881` (Release 0.1.3) · uncommitted

# Goal

Checkpoint 00: dashboard sliders ("Injury Ranges") that set where a body part's **real** wound becomes minor / major /
severe, plus a gate so a part can't be hurt worse than the body as a whole.

# Decisions (agreed with the user this session)

- **Real damage only.** Torture keeps its 20 / 8 / 6 points, thresholds 4 / 11, health link, overlay, bleedout.
- **Own real scale:** 1 real point = 1% of max health on any part. Core parts hold 100 (`whipping.json` `real_max`),
  sexual parts nipples / pussy / penis 10, lips / ass hole 5 (`sexual_parts.json` `real_max`).
- **Stage** = real points / part real max against the sliders (defaults 25 / 75 / 90): below minor the wound counts
  (holds health) but has no stage, no narration, no looks / feels; minor, major, severe = the source's three stages.
- **Cap = missing health:** no part holds a larger share of its real max than the body has lost (`RealCaps`, from the
  Health `kDamage` modifier). A hit adds its own size; at the cap the rest moves to another part; all capped → dropped.
  Example: at 24% health lost the arm holds at most 24 (silent); while health is 75%..26% no part passes minor.
- **Trim to health:** `FitRealToHealth` (each `SyncHealth`, after `ConvertHealth`) clamps parts to the cap and the
  real total to the missing health beyond the torture hold. It also fixes old saves on the first sync after load.
- Co-save **version 7**: v6 and older real points are rescaled ×100 / `CapacityTorture()` on load.

# Results (built and deployed, not tested in game)

- C++: [InjuryEngine.cpp](../../SKSE_Source/src/InjuryEngine.cpp): `DamageStage` split into `TortureStage` /
  `RealStage`; `CapacityDamage` → `CapacityTorture`; `RatioReal` = max / 100; `MissingPercent`, `RealCaps`,
  `PartFullTorture` (real counted by health worth) / `PartFullReal`; `PickPart(..., caps)`; `FitRealToHealth`;
  `OnDamage` caps + `narrateMask` (a hit whose parts all stay below minor pushes no `HitEntry`, so no narration or
  event; death records unchanged); `SetInjuryRanges`; `RescaleOldReal` + load accepts v6.
  [InjuryEngine.h](../../SKSE_Source/include/InjuryEngine.h): `kRecordVersion` 7, `OnDamage(..., applied = true)`,
  `SetInjuryRanges`. [InjuryConfig.h](../../SKSE_Source/include/InjuryConfig.h) / [InjuryConfig.cpp](../../SKSE_Source/src/InjuryConfig.cpp):
  `Body::realMax`, `real_max` keys. [Hooks.cpp](../../SKSE_Source/src/Hooks.cpp) ModActorValue and `RealHit` pass
  `applied = false`. [Papyrus_Engine.cpp](../../SKSE_Source/src/Papyrus_Engine.cpp): native `SetInjuryRanges`.
- Papyrus: [Injury_Engine_Engine.psc](../../Scripts/Source/Injury_Engine_Engine.psc) native;
  [Injury_Engine_Main.psc](../../Scripts/Source/Injury_Engine_Main.psc) `ApplyPluginConfig` reads
  `injury_engine.ranges.minor_min / major_min / severe_min` (defaults `whipping.json` `defaults.range_*`), `[v]` trace.
- Data: [manifest.yaml](../../SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml) three `int` 0..100
  fields, category "Injury Ranges", right after the four Narration selects; `whipping.json` `real_max`, `defaults.range_*`;
  `sexual_parts.json` `real_max`.
- Docs: CLAUDE.md (model `health`, co-save 7), README settings table, `injuries/guide.md`, `docs/developers/papyrus.md`,
  KNOWLEDGEBASE "Real and torture points are two scales".
- Builds: `make dll` OK → `SKSE/Plugins/Injury_Engine.dll` (08:34); Papyrus `compile: papyrus` OK, 5 `.pex` written.
  manifest.yaml was not parsed (no PyYAML here): check the dashboard loads it.
- No logs used this session (no `logs/` folder made).

# Findings / risks

- **Behavior change for External API:** `Injury_Engine_Engine.RealHit` points are now 1% of max health each (were
  1 / 120). Callers such as SkyrimNet_Leashed that pass small counts now deal less damage.
- The cap reads the modifier at the start of `OnDamage`; torture health the hit replaces is given back afterwards, so
  that hit's cap can be slightly too high. `FitRealToHealth` corrects it on the next sync.
- Real hits now narrate much less: early hits stay silent until a part reaches 25 points (25% of max health on one part).
  The level text index is still "stage before the point" (0 fresh wound .. 3 severe); a part crossing into minor reads
  as the fresh-wound line.
- Health source `stages` lists are named light / medium / severe in the JSON; the prompts show those names, not
  minor / major / severe. Rename in the JSON if wanted.
- `bug-logs/` files show as deleted in `git status`; not done by this session (they were present at its start).

# Not yet tested

Everything in game: narration gating, caps, trims on regen, old-save rescale, torture unchanged, dashboard section.

# Next

1. Launch the game with this MO2 folder; enable `injury_engine.debug.verbose_logging`.
2. Dashboard: confirm "Injury Ranges" shows below Narration with 25 / 75 / 90; save settings and look for
   `[v] ApplyPluginConfig injury ranges ...` in `Papyrus.0.log`.
3. Hit an NPC with small sword hits: no `injury_hit` until a part reaches minor; `Injury_Engine.log` lines
   `below minor, not narrated`; major only once health ≤ 25%, severe ≤ 10%.
4. Let the NPC regenerate: `fit ... real points trimmed` lines; wounds drop out of the bio.
5. Load a save made with 0.1.3 that has real wounds: no oversize parts after the first sync.
6. Torture check: whip, punch, Flames (torture): stages, overlay, bleedout as before; a sword on a tortured part still
   grows the red bar.
7. Snapshot the logs into `checkpoints/2026-10-20-injury-bands/logs/` next session and review.
