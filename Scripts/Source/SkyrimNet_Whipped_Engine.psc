Scriptname SkyrimNet_Whipped_Engine Hidden

; Natives from SkyrimNet_Whipped.dll (SKSE_Source/src/Papyrus_Engine.cpp).
; InjuryEngine: per-actor injury per source (whipping, slashing, ...) for six body parts
; (0 face, 1 left arm, 2 right arm, 3 back, 4 left leg, 5 right leg). All data and text come from
; SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json (see injuries/guide.md); sources are named by id.
; Whip wounds hold health down (90% of max health at full damage); real damage replaces them first;
; healing above the wounded level converts back into points in SyncHealth.

; Hit code: fresh * 1000000 + source * 10000 + part mask * 10 + level
;   fresh: the victim had no open wounds before (1/0); source: index (SourceId); part mask: bit per part;
;   level: worst part state before the hit (0 undamaged, 1 light, 2 medium, 3 severe).

; Whip hit (source id of a model "whip" file, i.e. "whipping") on a random part; takes the matching health
; (never below 10% of max health). Returns a hit code, or -1 for None/dead actors and unknown sources.
Int Function Hit(Actor akActor, String sourceId) global native

; True if any damage is left (any source).
Bool Function HasOpenWounds(Actor akActor) global native

; Applies time to every tracked actor; returns those with parts that healed to a new stage
; since the last TakeHealed.
Actor[] Function TakeHealedActors() global native

; Heal codes for the actor, cleared on take (pass them to HealText):
;   source * 64 + part * 8 + stage   a part reached a new stage (0 light .. 4 mostly healed, 5 healed)
;   10000 + source                   a source has healed on every part it touched
;   20000 + part                     a part has no injury left from any source
Int[] Function TakeHealed(Actor akActor) global native

; Real (non-whip) hits not yet narrated, as hit codes. HitVictims / HitAttackers are parallel to the last result
; (attacker may be None). The DLL sends mod event "SkyrimNet_Whipped_Injury" when one is queued.
Int[] Function TakeHits() global native
Actor[] Function HitVictims() global native
Actor[] Function HitAttackers() global native

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

; Source id by index ("" out of range) and the number of injuries/*.json loaded (0 = none found).
String Function SourceId(Int source) global native
Int Function SourceCount() global native

; Decorator data with text resolved for this actor (empty sources when no injury):
; {"sources":[{"id","looks_heading","feels_intro","feels_outro","parts":[{"part","region","stage","look","feel"}]}]}
String Function GetStateJson(Actor akActor) global native

; Narration text rendered from the JSON files.
String Function HitText(String sourceId, Actor attacker, Actor victim, Int mask, Int level, Int count) global native
String Function HealText(Actor victim, Int[] codes) global native
; kind / source as in SyncHealth; percent is turned into words (no numbers in narration).
String Function SourceHealText(Int kind, Int source, Int percent, Actor caster, Actor target) global native
; Renders the string at a dotted path ("narration.summary.text", "action.no_whip") with {speaker}, {target} and
; {times} (words for count, from narration.summary.times).
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

Function Forget(Actor akActor) global native
