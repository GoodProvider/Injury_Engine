Scriptname Injury_Engine_Main extends Quest

; Injuries:
;   The visible welt is an engine impact decal (WEAP -> IPDS -> IPCT -> TXST), copied from
;   PAH Diary Of Mine's padded whip. The engine owns decals (they vanish on cell change or
;   reload), so the wounds the LLM is told about are tracked separately by the C++ InjuryEngine
;   (Injury_Engine_Engine natives): per body part and per source (whipping, slashing, ...).
;   All injury data and text live in SKSE/Plugins/Injury_Engine/injuries/<source>.json.

Weapon Property PaddedWhip Auto
ImpactDataSet Property ImpactSet Auto
GlobalVariable Property require_whip_global Auto
Spell Property HealingHands Auto
; Flames / Frostbite / Sparks (torture): fire / frost / shock torture wounds instead of real damage
Spell Property TortureFlames Auto
Spell Property TortureFrostbite Auto
Spell Property TortureSparks Auto

String Property PLUGIN_CONFIG = "Plugin_Injury_Engine" AutoReadOnly
String Property SOURCE_WHIP = "whipping" AutoReadOnly
String Property KEY_LAST_NARRATION = "injury_engine.last_narration" AutoReadOnly
; Real time of the victim's last torture hit (any torture type), for the torture narration window / interval
String Property KEY_LAST_TORTURE = "injury_engine.last_torture" AutoReadOnly
String Property KEY_IN_ACTION = "injury_engine.in_action" AutoReadOnly
; Torture scene: parallel lists on this quest, one entry per (attacker, victim) pair (no attacker: this quest itself)
String Property KEY_SC_ATT = "injury_engine.sc_att" AutoReadOnly
String Property KEY_SC_VIC = "injury_engine.sc_vic" AutoReadOnly
String Property KEY_SC_SRC = "injury_engine.sc_src" AutoReadOnly
String Property KEY_SC_MASK = "injury_engine.sc_mask" AutoReadOnly
String Property KEY_SC_LEVEL = "injury_engine.sc_level" AutoReadOnly
String Property KEY_SC_COUNT = "injury_engine.sc_count" AutoReadOnly
String Property KEY_SC_LAST = "injury_engine.sc_last" AutoReadOnly
String Property KEY_SC_NARR = "injury_engine.sc_narr" AutoReadOnly
String Property KEY_SC_STATE = "injury_engine.sc_state" AutoReadOnly
String Property KEY_SC_SWITCH = "injury_engine.sc_switch" AutoReadOnly
String Property KEY_SC_CAUSE = "injury_engine.sc_cause" AutoReadOnly
; KEY_SC_STATE values: not narrated yet / narrated and in sight / out of sight (never narrated)
Int Property SC_PENDING = 0 AutoReadOnly
Int Property SC_RUNNING = 1 AutoReadOnly
Int Property SC_HIDDEN = 2 AutoReadOnly
Int Property SC_SEL_START = 0 AutoReadOnly
Int Property SC_SEL_SWITCH = 1 AutoReadOnly
Int Property SC_SEL_AGAIN = 2 AutoReadOnly
; Heal session (mirrors the torture scene): parallel lists on this quest, one entry per healed actor.
; caster (this quest when none), last time a healing source was working, last narration, last line sent
String Property KEY_HS_TGT = "injury_engine.hs_tgt" AutoReadOnly
String Property KEY_HS_CASTER = "injury_engine.hs_caster" AutoReadOnly
String Property KEY_HS_LAST = "injury_engine.hs_last" AutoReadOnly
String Property KEY_HS_NARR = "injury_engine.hs_narr" AutoReadOnly
String Property KEY_HS_TEXT = "injury_engine.hs_text" AutoReadOnly

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
; Health (real damage) hits: one DirectNarration at most this often, hits in between are short-lived events
Float Property health_narration_cooloff = 6.0 Auto Hidden
Float Property last_health_narration = -1000.0 Auto Hidden
; Killing blows of real damage: narrate the death line (death block of the source's injuries JSON)
; Narration mode per category (injury_engine.narration.*_mode): 0 Direct Narration, 1 Event (short-lived only), 2 Silent
int Property injury_mode = 0 Auto Hidden
int Property healing_mode = 0 Auto Hidden
int Property death_mode = 0 Auto Hidden
int Property torture_mode = 0 Auto Hidden
Float Property torture_narration_window_size = 30.0 Auto Hidden
; A pause this long between hits ends the torture (end narration); this often one combined line during it
Float Property torture_maximum_break = 3.0 Auto Hidden
Float Property torture_direct_narration_gap = 6.0 Auto Hidden
; An again line only while hits are still landing: the pair's last hit at most this many seconds ago
Float Property torture_again_recent = 1.0 AutoReadOnly
Float Property narration_max_distance = 15.0 Auto Hidden
Float Property event_ttl = 30.0 Auto Hidden
Bool Property include_sexual_parts = False Auto Hidden
Bool Property verbose_logging = False Auto Hidden

Bool Property dll_ok = False Auto Hidden
; DirectNarration reenters Papyrus; SceneTick ignores nested calls while the outer tick runs.
Bool Property scene_tick_active = False Auto Hidden
Bool Property heal_tick_active = False Auto Hidden
; TakeHits / HitVictims / HitAttackers share one C++ buffer: only one NarrateRealHits drains it at a time.
Bool Property hits_draining = False Auto Hidden
; True while a 0.2 s sync update is already scheduled (RegisterForSingleUpdate replaces a pending one).
Bool Property sync_pending = False Auto Hidden
; Method leg of the last healing short-lived event id (wound source or heal-sync source).
String heal_short_method = "none"

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[Injury_Engine_Main] " + func + ": " + msg)
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

; Every DirectNarration goes through here so the exact text sent is in Injury_Engine.log ([hh:mm:ss] torture:direct / heal:direct / health:direct).
Function DirectNarrate(String msg, Actor speaker, Actor target, String category = "torture", String session = "start") global
    Trace("DirectNarration", NameOf(speaker) + " -> " + NameOf(target) + ": " + msg)
    Injury_Engine_Engine.LogLine(category, "direct", session, msg)
    SkyrimNetApi.DirectNarration(msg, speaker, target)
EndFunction

Function ShortEvent(String msg, Actor speaker, Actor target, String category, String eventId, String eventType, int ttlMs, String session = "start") global
    if msg != ""
        Injury_Engine_Engine.LogLine(category, "short", session, msg)
    endif
    SkyrimNetApi.RegisterShortLivedEvent(eventId, eventType, msg, "", ttlMs, speaker, target)
EndFunction

Injury_Engine_Main Function Get() global
    return Game.GetFormFromFile(0x800, "Injury_Engine.esp") as Injury_Engine_Main
EndFunction

;----------------------------------------------------------------------------------------------------
; Setup
;----------------------------------------------------------------------------------------------------
Function Setup()
    Trace("Setup", "")
    scene_tick_active = False
    hits_draining = False
    sync_pending = False
    if !Setup_CheckLinks()
        return
    endif

    UnRegisterForModEvent("SkyrimNet_OnPluginConfigSaved")
    RegisterForModEvent("SkyrimNet_OnPluginConfigSaved", "OnPluginConfigSaved")
    UnRegisterForModEvent("Injury_Engine_Injury")
    RegisterForModEvent("Injury_Engine_Injury", "OnInjury")

    dll_ok = Injury_Engine_Engine.PartName(0) != "" && Injury_Engine_Engine.SourceCount() > 0
    if Injury_Engine_Engine.PartName(0) == ""
        Trace("Setup", "ERROR: Injury_Engine.dll is missing; injuries will not be tracked.", true)
    elseif Injury_Engine_Engine.SourceCount() == 0
        Trace("Setup", "ERROR: no SKSE/Plugins/Injury_Engine/injuries/*.json found; injuries will not be tracked.", true)
    endif
    ApplyPluginConfig()

    GiveGear()
    GiveHealingHands()

    if dll_ok
        StartSync() ; actors loaded from the co-save
    endif
    SkyrimNetApi.RegisterDecorator("injury_engine_injuries", "Injury_Engine_Main", "Injuries_Decorator")

    Injury_Engine_Actions actions = (self as Quest) as Injury_Engine_Actions
    if actions != None
        actions.Setup()
    else
        Trace("Setup", "ERROR: Failed to get Injury_Engine_Actions.", true)
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
    return Injury_Engine_Engine.Number(SOURCE_WHIP, "defaults." + setting, fallback)
EndFunction

Bool Function DefaultBool(String setting)
    return Injury_Engine_Engine.Number(SOURCE_WHIP, "defaults." + setting, 1.0) > 0.5
EndFunction

; A narration mode select: "Direct Narration" 0, "Event" 1, "Silent" 2 (anything else: 0).
int Function NarrationMode(String path)
    String v = SkyrimNetApi.GetConfigString(PLUGIN_CONFIG, path, "Direct Narration")
    if v == "Silent"
        return 2
    elseif v == "Event"
        return 1
    endif
    return 0
EndFunction

; Mode for an event kind: "health" injury, "torturing" torture, "death" death, else healing.
int Function ModeOf(String kind)
    if kind == "health"
        return injury_mode
    elseif kind == "torturing"
        return torture_mode
    elseif kind == "death"
        return death_mode
    endif
    return healing_mode
EndFunction

; Direct Narration allowed for kind: narration on and the kind's mode is Direct Narration.
Bool Function DirectAllowed(String kind)
    return narration_enabled && ModeOf(kind) == 0
EndFunction

Function ApplyPluginConfig()
    require_whip = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.action.require_whip", DefaultBool("require_whip"))
    give_gear = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.action.give_to_player", DefaultBool("give_to_player"))
    max_strikes = SkyrimNetApi.GetConfigInt(PLUGIN_CONFIG, "injury_engine.action.max_strikes", DefaultNumber("max_strikes", 10.0) as int)
    hours_per_point = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.healing.hours_per_point", DefaultNumber("hours_per_point", 1.0))
    give_healing_hands = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.healing.give_healing_hands", DefaultBool("give_healing_hands"))
    regen_rate = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.healing.regen_rate", DefaultNumber("regen_rate", 10.0))
    narration_enabled = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.narration.enabled", DefaultBool("narration_enabled"))
    narration_cooldown = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.cooldown", DefaultNumber("narration_cooldown", 8.0))
    health_narration_cooloff = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.health_narration_cooloff", DefaultNumber("health_narration_cooloff", 6.0))
    injury_mode = NarrationMode("injury_engine.narration.injury_mode")
    healing_mode = NarrationMode("injury_engine.narration.healing_mode")
    death_mode = NarrationMode("injury_engine.narration.death_mode")
    torture_mode = NarrationMode("injury_engine.narration.torture_mode")
    torture_narration_window_size = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.torture_narration_window_size", DefaultNumber("torture_narration_window_size", 30.0))
    torture_maximum_break = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.torture_maximum_break", DefaultNumber("torture_maximum_break", 3.0))
    torture_direct_narration_gap = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.torture_direct_narration_gap", DefaultNumber("torture_direct_narration_gap", 6.0))
    narration_max_distance = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.max_distance", DefaultNumber("narration_max_distance", 15.0))
    event_ttl = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "injury_engine.narration.event_ttl", DefaultNumber("event_ttl", 30.0))
    include_sexual_parts = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.body.include_sexual_parts", DefaultNumber("include_sexual_parts", 0.0) > 0.5)
    Bool show_overlay = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.overlay.enabled", DefaultBool("overlay_enabled"))
    verbose_logging = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.debug.verbose_logging", false)
    if max_strikes < 1
        max_strikes = 1
    endif
    Injury_Engine_Engine.SetHoursPerPoint(hours_per_point)
    Injury_Engine_Engine.SetRegenRate(regen_rate)
    Injury_Engine_Engine.SetIncludeSexualParts(include_sexual_parts)
    Injury_Engine_Engine.SetPunchCombatHealth(SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.body.punch_combat_health", false))
    Injury_Engine_Engine.SetTortureBleedout(SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "injury_engine.body.torture_bleedout", true))
    Injury_Engine_Engine.SetOverlayEnabled(show_overlay)
    Injury_Engine_Engine.SetVerboseLogging(verbose_logging)
    RegisterForSingleUpdateGameTime(hours_per_point) ; healing poll follows the heal rate
    if require_whip_global
        require_whip_global.SetValue(require_whip as Float)
    endif
    Trace("ApplyPluginConfig", "require_whip:" + require_whip + " give_gear:" + give_gear + " max_strikes:" + max_strikes + " hours_per_point:" + hours_per_point \
        + " give_healing_hands:" + give_healing_hands + " regen_rate:" + regen_rate \
        + " narration:" + narration_enabled + " modes injury/heal/death/torture:" + injury_mode + "/" + healing_mode + "/" + death_mode + "/" + torture_mode + " cooldown:" + narration_cooldown + " torture_window:" + torture_narration_window_size + " break:" + torture_maximum_break + " gap:" + torture_direct_narration_gap + " max_distance:" + narration_max_distance + " event_ttl:" + event_ttl + " sexual_parts:" + include_sexual_parts + " overlay:" + show_overlay + " verbose:" + verbose_logging)
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
    int nodes = Injury_Engine_Engine.TextCount(SOURCE_WHIP, "action.impact_nodes")
    if nodes > 0
        node = Injury_Engine_Engine.Text(SOURCE_WHIP, "action.impact_nodes." + Utility.RandomInt(0, nodes - 1))
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
; Returns a hit code (see Injury_Engine_Engine), or -1.
Int Function OnLash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None || victim.IsDead()
        return -1
    endif
    float health_before = 0.0
    if verbose_logging
        health_before = victim.GetActorValue("Health")
    endif
    int code = Injury_Engine_Engine.Hit(victim, attacker, SOURCE_WHIP)
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
        Injury_Engine_Engine.QueueHit(victim, attacker, code)
        NarrateRealHits()
    endif
    ClearHealBatch(victim) ; pending heals are stale; the next heal is fresh
    StartSync()
    return code
