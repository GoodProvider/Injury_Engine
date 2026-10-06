Scriptname SkyrimNet_Whipped_Main extends Quest

; Injuries:
;   The visible welt is an engine impact decal (WEAP -> IPDS -> IPCT -> TXST), copied from
;   PAH Diary Of Mine's padded whip. The engine owns decals (they vanish on cell change or
;   reload), so the wounds the LLM is told about are tracked separately by the C++ InjuryEngine
;   (SkyrimNet_Whipped_Engine natives): per body part and per source (whipping, slashing, ...).
;   All injury data and text live in SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json.

Weapon Property PaddedWhip Auto
ImpactDataSet Property ImpactSet Auto
GlobalVariable Property require_whip_global Auto
Spell Property HealingHands Auto

String Property PLUGIN_CONFIG = "Plugin_SkyrimNet_Whipped" AutoReadOnly
String Property SOURCE_WHIP = "whipping" AutoReadOnly
String Property KEY_LAST_NARRATION = "skyrimnet_whipped.last_narration" AutoReadOnly
String Property KEY_IN_ACTION = "skyrimnet_whipped.in_action" AutoReadOnly
; Hits since the victim's last narration, per source (key + "." + source id): part bitmask, worst hit level, count
String Property KEY_PENDING_PARTS = "skyrimnet_whipped.pending_parts" AutoReadOnly
String Property KEY_PENDING_LEVEL = "skyrimnet_whipped.pending_level" AutoReadOnly
String Property KEY_PENDING_COUNT = "skyrimnet_whipped.pending_count" AutoReadOnly
; Heals since the victim's last heal narration: IntList of heal codes (see SkyrimNet_Whipped_Engine.TakeHealed), count;
; heal_narrated is set once a heal was narrated since the last hit (unset = the next heal is fresh)
String Property KEY_HEAL_PENDING = "skyrimnet_whipped.heal_pending" AutoReadOnly
String Property KEY_HEAL_COUNT = "skyrimnet_whipped.heal_count" AutoReadOnly
String Property KEY_HEAL_NARRATED = "skyrimnet_whipped.heal_narrated" AutoReadOnly

Float Property UNITS_PER_METER = 70.0 AutoReadOnly

; Plugin config (dashboard), refreshed by ApplyPluginConfig. Defaults come from injuries/whipping.json "defaults".
Bool Property require_whip = True Auto Hidden
Bool Property give_whip_to_player = True Auto Hidden
Int Property max_strikes = 10 Auto Hidden
Float Property hours_per_point = 1.0 Auto Hidden
Bool Property give_healing_hands = True Auto Hidden
Float Property regen_rate = 10.0 Auto Hidden
Bool Property narration_enabled = True Auto Hidden
Float Property narration_cooldown = 8.0 Auto Hidden
Float Property narration_max_distance = 15.0 Auto Hidden
Float Property event_ttl = 30.0 Auto Hidden

Bool Property dll_ok = False Auto Hidden

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[SkyrimNet_Whipped_Main] " + func + ": " + msg)
    if notification
        Debug.Notification(msg)
    endif
EndFunction

; Every DirectNarration goes through here so the exact text sent is in the Papyrus log.
Function DirectNarrate(String msg, Actor speaker, Actor target) global
    Trace("DirectNarration", NameOf(speaker) + " -> " + NameOf(target) + ": " + msg)
    SkyrimNetApi.DirectNarration(msg, speaker, target)
EndFunction

SkyrimNet_Whipped_Main Function Get() global
    return Game.GetFormFromFile(0x800, "SkyrimNet_Whipped.esp") as SkyrimNet_Whipped_Main
EndFunction

