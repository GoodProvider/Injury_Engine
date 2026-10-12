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
; Torture scene: parallel lists on this quest, one entry per (attacker, victim) pair (no attacker: this quest itself)
String Property KEY_SC_ATT = "skyrimnet_whipped.sc_att" AutoReadOnly
String Property KEY_SC_VIC = "skyrimnet_whipped.sc_vic" AutoReadOnly
String Property KEY_SC_SRC = "skyrimnet_whipped.sc_src" AutoReadOnly
String Property KEY_SC_MASK = "skyrimnet_whipped.sc_mask" AutoReadOnly
String Property KEY_SC_LEVEL = "skyrimnet_whipped.sc_level" AutoReadOnly
String Property KEY_SC_COUNT = "skyrimnet_whipped.sc_count" AutoReadOnly
String Property KEY_SC_LAST = "skyrimnet_whipped.sc_last" AutoReadOnly
String Property KEY_SC_NARR = "skyrimnet_whipped.sc_narr" AutoReadOnly
String Property KEY_SC_STATE = "skyrimnet_whipped.sc_state" AutoReadOnly
String Property KEY_SC_SWITCH = "skyrimnet_whipped.sc_switch" AutoReadOnly
String Property KEY_SC_CAUSE = "skyrimnet_whipped.sc_cause" AutoReadOnly
; KEY_SC_STATE values: not narrated yet / narrated and in sight / out of sight (never narrated)
Int Property SC_PENDING = 0 AutoReadOnly
Int Property SC_RUNNING = 1 AutoReadOnly
Int Property SC_HIDDEN = 2 AutoReadOnly
Int Property SC_SEL_START = 0 AutoReadOnly
Int Property SC_SEL_SWITCH = 1 AutoReadOnly
Int Property SC_SEL_AGAIN = 2 AutoReadOnly
; Heals since the victim's last heal narration: IntList of heal codes (see SkyrimNet_Whipped_Engine.TakeHealed)
String Property KEY_HEAL_PENDING = "skyrimnet_whipped.heal_pending" AutoReadOnly
; Heal session (mirrors the torture scene): parallel lists on this quest, one entry per healed actor.
; caster (this quest when none), last heal activity, last narration, extra text for the next line, end text, ended flag
String Property KEY_HS_TGT = "skyrimnet_whipped.hs_tgt" AutoReadOnly
String Property KEY_HS_CASTER = "skyrimnet_whipped.hs_caster" AutoReadOnly
String Property KEY_HS_LAST = "skyrimnet_whipped.hs_last" AutoReadOnly
String Property KEY_HS_NARR = "skyrimnet_whipped.hs_narr" AutoReadOnly
String Property KEY_HS_EXTRA = "skyrimnet_whipped.hs_extra" AutoReadOnly
String Property KEY_HS_END = "skyrimnet_whipped.hs_end" AutoReadOnly
String Property KEY_HS_ENDED = "skyrimnet_whipped.hs_ended" AutoReadOnly

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
; Model of the last TakePendingHealText (torture / health)
String pending_heal_model = "health"
; Method leg of the last healing short-lived event id (wound source or heal-sync source).
String heal_short_method = "none"

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

; Every DirectNarration goes through here so the exact text sent is in SkyrimNet_Whipped.log ([hh:mm:ss] torture:direct / heal:direct / health:direct).
Function DirectNarrate(String msg, Actor speaker, Actor target, String category = "torture", String session = "start") global
    Trace("DirectNarration", NameOf(speaker) + " -> " + NameOf(target) + ": " + msg)
    SkyrimNet_Whipped_Engine.LogLine(category, "direct", session, msg)
    SkyrimNetApi.DirectNarration(msg, speaker, target)
EndFunction

Function ShortEvent(String msg, Actor speaker, Actor target, String category, String eventId, String eventType, int ttlMs, String session = "start") global
    if msg != ""
        SkyrimNet_Whipped_Engine.LogLine(category, "short", session, msg)
    endif
    SkyrimNetApi.RegisterShortLivedEvent(eventId, eventType, msg, "", ttlMs, speaker, target)