EndFunction

; Hit code fields (see Injury_Engine_Engine).
Bool Function FreshOf(int code) global
    return code / 100000000 > 0
EndFunction

String Function SourceOfCode(int code) global
    return Injury_Engine_Engine.SourceId((code / 1000000) % 100)
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
; Torture hits (whipping, slapping, ...) form one scene of (attacker, victim) pairs, see "Torture scene" below:
;   - a pair's first hit (pending start) or a method change: NarrateHits sends "start" immediately; player in batch: purge + DirectNarration, else OptionalDirectNarrate
;   - a hit after that is a short-lived event; every torture_direct_narration_gap seconds one combined "again" line
;   - a pair whose last hit is torture_maximum_break seconds old is dropped with no line (stops "again")
;   - only pairs in sight of the player are narrated
Function NarrateInjury(Actor attacker, Actor victim, int code, Bool unarmedForm = False)
    NarrateHits(attacker, victim, SourceOfCode(code), MaskOf(code), LevelOf(code), 1, "", unarmedForm)
EndFunction

; NarrateInjury for hits hits of source at once (mask: parts hit, level: the worst).
; cause ("" = none) leads every narration or event sent and replaces our narration.hit.by_* text.
Function NarrateHits(Actor attacker, Actor victim, String source, int part_mask, int level, int hits, String cause, Bool unarmedForm = False)
    Bool noCause = cause != ""
    String msg = CauseText(cause) + Injury_Engine_Engine.HitText(source, attacker, victim, part_mask, level, hits, noCause, unarmedForm)
    String kind = "health"
    if Injury_Engine_Engine.IsTorture(source)
        kind = "torturing"
    endif
    if kind == "health"
        if msg != ""
            Float now = Utility.GetCurrentRealTime()
            Bool cooled = now < last_health_narration || now - last_health_narration >= health_narration_cooloff
            if DirectAllowed(kind) && cooled && SkyrimNetApi.GetSpeechQueueSize() == 0
                last_health_narration = now
                DirectNarrate(msg, attacker, victim, "health", "start")
            else
                Trace("NarrateInjury", source + " short-lived event: " + msg)
                RegisterInjuryShortEvent(msg, attacker, victim, kind, source, attacker, victim)
            endif
        endif
        return
    endif
    StorageUtil.SetFloatValue(victim, KEY_LAST_TORTURE, Utility.GetCurrentRealTime())
    Bool visible = PairVisible(attacker, victim)
    int i = SceneFind(attacker, victim)
    String session = "update"
    if i < 0
        session = "start"
        SceneAdd(attacker, victim, source, part_mask, level, hits, cause, visible)
        i = StorageUtil.FormListCount(self, KEY_SC_VIC) - 1
        StartSync()
    else
        SceneUpdate(i, source, part_mask, level, hits, cause, visible)
        StartSync()
    endif
    Bool needStart = false
    if StorageUtil.IntListGet(self, KEY_SC_STATE, i) == SC_PENDING
        needStart = true
    elseif StorageUtil.IntListGet(self, KEY_SC_SWITCH, i) == 1
        needStart = true
        session = "switch"
        StorageUtil.IntListSet(self, KEY_SC_SWITCH, i, 0)
    endif
    if msg != ""
        Trace("NarrateInjury", source + " short-lived event: " + msg)
        RegisterInjuryShortEvent(msg, attacker, victim, kind, source, attacker, victim, session)
    endif
    if verbose_logging
        Verbose("NarrateInjury", source + " " + victim.GetDisplayName() + " pair:" + i + " state:" + StorageUtil.IntListGet(self, KEY_SC_STATE, i) \
            + " hits:" + StorageUtil.IntListGet(self, KEY_SC_COUNT, i) + " level:" + level + " " + GateText(victim))
    endif
    if needStart && visible
        StorageUtil.IntListSet(self, KEY_SC_STATE, i, SC_RUNNING)
        int[] picked = Utility.CreateIntArray(1)
        picked[0] = i
        SceneSend("start", picked, true)
    endif
