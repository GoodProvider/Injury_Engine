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
; Flames / Frostbite / Sparks (torture): fire / frost / shock torture wounds instead of real damage
Spell Property TortureFlames Auto
Spell Property TortureFrostbite Auto
Spell Property TortureSparks Auto

String Property PLUGIN_CONFIG = "Plugin_SkyrimNet_Whipped" AutoReadOnly
String Property SOURCE_WHIP = "whipping" AutoReadOnly
String Property KEY_LAST_NARRATION = "skyrimnet_whipped.last_narration" AutoReadOnly
; Real time of the victim's last torture hit (any torture type), for the torture narration window / interval
String Property KEY_LAST_TORTURE = "skyrimnet_whipped.last_torture" AutoReadOnly
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
Bool Property give_gear = True Auto Hidden
Int Property max_strikes = 10 Auto Hidden
Float Property hours_per_point = 1.0 Auto Hidden
Bool Property give_healing_hands = True Auto Hidden
Float Property regen_rate = 10.0 Auto Hidden
Bool Property narration_enabled = True Auto Hidden
Float Property narration_cooldown = 8.0 Auto Hidden
Float Property torture_narration_window_size = 30.0 Auto Hidden
Float Property torture_narration_interval = 6.0 Auto Hidden
Float Property narration_max_distance = 15.0 Auto Hidden
Float Property event_ttl = 30.0 Auto Hidden
Bool Property include_sexual_parts = False Auto Hidden
Bool Property verbose_logging = False Auto Hidden

Bool Property dll_ok = False Auto Hidden

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[SkyrimNet_Whipped_Main] " + func + ": " + msg)
    if notification
        Debug.Notification(msg)
    endif
EndFunction

; "Verbose logging" only. Papyrus builds the arguments even when this returns early, so wrap calls whose
; message is costly (natives, distance, loops) in "if verbose_logging".
Function Verbose(String func, String msg)
    if verbose_logging
        Trace(func, "[v] " + msg)
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

    GiveGear()
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
    GiveGear()
    GiveHealingHands()
EndEvent

; "Add gear to player": the padded whip and the torture spells, each only if the player lacks it.
; Turning the setting off never takes anything away.
Function GiveGear()
    Actor player = Game.GetPlayer()
    int have = player.GetItemCount(PaddedWhip)
    if !give_gear
        Trace("GiveGear", "give_gear is off; player has " + have + " whip(s)")
        return
    endif
    if have > 0
        Trace("GiveGear", "player already has " + have + " whip(s); not giving another")
    else
        player.AddItem(PaddedWhip, 1, true)
        Trace("GiveGear", "gave " + PaddedWhip.GetName() + " to player (now " + player.GetItemCount(PaddedWhip) + ")")
    endif
    GiveSpell(player, TortureFlames)
    GiveSpell(player, TortureFrostbite)
    GiveSpell(player, TortureSparks)
EndFunction

Function GiveSpell(Actor player, Spell sp)
    if sp == None
        Trace("GiveSpell", "ERROR: torture spell property is None")
    elseif !player.HasSpell(sp)
        player.AddSpell(sp, false)
        Trace("GiveSpell", "gave " + sp.GetName() + " to player")
    endif
