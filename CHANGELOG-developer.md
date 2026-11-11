https://github.com/GoodProvider/Injury_Engine/releases/tag/0.2.1

Arrow and vampire / spell wounds release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and Magelight UI 1.7.1 or later.

- **Arrow wounds.** New health source `arrow` claims bow and crossbow hits (`"bash": false`); `piercing` keeps daggers. Every group has its own `arrow.json`.
- **Vampire and spell sources.** `draining` (Vampiric Drain 0x0F5B57, every Absorb Health effect), `poisoning` (ResistPoison), `clawing` (Vampire Lord unarmed, `attacker_races`) and `blood_magic` (keywords `Injury_Engine_BloodMagic` / `MagicBloodMagic`). All 19 groups have their own text for each.
- **Spells classify by their own effect.** An unmatched Health damage effect is `generic`, never the weapon in the other hand; value-modifier ticks (plain, dual, Absorb via a new vtable hook) run inside `Classifier::MagicScope`.
- **New classify keys.** `absorb`, `effect_keywords`, `attacker_races`, `bash`, `resist: ResistPoison`; `effect_file` / `effect_id` work for health sources. Throttled `health effect:` log line.
- **Bow bash is bludgeoning.** `TESHitEvent` kBashAttack and `HitData` kBash / kTimedBash.
- **Docs.** `docs/authors/custom-weapons.md` covers weapon and spell mods.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.2.0

Race groups release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and Magelight UI 1.7.1 or later. Includes the unreleased 0.1.7 (overlay on Magelight UI).

- **Race groups.** `races.json` maps every race (or one actor) to a folder under `injuries/<group>/` with its own `body.json` (up to 16 parts, core / sexual / lungs, `bleedout`, `disabled_sources`) and text (`common.json`, per-source patches, `sexual_parts.json`). Humanoid sources moved to `injuries/humanoid/`. Co-save version 8.
- **Creature bodies.** 19 groups ship: humanoid, undead, werewolf, vampire lord, insectoid, quadruped, trolls / lurkers, spriggans, four dragon kinds (Durnehviir by actor), Dwarven constructs, elementals, spirits, netch / seekers, mudcrabs, slaughterfish and birds.
- **Group sexual-part text.** A group's `sexual_parts.json` takes one `health` and one `torture` block for every source of that model; insectoid, werewolf, undead and vampire lord use it.
- **Werewolf feeding heals.** `kSourceFeed` = 6 with `session.json` `heal_feed` text; silent at full health.
- **Overlay on Magelight UI.** PrismaUI is removed; the page is `Magelight/Injury_Engine/views/overlay/index.html` and the host queues calls before DOM ready. Magelight UI is not bundled.
- **VR hook slot.** `HandleHealthDamage` is hooked at vtable 0x106 on VR (untested in game).
- **Review fixes.** Health changes under the lock are batched, invalid UTF-8 names no longer throw, Fortify Health is not healing, a max-health buff ending leaves no phantom wound, and the sync loop runs only for loaded actors.
- **Papyrus API.** `External_Damage(narrate = False)` is silent (`RealHitQuiet`); new native `PartNameFor(actor, part)`; `Torture_Target` runs one action per speaker and returns lent whips on load.
- **Settings removed.** `narration.cooldown` and `narration.torture_narration_window_size`.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.6

Combat and healing fix release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **No torture punches in combat.** `UnarmedMatches` no longer lets the out-of-combat `punching` rule match in combat, so in-combat fists (mages out of magicka) fall to `bludgeoning`. Setting `injury_engine.body.punch_combat_health` and native `SetPunchCombatHealth` removed. Torture spells and the padded whip are unchanged.
- **DOM slap is slap torture.** New classify rule keys `weapon_file` / `weapon_id` (one weapon form, in or out of combat, before its weapon type). `slapping.json` names DiaryOfMine.esm `DOMSlap` 0x404724, which was a OneHandSword and read as `slashing`. Its hits become torture points (`ProcessHitData`), start no combat (`IsTortureAttacker`) and are calmed (hit sink).
- **Healing costs vanilla health.** Healing lowers a part's torture maximum and clamps every type to it (like real damage replacing torture); `ConvertHealth` tries the other kind (torture / real point) before giving up on a budget, and healing-source health below a point is kept as `g_healCredit` instead of being taken back as regen once the session ends.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.5

Healing and integration release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Torture mod event.** `Injury_Engine_Torture` (`Form attacker, Form victim, String source, Int hits, String cause`) is sent for every torture batch that lands, narrated or silent; SkyrimNet_DOM counts it as a punishment.
- **Heal start mod event.** `Injury_Engine_HealStart` (`Form caster, Form target, Int source`) is sent once when a heal session opens (source 1 spell / 2 staff / 3 potion / 4 other magic / 5 food).
- **Food is its own healing source.** `kSourceFood` = 5: eating or drinking reads "eats the Apple Pie and feels better", with no caster.
- **Ambient healing is quiet.** Regen abilities re-cast by unhurt NPCs open no heal session; an unhurt actor gets at most one heal start per 60 s.
- **Group heal lines.** One cast healing several actors is narrated as one line; a closed reply gate holds it, and missing `heal_group` text falls back to one line per target.

https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.4

Injury bands release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Real damage on a 1% scale.** One real point is 1% of max health (core parts 100, sexual parts 10 / 5); co-save version 7 converts older saves on load.
- **Injury Ranges sliders.** `injury_engine.ranges.minor_min` / `major_min` / `severe_min` (25 / 75 / 90) set where a real wound becomes minor, major or severe; hits below minor are tracked but not narrated.
- **Wounds fit missing health.** No part can be hurt worse than the body has lost health (`RealCaps` / `FitRealToHealth`).
- **Killing blow named correctly.** Hits that land at or below 0 health are ignored, so the death narration names the hit that killed.
- **Killing blows narrated in combat.** A death line held by a closed reply gate waits up to 1 s (`DeathHold` / `DeathTick`) instead of becoming an event, and the gate opens when the last narration's target is dead.
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