EndFunction

;----------------------------------------------------------------------------------------------------
; Torture scene: every (attacker, victim) pair torturing right now, narrated together
;----------------------------------------------------------------------------------------------------

; Index of the pair, -1 when it is not in the scene. No attacker is stored as this quest.
Int Function SceneFind(Actor attacker, Actor victim)
    Form att = self
    if attacker != None
        att = attacker
    endif
    int i = StorageUtil.FormListCount(self, KEY_SC_VIC) - 1
    while i >= 0
        if StorageUtil.FormListGet(self, KEY_SC_VIC, i) == victim && StorageUtil.FormListGet(self, KEY_SC_ATT, i) == att
            return i
        endif
        i -= 1
    endwhile
    return -1
EndFunction

Actor Function SceneAttacker(int i)
    return StorageUtil.FormListGet(self, KEY_SC_ATT, i) as Actor
EndFunction

Actor Function SceneVictim(int i)
    return StorageUtil.FormListGet(self, KEY_SC_VIC, i) as Actor
EndFunction

; In sight of the player: the victim and the attacker (if any) are loaded and within the narration distance.
Bool Function PairVisible(Actor attacker, Actor victim)
    if TooFar(victim)
        return false
    endif
    return attacker == None || !TooFar(attacker)
EndFunction

Function SceneAdd(Actor attacker, Actor victim, String source, int mask, int level, int hits, String cause, Bool visible)
    Form att = self
    if attacker != None
        att = attacker
    endif
    int st = SC_PENDING
    if !visible
        st = SC_HIDDEN
    endif
    float now = Utility.GetCurrentRealTime()
    StorageUtil.FormListAdd(self, KEY_SC_ATT, att, true)
    StorageUtil.FormListAdd(self, KEY_SC_VIC, victim, true)
    StorageUtil.StringListAdd(self, KEY_SC_SRC, source, true)
    StorageUtil.IntListAdd(self, KEY_SC_MASK, mask, true)
    StorageUtil.IntListAdd(self, KEY_SC_LEVEL, level, true)
    StorageUtil.IntListAdd(self, KEY_SC_COUNT, hits, true)
    StorageUtil.FloatListAdd(self, KEY_SC_LAST, now, true)
    StorageUtil.FloatListAdd(self, KEY_SC_NARR, now, true)
    StorageUtil.IntListAdd(self, KEY_SC_STATE, st, true)
    StorageUtil.IntListAdd(self, KEY_SC_SWITCH, 0, true)
    StorageUtil.StringListAdd(self, KEY_SC_CAUSE, cause, true)
EndFunction

Function SceneUpdate(int i, String source, int mask, int level, int hits, String cause, Bool visible)
    int st = StorageUtil.IntListGet(self, KEY_SC_STATE, i)
    StorageUtil.FloatListSet(self, KEY_SC_LAST, i, Utility.GetCurrentRealTime())
    StorageUtil.IntListSet(self, KEY_SC_MASK, i, mask)
    StorageUtil.IntListSet(self, KEY_SC_LEVEL, i, level)
    if StorageUtil.StringListGet(self, KEY_SC_SRC, i) != source
        StorageUtil.StringListSet(self, KEY_SC_SRC, i, source)
        StorageUtil.IntListSet(self, KEY_SC_COUNT, i, hits)
        if st == SC_RUNNING
            StorageUtil.IntListSet(self, KEY_SC_SWITCH, i, 1)
        endif
    else
        StorageUtil.IntListSet(self, KEY_SC_COUNT, i, StorageUtil.IntListGet(self, KEY_SC_COUNT, i) + hits)
    endif
    if cause != ""
        StorageUtil.StringListSet(self, KEY_SC_CAUSE, i, cause)
    endif
    if visible && st == SC_HIDDEN
        StorageUtil.IntListSet(self, KEY_SC_STATE, i, SC_RUNNING) ; back in sight: joins the next combined line, no start line
    elseif !visible && st != SC_HIDDEN
        StorageUtil.IntListSet(self, KEY_SC_STATE, i, SC_HIDDEN)
    endif