EndFunction

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
    give_gear = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.action.give_to_player", DefaultBool("give_to_player"))
    max_strikes = SkyrimNetApi.GetConfigInt(PLUGIN_CONFIG, "whip.action.max_strikes", DefaultNumber("max_strikes", 10.0) as int)
    hours_per_point = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.hours_per_point", DefaultNumber("hours_per_point", 1.0))
    give_healing_hands = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.healing.give_healing_hands", DefaultBool("give_healing_hands"))
    regen_rate = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.regen_rate", DefaultNumber("regen_rate", 10.0))
    narration_enabled = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.narration.enabled", DefaultBool("narration_enabled"))
    narration_cooldown = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.cooldown", DefaultNumber("narration_cooldown", 8.0))
    torture_narration_window_size = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.torture_narration_window_size", DefaultNumber("torture_narration_window_size", 30.0))
    torture_narration_interval = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.torture_narration_interval", DefaultNumber("torture_narration_interval", 6.0))
    narration_max_distance = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.max_distance", DefaultNumber("narration_max_distance", 15.0))
    event_ttl = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.event_ttl", DefaultNumber("event_ttl", 30.0))
    include_sexual_parts = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.body.include_sexual_parts", DefaultNumber("include_sexual_parts", 0.0) > 0.5)
    Bool show_overlay = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.overlay.enabled", DefaultBool("overlay_enabled"))
    verbose_logging = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.debug.verbose_logging", false)
    if max_strikes < 1
        max_strikes = 1
    endif
    if torture_narration_interval > torture_narration_window_size
        torture_narration_interval = torture_narration_window_size
    endif
    SkyrimNet_Whipped_Engine.SetHoursPerPoint(hours_per_point)
    SkyrimNet_Whipped_Engine.SetRegenRate(regen_rate)
    SkyrimNet_Whipped_Engine.SetIncludeSexualParts(include_sexual_parts)
    SkyrimNet_Whipped_Engine.SetOverlayEnabled(show_overlay)
    SkyrimNet_Whipped_Engine.SetVerboseLogging(verbose_logging)
    RegisterForSingleUpdateGameTime(hours_per_point) ; healing poll follows the heal rate
    if require_whip_global
        require_whip_global.SetValue(require_whip as Float)
    endif
    Trace("ApplyPluginConfig", "require_whip:" + require_whip + " give_gear:" + give_gear + " max_strikes:" + max_strikes + " hours_per_point:" + hours_per_point \
        + " give_healing_hands:" + give_healing_hands + " regen_rate:" + regen_rate \
        + " narration:" + narration_enabled + " cooldown:" + narration_cooldown + " torture_window:" + torture_narration_window_size + " torture_interval:" + torture_narration_interval + " max_distance:" + narration_max_distance + " event_ttl:" + event_ttl + " sexual_parts:" + include_sexual_parts + " overlay:" + show_overlay + " verbose:" + verbose_logging)
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
    if verbose_logging
        Verbose("Lash", NameOf(attacker) + " -> " + victim.GetDisplayName() + " node:" + node + " dir:" + dx + "," + dy \
            + " distance:" + DistanceText(attacker, victim))
    endif
    victim.PlayImpactEffect(ImpactSet, node, dx, dy, 0.0, 128.0)
    return OnLash(attacker, victim, narrate)
EndFunction

; Every strike ends here: the engine damages a random body part, then narrate.
; Returns a hit code (see SkyrimNet_Whipped_Engine), or -1.
Int Function OnLash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None || victim.IsDead()
        return -1
    endif
    float health_before = 0.0
    if verbose_logging
        health_before = victim.GetActorValue("Health")
    endif
    int code = SkyrimNet_Whipped_Engine.Hit(victim, SOURCE_WHIP)
    if code < 0
        Verbose("OnLash", "Hit returned " + code + " for " + victim.GetDisplayName())
        return code
    endif
    if verbose_logging
        Verbose("OnLash", victim.GetDisplayName() + " code:" + code + " health " + health_before + " -> " + victim.GetActorValue("Health") \
            + " / " + victim.GetBaseActorValue("Health") + " narrate:" + narrate)
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
    return code / 100000000 > 0
EndFunction

String Function SourceOfCode(int code) global
    return SkyrimNet_Whipped_Engine.SourceId((code / 1000000) % 100)
EndFunction

int Function MaskOf(int code) global
    return (code % 1000000) / 10
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

; Real-damage hits are a short-lived event each: never narrated, no batch, no cooldown change.
; Torture hits (whipping, slapping, ...) are batched per victim and type (BatchKey(KEY_PENDING_*, source)):
;   - fresh (no open wounds before this hit): always narrated; an event when narration is off or too far
;   - later hits, by the time since the victim's last torture hit (KEY_LAST_TORTURE):
;     - over torture_narration_window_size: a new session; the batch restarts and this hit is narrated
;     - between torture_narration_interval and the window: one narration of every part hit since the
;       last narration at the worst level
;     - under the interval: a short-lived event per hit, kept in the batch
;     A narration is optional: when narration is off, too far or the speech queue is not empty it becomes a
;     short-lived event (the batch is kept, except for a new session).
Function NarrateInjury(Actor attacker, Actor victim, int code)
    NarrateHits(attacker, victim, SourceOfCode(code), FreshOf(code), MaskOf(code), LevelOf(code), 1, "")
