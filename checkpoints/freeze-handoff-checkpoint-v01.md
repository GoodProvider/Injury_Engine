# Freeze in OnDamage: handoff checkpoint v01 (2026-10-17)

Built and deployed: `make dll` → `SKSE/Plugins/Injury_Engine.dll` (no stale copy in MO2 overwrite). No Papyrus, JSON or ESP changes. Not committed.

## Bug
Game froze, no crash log (`bug-logs/freeze/Injury_Engine-freez.log`). The `OnDamage` part loop could never end: every tortured part at its maximum and the rest of the hit smaller than one torture point. `PickPart` kept returning a full tortured part. It ran on an actor-update worker with `g_lock` held, and the main thread waited on that worker.

## Changed
- `InjuryEngine.cpp`: `PickPart(st, female, weight, canReplace)`. A full tortured part is picked only when `canReplace` is true. The carry loop passes `false`, so a real point never lands on a full part. New `PartActive` (active part or lungs, fits the actor's sex) is used in both branches, so switched-off parts get no real damage. The loop stops after 1000 rounds with an `error` line and drops the rest.
- `Classifier.cpp/.h`, `Hooks.cpp`: `NoteEffect(victim, effect, caster)` also records real health-damage spell effects (`g_realMagic`: source, caster, time). `Classify` uses them in the empty-hands branch before the unarmed rule. A Fireball's second damage call is no longer read as `punching`.
- Docs: CLAUDE.md flow (health model, Classifier), KNOWLEDGEBASE "A loop under g_lock froze the game".

## Other agent's patch (`bug-logs/freeze/injury-engine-pickpart.patch`)
Its diagnosis and approach were right, but it no longer applied: `PickPart` has had a `weight` parameter since the drowning work. It was rebased by hand. Added beyond the patch: `part = -1` when the guard trips, the active-part filter, and the Fireball classification fix.

## Checked
Standalone copy of the loop (scratchpad, not in the repo). The horker case: old loop hangs, new loop returns at once. Over 20,000 random states: old loop hung 1594 times; new loop never hit the guard (max 139 rounds) and never overfilled a part.

## Open
- Report note 3 (HandleHealthDamage return value / `kept` passed to the original): not changed. The current design is documented in KNOWLEDGEBASE.
- The Fireball → punching cause comes from the log pattern. No verbose log confirms it yet: look for `classify ... unarmed but spell effect -> burning`.

## In-game tests
1. Verbose logging on. Let wolves maul a horker until most parts are full. Punch it once out of combat (one torture point), then hit it with Fireball, several times. Expected: no freeze, no `part loop stopped` line.
2. Cast Fireball with empty hands (hotkey / quick-cast) on an NPC out of combat. Expected: `burning`, no `torture ... punching`.
3. Regression: torture first (whip / punch), then real hits still replace torture points first. Drowning still lands on the lungs only. Sexual parts off: no real points there.