EndFunction

Function SceneRemove(int i)
    StorageUtil.FormListRemoveAt(self, KEY_SC_ATT, i)
    StorageUtil.FormListRemoveAt(self, KEY_SC_VIC, i)
    StorageUtil.StringListRemoveAt(self, KEY_SC_SRC, i)
    StorageUtil.IntListRemoveAt(self, KEY_SC_MASK, i)
    StorageUtil.IntListRemoveAt(self, KEY_SC_LEVEL, i)
    StorageUtil.IntListRemoveAt(self, KEY_SC_COUNT, i)
    StorageUtil.FloatListRemoveAt(self, KEY_SC_LAST, i)
    StorageUtil.FloatListRemoveAt(self, KEY_SC_NARR, i)
    StorageUtil.IntListRemoveAt(self, KEY_SC_STATE, i)
    StorageUtil.IntListRemoveAt(self, KEY_SC_SWITCH, i)
    StorageUtil.StringListRemoveAt(self, KEY_SC_CAUSE, i)
EndFunction

; Another activity (healing, real damage) took over: drop the victim's torture pairs without a line or memory.
Function ClearTortureScene(Actor victim)
    int dropped = 0
    int i = StorageUtil.FormListCount(self, KEY_SC_VIC) - 1
    while i >= 0
        if SceneVictim(i) == victim
            SceneRemove(i)
            dropped += 1
        endif
        i -= 1
    endwhile
    if dropped > 0 && verbose_logging
        Verbose("ClearTortureScene", NameOf(victim) + " pairs dropped:" + dropped)
    endif
EndFunction

; Does pair i belong to the selection: SC_SEL_START pending starts, SC_SEL_SWITCH method switches, SC_SEL_AGAIN running
; pairs with more than one hit, the last within torture_again_recent. Starts, switches and again lines skip pairs whose
; break already elapsed.
Bool Function SceneMatches(int i, int mode, float now)
    int st = StorageUtil.IntListGet(self, KEY_SC_STATE, i)
    float sinceHit = now - StorageUtil.FloatListGet(self, KEY_SC_LAST, i)
    if sinceHit >= torture_maximum_break
        return false
    endif
    if mode == SC_SEL_START
        return st == SC_PENDING
    elseif mode == SC_SEL_SWITCH
        return st == SC_RUNNING && StorageUtil.IntListGet(self, KEY_SC_SWITCH, i) == 1
    endif
    return sinceHit <= torture_again_recent && st == SC_RUNNING && StorageUtil.IntListGet(self, KEY_SC_SWITCH, i) == 0 && StorageUtil.IntListGet(self, KEY_SC_COUNT, i) >= 2
EndFunction

; Indices of the pairs of a selection (empty array when none).
Int[] Function SceneSelect(int mode)
    float now = Utility.GetCurrentRealTime()
    int total = StorageUtil.FormListCount(self, KEY_SC_VIC)
    int found = 0
    int i = 0
    while i < total
        if SceneMatches(i, mode, now)
            found += 1
        endif
        i += 1
    endwhile
    int[] picked = Utility.CreateIntArray(found)
    found = 0
    i = 0
    while i < total
        if SceneMatches(i, mode, now)
            picked[found] = i
            found += 1
        endif
        i += 1
    endwhile
    return picked
EndFunction

; The combined line (Injury_Engine_Engine.SceneText) for the pairs picked.
String Function SceneLine(String phase, int[] picked)
    int n = picked.Length
    Form[] attackers = Utility.CreateFormArray(n)
    Form[] victims = Utility.CreateFormArray(n)
    String[] sources = Utility.CreateStringArray(n)
    int[] masks = Utility.CreateIntArray(n)
    int[] levels = Utility.CreateIntArray(n)
    int k = 0
    while k < n
        int i = picked[k]
        attackers[k] = StorageUtil.FormListGet(self, KEY_SC_ATT, i)
        victims[k] = StorageUtil.FormListGet(self, KEY_SC_VIC, i)
        sources[k] = StorageUtil.StringListGet(self, KEY_SC_SRC, i)
        masks[k] = StorageUtil.IntListGet(self, KEY_SC_MASK, i)
        levels[k] = StorageUtil.IntListGet(self, KEY_SC_LEVEL, i)
        k += 1
    endwhile
    return Injury_Engine_Engine.SceneText(phase, attackers, victims, sources, masks, levels)
EndFunction

; DirectNarration when the speech queue is empty, otherwise a short-lived event.
Function OptionalDirectNarrate(String msg, Actor speaker, Actor target, String session = "start")
    if DirectAllowed("torturing") && SkyrimNetApi.GetSpeechQueueSize() == 0
        DirectNarrate(msg, speaker, target, "torture", session)
    else
        Trace("OptionalDirectNarrate", "speech queue busy, event: " + msg)
        int pi = SceneFind(speaker, target)
        String method = SOURCE_WHIP
        if pi >= 0
            method = StorageUtil.StringListGet(self, KEY_SC_SRC, pi)
            if method == ""
                method = SOURCE_WHIP
            endif
        endif
        RegisterInjuryShortEvent(msg, speaker, target, "torturing", method, speaker, target, session)
    endif
EndFunction

; Log session type of a scene / heal phase.
String Function SessionOf(String phase) global
    if phase == "again"
        return "update"
    elseif phase == "end"
        return "finish"
    endif
    return phase
EndFunction

; One combined line for the picked pairs. If the player is attacker or victim of any pair in the batch and purge is true,
; clear the speech queue then DirectNarration; otherwise OptionalDirectNarrate (DirectNarration only when the queue is empty).
; Narration off: a short-lived event. A cause from External_Damage leads start and switch lines.
Function SceneSend(String phase, int[] picked, Bool purge)
    if picked.Length == 0
        return
    endif
    String msg = SceneLine(phase, picked)
    Actor player = Game.GetPlayer()
    Bool involved = false
    String cause = ""
    int k = 0
    while k < picked.Length
        Actor att = SceneAttacker(picked[k])
        Actor vic = SceneVictim(picked[k])
        involved = involved || att == player || vic == player
        String own = StorageUtil.StringListGet(self, KEY_SC_CAUSE, picked[k])
        if own != ""
            if cause == ""
                cause = own
            endif
            StorageUtil.StringListSet(self, KEY_SC_CAUSE, picked[k], "")
        endif
        StorageUtil.FloatListSet(self, KEY_SC_NARR, picked[k], Utility.GetCurrentRealTime())
        k += 1
    endwhile
    msg = CauseText(cause) + msg
    if msg == ""
        return
    endif
    Actor speaker = SceneAttacker(picked[0])
    Actor target = SceneVictim(picked[0])
    Trace("SceneSend", phase + " pairs:" + picked.Length + " player:" + involved + ": " + msg)
    if torture_mode == 2
        return
    elseif !DirectAllowed("torturing")
        String method = StorageUtil.StringListGet(self, KEY_SC_SRC, picked[0])
        if method == ""
            method = SOURCE_WHIP
        endif
        RegisterInjuryShortEvent(msg, speaker, target, "torturing", method, speaker, target, SessionOf(phase))
    elseif involved
        if purge
            SkyrimNetApi.PurgeDialogue(true) ; the torture comes first: drop queued lines, let the current one finish
        endif
        DirectNarrate(msg, speaker, target, "torture", SessionOf(phase))
    else
        OptionalDirectNarrate(msg, speaker, target, SessionOf(phase))
    endif
