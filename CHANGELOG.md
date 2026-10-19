# Changelog

## [0.1.2](https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.2) — since [0.1.1](https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.1)

Code-review fix pass ([checkpoints/reviews/checkpoint-v03.md](checkpoints/reviews/checkpoint-v03.md)).

### Torture / action
- Truce partners: per-NPC StorageUtil lists `injury_engine.truce_partners` / `injury_engine.truce_partner_until`, each partner with its own end; `RestoreExpiredTruces` prunes and re-notes each on load; `Start_Fighting` removes both fighters from every list. A pre-0.1.2 `injury_engine.truce_with` is migrated.
- Truce requests carry a ticket (`TruceDecision.cpp`): a late LLM or message-box answer for a timed-out, replaced or earlier-load request is ignored. The decision timeline reads `data` (falling back to `text`), every field `is_string()`-safe.
- Queued combat stops re-check the calm / truce on the game thread, so `Start_Fighting` (`ForgetCalm` / `ForgetTruces`) in between wins.
- `injury_engine.in_action_time` is refreshed on each strike; `Injury_Engine_LashEffect` clears an `in_action` flag older than 3 s or stamped in the future, so a load mid-`Torture_Target` no longer swallows real lashes.

### Wounds / healing
- Falls must be physically plausible: `Classifier::ClassifyFall` also needs `fallTime >= 0.3 s` and a drop no larger than `686 × max(gravity, 1) × t² + 100` units. `fallStartHeight` is stale between falls, so a brief `fallTime` on a step read a buff ending as a fall ("Camilla Valerius stumbles"). An accepted landing logs an info `landing` line.
- Buff changes are not wounds: a Recover-flagged Health effect going negative (`ValueModifierEffect::ModifyActorValue`, e.g. Fortify Health from Courage ending) or a negative `kTemporary` Health `ModActorValue` marks the actor (`Classifier::NoteBuffChange`). For 1 s after the mark, and inside that effect's call, attacker-less Health damage passes through unclassified (always-on, throttled `[buff]` info line), and held uncaused damage near a mark is dropped (`dropped (buff change)`).

### Papyrus
- `sync_draining` / `deaths_draining` drain flags (as `hits_draining`); `OnUpdate` skips its `SyncHealth` while another thread drains.
- Scene / heal indices are found again by their actors (`SceneIndexOf`, `HealFind`) after yielding calls.
- `Setup` drops every torture pair and heal session (`ClearSessions`) and clears `heal_tick_active`; a future stamp counts as expired.
- New scene state `SC_WAITING_HIDDEN`: a pending torture start out of sight waits instead of being cancelled.
- A heal session with a held start line (`KEY_HS_PEND`) lives `torture_maximum_break + reply_timeout`.
- `Strike` (External API) routes landed hits through the scene and reply gate.

### SKSE / overlay
- `ProcessHitData` classifies the swing (`HitData.weapon`) with `Classifier::ClassifyMelee`, not the equipped gear or a live spell effect; its "already handled" mark is per victim (`MarkHandled` / `TakeHandled`).
- A stored Padded Whip hit event no longer answers every damage call in its window; `Classify` falls through to the hands, the live torture effect and drowning.
- `Classifier::ResetSession` on kPreLoadGame / kNewGame clears truces, hit / effect / calm caches and releases `kIgnoreFriendlyHits`; a background timer releases friend-hit flags when the sync loop is not running.
- Classify effect and ignore-weapon forms are resolved to FormIDs once in `Classifier::Init` (no per-tick `LookupForm`).
- Hit, sync and death result buffers hold FormIDs, resolved in the read native; an empty take no longer clears them.
- A hit that clears the last torture point now stands the actor up (`Erase`); RNG draws are locked; `Revert` clears `g_hitUnarmed` / `g_hitCounts`.
- With sexual parts on, the lungs are no longer a torture part or in the health capacity.
- Overlay: `injuryEngineUpdate(state, seq, reset)`; the page drops older pushes, one pending payload and one queued task (`Overlay.cpp`, `overlay.html`).
- One `AllocTrampoline(28)`; `UnderwaterProbe` exits early on cheap flags.