EndFunction

SkyrimNet_Whipped_Main Function Get() global
    return Game.GetFormFromFile(0x800, "SkyrimNet_Whipped.esp") as SkyrimNet_Whipped_Main
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
    torture_maximum_break = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.torture_maximum_break", DefaultNumber("torture_maximum_break", 3.0))
    torture_direct_narration_gap = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.torture_direct_narration_gap", DefaultNumber("torture_direct_narration_gap", 6.0))
    narration_max_distance = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.max_distance", DefaultNumber("narration_max_distance", 15.0))
    event_ttl = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.event_ttl", DefaultNumber("event_ttl", 30.0))
    include_sexual_parts = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.body.include_sexual_parts", DefaultNumber("include_sexual_parts", 0.0) > 0.5)
    Bool show_overlay = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.overlay.enabled", DefaultBool("overlay_enabled"))
    verbose_logging = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.debug.verbose_logging", false)
    if max_strikes < 1
        max_strikes = 1
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
        + " narration:" + narration_enabled + " cooldown:" + narration_cooldown + " torture_window:" + torture_narration_window_size + " break:" + torture_maximum_break + " gap:" + torture_direct_narration_gap + " max_distance:" + narration_max_distance + " event_ttl:" + event_ttl + " sexual_parts:" + include_sexual_parts + " overlay:" + show_overlay + " verbose:" + verbose_logging)
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
    int code = SkyrimNet_Whipped_Engine.Hit(victim, attacker, SOURCE_WHIP)
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
        SkyrimNet_Whipped_Engine.QueueHit(victim, attacker, code)
        NarrateRealHits()
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
    String msg = CauseText(cause) + SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, part_mask, level, hits, noCause, unarmedForm)
    String kind = "health"
    if SkyrimNet_Whipped_Engine.IsTorture(source)
        kind = "torturing"
    endif
    if kind == "health"
        if msg != ""
            Trace("NarrateInjury", source + " short-lived event: " + msg)
            RegisterInjuryShortEvent(msg, attacker, victim, kind, source, attacker, victim)
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

; The combined line (SkyrimNet_Whipped_Engine.SceneText) for the pairs picked.
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
    return SkyrimNet_Whipped_Engine.SceneText(phase, attackers, victims, sources, masks, levels)
EndFunction

; DirectNarration when the speech queue is empty, otherwise a short-lived event.
Function OptionalDirectNarrate(String msg, Actor speaker, Actor target, String session = "start")
    if SkyrimNetApi.GetSpeechQueueSize() == 0
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
    if !narration_enabled
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
    if msg == ""
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

String Function HealMethodFromCode(int code) global
    int raw = code
    if raw >= 100000
        raw = raw - 100000
    endif
    if raw < 10000
        return SkyrimNet_Whipped_Engine.SourceId(raw / 128)
    elseif raw < 20000
        return "part"
    endif
    return SkyrimNet_Whipped_Engine.SourceId(raw - 10000)
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

; Seconds since the victim's last torture hit; never hit (or a stale time from an earlier session) is long ago.
float Function SinceLastTorture(Actor victim)
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_TORTURE, -1.0)
    if last < 0.0 || last > now
        return 100000.0
    endif
    return now - last
EndFunction

; A torture hit within torture_narration_window_size: passive healing ("ordinary, nothing to remark on") is not
; reported yet, so it does not undo the punishment just narrated. The wound still heals.
Bool Function InTortureSession(Actor victim)
    return SinceLastTorture(victim) <= torture_narration_window_size
EndFunction