EndFunction

; Called by OnUpdate: visibility, then switches, new pairs, the periodic line; drop pairs past torture_maximum_break.
Function SceneTick()
    if scene_tick_active
        return
    endif
    int total = StorageUtil.FormListCount(self, KEY_SC_VIC)
    if total == 0
        return
    endif
    scene_tick_active = True
    float now = Utility.GetCurrentRealTime()
    int i = 0
    while i < total
        int st = StorageUtil.IntListGet(self, KEY_SC_STATE, i)
        Bool visible = PairVisible(SceneAttacker(i), SceneVictim(i))
        if visible && st == SC_HIDDEN
            StorageUtil.IntListSet(self, KEY_SC_STATE, i, SC_RUNNING)
        elseif !visible && st != SC_HIDDEN
            StorageUtil.IntListSet(self, KEY_SC_STATE, i, SC_HIDDEN)
        endif
        i += 1
    endwhile
    int[] switches = SceneSelect(SC_SEL_SWITCH)
    if switches.Length > 0
        i = 0
        while i < switches.Length
            StorageUtil.IntListSet(self, KEY_SC_SWITCH, switches[i], 0)
            i += 1
        endwhile
        SceneSend("switch", switches, true)
    endif
    int[] starts = SceneSelect(SC_SEL_START)
    if starts.Length > 0
        i = 0
        while i < starts.Length
            StorageUtil.IntListSet(self, KEY_SC_STATE, starts[i], SC_RUNNING)
            i += 1
        endwhile
        SceneSend("start", starts, true) ; player-involved first-hit batch always may purge (see SceneSend)
    endif
    int[] again = SceneSelect(SC_SEL_AGAIN)
    Bool due = false
    i = 0
    while i < again.Length && !due
        due = now - StorageUtil.FloatListGet(self, KEY_SC_NARR, again[i]) >= torture_direct_narration_gap
        i += 1
    endwhile
    if due
        SceneSend("again", again, true)
    endif
    i = total - 1
    while i >= 0
        if now - StorageUtil.FloatListGet(self, KEY_SC_LAST, i) >= torture_maximum_break
            SceneRemove(i)
        endif
        i -= 1
    endwhile
    scene_tick_active = False
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

; Short-lived event id: kind + method + torturer + victim; re-register overwrites and refreshes TTL.
String Function InjuryShortEventId(String kind, String method, Actor torturer, Actor victim) global
    int tortId = 0
    if torturer != None
        tortId = torturer.GetFormID()
    endif
    int vicId = 0
    if victim != None
        vicId = victim.GetFormID()
    endif
    return kind + "_" + method + "_" + tortId + "_to_" + vicId
EndFunction

String Function InjuryLogCategory(String kind) global
    if kind == "torturing"
        return "torture"
    elseif kind == "healing"
        return "heal"
    endif
    return "health"
EndFunction

Function RegisterInjuryShortEvent(String msg, Actor speaker, Actor target, String kind, String method, Actor torturer, Actor victim, String session = "start")
    if msg == "" || ModeOf(kind) == 2
        return
    endif
    String eventType = "whip_heal"
    if kind == "torturing"
        eventType = "torture_hit"
    elseif kind == "health"
        eventType = "injury_hit"
    endif
    ShortEvent(msg, speaker, target, InjuryLogCategory(kind), InjuryShortEventId(kind, method, torturer, victim), eventType, (event_ttl * 1000.0) as int, session)
EndFunction

String Function HealSyncSourceMethod(int source) global
    if source == 1
        return "spell"
    elseif source == 2
        return "staff"
    elseif source == 3
        return "potion"
    elseif source == 0
        return "none"
    endif
    return "other"
EndFunction

Bool Function TooFar(Actor akActor)
    return !akActor.Is3DLoaded() || Game.GetPlayer().GetDistance(akActor) > narration_max_distance * UNITS_PER_METER
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
            text += Injury_Engine_Engine.PartName(i)
        endif
        i += 1
    endwhile
    return text
EndFunction

; One torture hit by hit code (the type is in the code). Used by the Torture_Target summary.
String Function LashText(Actor attacker, Actor victim, int code)
    return Injury_Engine_Engine.HitText(SourceOfCode(code), attacker, victim, MaskOf(code), LevelOf(code), 1)
EndFunction

; One scripted torture hit of type source (injuries/<source>.json, model "torture"). Whipping is a Lash
; (decal + sound); other types play the type's action.animation event when it has one. Returns the hit code, or -1.
Int Function Torture(Actor attacker, Actor victim, String source, Bool narrate=True)
    if source == SOURCE_WHIP
        return Lash(attacker, victim, narrate)
    endif
    if victim == None || victim.IsDead() || !Injury_Engine_Engine.IsTorture(source)
        return -1
    endif
    String anim = Injury_Engine_Engine.Text(source, "action.animation")
    if anim != "" && attacker != None
        Debug.SendAnimationEvent(attacker, anim)
    endif
    int code = Injury_Engine_Engine.Hit(victim, attacker, source)
    if code < 0
        Verbose("Torture", source + " Hit returned " + code + " for " + victim.GetDisplayName())
        return code
    endif
    Trace("Torture", NameOf(attacker) + " -> " + victim.GetDisplayName() + " " + source + " part:" + PartsOf(code) + " level:" + LevelOf(code) + " fresh:" + FreshOf(code))
    if narrate
        Injury_Engine_Engine.QueueHit(victim, attacker, code)
        NarrateRealHits()
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
EndFunction

;----------------------------------------------------------------------------------------------------
; Game time heals torture points (applied by the DLL on every access); this keeps the sync loop running so
; fully-healed events go out.
;----------------------------------------------------------------------------------------------------
Event OnUpdateGameTime()
    if !dll_ok
        return
    endif
    int tracked = Injury_Engine_Engine.TrackedCount()
    Verbose("OnUpdateGameTime", "tracked:" + tracked + " next in " + hours_per_point + " game hours")
    if tracked > 0
        StartSync()
    endif
    Injury_Engine_Actions actions = (self as Quest) as Injury_Engine_Actions
    if actions != None
        actions.RestoreExpiredTruces() ; Stop_Fighting truces end after TRUCE_HOURS
    endif
    RegisterForSingleUpdateGameTime(hours_per_point)
EndEvent

; A new hit: close the victim's heal session without a line.
Function ClearHealBatch(Actor victim)
    int s = HealFind(victim)
    if s >= 0
        HealRemove(s)
    endif
EndFunction