EndFunction

; NarrateInjury for hits hits of source at once (mask: parts hit, level: the worst; fresh: no open wounds before
; the first). cause ("" = none) leads every narration or event sent and replaces our narration.hit.by_* text.
Function NarrateHits(Actor attacker, Actor victim, String source, Bool fresh, int part_mask, int level, int hits, String cause)
    Bool noCause = cause != ""
    String lead = CauseText(cause)
    if !SkyrimNet_Whipped_Engine.IsTorture(source)
        String hit = lead + SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, part_mask, level, hits, noCause)
        if hit != ""
            Trace("NarrateInjury", source + " short-lived event: " + hit)
            SkyrimNetApi.RegisterShortLivedEvent("injury_hit_" + source + "_" + victim.GetFormID() + "_" + NowMs(), "injury_hit",                 hit, "", (event_ttl * 1000.0) as int, attacker, victim)
        endif
        return
    endif
    float since = SinceLastTorture(victim)
    StorageUtil.SetFloatValue(victim, KEY_LAST_TORTURE, Utility.GetCurrentRealTime())
    Bool new_session = since > torture_narration_window_size
    if fresh || new_session
        ClearBatch(victim, source)
    endif
    int mask = Math.LogicalOr(StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source), 0), part_mask)
    int worst = StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source), -1)
    if level > worst
        worst = level
    endif
    int count = StorageUtil.GetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source), 0) + hits
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source), mask)
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source), worst)
    StorageUtil.SetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source), count)

    String event_type = "torture_hit"
    if source == SOURCE_WHIP
        event_type = "whip_lash"
    endif
    Bool narrate = narration_enabled && !TooFar(victim)
    if verbose_logging
        Verbose("NarrateInjury", source + " " + victim.GetDisplayName() + " fresh:" + fresh + " mask:" + mask + " worst:" + worst             + " count:" + count + " since_torture:" + since + " interval:" + torture_narration_interval             + " window:" + torture_narration_window_size + " " + GateText(victim))
    endif
    ; the speech queue gates every narration except the fresh one
    Bool narrate_now = narrate && (fresh || (since >= torture_narration_interval && SkyrimNetApi.GetSpeechQueueSize() == 0))
    if fresh || narrate_now || new_session
        String msg = lead + SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, mask, worst, count, noCause)
        ClearBatch(victim, source)
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        if msg == ""
            return
        endif
        if narrate_now
            Trace("NarrateInjury", source + " narration fresh:" + fresh + " new_session:" + new_session + " hits:" + count)
            DirectNarrate(msg, attacker, victim)
        else
            Trace("NarrateInjury", source + " event fresh:" + fresh + " new_session:" + new_session)
            SkyrimNetApi.RegisterShortLivedEvent(event_type + "_" + source + "_" + victim.GetFormID() + "_" + NowMs(), event_type,                 msg, "", (event_ttl * 1000.0) as int, attacker, victim)
        endif
        return
    endif
    Trace("NarrateInjury", source + " short-lived event, pending hits:" + count)
    SkyrimNetApi.RegisterShortLivedEvent(event_type + "_" + source + "_" + victim.GetFormID() + "_" + count, event_type,         lead + SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, part_mask, level, hits, noCause), "", (event_ttl * 1000.0) as int, attacker, victim)
EndFunction

; A caller's cause as the start of a narration: "" stays "", otherwise it ends in a sentence stop and a space.
String Function CauseText(String cause) global
    if cause == ""
        return ""
    endif
    String last = StringUtil.Substring(cause, StringUtil.GetLength(cause) - 1, 1)
    if last != "." && last != "!" && last != "?"
        cause += "."
    endif
    return cause + " "
EndFunction

; Real time in ms, to make short-lived event ids unique.
int Function NowMs() global
    return (Utility.GetCurrentRealTime() * 1000.0) as int
EndFunction

Function ClearBatch(Actor victim, String source)
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_PARTS, source))
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_LEVEL, source))
    StorageUtil.UnsetIntValue(victim, BatchKey(KEY_PENDING_COUNT, source))
EndFunction

Bool Function TooFar(Actor akActor)
    return !akActor.Is3DLoaded() || Game.GetPlayer().GetDistance(akActor) > narration_max_distance * UNITS_PER_METER
