Repo: Injury_Engine · branch: main · baseline: 1f804ee (plus the uncommitted 0.1.7 work in the tree) · 2026-11-07

# Werewolf feeding heal narration: checkpoint 01

## Goal (from checkpoint-00)
- When a werewolf heals by feeding, narrate a line modeled on "As a werewolf, {name}'s fangs tear into the corpse's warm throat and intestines, the blood regenerating and strengthening {name}."
- Narrate nothing when the werewolf feeds at full health.

## Decisions
- New healing source `kSourceFeed = 6` ([InjuryEngine.h](../../SKSE_Source/include/InjuryEngine.h)), handled like food: no caster, no stopped line, `.none` wording for full / pain gone, never grouped.
- How feeding is detected: a heal whose caster is the target itself or nobody, on an actor whose race is listed in `session.json` `heal_feed.races` (WerewolfBeastRace Skyrim.esm 0x0CDD84 as `werewolf`, DLC2WerebearBeastRace Dragonborn.esm 0x01E17B as `werebear`). `FeedRace` is in [InjuryEngine.cpp:1079](../../SKSE_Source/src/InjuryEngine.cpp#L1079), and its call site is at [InjuryEngine.cpp:2292](../../SKSE_Source/src/InjuryEngine.cpp#L2292). Vanilla `PlayerWerewolfChangeScript.Feed()` casts `FeedBoost` on the player (`c:\Skyrim\Source\Scripts\PlayerWerewolfChangeScript.psc:307`).
- Full health: the `AddTarget` hook reads the Health `kDamage` modifier **before** the original call, because an instant heal has already landed by the time it returns ([Hooks.cpp:174](../../SKSE_Source/src/Hooks.cpp#L174)). `Seen` then returns without a session or event when the actor was not hurt and has no points ([InjuryEngine.cpp:1121](../../SKSE_Source/src/InjuryEngine.cpp#L1121)).
- User decision: the feed line is followed by the normal healing message. Papyrus `HealSend` already sends `lead + HealMapText`, so the feed line is the lead. The heal map covers torture only, so with real wounds only, `heal_feed.wounds` ("{name}'s wounds begin to close.") is appended in `SourceHealText`.
- Papyrus: `HEAL_SOURCE_FEED = 6`, `HealSyncSourceMethod(6) = "feed"` (event id `healing_feed_<caster>_to_<victim>`), excluded from heal-start grouping.

## Results
- `make dll` built clean; `SKSE/Plugins/Injury_Engine.dll` at 14:07.
- `make papyrus` compiled 5 files; `.pex` at 14:07.
- `session.json` parses.
- Docs: CLAUDE.md (healing paragraph), `injuries/guide.md` (`heal_feed`), KNOWLEDGEBASE ("Instant heals land inside AddTarget").

## Findings
- Logs: [logs/2026-11-07-14-05-14/](logs/2026-11-07-14-05-14/) ([MANIFEST.txt](logs/2026-11-07-14-05-14/MANIFEST.txt)).
- The attached [SkyrimNet.2026-11-07_06-14-47.log](logs/2026-11-07-14-05-14/SkyrimNet.2026-11-07_06-14-47.log) comes from a Vortex install on `D:`. It shows no feed narration. It does show the **Devour** mod loaded (lines 8648-9108). If Devour heals with `RestoreActorValue`, no magic effect is involved, so this feature won't see it.
- The live `Injury_Engine.log` / `Papyrus.0.log` copies hold no werewolf activity (older session).

## Not yet tested
- Everything in game. Also unchecked: the race form IDs (look for `feed race werewolf ...` or `feed race ... not found` in `Injury_Engine.log` after the first heal of a session), whether `FeedBoost` passes `IsHealing`, the Gorging / Savage Feeding perks, the werebear, and Devour feeding. VR is not tested.

## Next
1. Launch the game with verbose logging on (`injury_engine.debug.verbose_logging`).
2. As a human, take damage, then `player.setrace WerewolfBeastRace` (or use Beast Form). Kill something and feed. Expected: `heal start ... source:6` in `Injury_Engine.log`, and the werewolf line followed by "Bob's wounds begin to close." (real wounds) or the heal map (torture wounds).
3. Feed again at full health. Expected: `feed at full health ... no heal line`, and no narration or event.
4. As a human, heal with Healing Hands. Expected: the normal spell line, unchanged.
5. Optional: werebear (Dragonborn), and Devour's feeding if installed.
6. Capture `Injury_Engine.log`, `Papyrus.0.log` and the SkyrimNet `conversation_log.log` for the next session.