;----------------------------------------------------------------------------------------------------
; Heal session, mirroring the torture scene: a healing source starting on an actor opens it with a start
; line at once; while the source keeps working, one "again" line every torture_direct_narration_gap; after
; torture_maximum_break with no healing it is dropped with no line. Every line is HealMapText: each part's
; torture points at their heal level (high / mid / low). Lines go out like SceneSend.
;----------------------------------------------------------------------------------------------------
int Function HealFind(Actor target)
    int i = StorageUtil.FormListCount(self, KEY_HS_TGT) - 1
    while i >= 0
        if StorageUtil.FormListGet(self, KEY_HS_TGT, i) == target
            return i
        endif
        i -= 1
    endwhile
    return -1
EndFunction

Actor Function HealTarget(int i)
    return StorageUtil.FormListGet(self, KEY_HS_TGT, i) as Actor
EndFunction

Actor Function HealCaster(int i)
    return StorageUtil.FormListGet(self, KEY_HS_CASTER, i) as Actor
EndFunction

; Opens a session for target and sends the start line (lead + heal map) right away. Returns its index.
int Function HealOpen(Actor target, Actor caster, String lead)
    Form c = caster
    if c == None
        c = self
    endif
    ClearTortureScene(target) ; healing replaces the torture scene silently
    float now = Utility.GetCurrentRealTime()
    StorageUtil.FormListAdd(self, KEY_HS_TGT, target, true)
    StorageUtil.FormListAdd(self, KEY_HS_CASTER, c, true)
    StorageUtil.FloatListAdd(self, KEY_HS_LAST, now, true)
    StorageUtil.FloatListAdd(self, KEY_HS_NARR, now, true)
    StorageUtil.StringListAdd(self, KEY_HS_TEXT, "", true)
    int i = StorageUtil.FormListCount(self, KEY_HS_TGT) - 1
    HealSend("start", i, lead)
    StartSync()
    return i
EndFunction

Function HealRemove(int i)
    StorageUtil.FormListRemoveAt(self, KEY_HS_TGT, i)
    StorageUtil.FormListRemoveAt(self, KEY_HS_CASTER, i)
    StorageUtil.FloatListRemoveAt(self, KEY_HS_LAST, i)
    StorageUtil.FloatListRemoveAt(self, KEY_HS_NARR, i)
    StorageUtil.StringListRemoveAt(self, KEY_HS_TEXT, i)
EndFunction

String Function JoinText(String a, String b) global
    if a == ""
        return b
    elseif b == ""
        return a
    endif
    return a + " " + b
EndFunction

; One heal line for session i: lead (source text, start only) + the heal map. An "again" line equal to the last
; line sent is skipped. Out of sight: nothing. Narration off: a short-lived event with one id per (caster, target).
; Player involved: purge + DirectNarration, otherwise DirectNarration when the speech queue is empty, else that event.
Function HealSend(String phase, int i, String lead)
    Actor target = HealTarget(i)
    Actor caster = HealCaster(i)
    if target == None
        return
    endif
    if !PairVisible(caster, target)
        Verbose("HealSend", phase + " " + target.GetDisplayName() + " out of sight")
        return
    endif
    String map = Injury_Engine_Engine.HealMapText(target)
    if phase == "again" && map == StorageUtil.StringListGet(self, KEY_HS_TEXT, i)
        return
    endif
    StorageUtil.StringListSet(self, KEY_HS_TEXT, i, map)
    StorageUtil.FloatListSet(self, KEY_HS_NARR, i, Utility.GetCurrentRealTime())
    String msg = JoinText(lead, map)
    if msg == ""
        return
    endif
    Actor toward = caster
    if caster == target
        toward = None
    endif
    Actor player = Game.GetPlayer()
    Trace("HealSend", phase + " " + target.GetDisplayName() + " caster:" + NameOf(caster) + ": " + msg)
    if healing_mode == 2
        return
    elseif !DirectAllowed("healing")
        RegisterInjuryShortEvent(msg, target, toward, "healing", heal_short_method, caster, target, SessionOf(phase))
    elseif target == player || caster == player
        SkyrimNetApi.PurgeDialogue(true)
        DirectNarrate(msg, target, toward, "heal", SessionOf(phase))
    elseif SkyrimNetApi.GetSpeechQueueSize() == 0
        DirectNarrate(msg, target, toward, "heal", SessionOf(phase))
    else
        Trace("HealSend", "speech queue busy, event: " + msg)
        RegisterInjuryShortEvent(msg, target, toward, "healing", heal_short_method, caster, target, SessionOf(phase))
    endif
EndFunction

; Called by OnUpdate: refresh sessions whose source is still working, drop those idle for torture_maximum_break
; (or healed / dead) with no line, and send the periodic line while healing is still going on.
Function HealTick()
    if heal_tick_active
        return
    endif
    int i = StorageUtil.FormListCount(self, KEY_HS_TGT) - 1
    if i < 0
        return
    endif
    heal_tick_active = True
    float now = Utility.GetCurrentRealTime()
    while i >= 0
        Actor target = HealTarget(i)
        if target != None && Injury_Engine_Engine.IsHealing(target)
            StorageUtil.FloatListSet(self, KEY_HS_LAST, i, now)
        endif
        if target == None || target.IsDead() || now - StorageUtil.FloatListGet(self, KEY_HS_LAST, i) >= torture_maximum_break
            HealRemove(i)
        elseif now - StorageUtil.FloatListGet(self, KEY_HS_LAST, i) <= torture_again_recent && now - StorageUtil.FloatListGet(self, KEY_HS_NARR, i) >= torture_direct_narration_gap
            HealSend("again", i, "")
        endif
        i -= 1
    endwhile
    heal_tick_active = False
EndFunction

;----------------------------------------------------------------------------------------------------
; Health sync: wounds hold health down, healing above that converts back into points
; (Injury_Engine_Engine.SyncHealth). Runs every 0.2 s while any actor is tracked.
; Real (non-whip) damage reaches Papyrus as queued hits (TakeHits) plus the mod event
; "Injury_Engine_Injury", which starts this loop.
;----------------------------------------------------------------------------------------------------
Int Property HEAL_START = 0 AutoReadOnly
Int Property HEAL_PAIN_GONE = 1 AutoReadOnly
Int Property HEAL_FULL = 2 AutoReadOnly
Int Property HEAL_STOPPED = 3 AutoReadOnly
Int Property HEAL_SOURCE_NONE = 0 AutoReadOnly ; SyncHealth source: no healing source (natural healing)

Float Property SYNC_INTERVAL = 0.2 AutoReadOnly

; Schedules the 0.2 s sync loop once; a second call while one is pending does not reset the timer.
Function StartSync()
    if !dll_ok || sync_pending
        return
    endif
    sync_pending = True
    RegisterForSingleUpdate(SYNC_INTERVAL)
EndFunction

Function ProcessSyncEventBatch()
    int[] codes = Injury_Engine_Engine.TakeSyncEvents()
    if codes.Length == 0
        return
    endif
    Actor[] targets = Injury_Engine_Engine.SyncActors()
    Actor[] casters = Injury_Engine_Engine.SyncCasters()
    int i = 0
    while i < codes.Length
        NarrateSourceHeal(codes[i] / 10000, (codes[i] / 1000) % 10, codes[i] % 1000, casters[i], targets[i])
        i += 1
    endwhile
EndFunction

Function DrainInjuryQueues()
    NarrateDeaths()
    NarrateRealHits()
    ProcessSyncEventBatch()