EndFunction

; Seconds since the victim's last torture hit; never hit (or a stale time from an earlier session) is long ago.
float Function SinceLastTorture(Actor victim)
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_TORTURE, -1.0)
    if last < 0.0 || last > now
        return 100000.0
    endif
    return now - last
EndFunction

; Speech queue empty and the victim's narration cooldown has passed (healing; torture hits use the
; torture narration window / interval, see NarrateInjury).
Bool Function NarrationAllowed(Actor victim)
    if SkyrimNetApi.GetSpeechQueueSize() > 0
        return false
    endif
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_NARRATION, -1000.0)
    ; real time restarts each session, so a "last" in the future is stale
    return !(last <= now && (now - last) < narration_cooldown)
EndFunction

; Verbose log: why narration is or is not allowed for this actor.
String Function GateText(Actor victim)
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_NARRATION, -1000.0)
    return "narration:" + narration_enabled + " loaded:" + victim.Is3DLoaded() \
        + " distance_m:" + (Game.GetPlayer().GetDistance(victim) / UNITS_PER_METER) + "/" + narration_max_distance \
        + " queue:" + SkyrimNetApi.GetSpeechQueueSize() + " since_last:" + (now - last) + "/" + narration_cooldown \
        + " too_far:" + TooFar(victim) + " allowed:" + NarrationAllowed(victim)
EndFunction

; Verbose log: attacker to victim distance in meters.
String Function DistanceText(Actor attacker, Actor victim)
    if attacker == None
        return "no attacker"
    endif
    return (victim.GetDistance(attacker) / UNITS_PER_METER) + "m"
EndFunction

; Part names for a bitmask, e.g. "face, back and left arm" (for the Papyrus log only; narration text comes from the DLL).
String Function PartsText(int mask) global
    String text = ""
    int i = 0
    while i < 11
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

; One torture hit by hit code (the type is in the code). Used by the Torture_Target summary.
String Function LashText(Actor attacker, Actor victim, int code)
    return SkyrimNet_Whipped_Engine.HitText(SourceOfCode(code), attacker, victim, MaskOf(code), LevelOf(code), 1)
EndFunction

; One scripted torture hit of type source (injuries/<source>.json, model "torture"). Whipping is a Lash
; (decal + sound); other types play the type's action.animation event when it has one. Returns the hit code, or -1.
Int Function Torture(Actor attacker, Actor victim, String source, Bool narrate=True)
    if source == SOURCE_WHIP
        return Lash(attacker, victim, narrate)
    endif
    if victim == None || victim.IsDead() || !SkyrimNet_Whipped_Engine.IsTorture(source)
        return -1
    endif
    String anim = SkyrimNet_Whipped_Engine.Text(source, "action.animation")
    if anim != "" && attacker != None
        Debug.SendAnimationEvent(attacker, anim)
    endif
    int code = SkyrimNet_Whipped_Engine.Hit(victim, source)
    if code < 0
        Verbose("Torture", source + " Hit returned " + code + " for " + victim.GetDisplayName())
        return code
    endif
    Trace("Torture", NameOf(attacker) + " -> " + victim.GetDisplayName() + " " + source + " part:" + PartsOf(code) + " level:" + LevelOf(code) + " fresh:" + FreshOf(code))
    if narrate
        NarrateInjury(attacker, victim, code)
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
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
    int tracked = SkyrimNet_Whipped_Engine.TrackedCount()
    Verbose("OnUpdateGameTime", "tracked:" + tracked + " next in " + hours_per_point + " game hours")
    if tracked > 0
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