; Injury model for a heal narration: torture when any code names a torture source (stage or source-healed).
String Function HealCodesModel(int[] codes)
    int i = 0
    while i < codes.Length
        int code = codes[i]
        int source = -1
        if code < 10000
            source = code / 128
        elseif code < 20000
            source = code - 10000
        endif
        if source >= 0 && SkyrimNet_Whipped_Engine.IsTorture(SkyrimNet_Whipped_Engine.SourceId(source))
            return "torture"
        endif
        i += 1
    endwhile
    return "health"
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
    int code = SkyrimNet_Whipped_Engine.Hit(victim, attacker, source)
    if code < 0
        Verbose("Torture", source + " Hit returned " + code + " for " + victim.GetDisplayName())
        return code
    endif
    Trace("Torture", NameOf(attacker) + " -> " + victim.GetDisplayName() + " " + source + " part:" + PartsOf(code) + " level:" + LevelOf(code) + " fresh:" + FreshOf(code))
    if narrate
        SkyrimNet_Whipped_Engine.QueueHit(victim, attacker, code)
        NarrateRealHits()
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
; Other heals join the victim's heal session (mirrors the torture scene): the first opens it with an immediate
; start line, later ones wait in KEY_HEAL_PENDING for HealTick's periodic line or the end line.
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
            heal_short_method = "none"
            RegisterInjuryShortEvent(quiet, victim, None, "healing", "none", None, victim)
        endif
    endif
    if codes.Length == 0
        return
    endif

    SendHealShortEvents(victim, codes, None)

    i = 0
    while i < codes.Length
        StorageUtil.IntListAdd(victim, KEY_HEAL_PENDING, codes[i])
        i += 1
    endwhile
    int s = HealFind(victim)
    if verbose_logging
        Verbose("NarrateHealing", victim.GetDisplayName() + " codes:" + codes.Length + " session:" + s + " " + GateText(victim))
    endif
    if s < 0
        HealOpen(victim, None, "") ; start line: what healed so far
    else
        StorageUtil.FloatListSet(self, KEY_HS_LAST, s, Utility.GetCurrentRealTime())
    endif
EndFunction

; A new hit: drop pending heals and close the victim's heal session without a line.
Function ClearHealBatch(Actor victim)
    StorageUtil.IntListClear(victim, KEY_HEAL_PENDING)
    int s = HealFind(victim)
    if s >= 0
        HealRemove(s)
    endif
EndFunction

;----------------------------------------------------------------------------------------------------
; Heal session, mirroring the torture scene: start line at once when healing begins, one combined line
; every torture_direct_narration_gap while parts heal, end line when healing ends (fully healed, source
; stopped, or no healing for torture_narration_window_size). Lines go out like SceneSend.
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

; Opens a session for target and sends the start line (lead + pending heals) right away. Returns its index.
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
    StorageUtil.StringListAdd(self, KEY_HS_EXTRA, "", true)
    StorageUtil.StringListAdd(self, KEY_HS_END, "", true)
    StorageUtil.IntListAdd(self, KEY_HS_ENDED, 0, true)
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
    StorageUtil.StringListRemoveAt(self, KEY_HS_EXTRA, i)
    StorageUtil.StringListRemoveAt(self, KEY_HS_END, i)
    StorageUtil.IntListRemoveAt(self, KEY_HS_ENDED, i)
EndFunction

; HealText of the victim's pending heal codes (cleared); sets pending_heal_model. "" when none.
String Function TakePendingHealText(Actor target)
    pending_heal_model = "health"
    if InTortureSession(target)
        pending_heal_model = "torture"
    endif
    int[] pending = StorageUtil.IntListToArray(target, KEY_HEAL_PENDING)
    StorageUtil.IntListClear(target, KEY_HEAL_PENDING)
    if pending.Length == 0
        return ""
    endif
    pending_heal_model = HealCodesModel(pending)
    return SkyrimNet_Whipped_Engine.HealText(target, pending)
EndFunction

String Function JoinText(String a, String b) global
    if a == ""
        return b
    elseif b == ""
        return a
    endif
    return a + " " + b