;----------------------------------------------------------------------------------------------------
; Setup
;----------------------------------------------------------------------------------------------------
Function Setup()
    Trace("Setup", "")
    if !Setup_CheckLinks()
        return
    endif

    UnRegisterForModEvent("SkyrimNet_OnPluginConfigSaved")
    RegisterForModEvent("SkyrimNet_OnPluginConfigSaved", "OnPluginConfigSaved")
    UnRegisterForModEvent("SkyrimNet_Whipped_Injury")
    RegisterForModEvent("SkyrimNet_Whipped_Injury", "OnInjury")

    dll_ok = SkyrimNet_Whipped_Engine.PartName(0) != "" && SkyrimNet_Whipped_Engine.SourceCount() > 0
    if SkyrimNet_Whipped_Engine.PartName(0) == ""
        Trace("Setup", "ERROR: SkyrimNet_Whipped.dll is missing; injuries will not be tracked.", true)
    elseif SkyrimNet_Whipped_Engine.SourceCount() == 0
        Trace("Setup", "ERROR: no SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json found; injuries will not be tracked.", true)
    endif
    ApplyPluginConfig()

    Actor player = Game.GetPlayer()
    int have = player.GetItemCount(PaddedWhip)
    if !give_whip_to_player
        Trace("Setup", "give_whip_to_player is off; player has " + have + " whip(s)")
    elseif have > 0
        Trace("Setup", "player already has " + have + " whip(s); not giving another")
    else
        player.AddItem(PaddedWhip, 1, true)
        Trace("Setup", "gave " + PaddedWhip.GetName() + " to player (now " + player.GetItemCount(PaddedWhip) + ")")
    endif
    GiveHealingHands()

    if dll_ok
        StartSync() ; actors loaded from the co-save
    endif
    SkyrimNetApi.RegisterDecorator("skyrimnet_whipped_injuries", "SkyrimNet_Whipped_Main", "Injuries_Decorator")

    SkyrimNet_Whipped_Actions actions = (self as Quest) as SkyrimNet_Whipped_Actions
    if actions != None
        actions.Setup()
    else
        Trace("Setup", "ERROR: Failed to get SkyrimNet_Whipped_Actions.", true)
    endif
EndFunction

Bool Function Setup_CheckLinks()
    Bool links_ok = true
    if PaddedWhip == None
        Trace("Setup_CheckLinks", "ERROR: PaddedWhip is None", true)
        links_ok = false
    endif
    if ImpactSet == None
        Trace("Setup_CheckLinks", "ERROR: ImpactSet is None", true)
        links_ok = false
    endif
    return links_ok
EndFunction

Event OnPluginConfigSaved(string eventName, string strArg, float numArg, Form sender)
    ApplyPluginConfig()
    GiveHealingHands()
EndEvent

; Turning the setting off never removes the spell (the player may have learned it).
Function GiveHealingHands()
    if !give_healing_hands || HealingHands == None
        return
    endif
    Actor player = Game.GetPlayer()
    if !player.HasSpell(HealingHands)
        player.AddSpell(HealingHands, false)
        Trace("GiveHealingHands", "gave " + HealingHands.GetName() + " to player")
    endif
EndFunction

; injuries/whipping.json "defaults" for a setting the dashboard has not set yet.
Float Function DefaultNumber(String setting, Float fallback)
    return SkyrimNet_Whipped_Engine.Number(SOURCE_WHIP, "defaults." + setting, fallback)
EndFunction

Bool Function DefaultBool(String setting)
    return SkyrimNet_Whipped_Engine.Number(SOURCE_WHIP, "defaults." + setting, 1.0) > 0.5
EndFunction

