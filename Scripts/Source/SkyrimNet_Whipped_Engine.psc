Scriptname SkyrimNet_Whipped_Engine Hidden

; Natives from SkyrimNet_Whipped.dll (SKSE_Source/src/Papyrus_Engine.cpp).
; InjuryEngine: per-actor injury per source (whipping, slashing, ...) for six body parts
; (0 face, 1 left arm, 2 right arm, 3 back, 4 left leg, 5 right leg), plus with "include sexual parts"
; 6 nipples, 7 pussy, 8 penis, 9 lips, 10 ass hole (small: fewer points, hit less often). All data and text come from
; SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json (see injuries/guide.md); sources are named by id.
; Whip wounds hold health down (90% of max health at full damage); real damage replaces them first;
; healing above the wounded level converts back into points in SyncHealth.

; Hit code: fresh * 100000000 + source * 1000000 + part mask * 10 + level
;   fresh: the victim had no open wounds before (1/0); source: index (SourceId); part mask: bit per part;
;   level: worst part state before the hit (0 undamaged, 1 light, 2 medium, 3 severe).

; Torture hit (source id of a model "torture" file: "whipping", "slapping", ...) on a random part (the type's
; part_weights); takes the health the part's new maximum holds down
; (never below 10% of max health). Returns a hit code, or -1 for None/dead actors and unknown sources.
Int Function Hit(Actor akActor, Actor attacker, String sourceId) global native

; Hit on one part (PartIndex). -1, a part outside the active set (sexual parts off) or one the actor's sex lacks: random.
Int Function HitPart(Actor akActor, Actor attacker, String sourceId, Int part) global native

; Scripted real damage: points damage points of the health source with file id sourceId (a model "health" file, or
; the health half of a "both" file such as "falling"), applied like a hooked hit: torture wounds absorb their share,
; the rest is taken from health (can kill), and the hit is queued for NarrateRealHits. The part is picked as for an
; attack. Returns the source index, or -1 for None/dead actors and unknown sources.
Int Function RealHit(Actor akActor, Actor attacker, String sourceId, Int points) global native

; True when sourceId (a file id) has a health model: a "health" file or a "both" file.
Bool Function HasHealth(String sourceId) global native

; A caller's heal (spell, potion): health becomes whip points healed on part first (-1: random), then any part;
; health rises by the points healed. Returns points healed (0 nothing open, -1 bad args). Heal codes queue as usual.
Int Function HealPart(Actor akActor, Int part, Float health) global native

; Part index by part name ("left arm", "ass hole") or region ("head" -> face); -1 when unknown or "".
Int Function PartIndex(String name) global native

; True if any damage is left (any source).
Bool Function HasOpenWounds(Actor akActor) global native

; Applies time to every tracked actor; returns those with parts that healed to a new stage
; since the last TakeHealed.
Actor[] Function TakeHealedActors() global native

; Actors with heal codes already queued (does not apply game time).
Actor[] Function TakeQueuedHealActors() global native

; Heal codes for the actor, cleared on take (pass them to HealText):
;   source * 128 + part * 8 + stage  a part reached a new stage (0 light .. 4 mostly healed, 5 healed)
;   10000 + source                   a source has healed on every part it touched
;   20000 + part                     a part has no injury left from any source
; plus 100000 when no healing source contributed (game time, natural regen): passive, narrated silently.
Int[] Function TakeHealed(Actor akActor) global native

; Real (non-whip) hits not yet narrated, as hit codes. HitVictims / HitAttackers are parallel to the last result
; (attacker may be None). The DLL sends mod event "SkyrimNet_Whipped_Injury" when one is queued.
Int[] Function TakeHits() global native
Actor[] Function HitVictims() global native
Actor[] Function HitAttackers() global native

; Pending sync events (heal start / pain gone / ...) not yet taken; does not run health conversion.
; kind * 10000 + source * 1000 + percent. SyncActors / SyncCasters are parallel to the last result.
Int[] Function TakeSyncEvents() global native