EndFunction

; One heal line for session i: lead (source text) + extra (pain gone) + pending heals, then for "end" the end text.
; Out of sight: start / again lines are skipped (pending heals stay for later), the end is a persistent memory.
; Narration off: a short-lived event with one id per (caster, target). Player involved: purge + DirectNarration,
; otherwise OptionalDirectNarrate.
Function HealSend(String phase, int i, String lead)
    Actor target = HealTarget(i)
    Actor caster = HealCaster(i)
    if target == None
        return
    endif
    Bool visible = PairVisible(caster, target)
    if !visible && phase != "end"
        Verbose("HealSend", phase + " " + target.GetDisplayName() + " out of sight")
        return
    endif
    String msg = JoinText(lead, StorageUtil.StringListGet(self, KEY_HS_EXTRA, i))
    StorageUtil.StringListSet(self, KEY_HS_EXTRA, i, "")
    msg = JoinText(msg, TakePendingHealText(target))
    if phase == "end"
        msg = JoinText(msg, StorageUtil.StringListGet(self, KEY_HS_END, i))
    endif
    StorageUtil.FloatListSet(self, KEY_HS_NARR, i, Utility.GetCurrentRealTime())
    if msg == ""
        return
    endif
    Actor toward = caster
    if caster == target
        toward = None
    endif
    Actor player = Game.GetPlayer()
    Trace("HealSend", phase + " " + target.GetDisplayName() + " caster:" + NameOf(caster) + " visible:" + visible + ": " + msg)
    if !visible
        SkyrimNet_Whipped_Engine.LogLine("heal", "event", SessionOf(phase), msg)
        SkyrimNetApi.RegisterPersistentEvent(msg, target, toward)
    elseif !narration_enabled
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

; One short-lived healing event per (caster, victim, wound method); groups codes that share a method.
Function SendHealShortEvents(Actor victim, int[] codes, Actor caster)
    int[] work = Utility.CreateIntArray(codes.Length)
    int i = 0
    while i < codes.Length
        work[i] = codes[i]
        i += 1
    endwhile
    while work.Length > 0
        String method = HealMethodFromCode(work[0])
        int n = 0
        i = 0
        while i < work.Length
            if HealMethodFromCode(work[i]) == method
                n += 1
            endif
            i += 1
        endwhile
        int[] group = Utility.CreateIntArray(n)
        int[] rest = Utility.CreateIntArray(work.Length - n)
        n = 0
        int r = 0
        i = 0
        while i < work.Length
            if HealMethodFromCode(work[i]) == method
                group[n] = work[i]
                n += 1
            else
                rest[r] = work[i]
                r += 1
            endif
            i += 1
        endwhile
        String text = SkyrimNet_Whipped_Engine.HealText(victim, group)
        Actor toward = caster
        if caster == victim
            toward = None
        endif
        heal_short_method = method
        RegisterInjuryShortEvent(text, victim, toward, "healing", method, caster, victim)
        work = rest
    endwhile
EndFunction

; Called by OnUpdate after the heals of the tick: end lines for ended sessions, periodic lines for the rest.
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
        if target == None || target.IsDead()
            HealRemove(i)
        elseif StorageUtil.IntListGet(self, KEY_HS_ENDED, i) == 1 || now - StorageUtil.FloatListGet(self, KEY_HS_LAST, i) >= torture_narration_window_size
            HealSend("end", i, "")
            HealRemove(i)
        elseif now - StorageUtil.FloatListGet(self, KEY_HS_NARR, i) >= torture_direct_narration_gap
            if StorageUtil.IntListCount(target, KEY_HEAL_PENDING) > 0 || StorageUtil.StringListGet(self, KEY_HS_EXTRA, i) != ""
                HealSend("again", i, "")
            endif
        endif
        i -= 1
    endwhile
    heal_tick_active = False
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