Function ApplyPluginConfig()
    require_whip = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.action.require_whip", DefaultBool("require_whip"))
    give_whip_to_player = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.action.give_to_player", DefaultBool("give_to_player"))
    max_strikes = SkyrimNetApi.GetConfigInt(PLUGIN_CONFIG, "whip.action.max_strikes", DefaultNumber("max_strikes", 10.0) as int)
    hours_per_point = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.hours_per_point", DefaultNumber("hours_per_point", 1.0))
    give_healing_hands = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.healing.give_healing_hands", DefaultBool("give_healing_hands"))
    regen_rate = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.regen_rate", DefaultNumber("regen_rate", 10.0))
    narration_enabled = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.narration.enabled", DefaultBool("narration_enabled"))
    narration_cooldown = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.cooldown", DefaultNumber("narration_cooldown", 8.0))
    narration_max_distance = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.max_distance", DefaultNumber("narration_max_distance", 15.0))
    event_ttl = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.event_ttl", DefaultNumber("event_ttl", 30.0))
    Bool show_overlay = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.overlay.enabled", DefaultBool("overlay_enabled"))
    if max_strikes < 1
        max_strikes = 1
    endif
    SkyrimNet_Whipped_Engine.SetHoursPerPoint(hours_per_point)
    SkyrimNet_Whipped_Engine.SetRegenRate(regen_rate)
    SkyrimNet_Whipped_Engine.SetOverlayEnabled(show_overlay)
    RegisterForSingleUpdateGameTime(hours_per_point) ; healing poll follows the heal rate
    if require_whip_global
        require_whip_global.SetValue(require_whip as Float)
    endif
    Trace("ApplyPluginConfig", "require_whip:" + require_whip + " give_whip_to_player:" + give_whip_to_player + " max_strikes:" + max_strikes + " hours_per_point:" + hours_per_point \
        + " give_healing_hands:" + give_healing_hands + " regen_rate:" + regen_rate \
        + " narration:" + narration_enabled + " cooldown:" + narration_cooldown + " max_distance:" + narration_max_distance + " event_ttl:" + event_ttl + " overlay:" + show_overlay)
EndFunction

;----------------------------------------------------------------------------------------------------
; Lashes
;----------------------------------------------------------------------------------------------------

; Scripted strike (LLM action): same decal and sound as a real weapon hit, then OnLash.
; Returns OnLash's hit code.
Int Function Lash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None
        return -1
    endif
    String node = "NPC Spine2 [Spn2]"
    int nodes = SkyrimNet_Whipped_Engine.TextCount(SOURCE_WHIP, "action.impact_nodes")
    if nodes > 0
        node = SkyrimNet_Whipped_Engine.Text(SOURCE_WHIP, "action.impact_nodes." + Utility.RandomInt(0, nodes - 1))
    endif
    ; Pick from the attacker's side toward the victim so the decal lands on the facing skin.
    float dx = 0.0
    float dy = 0.0
    if attacker != None
        dx = victim.GetPositionX() - attacker.GetPositionX()
        dy = victim.GetPositionY() - attacker.GetPositionY()
        float len = Math.sqrt(dx * dx + dy * dy)
        if len > 0.0
            dx = dx / len
            dy = dy / len
        endif
    endif
    ; The flesh impact (IPCT) carries the hit sound.
    victim.PlayImpactEffect(ImpactSet, node, dx, dy, 0.0, 128.0)
    return OnLash(attacker, victim, narrate)
EndFunction

; Every strike ends here: the engine damages a random body part, then narrate.
; Returns a hit code (see SkyrimNet_Whipped_Engine), or -1.
Int Function OnLash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None || victim.IsDead()
        return -1
    endif
    int code = SkyrimNet_Whipped_Engine.Hit(victim, SOURCE_WHIP)
    if code < 0
        return code
    endif
    Trace("OnLash", NameOf(attacker) + " -> " + victim.GetDisplayName() + " part:" + PartsOf(code) + " level:" + LevelOf(code) + " fresh:" + FreshOf(code))
    if narrate
        NarrateInjury(attacker, victim, code)
    endif
    ClearHealBatch(victim) ; pending heals are stale; the next heal is fresh
    StartSync()
    return code
EndFunction

; Hit code fields (see SkyrimNet_Whipped_Engine).
Bool Function FreshOf(int code) global
    return code / 1000000 > 0
EndFunction

String Function SourceOfCode(int code) global
    return SkyrimNet_Whipped_Engine.SourceId((code / 10000) % 100)
EndFunction

int Function MaskOf(int code) global
    return (code % 10000) / 10
EndFunction

