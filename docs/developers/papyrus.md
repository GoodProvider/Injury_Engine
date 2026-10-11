# Papyrus / code

Developer guide: paths, compile, settings, ESP.

Contracts: [../reference/papyrus-rules.md](../reference/papyrus-rules.md), [../reference/json-keys.md](../reference/json-keys.md). SKSE / overlay: [overlay.md](overlay.md). Architecture and flow: [../../CLAUDE.md](../../CLAUDE.md#flow). Quirks: [../../KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md).

## Paths

| Path | Role |
|------|------|
| `Scripts/Source/SkyrimNet_Whipped_Main.psc` | Quest script: setup, settings, `OnLash`, narration batching, healing poll, `SyncHealth` loop, decorator |
| `Scripts/Source/SkyrimNet_Whipped_Actions.psc` | `Torture_Target` registration, eligibility, execution |
| `Scripts/Source/SkyrimNet_Whipped_LashEffect.psc` | Enchantment effect on a real weapon hit → `Main.OnLash` |
| `Scripts/Source/SkyrimNet_Whipped_PlayerRef.psc` | Player alias (load game → setup) |
| `Scripts/Source/SkyrimNet_Whipped_Engine.psc` | Natives in `SkyrimNet_Whipped.dll` |
| `Scripts/` | Compiled `.pex` |
| `skyrimse.ppj` | Pyro project (imports SKSE, SkyrimNet, PapyrusUtil sources) |
| `Spriggit/SkyrimNet_Whipped/` | ESP ↔ JSON |

## Compile

`pyro skyrimse.ppj` (VS Code task **`compile: pyro`** when present). Do not invent Caprica / `papyrus.exe` one-offs unless asked.

## Settings

Schema: `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml`. `Main.ApplyPluginConfig` reads `Plugin_SkyrimNet_Whipped` with `SkyrimNetApi.GetConfig*`; defaults in Papyrus must match the schema `defaultValue`.

| Key | Used by |
|-----|---------|
| `whip.action.require_whip`, `whip.action.give_to_player`, `whip.action.max_strikes` | Action eligibility, setup (`give_to_player` = "Add gear to player": `GiveGear` adds the whip and the Flames / Frostbite / Sparks (torture) spells); `max_strikes` needs a reload (re-registers the action) |
| `whip.healing.hours_per_point`, `whip.healing.regen_rate` | Passed to the DLL (`SetHoursPerPoint`, `SetRegenRate`) |
| `whip.healing.give_healing_hands` | Adds Healing Hands (Skyrim.esm 0x4D3F2) to the player |
| `whip.narration.enabled`, `.cooldown`, `.max_distance`, `.event_ttl` | Narration gate (`cooldown`: healing only) |
| `whip.narration.torture_narration_window_size` | Passive healing is not reported this long after a torture hit (`InTortureSession`); torture narration itself is a session (`NarrateHits`: start narration, summary 2 s after the last hit) |
| `whip.narration.torture_maximum_break` | Seconds without a hit that end the torture (end narration), default 3 |
| `whip.narration.torture_direct_narration_gap` | Seconds between combined "again" narrations during continuous torture, default 6 |
| `whip.overlay.enabled` | Overlay on/off |
| `whip.debug.verbose_logging` | `Main.verbose_logging`; passed to the DLL (`SetVerboseLogging`) |

### Verbose logging

`Main.Verbose(func, msg)` writes `Trace(func, "[v] " + msg)` only while `verbose_logging` is on (Actions checks `whip_main.verbose_logging` itself, since its functions are global). Papyrus evaluates arguments before the call, so a message that calls natives, measures distance or loops (`GateText`, `GetActorValue`, ...) goes inside `if verbose_logging`; only cheap concatenations call `Verbose` directly. Normal (non-verbose) traces stay as they are.

## External API (soft dependency)

Global functions on `SkyrimNet_Whipped_Main` for other mods (SkyrimNet_SexLab Force / Aid; SkyrimNet_Leashed injuries). Detect with `Game.GetFormFromFile(0x800, "SkyrimNet_Whipped.esp")`. From C++, require the plugin loaded and a VM global on `SkyrimNet_Whipped_Main` for the API you call (`External_Strike` / `External_Torture` / `External_Heal` for SexLab; `External_Injure` for Leashed).

| Function | Does |
|----------|------|
| `Int External_Injure(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True, Bool torture = True)` | Any injury type by file id, including types other mods ship in their own `injuries/<id>.json` (`injuries/guide.md`, "Injury types from other mods"). `torture = True`: `count` torture hits, like `External_Torture` (needs a `torture` or `both` file); returns the last hit code or -1. `torture = False`: `count` real damage points (needs a `health` or `both` file, e.g. `falling`; can kill; `part` ignored, the part is picked like an attack's), narrated like any real hit; `narrate` only sends `text`. Returns the source index or -1 |
| `Int External_Torture(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True)` | `External_Injure(..., torture = True)`: any torture type (`whipping`, `pinching`, `slapping`, `pulling`, `punching`, `fire`, `frost`, `shock`, `falling`, or another mod's); -1 for an unknown type |
| `String External_TortureText(Actor attacker, Actor victim, String damage_type, String body_part = "", Int damage_amount = 1)` | `External_Damage` for a torture type, **silent**: the hits land (no narration, no event) and the feeling text the injuries would have been narrated with is returned for the caller to narrate itself. `""` on failure (victim None or dead, not a torture type, no hit landed). Used by SkyrimNet_SexLab Punish. |
| `Int External_Strike(Actor attacker, Actor victim, String text, Int count = 1, String part = "", Bool narrate = True)` | `count` whip hits on `part` (part name or region, `""` random). One DirectNarration: `text` plus the injury text (`text` alone while narration is off). Returns the last hit code or -1 |
| `Int External_Heal(Actor caster, Actor target, String text, Float health, String part = "", Bool narrate = True)` | Heals `health` worth of torture points on `part` first (each point lowers every torture type on its part), then any part, and raises health to match. Narrates `text`; the heal stages narrate through the usual sync. Returns points healed |
| `Bool External_SexualParts()` | `whip.body.include_sexual_parts` |
| `External_SetSexualParts(Bool enabled)` | `PatchConfig` of that key and applies it now |

Natives behind them: `Engine.RealHit(actor, attacker, source, points)` / `Engine.HasHealth(source)` (real damage by file id), `Engine.HitPart(actor, source, part)` (a part outside the active set or the actor's sex falls back to random), `Engine.HealPart(actor, part, health)`, `Engine.PartIndex(name)`.

## StorageUtil keys (per actor)

| Key | Role |
|-----|------|
| `skyrimnet_whipped.in_action` | Set during `Torture_Target` so the enchantment hit is not counted twice |
| `skyrimnet_whipped.pending_parts` / `_level` / `_count` | Hits since the last narration (part bitmask, worst level, count) |
| `skyrimnet_whipped.heal_pending` (IntList) | Heal codes since the last heal-session line |
| `skyrimnet_whipped.hs_*` (lists on the Main quest) | Heal session per healed actor: target, caster, last activity, last line, pending pain-gone text, end text, ended |
| Session exclusivity | One activity per victim: opening a heal session (`HealOpen`) or a real (non-torture) hit (`NarrateRealHits`) calls `ClearTortureScene` (drops the victim's `sc_*` pairs, no line or memory); a torture or real hit calls `ClearHealBatch`. |
| Short-lived event ids | `InjuryShortEventId(kind, method, torturer, victim)`: `torturing` / `health` / `healing` + injury or heal-sync method + form ids (`0` when the actor is missing). Re-register overwrites TTL. |
| `OnInjury` / sync loop | `SkyrimNet_Whipped_Injury` runs `DrainInjuryQueues` then `StartSync`. `StartSync` sets `sync_pending` and calls `RegisterForSingleUpdate(0.2)` only when no update is already pending (a second `RegisterForSingleUpdate` replaces the timer and delayed hooked hits). Natives `TakeQueuedHealActors` and `TakeSyncEvents` drain C++ queues without applying game time or running `SyncHealth` conversion. |
| `skyrimnet_whipped.last_narration` | Real time of the last narration (cooldown) |
| `skyrimnet_whipped.last_torture` | Real time of the last torture hit (torture narration window / interval) |

## ESP / Spriggit

Do not hand-edit `.esp`. `Spriggit/SkyrimNet_Whipped/` is the source of truth.

| Command | Direction |
|---------|-----------|
| `make serialize` | `.esp` → `Spriggit/` (after xEdit/CK edits) |
| `make esp` | `Spriggit/` → `.esp` |
| `make release` | dll + esp + pack `.7z` |

Form IDs: [../../CLAUDE.md](../../CLAUDE.md#form-ids-skyrimnet_whippedesp-esl).

## Content authors

Prompts → [../authors/prompts.md](../authors/prompts.md). Action → [../authors/actions.md](../authors/actions.md).