; Schedules the 0.2 s sync loop once; a second call while one is pending does not reset the timer.
Function StartSync()
    if !dll_ok || sync_pending
        return
    endif
    sync_pending = True
    RegisterForSingleUpdate(SYNC_INTERVAL)
EndFunction

Function ProcessSyncEventBatch()
    int[] codes = SkyrimNet_Whipped_Engine.TakeSyncEvents()
    if codes.Length == 0
        return
    endif
    Actor[] targets = SkyrimNet_Whipped_Engine.SyncActors()
    Actor[] casters = SkyrimNet_Whipped_Engine.SyncCasters()
    int i = 0
    while i < codes.Length
        NarrateSourceHeal(codes[i] / 10000, (codes[i] / 1000) % 10, codes[i] % 1000, casters[i], targets[i])
        i += 1
    endwhile
EndFunction

Function DrainInjuryQueues()
    NarrateRealHits()
    Actor[] queued = SkyrimNet_Whipped_Engine.TakeQueuedHealActors()
    int i = 0
    while i < queued.Length
        NarrateHealing(queued[i])
        i += 1
    endwhile
    ProcessSyncEventBatch()
EndFunction

Event OnInjury(string eventName, string strArg, float numArg, Form sender)
    Verbose("OnInjury", "injury/heal queued, draining")
    DrainInjuryQueues()
    StartSync()
EndEvent

Event OnUpdate()
    sync_pending = False
    NarrateRealHits()
    SceneTick()
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
    HealTick()
    if SkyrimNet_Whipped_Engine.TrackedCount() > 0 || StorageUtil.FormListCount(self, KEY_SC_VIC) > 0 || StorageUtil.FormListCount(self, KEY_HS_TGT) > 0
        sync_pending = True
        RegisterForSingleUpdate(SYNC_INTERVAL)
    else
        Verbose("OnUpdate", "nothing tracked, sync loop stops")
    endif
EndEvent

; Hits queued by the damage hooks (real damage and hooked torture): narrate like lashes.
; One drain at a time (hits_draining); a call while one runs returns, the running drain takes its hits.
Function NarrateRealHits()
    if hits_draining
        return
    endif
    hits_draining = True
    int[] codes = SkyrimNet_Whipped_Engine.TakeHits()
    while codes.Length > 0
        Actor[] victims = SkyrimNet_Whipped_Engine.HitVictims()
        Actor[] attackers = SkyrimNet_Whipped_Engine.HitAttackers()
        bool[] unarmed = Utility.CreateBoolArray(codes.Length)
        int[] counts = Utility.CreateIntArray(codes.Length)
        int i = 0
        while i < codes.Length
            unarmed[i] = SkyrimNet_Whipped_Engine.HitUnarmed(i)
            counts[i] = SkyrimNet_Whipped_Engine.HitCount(i)
            i += 1
        endwhile
        i = 0
        while i < codes.Length && i < victims.Length && i < attackers.Length
            if victims[i] != None
                Trace("NarrateRealHits", NameOf(attackers[i]) + " -> " + NameOf(victims[i]) + " " + SourceOfCode(codes[i]) + " part:" + PartsOf(codes[i]) \
                    + " level:" + LevelOf(codes[i]) + " fresh:" + FreshOf(codes[i]) + " hits:" + counts[i])
                NarrateHits(attackers[i], victims[i], SourceOfCode(codes[i]), MaskOf(codes[i]), LevelOf(codes[i]), counts[i], "", unarmed[i])
                ClearHealBatch(victims[i])
                if !SkyrimNet_Whipped_Engine.IsTorture(SourceOfCode(codes[i]))
                    ClearTortureScene(victims[i]) ; real damage replaces the torture scene silently
                endif
            endif
            i += 1
        endwhile
        codes = SkyrimNet_Whipped_Engine.TakeHits()
    endwhile
    hits_draining = False
EndFunction