EndFunction

Event OnInjury(string eventName, string strArg, float numArg, Form sender)
    Verbose("OnInjury", "injury/heal queued, draining")
    DrainInjuryQueues()
    StartSync()
EndEvent

Event OnUpdate()
    sync_pending = False
    NarrateDeaths()
    NarrateRealHits()
    SceneTick()
    int[] codes = Injury_Engine_Engine.SyncHealth()
    if codes.Length > 0
        Actor[] targets = Injury_Engine_Engine.SyncActors()
        Actor[] casters = Injury_Engine_Engine.SyncCasters()
        int i = 0
        while i < codes.Length
            int code = codes[i]
            NarrateSourceHeal(code / 10000, (code / 1000) % 10, code % 1000, casters[i], targets[i])
            i += 1
        endwhile
    endif
    HealTick()
    if Injury_Engine_Engine.TrackedCount() > 0 || StorageUtil.FormListCount(self, KEY_SC_VIC) > 0 || StorageUtil.FormListCount(self, KEY_HS_TGT) > 0
        sync_pending = True
        RegisterForSingleUpdate(SYNC_INTERVAL)
    else
        Verbose("OnUpdate", "nothing tracked, sync loop stops")
    endif
EndEvent

; Killing blows queued by the DLL (a death soon after a real hit): one death line each. In sight with narration on:
; DirectNarration (once per death, no cooloff); otherwise a short-lived injury_death event. Mode Event: always the event; Silent: nothing.
Function NarrateDeaths()
    String[] texts = Injury_Engine_Engine.TakeDeaths()
    if texts.Length == 0
        return
    endif
    Actor[] victims = Injury_Engine_Engine.DeathVictims()
    Actor[] attackers = Injury_Engine_Engine.DeathAttackers()
    String[] sources = Injury_Engine_Engine.DeathSources()
    int i = 0
    while i < texts.Length && i < victims.Length && i < attackers.Length && i < sources.Length
        Actor victim = victims[i]
        if victim != None && death_mode != 2 && texts[i] != ""
            ClearTortureScene(victim)
            ClearHealBatch(victim)
            if DirectAllowed("death") && PairVisible(attackers[i], victim)
                DirectNarrate(texts[i], attackers[i], victim, "health", "death")
            else
                Trace("NarrateDeaths", sources[i] + " short-lived event: " + texts[i])
                ShortEvent(texts[i], attackers[i], victim, "health", InjuryShortEventId("death", sources[i], attackers[i], victim),                     "injury_death", (event_ttl * 1000.0) as int, "death")
            endif
        endif
        i += 1
    endwhile
EndFunction

; Hits queued by the damage hooks (real damage and hooked torture): narrate like lashes.
; One drain at a time (hits_draining); a call while one runs returns, the running drain takes its hits.
Function NarrateRealHits()
    if hits_draining
        return
    endif
    hits_draining = True
    int[] codes = Injury_Engine_Engine.TakeHits()
    while codes.Length > 0
        Actor[] victims = Injury_Engine_Engine.HitVictims()
        Actor[] attackers = Injury_Engine_Engine.HitAttackers()
        bool[] unarmed = Utility.CreateBoolArray(codes.Length)
        int[] counts = Utility.CreateIntArray(codes.Length)
        int i = 0
        while i < codes.Length
            unarmed[i] = Injury_Engine_Engine.HitUnarmed(i)
            counts[i] = Injury_Engine_Engine.HitCount(i)
            i += 1
        endwhile
        i = 0
        while i < codes.Length && i < victims.Length && i < attackers.Length
            if victims[i] != None && victims[i].IsDead()
                Trace("NarrateRealHits", NameOf(victims[i]) + " is dead, hit not narrated (the death line covers it)")
            elseif victims[i] != None
                Trace("NarrateRealHits", NameOf(attackers[i]) + " -> " + NameOf(victims[i]) + " " + SourceOfCode(codes[i]) + " part:" + PartsOf(codes[i]) \
                    + " level:" + LevelOf(codes[i]) + " fresh:" + FreshOf(codes[i]) + " hits:" + counts[i])
                NarrateHits(attackers[i], victims[i], SourceOfCode(codes[i]), MaskOf(codes[i]), LevelOf(codes[i]), counts[i], "", unarmed[i])
                ClearHealBatch(victims[i])
                if !Injury_Engine_Engine.IsTorture(SourceOfCode(codes[i]))
                    ClearTortureScene(victims[i]) ; real damage replaces the torture scene silently
                endif
            endif
            i += 1
        endwhile
        codes = Injury_Engine_Engine.TakeHits()
    endwhile
    hits_draining = False
EndFunction

; Every sync event is a short-lived event. Heal start (a healing source) also opens the target's heal session with
; its start line at once; the session ends on its own (HealTick). The healed actor speaks, toward the caster.
Function NarrateSourceHeal(int kind, int source, int percent, Actor caster, Actor target)
    if target == None
        return
    endif
    String msg = Injury_Engine_Engine.SourceHealText(kind, source, percent, caster, target)
    if msg == ""
        return
    endif
    Actor toward = None
    if caster != target
        toward = caster
    endif
    if verbose_logging
        Verbose("NarrateSourceHeal", target.GetDisplayName() + " kind:" + kind + " source:" + source + " percent:" + percent \
            + " caster:" + NameOf(caster) + " session:" + HealFind(target) + " " + GateText(target))
    endif
    heal_short_method = HealSyncSourceMethod(source)
    Trace("NarrateSourceHeal", "short-lived kind:" + kind + " source:" + source + ": " + msg)
    RegisterInjuryShortEvent(msg, target, toward, "healing", heal_short_method, caster, target)
    if source == HEAL_SOURCE_NONE || kind != HEAL_START
        return
    endif
    int s = HealFind(target)
    if s >= 0
        StorageUtil.FloatListSet(self, KEY_HS_LAST, s, Utility.GetCurrentRealTime())
        if caster != None
            StorageUtil.FormListSet(self, KEY_HS_CASTER, s, caster)
        endif
        return
    endif
    HealOpen(target, caster, msg)
EndFunction

String Function NameOf(Actor akActor) global
    if akActor == None
        return "Someone"
    endif
    return akActor.GetDisplayName()
EndFunction

;----------------------------------------------------------------------------------------------------
; Decorator injury_engine_injuries(uuid): Injury_Engine_Engine.GetStateJson
;   {"sources":[{"id","looks_heading","feels_intro","feels_outro","parts":[{"part","region","stage","look","feel"}]}]}
;----------------------------------------------------------------------------------------------------
String Function Injuries_Decorator(Actor akActor) global
    if akActor == None
        return "{\"sources\":[]}"
    endif
    return Injury_Engine_Engine.GetStateJson(akActor)
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
    Injury_Engine_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    if damage_amount < 1
        damage_amount = 1
    endif
    if Injury_Engine_Engine.IsTorture(damage_type)
        return main.Damage(attacker, victim, cause, damage_type, damage_amount, body_part, narrate)
    endif
    if !Injury_Engine_Engine.HasHealth(damage_type)
        Trace("External_Damage", "unknown injury type: " + damage_type)
        return -1
    endif
    int source = Injury_Engine_Engine.RealHit(victim, attacker, damage_type, damage_amount)
    Trace("External_Damage", NameOf(attacker) + " -> " + NameOf(victim) + " " + damage_type + " real points:" + damage_amount + " source:" + source)
    if narrate && cause != ""
        main.RegisterInjuryShortEvent(cause, attacker, victim, "health", damage_type, attacker, victim)
    endif
    return source
