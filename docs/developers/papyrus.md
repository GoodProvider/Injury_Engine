# Papyrus / code

Developer guide: paths, compile, settings, ESP.

Contracts: [../reference/papyrus-rules.md](../reference/papyrus-rules.md), [../reference/json-keys.md](../reference/json-keys.md). SKSE / overlay: [overlay.md](overlay.md). Architecture and flow: [../../CLAUDE.md](../../CLAUDE.md#flow). Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md).

## Paths

| Path | Role |
|------|------|
| `Scripts/Source/Injury_Engine_Main.psc` | Quest script: setup, settings, `OnLash`, narration batching, healing poll, `SyncHealth` loop, decorator |
| `Scripts/Source/Injury_Engine_Actions.psc` | Execution of the YAML actions `Torture_Target` (`TortureTarget`), `Stop_Fighting` (`StopFighting`), `Stop_Fighting_by_Yielding` (`StopFightingByYielding`), `Start_Fighting` (`StartFighting`): member functions with positional params; the YAML (`external/goodprovider.injuryengine/actions/`) holds names, descriptions and eligibility. Native `ActionAliases` is no longer used; truce answers (`OnTruce`) and the Aggression calm (`Calm`, `RestoreAggression`, `RestoreExpiredTruces`) |
| `Scripts/Source/Injury_Engine_LashEffect.psc` | Enchantment effect on a real weapon hit → `Main.OnLash` |
| `Scripts/Source/Injury_Engine_PlayerRef.psc` | Player alias (load game → setup) |
| `Scripts/Source/Injury_Engine_Engine.psc` | Natives in `Injury_Engine.dll` |
| `Scripts/` | Compiled `.pex` |
| `skyrimse.ppj` | Pyro project (fallback only; imports SKSE, SkyrimNet, PapyrusUtil sources) |
| `.vscode/tasks.json` | VS Code task `compile: papyrus` |
| `Spriggit/Injury_Engine/` | ESP ↔ JSON |

## Compile

VS Code task **`compile: papyrus`** (`.vscode/tasks.json`): `c:/Skyrim/tools/papyrus-compiler/papyrus.exe compile -nocache -i Scripts/Source -o Scripts` with `-h` header dirs for vanilla `Data/Source/Scripts`, PapyrusUtil, SkyrimNet and SKSE64. On a script found in several header dirs the **last** `-h` wins, so vanilla goes first and SKSE last (the reverse of Pyro's import order). Agents run the same command from the repo root. Add new dependencies to both the task and `skyrimse.ppj`. Pyro (`skyrimse.ppj`) is a fallback only; do not invent Caprica one-offs.

## Settings

Schema: `SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml`. `Main.ApplyPluginConfig` reads `Plugin_Injury_Engine` with `SkyrimNetApi.GetConfig*`; defaults in Papyrus must match the schema `defaultValue`.

| Key | Used by |
|-----|---------|
| `injury_engine.action.require_whip`, `injury_engine.action.give_to_player`, `injury_engine.action.max_strikes` | Action eligibility, setup (`give_to_player` = "Add gear to player": `GiveGear` adds the whip and the Flames / Frostbite / Sparks (torture) spells); `max_strikes` needs a reload (re-registers the action) |
| `injury_engine.healing.hours_per_point`, `injury_engine.healing.regen_rate` | Passed to the DLL (`SetHoursPerPoint`, `SetRegenRate`) |
| `injury_engine.ranges.minor_min`, `major_min`, `severe_min` | Passed to the DLL (`SetInjuryRanges`; defaults `whipping.json` `defaults.range_minor / range_major / range_severe`): real-wound stage starts in percent of a part's real max |
| `injury_engine.healing.give_healing_hands` | Adds Healing Hands (Skyrim.esm 0x4D3F2) to the player |
| `injury_engine.narration.enabled`, `.cooldown`, `.max_distance`, `.event_ttl` | Narration gate (`cooldown`: healing only) |
| `injury_engine.narration.torture_narration_window_size` | Unused since passive heals are no longer narrated (was `InTortureSession`); torture narration itself is a session (`NarrateHits`: start narration, summary 2 s after the last hit) |
| `injury_engine.narration.torture_maximum_break` | Seconds without a hit that end the torture session (no line; stops \"again\"), default 3 |
| `injury_engine.narration.health_narration_cooloff` | Seconds between spoken (DirectNarration) real-damage narrations, default 6; hits in between, or while the reply gate is closed, are short-lived events |
| `injury_engine.narration.torture_direct_narration_gap` | Seconds between combined "again" narrations during continuous torture, default 6 |
| `injury_engine.narration.reply_timeout` | Reply gate (`Main.ClaimNarration`): after any DirectNarration, the next automatic one (torture start / switch / again, heal start / again, real hit, death) waits until an audio line has ended at least 1.5 s after it with the speech queue empty, or until this many seconds pass with no dialogue; default 15. Waiting torture starts / switches and heal starts are sent when the gate opens; real hits and deaths fall back to short-lived events. External API narrations are never held back but do close the gate |
| `injury_engine.overlay.enabled` | Overlay on/off |
| `injury_engine.debug.verbose_logging` | `Main.verbose_logging`; passed to the DLL (`SetVerboseLogging`) |

### Verbose logging

`Main.Verbose(func, msg)` writes `Trace(func, "[v] " + msg)` only while `verbose_logging` is on (Actions checks `whip_main.verbose_logging` itself, since its functions are global). Papyrus evaluates arguments before the call, so a message that calls natives, measures distance or loops (`GateText`, `GetActorValue`, ...) goes inside `if verbose_logging`; only cheap concatenations call `Verbose` directly. Normal (non-verbose) traces stay as they are.

## External API (soft dependency)

Global functions on `Injury_Engine_Main` for other mods (SkyrimNet_SexLab Force / Aid; SkyrimNet_Leashed injuries). Detect with `Game.GetFormFromFile(0x800, "Injury_Engine.esp")`. From C++, require the plugin loaded and a VM global on `Injury_Engine_Main` for the API you call (`External_Strike` / `External_Torture` / `External_Heal` for SexLab; `External_Injure` for Leashed).

| Function | Does |
|----------|------|
| `Int External_Damage(Actor attacker, Actor victim, String cause = "", String body_part = "", String damage_type = "whipping", Int damage_amount = 1, Bool narrate = True)` | Preferred API: `damage_amount` torture hits (or real damage for a health-only type) on `body_part` (part name or region, `""` random; ignored for real damage). `narrate`: the usual torture narration rules with `cause` leading the text; real damage narrates `cause` as a short-lived event; `False` is silent. Returns the last hit code (torture) or the source index (real damage), or -1 |
| `Int External_Injure(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True, Bool torture = True)` | Any injury type by file id, including types other mods ship in their own `injuries/<id>.json` (`injuries/guide.md`, "Injury types from other mods"). `torture = True`: `count` torture hits, like `External_Torture` (needs a `torture` or `both` file); returns the last hit code or -1. `torture = False`: `count` real damage points (needs a `health` or `both` file, e.g. `falling`; can kill; `part` ignored, the part is picked like an attack's), narrated like any real hit; `narrate` only sends `text`. Returns the source index or -1 |
| `Int External_Torture(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True)` | `External_Injure(..., torture = True)`: any torture type (`whipping`, `pinching`, `slapping`, `pulling`, `punching`, `fire`, `frost`, `shock`, `falling`, or another mod's); -1 for an unknown type |
| `String External_TortureText(Actor attacker, Actor victim, String damage_type, String body_part = "", Int damage_amount = 1)` | `External_Damage` for a torture type, **silent**: the hits land (no narration, no event) and the feeling text the injuries would have been narrated with is returned for the caller to narrate itself. `""` on failure (victim None or dead, not a torture type, no hit landed). Used by SkyrimNet_SexLab Punish. |
| `Int External_Strike(Actor attacker, Actor victim, String text, Int count = 1, String part = "", Bool narrate = True)` | `count` whip hits on `part` (part name or region, `""` random). One DirectNarration: `text` plus the injury text (`text` alone while narration is off). Returns the last hit code or -1 |
| `Int External_Heal(Actor caster, Actor target, String text, Float health, String part = "", Bool narrate = True)` | Heals `health` worth of torture points on `part` first (each point lowers every torture type on its part), then any part, and raises health to match. Narrates `text`; the heal stages narrate through the usual sync. Returns points healed |
| `String External_PartPoints(Actor target)` | Torture points per body part as JSON (`{"total","max","parts":[{"name","points","max"}]}`, native `PartPointsJson`); `""` when the target is None. Used by SkyrimNet_SexLab's Aid / Punish torture bar |
| `Bool External_SexualParts()` | `injury_engine.body.include_sexual_parts` |
| `External_SetSexualParts(Bool enabled)` | `PatchConfig` of that key and applies it now |

Natives behind them: `Engine.RealHit(actor, attacker, source, points)` / `Engine.HasHealth(source)` (real damage by file id), `Engine.HitPart(actor, source, part)` (a part outside the active set or the actor's sex falls back to random), `Engine.HealPart(actor, part, health)`, `Engine.PartIndex(name)`.

## StorageUtil keys (per actor)

| Key | Role |
|-----|------|
| `injury_engine.in_action` | Set during `Torture_Target` so the enchantment hit is not counted twice |
| `injury_engine.in_action_time` | Real time `in_action` was last refreshed (each strike); `LashEffect` treats an older (3 s) or future stamp as left over from a load and clears the flag |
| `injury_engine.pending_parts` / `_level` / `_count` | Hits since the last narration (part bitmask, worst level, count) |
| `injury_engine.hs_*` (lists on the Main quest) | Heal session per healed actor: target, caster, last time `IsHealing`, last narration time, last line sent |
| Session exclusivity | One activity per victim: opening a heal session (`HealOpen`) or a real (non-torture) hit (`NarrateRealHits`) calls `ClearTortureScene` (drops the victim's `sc_*` pairs, no line or memory); a torture or real hit calls `ClearHealBatch`. |
| Short-lived event ids | `InjuryShortEventId(kind, method, torturer, victim)`: `torturing` / `health` / `healing` + injury or heal-sync method + form ids (`0` when the actor is missing). Re-register overwrites TTL. |
| `OnInjury` / sync loop | `Injury_Engine_Injury` runs `DrainInjuryQueues` then `StartSync`. `StartSync` sets `sync_pending` and calls `RegisterForSingleUpdate(0.2)` only when no update is already pending (a second `RegisterForSingleUpdate` replaces the timer and delayed hooked hits). Native `TakeSyncEvents` drains the C++ event queue without applying game time or running `SyncHealth` conversion. |
| `injury_engine.last_narration` | Real time of the last narration (cooldown) |
| `injury_engine.last_torture` | Real time of the last torture hit (torture narration window / interval) |
| `injury_engine.truce_aggression` | Float: the actor's base Aggression before an accepted `Stop_Fighting` set it to 0 (only the first value is kept) |
| `injury_engine.truce_until` | Float: game time (days) when that truce ends and Aggression is restored |
| `injury_engine.truce_partners` / `injury_engine.truce_partner_until` | Form list / float list on a calmed NPC: every truce partner (the player too) and that truce's end, set on acceptance (`Actions.GuardTruce`); `RestoreExpiredTruces` prunes expired ones and notes the DLL truce guard again for each on load (`Engine.NoteTruce`); `Start_Fighting` removes both fighters from every list |
| `injury_engine.truce_with` | Form, before 0.1.2: the single saved partner; migrated into the lists above on the next load |
| `injury_engine.truce_actors` (form list on the Main quest) | Actors under a truce; `RestoreExpiredTruces` walks it on load and every `Main.OnUpdateGameTime` |

## ESP / Spriggit

Do not hand-edit `.esp`. `Spriggit/Injury_Engine/` is the source of truth.

| Command | Direction |
|---------|-----------|
| `make serialize` | `.esp` → `Spriggit/` (after xEdit/CK edits) |
| `make esp` | `Spriggit/` → `.esp` |
| `make release` | dll + esp + pack `.7z` |

Form IDs: [../../CLAUDE.md](../../CLAUDE.md#form-ids-injury_engineesp-esl).

Dialogue topics are folders: `DialogTopics/<EditorID> - <id>_Injury_Engine.esp/RecordData.json` plus `Responses/<id>_Injury_Engine.esp.json` per INFO (a flat `DialogTopics/*.json` is ignored). The `Injury_Engine_QuietDialogue` quest's silent Combat INFOs stop vanilla hit barks during torture ([../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md)).

## Content authors

Prompts → [../authors/prompts.md](../authors/prompts.md). Action → [../authors/actions.md](../authors/actions.md).