; Converts healing on loaded tracked actors into points (all of it while a healing source is active or the
; actor has no whip wounds, otherwise natural regen at the regen rate). Returns the queued events as
; kind * 10000 + source * 1000 + percent:
;   kind: 0 heal start, 1 pain gone (damage reached 0), 2 fully healed (actor forgotten), 3 heal stopped
;   source: 0 none (regen / game time), 1 spell, 2 staff, 3 potion, 4 other magic
; SyncActors / SyncCasters are parallel to the last result (caster may be None).
Int[] Function SyncHealth() global native
Actor[] Function SyncActors() global native
Actor[] Function SyncCasters() global native

; Tracked actors plus events and hits not yet taken.
Int Function TrackedCount() global native

; Narration noun for a part ("face", "left arm", ...); "" when the .dll is missing.
String Function PartName(Int part) global native

; Source id by index ("" out of range) and the number of sources loaded (0 = none found). A "both" file loads as
; two sources: the torture half under its id, the health half as "<id>_real".
String Function SourceId(Int source) global native
Int Function SourceCount() global native
; True for a torture type (whipping, pinching, slapping, pulling, punching, fire, frost, shock, falling, and any
; torture file another mod adds).
Bool Function IsTorture(String sourceId) global native
; Torture source id for an action alias ("whip", "pinch", ...) or for the source id itself. "" when none.
String Function SourceOfAlias(String alias) global native
; Torture_Target type list from each torture file's action.alias, joined with "|", action.order first.
String Function ActionAliases() global native

; Decorator data with text resolved for this actor (empty sources when no injury):
; {"sources":[{"id","looks_heading","feels_intro","feels_outro","parts":[{"part","region","stage","look","feel"}]}]}
String Function GetStateJson(Actor akActor) global native

; Narration text rendered from the JSON files. noCause leaves out narration.hit.by_* (the caller's cause leads instead).
String Function HitText(String sourceId, Actor attacker, Actor victim, Int mask, Int level, Int count, Bool noCause = false, Bool unarmedForm = false) global native
; Parallel to the last TakeHits() result (same index).
Bool Function HitUnarmed(Int index) global native
; Writes a torture / heal / health line to SkyrimNet_Whipped.log ("[hh:mm:ss] <category>:<narrationType> <text>", narrationType = direct / event / short).
Function LogLine(String category, String narrationType, String text) global native
; All codes passive: wrapped as ordinary healing (narration.heal.passive).
String Function HealText(Actor victim, Int[] codes) global native
; kind / source as in SyncHealth; percent is turned into words (no numbers in narration).
; Source none (pain gone / fully healed by natural healing): wrapped as ordinary healing.
; The feeling sentence of a hit level 0-3 (narration.hit.levels), rendered for the victim.
String Function LevelText(String sourceId, Actor victim, Int level) global native
String Function SourceHealText(Int kind, Int source, Int percent, Actor caster, Actor target) global native
; Renders the string at a dotted path ("narration.summary.text", "action.no_whip") with {speaker}, {target} and
; {times} (words for count, from narration.summary.times).
; Combined torture narration for parallel lists of pairs (an attacker that is not an Actor means none); phase start, again, or switch.
String Function SceneText(String phase, Form[] attackers, Form[] victims, String[] sources, Int[] masks, Int[] levels) global native
String Function ActionText(String sourceId, String path, Actor speaker, Actor target, Int count) global native

; Raw value at a dotted path of injuries/<source>.json, placeholders untouched ("" / 0 when absent).
String Function Text(String sourceId, String path) global native
Int Function TextCount(String sourceId, String path) global native
Float Function Number(String sourceId, String path, Float fallback) global native

; Game hours for one point of damage to drain (or one point of healing to accumulate).
Function SetHoursPerPoint(Float hours) global native

; Percent of natural regeneration that heals whip wounds; the rest of that regen is taken back.
Function SetRegenRate(Float percent) global native

; "Show whipping overlay": PrismaUI bars for whipped actors.
Function SetOverlayEnabled(Bool enabled) global native

; "Include sexual body parts": new hits, prompts and the overlay use parts 6..10 too.
Function SetIncludeSexualParts(Bool enabled) global native

; "Verbose logging": debug lines in SkyrimNet_Whipped.log.
Function SetVerboseLogging(Bool enabled) global native

Function Forget(Actor akActor) global native