int Function LevelOf(int code) global
    return code % 10
EndFunction

String Function PartsOf(int code) global
    return PartsText(MaskOf(code))
EndFunction

String Function BatchKey(String base, String source) global
    return base + "." + source
EndFunction

; Hits are batched per victim and source (BatchKey(KEY_PENDING_*, source)):
;   - fresh (no open wounds before this hit): always narrated; an event when narration is off or too far
;   - later hits: when the speech queue is empty and the cooldown has passed, one narration of every
;     part hit since the last narration at the worst level; otherwise a short-lived event per hit
Function NarrateInjury(Actor attacker, Actor victim, int code)
    String source = SourceOfCode(code)
    Bool fresh = FreshOf(code)
    int part_mask = MaskOf(code)
    int level = LevelOf(code)
    if fresh
        ClearBatch(victim, source)
    endif
    int mask = Math.LogicalOr(StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source), 0), part_mask)
    int worst = StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source), -1)
    if level > worst
        worst = level
    endif
    int count = StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source), 0) + 1
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source), mask)
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source), worst)
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source), count)

    String event_type = "injury_hit"
    if source == SOURCE_WHIP
        event_type = "whip_lash"
    endif
    Bool narrate = narration_enabled && !TooFar(victim)
    if fresh || (narrate && NarrationAllowed(victim))
        String msg = SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, mask, worst, count)
        ClearBatch(victim, source)
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        if msg == ""
            return
        endif
        if narrate
            Trace("NarrateInjury", source + " narration fresh:" + fresh + " hits:" + count)
            DirectNarrate(msg, attacker, victim)
        else
            Trace("NarrateInjury", source + " event fresh:" + fresh)
            SkyrimNetApi.RegisterEvent(event_type, msg, attacker, victim)
        endif
        return
    endif
    Trace("NarrateInjury", source + " short-lived event, pending hits:" + count)
    SkyrimNetApi.RegisterShortLivedEvent(event_type + "_" + source + "_" + victim.GetFormID() + "_" + count, event_type, \
        SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, part_mask, level, 1), "", (event_ttl * 1000.0) as int, attacker, victim)
EndFunction

Function ClearBatch(Actor victim, String source)
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source))
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source))
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source))
EndFunction

Bool Function TooFar(Actor akActor)
    return !akActor.Is3DLoaded() || Game.GetPlayer().GetDistance(akActor) > narration_max_distance * UNITS_PER_METER
EndFunction

; Speech queue empty and the victim's narration cooldown has passed.
Bool Function NarrationAllowed(Actor victim)
    if SkyrimNetApi.GetSpeechQueueSize() > 0
        return false
    endif
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_NARRATION, -1000.0)
    ; real time restarts each session, so a "last" in the future is stale
    return !(last <= now && (now - last) < narration_cooldown)
EndFunction

; Part names for a bitmask, e.g. "face, back and left arm" (for the Papyrus log only; narration text comes from the DLL).
String Function PartsText(int mask) global
    String text = ""
    int i = 0
    while i < 6
        if Math.LogicalAnd(mask, Math.LeftShift(1, i)) != 0
            if text != ""
                text += ","
            endif
            text += SkyrimNet_Whipped_Engine.PartName(i)
        endif
        i += 1
    endwhile
    return text
EndFunction

; One whip hit by hit code. Used by the Whip_Target summary.
String Function LashText(Actor attacker, Actor victim, int code)
    return SkyrimNet_Whipped_Engine.HitText(SOURCE_WHIP, attacker, victim, MaskOf(code), LevelOf(code), 1)
EndFunction

;----------------------------------------------------------------------------------------------------
; Healing: parts that healed to a new stage (game time, or health converted by SyncHealth) are taken
; every hours_per_point game hours and on every sync tick, and narrated like hits (NarrateHealing).
;----------------------------------------------------------------------------------------------------
Event OnUpdateGameTime()
    if !dll_ok
        return
    endif
    NarrateHealedActors()
    if SkyrimNet_Whipped_Engine.TrackedCount() > 0
        StartSync() ; flush fully-healed events queued while applying time
    endif
    RegisterForSingleUpdateGameTime(hours_per_point)