; Passive heals (game time, natural regen; code >= HEAL_PASSIVE) are silent: one short-lived event per take,
; text marked as ordinary healing, nobody is made to speak and the batch below is untouched.
; Heals from a healing source are batched per victim (KEY_HEAL_*), mirroring NarrateInjury:
;   - fresh (no heal narrated since the last hit): always narrated; an event when narration is off or too far
;   - later heals: when the speech queue is empty and the cooldown has passed, one narration of everything
;     healed since the last narration (latest stage per part); otherwise a short-lived event per take
Function NarrateHealing(Actor victim)
    if victim == None
        return
    endif
    int[] taken = SkyrimNet_Whipped_Engine.TakeHealed(victim)
    if verbose_logging
        Verbose("NarrateHealing", victim.GetDisplayName() + " taken:" + taken + " dead:" + victim.IsDead())
    endif
    if taken.Length == 0 || victim.IsDead()
        return
    endif
    int passive_count = 0
    int i = 0
    while i < taken.Length
        if taken[i] >= HEAL_PASSIVE
            passive_count += 1
        endif
        i += 1
    endwhile
    int[] passive = Utility.CreateIntArray(passive_count)
    int[] codes = Utility.CreateIntArray(taken.Length - passive_count)
    int p = 0
    int a = 0
    i = 0
    while i < taken.Length
        if taken[i] >= HEAL_PASSIVE
            passive[p] = taken[i]
            p += 1
        else
            codes[a] = taken[i]
            a += 1
        endif
        i += 1
    endwhile
    if passive_count > 0
        String quiet = SkyrimNet_Whipped_Engine.HealText(victim, passive)
        if quiet != ""
            Trace("NarrateHealing", victim.GetDisplayName() + " passive event: " + quiet)
            SkyrimNetApi.RegisterShortLivedEvent("whip_heal_passive_" + victim.GetFormID() + "_" + NowMs(), "whip_heal", \
                quiet, "", (event_ttl * 1000.0) as int, victim, None)
        endif
    endif
    if codes.Length == 0
        return
    endif

    Bool fresh = StorageUtil.GetIntValue(victim, KEY_HEAL_NARRATED, 0) == 0
    i = 0
    while i < codes.Length
        StorageUtil.IntListAdd(victim, KEY_HEAL_PENDING, codes[i])
        i += 1
    endwhile
    int count = StorageUtil.GetIntValue(victim, KEY_HEAL_COUNT, 0) + 1
    StorageUtil.SetIntValue(victim, KEY_HEAL_COUNT, count)

    Bool narrate = narration_enabled && !TooFar(victim)
    if verbose_logging
        Verbose("NarrateHealing", victim.GetDisplayName() + " fresh:" + fresh + " takes:" + count + " " + GateText(victim))
    endif
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
            SkyrimNetApi.RegisterShortLivedEvent("whip_heal_" + victim.GetFormID() + "_" + NowMs(), "whip_heal", \
                msg, "", (event_ttl * 1000.0) as int, victim, None)
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
Int Property HEAL_SOURCE_NONE = 0 AutoReadOnly ; SyncHealth source: no healing source (natural healing)
Int Property HEAL_PASSIVE = 100000 AutoReadOnly ; added to TakeHealed codes healed without a source

Float Property SYNC_INTERVAL = 0.2 AutoReadOnly

; Replaces any pending registration, so calling it again never doubles the loop.
Function StartSync()
    if dll_ok
        RegisterForSingleUpdate(SYNC_INTERVAL)
    endif
EndFunction

Event OnInjury(string eventName, string strArg, float numArg, Form sender)
    Verbose("OnInjury", "real hit queued, starting sync")
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
    else
        Verbose("OnUpdate", "nothing tracked, sync loop stops")
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