EndFunction

; External_Damage for a torture type, silent, that returns the feeling text instead of a hit code: the hits land
; (damage_amount hits of damage_type on body_part, "" = random), nothing is narrated and no event is sent, and the
; text the victim's injuries would have been narrated with is returned for the caller to narrate itself
; ("Nina's nipples throb in tight, bruised points. ..."). attacker may be None.
; Returns "" on failure (Injury Engine not ready, victim None or dead, not a torture type, no hit landed).
String Function External_TortureText(Actor attacker, Actor victim, String damage_type, String body_part = "", Int damage_amount = 1) global
    Injury_Engine_Main main = Get()
    if main == None || victim == None || victim.IsDead() || !Injury_Engine_Engine.IsTorture(damage_type)
        return ""
    endif
    return main.TortureText(attacker, victim, damage_type, body_part, damage_amount)
EndFunction

; Older API; use External_Damage.
; count whip hits on part (part name or region, "" = random; see Injury_Engine_Engine.PartIndex).
; narrate: one DirectNarration of text plus the injury text (text alone while narration is off), so the
; caller's line is never lost. Returns the last hit code, or -1.
Int Function External_Strike(Actor attacker, Actor victim, String text, Int count = 1, String part = "", Bool narrate = True) global
    Injury_Engine_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    return main.Strike(attacker, victim, text, count, part, narrate, main.SOURCE_WHIP)
EndFunction

; Older API; use External_Damage.
; External_Strike for any torture type: "whipping", "pinching", "slapping", "pulling", "punching", "fire", "frost",
; "shock", "falling", or a torture file another mod adds (see Injury_Engine_Engine.IsTorture).
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
    Injury_Engine_Main main = Get()
    if main == None || victim == None || victim.IsDead()
        return -1
    endif
    if torture
        if !Injury_Engine_Engine.IsTorture(type)
            Trace("External_Injure", "not a torture type: " + type)
            return -1
        endif
        return main.Strike(attacker, victim, text, count, part, narrate, type)
    endif
    if !Injury_Engine_Engine.HasHealth(type)
        Trace("External_Injure", "no health model: " + type)
        return -1
    endif
    if count < 1
        count = 1
    endif
    int source = Injury_Engine_Engine.RealHit(victim, attacker, type, count)
    Trace("External_Injure", NameOf(attacker) + " -> " + NameOf(victim) + " " + type + " real points:" + count + " source:" + source)
    if narrate && text != ""
        DirectNarrate(text, attacker, victim, "health")
    endif
    return source
EndFunction

; A heal of health points worth on part ("" = any). The heal narration follows through the usual sync;
; narrate sends text now. Returns whip points healed, or -1.
Int Function External_Heal(Actor caster, Actor target, String text, Float health, String part = "", Bool narrate = True) global
    Injury_Engine_Main main = Get()
    if main == None || target == None
        return -1
    endif
    int healed = Injury_Engine_Engine.HealPart(target, Injury_Engine_Engine.PartIndex(part), health)
    Trace("External_Heal", NameOf(caster) + " -> " + NameOf(target) + " part:" + part + " health:" + health + " points:" + healed)
    if narrate && text != ""
        DirectNarrate(text, caster, target, "heal")
    endif
    main.StartSync()
    return healed
EndFunction

; Torture points per part of target (Injury_Engine_Engine.PartPointsJson); "" when not ready.
String Function External_PartPoints(Actor target) global
    if Get() == None || target == None
        return ""
    endif
    return Injury_Engine_Engine.PartPointsJson(target)
EndFunction

Bool Function External_SexualParts() global
    Injury_Engine_Main main = Get()
    return main != None && main.include_sexual_parts
EndFunction

; Writes injury_engine.body.include_sexual_parts and applies it now.
Function External_SetSexualParts(Bool enabled) global
    Injury_Engine_Main main = Get()
    if main == None
        return
    endif
    String value = "false"
    if enabled
        value = "true"
    endif
    Bool ok = SkyrimNetApi.PatchConfig(main.PLUGIN_CONFIG, "{\"injury_engine\":{\"body\":{\"include_sexual_parts\":" + value + "}}}")
    main.include_sexual_parts = enabled
    Injury_Engine_Engine.SetIncludeSexualParts(enabled)
    Trace("External_SetSexualParts", "enabled:" + enabled + " patched:" + ok)
EndFunction

; External_Damage for a torture type: count hits on part, then NarrateHits with cause (narrate) or nothing.
Int Function Damage(Actor attacker, Actor victim, String cause, String source, Int count, String part, Bool narrate)
    int index = Injury_Engine_Engine.PartIndex(part)
    int mask = 0
    int worst = 0
    int code = -1
    int hits = 0
    int i = 0
    while i < count
        code = Injury_Engine_Engine.HitPart(victim, attacker, source, index)
        if code >= 0
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
        NarrateHits(attacker, victim, source, mask, worst, hits, cause)
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
EndFunction

; External_TortureText: the hits of Damage, no narration; returns Injury_Engine_Engine.HitText of them ("" when none landed).
String Function TortureText(Actor attacker, Actor victim, String source, String part, Int count)
    int index = Injury_Engine_Engine.PartIndex(part)
    if count < 1
        count = 1
    endif
    int mask = 0
    int worst = 0
    int hits = 0
    int code = -1
    int i = 0
    while i < count
        code = Injury_Engine_Engine.HitPart(victim, attacker, source, index)
        if code >= 0
            hits += 1
            mask = Math.LogicalOr(mask, MaskOf(code))
            if LevelOf(code) > worst
                worst = LevelOf(code)
            endif
        endif
        i += 1
    endwhile
    Trace("TortureText", NameOf(attacker) + " -> " + NameOf(victim) + " " + source + " part:" + part + " count:" + count + " hits:" + hits + " parts:" + PartsText(mask) + " level:" + worst)
    if hits == 0
        return ""
    endif
    ClearHealBatch(victim)
    StartSync()
    return Injury_Engine_Engine.HitText(source, attacker, victim, mask, worst, hits)
EndFunction

Int Function Strike(Actor attacker, Actor victim, String text, Int count, String part, Bool narrate, String source)
    int index = Injury_Engine_Engine.PartIndex(part)
    if count < 1
        count = 1
    endif
    int mask = 0
    int worst = 0
    int code = -1
    int i = 0
    while i < count
        code = Injury_Engine_Engine.HitPart(victim, attacker, source, index)
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
            DirectNarrate(text, attacker, victim, "torture")
        endif
        return code
    endif
    if narrate
        String msg = text
        if narration_enabled
            String injury = Injury_Engine_Engine.HitText(source, attacker, victim, mask, worst, count)
            if injury != ""
                if msg != ""
                    msg += " "
                endif
                msg += injury
            endif
        endif
        if msg != ""
            DirectNarrate(msg, attacker, victim, "torture")
        endif
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
    endif
    ClearHealBatch(victim)
    StartSync()
    return code
EndFunction