EndEvent

Function NarrateHealedActors()
    Actor[] healed = SkyrimNet_Whipped_Engine.TakeHealedActors()
    int i = 0
    while i < healed.Length
        NarrateHealing(healed[i])
        i += 1
    endwhile
EndFunction

; Heals are batched per victim (KEY_HEAL_*), mirroring NarrateInjury:
;   - fresh (no heal narrated since the last hit): always narrated; an event when narration is off or too far
;   - later heals: when the speech queue is empty and the cooldown has passed, one narration of everything
;     healed since the last narration (latest stage per part); otherwise a short-lived event per take
Function NarrateHealing(Actor victim)
    if victim == None
        return
    endif
    int[] codes = SkyrimNet_Whipped_Engine.TakeHealed(victim)
    if codes.Length == 0 || victim.IsDead()
        return
    endif
    Bool fresh = StorageUtil.GetIntValue(victim, KEY_HEAL_NARRATED, 0) == 0
    int i = 0
    while i < codes.Length
        StorageUtil.IntListAdd(victim, KEY_HEAL_PENDING, codes[i])
        i += 1
    endwhile
    int count = StorageUtil.GetIntValue(victim, KEY_HEAL_COUNT, 0) + 1
    StorageUtil.SetIntValue(victim, KEY_HEAL_COUNT, count)

    Bool narrate = narration_enabled && !TooFar(victim)
    if fresh || (narrate && NarrationAllowed(victim))
        String msg = SkyrimNet_Whipped_Engine.HealText(victim, StorageUtil.IntListToArray(victim, KEY_HEAL_PENDING))
        StorageUtil.IntListClear(victim, KEY_HEAL_PENDING)
        StorageUtil.UnsetIntValue(victim, KEY_HEAL_COUNT)
        if msg == ""
            return
        endif
        StorageUtil.SetIntValue(victim, KEY_HEAL_NARRATED, 1)
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        if narrate
            Trace("NarrateHealing", victim.GetDisplayName() + " narration fresh:" + fresh + " takes:" + count)
            DirectNarrate(msg, victim, None)
        else
            Trace("NarrateHealing", victim.GetDisplayName() + " event fresh:" + fresh + ": " + msg)
            SkyrimNetApi.RegisterEvent("whip_heal", msg, victim, None)
        endif
        return
    endif
    String take = SkyrimNet_Whipped_Engine.HealText(victim, codes)
    if take == ""
        return
    endif
    Trace("NarrateHealing", victim.GetDisplayName() + " short-lived event, pending takes:" + count + ": " + take)
    SkyrimNetApi.RegisterShortLivedEvent("whip_heal_" + victim.GetFormID() + "_" + count, "whip_heal", \
        take, "", (event_ttl * 1000.0) as int, victim, None)
EndFunction

Function ClearHealBatch(Actor victim)
    StorageUtil.IntListClear(victim, KEY_HEAL_PENDING)
    StorageUtil.UnsetIntValue(victim, KEY_HEAL_COUNT)
    StorageUtil.UnsetIntValue(victim, KEY_HEAL_NARRATED)
EndFunction

;----------------------------------------------------------------------------------------------------
; Health sync: wounds hold health down, healing above that converts back into points
; (SkyrimNet_Whipped_Engine.SyncHealth). Runs every 0.2 s while any actor is tracked.
; Real (non-whip) damage reaches Papyrus as queued hits (TakeHits) plus the mod event
; "SkyrimNet_Whipped_Injury", which starts this loop.
;----------------------------------------------------------------------------------------------------
Int Property HEAL_START = 0 AutoReadOnly
Int Property HEAL_PAIN_GONE = 1 AutoReadOnly
Int Property HEAL_FULL = 2 AutoReadOnly
Int Property HEAL_STOPPED = 3 AutoReadOnly