### Install / settings
- Injury JSON hardening (`InjuryConfig.cpp`): a wrong field type skips that classify rule or that source's hook block, not the whole load; non-bool `npc` / `combat` skips the rule; `overlay.order` is range-checked; bad region indices (missing `sexual_parts.json`) are remapped to `body` with a warning.

## [0.1.1](https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.1) — since [0.1.0](https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.0)

### Wounds / healing
- Drowning: `injuries/drowning.json` wounds the internal part `lungs` (index 11, body weight 0, no cap, stage by the share of max health the water took; `lungs` block `medium` / `severe`). Detected by `Classifier::ClassifyDrown` (no hit form or attacker, `kUnderwater` or submerged level, no WaterBreathing). Co-save version 6; version 5 saves load with empty lungs.
- `injury_engine.body.uncaused_min_percent` (default 5): health lost with no attacker, hit event or real fall (other mods' scripts) becomes a wound only if it is still missing about 1.5 s later and is at least this share of max health.
- Real damage no longer lands on switched-off body parts (`PickPart` takes only active parts).

### SKSE / overlay
- Fixed a game freeze (no crash log): `InjuryEngine::OnDamage` could loop forever on an actor-update worker while holding the plugin lock when every tortured part was full and the rest of a hit was less than one torture point. `PickPart(..., canReplace)` now skips those parts, and the loop stops after 1000 rounds with an error line in `Injury_Engine.log`.
- A real health spell effect on the victim (`Classifier::NoteEffect`, `g_realMagic`) now wins over the unarmed rule, so a Fireball tick from an empty-handed caster is burning, not `punching`.

### Install / settings
- The FOMOD has two optional, pre-checked (Recommended) options:
  - "Add fight/stop/yield actions" installs `Start_Fighting`, `Stop_Fighting` and `Stop_Fighting_by_Yielding` with the `accept_truce` / `accept_surrender` decision prompts (folder `fight_actions`). `Torture_Target` is always installed.
  - "Show torture overlay" keeps `injury_engine.overlay.enabled` on by default (folder `overlay`); unchecked, the core copies of `manifest.yaml` and `whipping.json` default it to off (`python_scripts/fomod-overlay-off.py`). A value already saved in the dashboard is kept.
- Installer image `images/injury-engine.jpg` on the module and both options.

## [0.1.0](https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.0) — initial release

### Torture / action
- Torture types `whipping`, `pinching`, `slapping`, `pulling`, `punching`, `fire`, `frost`, `shock` (`injuries/*.json`, `model: "torture"`). One value per (type, part); a part counts only its worst type; healing a part lowers every type on it.
- Padded whip `Injury_Engine_PaddedWhip` (WEAP 0x803, COBJ recipe, `NonHostile`); a real hit runs `Injury_Engine_LashEnch` → `Injury_Engine_LashEffect` → `Main.OnLash`. Weapon, impact data, sounds and welt decal come from PAH Diary Of Mine (DOM not required).
- Spells Flames / Frostbite / Sparks (torture) (SPEL 0x816–0x818, MGEF 0x813–0x815, non-hostile copies 0x822–0x825) cause `fire` / `frost` / `shock`; vanilla spells stay real damage. Unarmed hits by an attacker out of combat are `punching` (in combat too unless `injury_engine.body.punch_combat_health`).
- Torture can't kill: `ValueModifierEffect::ModifyActorValue` and `ProcessHitData` hooks turn torture health ticks and punches into torture points, and health is never taken below 10% by torture. Torture bleedout (`injury_engine.body.torture_bleedout`): a victim at 15% health collapses until healed.
- Torture is not aggression: lashes and out-of-combat punches start no combat (`GetFactionFightReaction` hook, `kIgnoreFriendlyHits`), and a combat start within 3 s of a torture hit is stopped. Vanilla hit barks are silenced on torture victims (`Injury_Engine_QuietMarker`, `Injury_Engine_QuietDialogue`).
- LLM actions ship as SkyrimNet YAML (`external/goodprovider.injuryengine/actions/`). See [docs/authors/actions.md](docs/authors/actions.md).
  - `Torture_Target` (target, type `whip|pinch|slap|pull|punch`, strikes capped by `injury_engine.action.max_strikes`); whipping needs the padded whip (`injury_engine.action.require_whip`).
  - `Stop_Fighting` / `Stop_Fighting_by_Yielding`: the target decides through SkyrimNet's decision model (templates `prompts/decisions/injury_engine/accept_truce` / `accept_surrender`, API v12+); with the player as target a vanilla Yes / No box asks. Accepted: both stand down and Aggression is 0 for 24 game hours, guarded against new combat between the pair (`Classifier::NoteTruce`).
  - `Start_Fighting`: restores Aggression, drops the torture calm and truce guard, then starts combat.

### Wounds / healing
- Real damage by type: `slashing`, `piercing`, `bludgeoning`, `burning`, `freezing`, `shocking`, `mauling`, `falling`, `generic` (`model: "health"`; `HandleHealthDamage` / `ModActorValue` hooks, `Classifier`). Health capacity is the sum of the actor's parts.
- `model: "both"` files hold a torture and a health source (`falling.json`; the health half is `falling_real`). Other mods can ship their own `injuries/<id>.json` (SkyrimNet_Leashed: leash dragging). Schema: `injuries/guide.md`.
- Six body parts (face, back, arms, legs) plus optional sexual parts (`injury_engine.body.include_sexual_parts`, `injuries/sexual_parts.json`).
- Real damage replaces torture damage on its part first, so a tortured actor dies only after its torture wounds are replaced.
- Healing over game time (`injury_engine.healing.hours_per_point`), by natural regeneration (`injury_engine.healing.regen_rate`) and by healing spells, staffs and potions, which open a heal session; `injury_engine.healing.give_healing_hands`.
- `Injury_Engine.dll` (`InjuryEngine`) tracks every actor per part and source in the SKSE co-save (version 5).

### Narration / prompts
- All narration text is rendered in C++ from the injury JSON (`HitText`, `SceneText`, `HealMapText`, `DeathText`); no numbers reach the LLM ([docs/authors/prompts.md](docs/authors/prompts.md#no-numbers)).
- Torture scene: one combined line for all (attacker, victim) pairs in sight: `start`, `again` every `injury_engine.narration.torture_direct_narration_gap`, `switch` on a change of method; dropped after `injury_engine.narration.torture_maximum_break`.
- Heal sessions narrate start and progress by heal level; real hits and killing blows are narrated per `injury_engine.narration.injury_mode` / `healing_mode` / `death_mode` / `torture_mode` (Direct Narration, Event, Silent).
- Reply gate: each automatic narration waits for SkyrimNet's spoken reply to the last one, or `injury_engine.narration.reply_timeout`.
- Prompts `character_bio/0420_injury_looks.prompt` and `user_final_instructions/0420_injury_feels.prompt` over decorator `injury_engine_injuries(uuid)`. See [docs/reference/json-keys.md](docs/reference/json-keys.md).

### Papyrus
- External API on `Injury_Engine_Main`: `External_Damage` (preferred), `External_Injure`, `External_Torture`, `External_TortureText`, `External_Strike`, `External_Heal`, `External_PartPoints`, `External_SexualParts`, `External_SetSexualParts`. See [docs/developers/papyrus.md](docs/developers/papyrus.md).

### SKSE / overlay
- PrismaUI overlay `PrismaUI/views/Injury_Engine/overlay.html` (`injury_engine.overlay.enabled`): a "Torture Damage" bar per tortured actor in the last torture type's color, light blue while healing, red where real damage replaced torture; fades 3 s after the last change. See [docs/developers/overlay.md](docs/developers/overlay.md).
- Built against MinLL/CommonLibVR 4.39.5 (MIT) through the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork`.

### Install / settings
- FOMOD (core only) built by `make release`; `python_scripts/check-version.py` checks the version in the Makefile, both manifests, `CMakeLists.txt` and `version.rc`.
- Requires SkyrimNet Beta 25 (0.25.0) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.
- Dashboard settings `injury_engine.*` (`config/plugins/Injury_Engine/manifest.yaml`); full list in the README. `injury_engine.debug.verbose_logging` for troubleshooting.

### Docs
- README, AGENTS.md, KNOWLEDGEBASE.md, `docs/`, [release-guide.md](release-guide.md); MIT `LICENSE`.
