https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.4

Injury bands release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Real damage on a 1% scale.** One real point is 1% of max health (core parts 100, sexual parts 10 / 5); co-save version 7 converts older saves on load.
- **Injury Ranges sliders.** `injury_engine.ranges.minor_min` / `major_min` / `severe_min` (25 / 75 / 90) set where a real wound becomes minor, major or severe; hits below minor are tracked but not narrated.
- **Wounds fit missing health.** No part can be hurt worse than the body has lost health (`RealCaps` / `FitRealToHealth`).
- **Killing blow named correctly.** Hits that land at or below 0 health are ignored, so the death narration names the hit that killed.
- **Torture gear sorts together.** Torture Padded Whip and Torture Flames / Frostbite / Sparks (names only; FormIDs unchanged).
- **Dashboard tidy.** The narration modes stay in "Narration"; the remaining narration settings move to "Narration Options".

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.3

Buff-ending and small-hit fix release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Healed line in the bio.** The `injury_engine_injuries` decorator carries `healed` ("{name} is completely healed.", `session.json` `healed`) when a living actor has no wound to show, and `character_bio/0420_injury_looks.prompt` prints it.
- **Buff endings, any archetype.** An attacker-less `HandleHealthDamage` that reports more loss than the Health `kDamage` modifier holds is max health shrinking, not damage, and passes through as a buff change; held uncaused damage counts only health missing below the current max.
- **Small real hits add up.** The carry of an unwounded actor's erased State is kept in `g_realCarry` (not saved) and seeds its next hit, clamped to the health the Health `kDamage` modifier still holds (not `max - current`: a temporary Health buff puts health above the permanent max), so hits below one point and damage over time can start a wound.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.2

Stability fix pass. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Papyrus threads.** Sync and death drains have their own drain flags, and scene / heal entries are found again by their actors after any yielding call.
- **Clean loads.** Torture scenes, heal sessions and the DLL's classify / calm / truce caches are reset on load; a stale `in_action` flag no longer swallows real lashes.
- **Swing-based melee.** `ProcessHitData` classifies the actual swing (`HitData.weapon`), and a recent whip hit no longer hides later damage.
- **Several truces per NPC.** Each truce partner is saved with its own end time; old single-partner saves are migrated.
- **Late truce answers ignored.** Each request has a ticket; answers for timed-out or replaced requests are dropped.
- **Torture start waits out of sight** instead of being cancelled, and held heal start lines are no longer dropped.
- **Overlay ordering.** Pushes carry a sequence number so the page never shows an older state.
- **Config hardening.** A bad field in an injury JSON skips that rule only; bad region indices fall back to `body`.
- **Performance.** Classify forms are resolved to FormIDs once instead of looked up by name per tick.
- **Buffs and steps are not falls.** A fall needs a real fall time and a drop that fits it; a Health buff ending (Courage, Fortify Health) marks the actor, and the attacker-less loss right after is not a wound.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.1

Freeze fix, drowning and installer update. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Game freeze fixed.** `InjuryEngine::OnDamage` could spin forever (game hang, no crash log) when every tortured part was full and what was left of a hit was less than one torture point; `PickPart` now skips those parts and the loop is capped at 1000 rounds with an error log line.
- **Fireball is not punching.** A real health spell effect on the victim (`g_realMagic`) wins over the unarmed rule.
- **Drowning.** `injuries/drowning.json` wounds the internal `lungs` part; co-save version 6 (version 5 loads with empty lungs).
- **Uncaused damage threshold.** `injury_engine.body.uncaused_min_percent`: scripted health loss with no cause must stick for about 1.5 s and reach this share of max health to become a wound.
- **Switched-off parts.** Real damage no longer lands on body parts that are turned off.
- **Optional fight actions.** The FOMOD option "Add fight/stop/yield actions" (on by default) installs `Start_Fighting`, `Stop_Fighting` and `Stop_Fighting_by_Yielding`; `Torture_Target` is always installed.
- **Optional overlay default.** The FOMOD option "Show torture overlay" (on by default); unchecked, the dashboard setting defaults to off. A saved dashboard value is kept.
- **Narration names, not pronouns.** Injury JSON `narration.*` / `death.*` strings use `{name}` in place of `{he}` / `{him}` / `{his}` / `{reflexive}`; `InjuryEngine.cpp` `NarrationVars` renders any pronoun placeholder as the name on every narration / event path (looks / feels keep `ActorVars`); `SourceHealText` drops the self-heal pronoun branch; `NameOf` falls back to base name, then race, never "Someone". Followers had read pronouns in narrations of other people's fights as the player.
- **Real hits need sight.** `Main.NarrateHits` (health) sends a DirectNarration only when `PairVisible`; otherwise the short-lived event.
- **Installer image.** The FOMOD shows the Injury Engine image.

## 0.1.0

First release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Injuries from all damage.** Swords, arrows, spells, creatures, falls and torture each wound a body part, which moves from light to medium to severe and heals over time.
- **Torture that can't kill.** Whipping, pinching, slapping, pulling, punching and fire / frost / shock torture spells hold health down but never kill; a victim worn down far enough collapses until healed.
- **Padded whip and torture spells.** Craft the whip or get it, with Flames / Frostbite / Sparks (torture), from "Add gear to player". Torture starts no fight.
- **Real damage replaces torture first.** A tortured actor only dies once its torture wounds are replaced by real ones.
- **Healing.** Game time, regeneration, and healing spells, staffs and potions; a heal session describes how far each wound has healed.
- **Narration.** Torture, healing, real hits and killing blows are narrated to the LLM, combined into one line per scene, and each narration waits for SkyrimNet's reply to the last one.
- **Prompts.** Everyone sees what the wounds look like; the victim knows how they feel.
- **LLM actions.** `Torture_Target`, `Stop_Fighting`, `Stop_Fighting_by_Yielding` and `Start_Fighting` as SkyrimNet YAML actions; offers made to the player show a Yes / No box.
- **Overlay.** A PrismaUI "Torture Damage" bar per tortured actor that fades out by itself.
- **Extensible.** Other mods can add injury types as JSON and use the External API (SkyrimNet_SexLab, SkyrimNet_Leashed).