; Natural healing (no source: pain gone / fully healed) is always a silent short-lived event, text marked as ordinary healing.
; With a source the event drives the target's heal session: start opens it (start line at once), pain gone joins the
; next periodic line, fully healed / stopped end it (HealTick sends the end line). The healed actor speaks, toward the caster.
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
    if verbose_logging
        Verbose("NarrateSourceHeal", target.GetDisplayName() + " kind:" + kind + " source:" + source + " percent:" + percent \
            + " caster:" + NameOf(caster) + " session:" + HealFind(target) + " " + GateText(target))
    endif
    heal_short_method = HealSyncSourceMethod(source)
    Trace("NarrateSourceHeal", "short-lived kind:" + kind + " source:" + source + ": " + msg)
    RegisterInjuryShortEvent(msg, target, toward, "healing", heal_short_method, caster, target)
    if source == HEAL_SOURCE_NONE
        return
    endif
    int s = HealFind(target)
    if s < 0
        if kind == HEAL_STOPPED
            return ; nothing was said about this healing
        endif
        String lead = ""
        if kind != HEAL_FULL
            lead = msg
        endif
        s = HealOpen(target, caster, lead)
        if kind == HEAL_START
            return
        endif
    endif
    StorageUtil.FloatListSet(self, KEY_HS_LAST, s, Utility.GetCurrentRealTime())
    if caster != None
        StorageUtil.FormListSet(self, KEY_HS_CASTER, s, caster)
    endif
    if kind == HEAL_FULL || kind == HEAL_STOPPED
        StorageUtil.StringListSet(self, KEY_HS_END, s, msg)
        StorageUtil.IntListSet(self, KEY_HS_ENDED, s, 1)
    elseif kind == HEAL_PAIN_GONE
        StorageUtil.StringListSet(self, KEY_HS_EXTRA, s, JoinText(StorageUtil.StringListGet(self, KEY_HS_EXTRA, s), msg))
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
        main.RegisterInjuryShortEvent(cause, attacker, victim, "health", damage_type, attacker, victim)
    endif
    return source
EndFunction

; External_Damage for a torture type, silent, that returns the feeling text instead of a hit code: the hits land
; (damage_amount hits of damage_type on body_part, "" = random), nothing is narrated and no event is sent, and the
; text the victim's injuries would have been narrated with is returned for the caller to narrate itself
; ("Nina's nipples throb in tight, bruised points. ..."). attacker may be None.
; Returns "" on failure (Whipped not ready, victim None or dead, not a torture type, no hit landed).
String Function External_TortureText(Actor attacker, Actor victim, String damage_type, String body_part = "", Int damage_amount = 1) global
    SkyrimNet_Whipped_Main main = Get()
    if main == None || victim == None || victim.IsDead() || !SkyrimNet_Whipped_Engine.IsTorture(damage_type)
        return ""
    endif
    return main.TortureText(attacker, victim, damage_type, body_part, damage_amount)
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
        DirectNarrate(text, attacker, victim, "health")
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
        DirectNarrate(text, caster, target, "heal")
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
    int hits = 0
    int i = 0
    while i < count
        code = SkyrimNet_Whipped_Engine.HitPart(victim, attacker, source, index)
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

; External_TortureText: the hits of Damage, no narration; returns SkyrimNet_Whipped_Engine.HitText of them ("" when none landed).
String Function TortureText(Actor attacker, Actor victim, String source, String part, Int count)
    int index = SkyrimNet_Whipped_Engine.PartIndex(part)
    if count < 1
        count = 1
    endif
    int mask = 0
    int worst = 0
    int hits = 0
    int code = -1
    int i = 0
    while i < count
        code = SkyrimNet_Whipped_Engine.HitPart(victim, attacker, source, index)
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
    return SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, mask, worst, hits)
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
        code = SkyrimNet_Whipped_Engine.HitPart(victim, attacker, source, index)
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
            String injury = SkyrimNet_Whipped_Engine.HitText(source, attacker, victim, mask, worst, count)
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