Float Property SYNC_INTERVAL = 0.2 AutoReadOnly

; Replaces any pending registration, so calling it again never doubles the loop.
Function StartSync()
    if dll_ok
        RegisterForSingleUpdate(SYNC_INTERVAL)
    endif
EndFunction

Event OnInjury(string eventName, string strArg, float numArg, Form sender)
    StartSync()
EndEvent

Event OnUpdate()
    NarrateRealHits()
    int[] codes = SkyrimNet_Whipped_Engine.SyncHealth()
    if codes.Length > 0
        Actor[] targets = SkyrimNet_Whipped_Engine.SyncActors()
        Actor[] casters = SkyrimNet_Whipped_Engine.SyncCasters()
        int i = 0
        while i < codes.Length
            int code = codes[i]
            NarrateSourceHeal(code / 10000, (code / 1000) % 10, code % 1000, casters[i], targets[i])
            i += 1
        endwhile
    endif
    NarrateHealedActors() ; parts healed by health conversion this tick
    if SkyrimNet_Whipped_Engine.TrackedCount() > 0
        RegisterForSingleUpdate(SYNC_INTERVAL)
    endif
EndEvent

; Real (non-whip) hits queued by the damage hooks: narrate like lashes.
Function NarrateRealHits()
    int[] codes = SkyrimNet_Whipped_Engine.TakeHits()
    if codes.Length == 0
        return
    endif
    Actor[] victims = SkyrimNet_Whipped_Engine.HitVictims()
    Actor[] attackers = SkyrimNet_Whipped_Engine.HitAttackers()
    int i = 0
    while i < codes.Length
        if victims[i] != None
            Trace("NarrateRealHits", NameOf(attackers[i]) + " -> " + NameOf(victims[i]) + " " + SourceOfCode(codes[i]) + " part:" + PartsOf(codes[i]) \
                + " level:" + LevelOf(codes[i]) + " fresh:" + FreshOf(codes[i]))
            NarrateInjury(attackers[i], victims[i], codes[i])
            ClearHealBatch(victims[i])
        endif
        i += 1
    endwhile
EndFunction

; Fully healed (the last healing narration) is always a DirectNarration while narration is on;
; start, pain gone and stopped go through the usual gate, otherwise an event.
; The healed actor speaks, toward the caster when there is one.
Function NarrateSourceHeal(int kind, int source, int percent, Actor caster, Actor target)
    if target == None
        return
    endif
    String msg = SkyrimNet_Whipped_Engine.SourceHealText(kind, source, percent, caster, target)
    if msg == ""
        return
    endif
    Actor toward = None
    if caster != target
        toward = caster
    endif
    if kind == HEAL_FULL
        ClearHealBatch(target) ; this narration covers the parts still pending
    endif
    if narration_enabled && (kind == HEAL_FULL || (!TooFar(target) && NarrationAllowed(target)))
        Trace("NarrateSourceHeal", "narration kind:" + kind + ": " + msg)
        StorageUtil.SetFloatValue(target, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        DirectNarrate(msg, target, toward)
    else
        Trace("NarrateSourceHeal", "event: " + msg)
        SkyrimNetApi.RegisterEvent("whip_heal", msg, target, toward)
    endif
EndFunction

String Function NameOf(Actor akActor) global
    if akActor == None
        return "Someone"
    endif
    return akActor.GetDisplayName()
EndFunction

;----------------------------------------------------------------------------------------------------
; Decorator skyrimnet_whipped_injuries(uuid): SkyrimNet_Whipped_Engine.GetStateJson
;   {"sources":[{"id","looks_heading","feels_intro","feels_outro","parts":[{"part","region","stage","look","feel"}]}]}
;----------------------------------------------------------------------------------------------------
String Function Injuries_Decorator(Actor akActor) global
    if akActor == None
        return "{\"sources\":[]}"
    endif
    return SkyrimNet_Whipped_Engine.GetStateJson(akActor)
EndFunction
