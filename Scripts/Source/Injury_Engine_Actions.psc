Scriptname Injury_Engine_Actions extends Quest

; LLM action "Torture_Target": the speaker walks to the target and tortures them: whips them with the padded whip,
; or pinches, slaps, pulls or punches them by hand. Registered from Papyrus (not YAML) so eligibility can check the
; speaker's inventory. Texts: SKSE/Plugins/Injury_Engine/injuries/whipping.json (action description) and each
; type's own injuries/<type>.json (no_whip, summary).

Injury_Engine_Main Property main Auto

String Property ACTION_NAME = "Torture_Target" AutoReadOnly
String Property TYPES = "whip|pinch|slap|pull|punch" AutoReadOnly

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[Injury_Engine_Actions] " + func + ": " + msg)
    if notification
        Debug.Notification(msg)
    endif
EndFunction

Function Setup()
    String strikes = "1"
    int i = 2
    while i <= main.max_strikes
        strikes += "|" + i
        i += 1
    endwhile
    String typeList = Injury_Engine_Engine.ActionAliases()
    if typeList == ""
        typeList = TYPES
    endif
    ; description from injuries/whipping.json action.description ({target}, {type} and {strikes} are SkyrimNet placeholders)
    SkyrimNetApi.RegisterAction(ACTION_NAME, \
            Injury_Engine_Engine.Text(main.SOURCE_WHIP, "action.description"), \
            "Injury_Engine_Actions", "Torture_IsEligible", \
            "Injury_Engine_Actions", "Torture_Execute", \
            "", "PAPYRUS", 1, \
            "{\"target\": \"Actor\", \"type\": \"" + typeList + "\", \"strikes\": \"" + strikes + "\"}")
    Trace("Setup", "registered " + ACTION_NAME + " types:" + typeList + " strikes:" + strikes)
EndFunction

; Action type -> injury source id ("" for an unknown type). Aliases live on each torture file as action.alias.
String Function SourceOfType(String type) global
    return Injury_Engine_Engine.SourceOfAlias(type)
EndFunction

; Hand torture needs no item, so the whip requirement only matters once the type is chosen (Torture_Execute).
Bool Function Torture_IsEligible(Actor akActor, string contextJson, string paramsJson) global
    Injury_Engine_Main whip_main = Injury_Engine_Main.Get()
    if whip_main == None || akActor == None
        return false
    endif
    if akActor == Game.GetPlayer() || akActor.IsDead() || akActor.IsInCombat()
        if whip_main.verbose_logging
            Trace("Torture_IsEligible", "[v] " + akActor.GetDisplayName() + " no: player/dead/in combat")
        endif
        return false
    endif
    if whip_main.verbose_logging
        Trace("Torture_IsEligible", "[v] " + akActor.GetDisplayName() + " yes")
    endif
    return true
EndFunction

Function Torture_Execute(Actor speaker, string contextJson, string paramsJson) global
    Injury_Engine_Main whip_main = Injury_Engine_Main.Get()
    if whip_main == None || speaker == None
        return
    endif
    Actor target = SkyrimNetApi.GetJsonActor(paramsJson, "target", None)
    if target == None || target == speaker || target.IsDead()
        Trace("Torture_Execute", "no valid target: " + paramsJson)
        return
    endif
    String source = SourceOfType(SkyrimNetApi.GetJsonString(paramsJson, "type", "whip"))
    if source == "" || !Injury_Engine_Engine.IsTorture(source)
        Trace("Torture_Execute", "unknown or unloaded type, using whip: " + paramsJson)
        source = whip_main.SOURCE_WHIP
    endif
    int strikes = SkyrimNetApi.GetJsonString(paramsJson, "strikes", "3") as int
    if strikes < 1
        strikes = 1
    elseif strikes > whip_main.max_strikes
        strikes = whip_main.max_strikes
    endif
    Bool whipping = source == whip_main.SOURCE_WHIP

    Weapon whip = whip_main.PaddedWhip
    Bool given = false
    Weapon previous = None
    if whipping
        if speaker.GetItemCount(whip) < 1
            if whip_main.require_whip
                String noWhip = Injury_Engine_Engine.ActionText(whip_main.SOURCE_WHIP, "action.no_whip", speaker, target, 0)
                Injury_Engine_Main.ShortEvent(noWhip, speaker, target, "torture", "whip_lash_no_whip_" + speaker.GetFormID(), "whip_lash", (whip_main.event_ttl * 1000.0) as int)
                return
            endif
            speaker.AddItem(whip, 1, true)
            given = true
        endif
        previous = speaker.GetEquippedWeapon(false)
    endif

    Trace("Torture_Execute", speaker.GetDisplayName() + " -> " + target.GetDisplayName() + " " + source + " strikes:" + strikes + " given:" + given)
    StorageUtil.SetIntValue(speaker, whip_main.KEY_IN_ACTION, 1)

    if whipping
        speaker.EquipItem(whip, false, true)
    endif
    if speaker.GetDistance(target) > 120.0
        speaker.PathToReference(target, 1.0)
    endif
    speaker.SetAngle(0.0, 0.0, speaker.GetAngleZ() + speaker.GetHeadingAngle(target))
    if whipping
        speaker.DrawWeapon()
    endif
    Utility.Wait(1.0)

    int done = 0
    while done < strikes && !target.IsDead() && !speaker.IsDead()
        int code
        if whipping
            Debug.SendAnimationEvent(speaker, "attackStart")
            Utility.Wait(0.45)
            code = whip_main.Lash(speaker, target)
        else
            ; by hand: no real swing (it would land as real damage and start a fight); the type's animation, if any
            code = whip_main.Torture(speaker, target, source)
        endif
        done += 1
        if whip_main.verbose_logging
            Trace("Torture_Execute", "[v] " + source + " strike " + done + "/" + strikes + " code:" + code \
                + " distance:" + (speaker.GetDistance(target) / whip_main.UNITS_PER_METER) + "m")
        endif
        Utility.Wait(0.75)
    endwhile

    Utility.Wait(0.5) ; let a late enchantment hit from the last swing see the in-action flag
    StorageUtil.UnsetIntValue(speaker, whip_main.KEY_IN_ACTION)
    if whipping
        speaker.SheatheWeapon()
        if previous != None && previous != whip
            speaker.EquipItem(previous, false, true)
        else
            speaker.UnequipItem(whip, false, true)
        endif
        if given
            speaker.RemoveItem(whip, 1, true)
        endif
    endif
EndFunction