; Natural healing (no source: pain gone / fully healed) is always a silent short-lived event, text marked as ordinary healing.
; With a source, fully healed (the last healing narration) is always a DirectNarration while narration is on;
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
    if verbose_logging
        Verbose("NarrateSourceHeal", target.GetDisplayName() + " kind:" + kind + " source:" + source + " percent:" + percent \
            + " caster:" + NameOf(caster) + " " + GateText(target))
    endif
    if source == HEAL_SOURCE_NONE
        Trace("NarrateSourceHeal", "passive event kind:" + kind + ": " + msg)
        SkyrimNetApi.RegisterShortLivedEvent("whip_heal_passive_" + target.GetFormID() + "_" + kind + "_" + NowMs(), "whip_heal", \
            msg, "", (event_ttl * 1000.0) as int, target, toward)
    elseif narration_enabled && (kind == HEAL_FULL || (!TooFar(target) && NarrationAllowed(target)))
        Trace("NarrateSourceHeal", "narration kind:" + kind + ": " + msg)
        StorageUtil.SetFloatValue(target, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        DirectNarrate(msg, target, toward)
    else
        Trace("NarrateSourceHeal", "event: " + msg)
        SkyrimNetApi.RegisterShortLivedEvent("whip_heal_" + target.GetFormID() + "_" + kind + "_" + NowMs(), "whip_heal", \
            msg, "", (event_ttl * 1000.0) as int, target, toward)
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

;----------------------------------------------------------------------------------------------------
; External API (soft dependency, e.g. SkyrimNet_SexLab Force / Aid). Call the globals; no quest lookup needed.
;----------------------------------------------------------------------------------------------------

; Preferred damage API. Always applies damage_amount hits of damage_type (injuries/<type>.json; a torture type,
; or real damage for a health-only type) on body_part (part name or region, "" = random; ignored for real damage).
; narrate True: the usual torture narration rules (NarrateInjury: a narration or a short-lived event), with cause
;   ("Bob yanks Nina off her feet") leading the text in place of our own "as Bob ..." / "by the fall".
;   Real damage is narrated as any real hit; cause alone becomes a short-lived event.
; narrate False: silent (no narration, no event).
; Returns the last hit code (torture) or the source index (real damage), or -1.
Int Function External_Damage(Actor attacker, Actor victim, String cause = "", String body_part = "", String damage_type = "whipping", Int damage_amount = 1, Bool narrate = True) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    if damage_amount < 1
        damage_amount = 1
    endif
    if SkyrimNet_Whipped_Engine.IsTorture(damage_type)
        return main.Damage(attacker, victim, cause, damage_type, damage_amount, body_part, narrate)
    endif
    if !SkyrimNet_Whipped_Engine.HasHealth(damage_type)
        Trace("External_Damage", "unknown injury type: " + damage_type)
        return -1
    endif
    int source = SkyrimNet_Whipped_Engine.RealHit(victim, attacker, damage_type, damage_amount)
    Trace("External_Damage", NameOf(attacker) + " -> " + NameOf(victim) + " " + damage_type + " real points:" + damage_amount + " source:" + source)
    if narrate && cause != ""
        SkyrimNetApi.RegisterShortLivedEvent("injury_hit_" + damage_type + "_" + victim.GetFormID() + "_" + NowMs(), "injury_hit",             cause, "", (main.event_ttl * 1000.0) as int, attacker, victim)
    endif
    return source
EndFunction

; Older API; use External_Damage.
; count whip hits on part (part name or region, "" = random; see SkyrimNet_Whipped_Engine.PartIndex).
; narrate: one DirectNarration of text plus the injury text (text alone while narration is off), so the
; caller's line is never lost. Returns the last hit code, or -1.
Int Function External_Strike(Actor attacker, Actor victim, String text, Int count = 1, String part = "", Bool narrate = True) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    return main.Strike(attacker, victim, text, count, part, narrate, main.SOURCE_WHIP)
EndFunction

; Older API; use External_Damage.
; External_Strike for any torture type: "whipping", "pinching", "slapping", "pulling", "punching", "fire", "frost",
; "shock", "falling", or a torture file another mod adds (see SkyrimNet_Whipped_Engine.IsTorture).
; Returns the last hit code, or -1 (also for an unknown type).
Int Function External_Torture(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True) global
    return External_Injure(attacker, victim, text, type, count, part, narrate, True)
EndFunction

; Older API; use External_Damage.
; Any injury type by file id (injuries/<type>.json, including files other mods add).
; torture True: count torture hits (External_Torture); the type needs a torture model ("torture" or "both").
;   Returns the last hit code, or -1.
; torture False: count real damage points (can kill); the type needs a health model ("health" or "both", e.g.
;   "falling"). part is ignored (real damage picks its part like an attack). The hit is narrated like any real hit
;   (NarrateRealHits); narrate only sends text, if any. Returns the source index, or -1.
Int Function External_Injure(Actor attacker, Actor victim, String text, String type, Int count = 1, String part = "", Bool narrate = True, Bool torture = True) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    if torture
        if !SkyrimNet_Whipped_Engine.IsTorture(type)
            Trace("External_Injure", "not a torture type: " + type)
            return -1
        endif
        return main.Strike(attacker, victim, text, count, part, narrate, type)
    endif
    if !SkyrimNet_Whipped_Engine.HasHealth(type)
        Trace("External_Injure", "no health model: " + type)
        return -1
    endif
    if count < 1
        count = 1
    endif
    int source = SkyrimNet_Whipped_Engine.RealHit(victim, attacker, type, count)
    Trace("External_Injure", NameOf(attacker) + " -> " + NameOf(victim) + " " + type + " real points:" + count + " source:" + source)
    if narrate && text != ""
        DirectNarrate(text, attacker, victim)
    endif
    return source
EndFunction

; A heal of health points worth on part ("" = any). The heal narration follows through the usual sync;
; narrate sends text now. Returns whip points healed, or -1.
Int Function External_Heal(Actor caster, Actor target, String text, Float health, String part = "", Bool narrate = True) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None || target == None
        return -1
    endif
    int healed = SkyrimNet_Whipped_Engine.HealPart(target, SkyrimNet_Whipped_Engine.PartIndex(part), health)
    Trace("External_Heal", NameOf(caster) + " -> " + NameOf(target) + " part:" + part + " health:" + health + " points:" + healed)
    if narrate && text != ""
        DirectNarrate(text, caster, target)
    endif
    main.StartSync()
    return healed
EndFunction

Bool Function External_SexualParts() global
    SkyrimNet_Whipped_Main main = Get()
    return main != None && main.include_sexual_parts
EndFunction

; Writes whip.body.include_sexual_parts and applies it now.
Function External_SetSexualParts(Bool enabled) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None
        return
    endif
    String value = "false"
    if enabled
        value = "true"
    endif
    Bool ok = SkyrimNetApi.PatchConfig(main.PLUGIN_CONFIG, "{\"whip\":{\"body\":{\"include_sexual_parts\":" + value + "}}}")
    main.include_sexual_parts = enabled
    SkyrimNet_Whipped_Engine.SetIncludeSexualParts(enabled)
    Trace("External_SetSexualParts", "enabled:" + enabled + " patched:" + ok)
EndFunction

; External_Damage for a torture type: count hits on part, then NarrateHits with cause (narrate) or nothing.
Int Function Damage(Actor attacker, Actor victim, String cause, String source, Int count, String part, Bool narrate)
    int index = SkyrimNet_Whipped_Engine.PartIndex(part)
    int mask = 0
    int worst = 0
    int code = -1
    Bool fresh = False
    int hits = 0
    int i = 0
    while i < count
        code = SkyrimNet_Whipped_Engine.HitPart(victim, source, index)
        if code >= 0
            if hits == 0
                fresh = FreshOf(code)
            endif
            hits += 1
            mask = Math.LogicalOr(mask, MaskOf(code))
            if LevelOf(code) > worst
                worst = LevelOf(code)
            endif
        endif
        i += 1
    endwhile
    Trace("Damage", NameOf(attacker) + " -> " + NameOf(victim) + " " + source + " part:" + part + " count:" + count + " parts:" + PartsText(mask)         + " level:" + worst + " narrate:" + narrate + " cause:" + cause)
    if hits == 0
        return code
    endif
    if narrate
        NarrateHits(attacker, victim, source, fresh, mask, worst, hits, cause)
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
EndFunction

Int Function Strike(Actor attacker, Actor victim, String text, Int count, String part, Bool narrate, String source)
    int index = SkyrimNet_Whipped_Engine.PartIndex(part)
    if count < 1
        count = 1
    endif
    int mask = 0
    int worst = 0
    int code = -1
    int i = 0
    while i < count
        code = SkyrimNet_Whipped_Engine.HitPart(victim, source, index)
        if code >= 0
            mask = Math.LogicalOr(mask, MaskOf(code))
            if LevelOf(code) > worst
                worst = LevelOf(code)
            endif
        endif
        i += 1
    endwhile
    Trace("Strike", NameOf(attacker) + " -> " + NameOf(victim) + " " + source + " part:" + part + " count:" + count + " parts:" + PartsText(mask) + " level:" + worst)
    if code < 0
        if narrate && text != ""
            DirectNarrate(text, attacker, victim)
        endif
        return code
    endif
    if narrate
        String msg = text
        if narration_enabled
            String injury = SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, mask, worst, count)
            if injury != ""
                if msg != ""
                    msg += " "
                endif
                msg += injury
            endif
        endif
        if msg != ""
            DirectNarrate(msg, attacker, victim)
        endif
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
EndFunction
