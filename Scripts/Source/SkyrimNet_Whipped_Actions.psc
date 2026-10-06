Scriptname SkyrimNet_Whipped_Actions extends Quest

; LLM action "Whip_Target": the speaker walks to the target and lashes them with the padded whip.
; Registered from Papyrus (not YAML) so eligibility can check the speaker's inventory
; when whip.action.require_whip is on. Texts: SKSE/Plugins/SkyrimNet_Whipped/injuries/whipping.json.

SkyrimNet_Whipped_Main Property main Auto

String Property ACTION_NAME = "Whip_Target" AutoReadOnly

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[SkyrimNet_Whipped_Actions] " + func + ": " + msg)
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
    ; description from injuries/whipping.json action.description ({target} and {strikes} are SkyrimNet placeholders)
    SkyrimNetApi.RegisterAction(ACTION_NAME, \
            SkyrimNet_Whipped_Engine.Text(main.SOURCE_WHIP, "action.description"), \
            "SkyrimNet_Whipped_Actions", "Whip_IsEligible", \
            "SkyrimNet_Whipped_Actions", "Whip_Execute", \
            "", "PAPYRUS", 1, \
            "{\"target\": \"Actor\", \"strikes\": \"" + strikes + "\"}")
    Trace("Setup", "registered " + ACTION_NAME + " strikes:" + strikes)
EndFunction

Bool Function Whip_IsEligible(Actor akActor, string contextJson, string paramsJson) global
    SkyrimNet_Whipped_Main whip_main = SkyrimNet_Whipped_Main.Get()
    if whip_main == None || akActor == None
        return false
    endif
    if akActor == Game.GetPlayer() || akActor.IsDead() || akActor.IsInCombat()
        return false
    endif
    if whip_main.require_whip && akActor.GetItemCount(whip_main.PaddedWhip) < 1
        return false
    endif
    return true
EndFunction

Function Whip_Execute(Actor speaker, string contextJson, string paramsJson) global
    SkyrimNet_Whipped_Main whip_main = SkyrimNet_Whipped_Main.Get()
    if whip_main == None || speaker == None
        return
    endif
    Actor target = SkyrimNetApi.GetJsonActor(paramsJson, "target", None)
    if target == None || target == speaker || target.IsDead()
        Trace("Whip_Execute", "no valid target: " + paramsJson)
        return
    endif
    int strikes = SkyrimNetApi.GetJsonString(paramsJson, "strikes", "3") as int
    if strikes < 1
        strikes = 1
    elseif strikes > whip_main.max_strikes
        strikes = whip_main.max_strikes
    endif

    Weapon whip = whip_main.PaddedWhip
    Bool given = false
    if speaker.GetItemCount(whip) < 1
        if whip_main.require_whip
            SkyrimNetApi.RegisterEvent("whip_lash", SkyrimNet_Whipped_Engine.ActionText(whip_main.SOURCE_WHIP, "action.no_whip", speaker, target, 0), \
                speaker, target)
            return
        endif
        speaker.AddItem(whip, 1, true)
        given = true
    endif

    Trace("Whip_Execute", speaker.GetDisplayName() + " -> " + target.GetDisplayName() + " strikes:" + strikes + " given:" + given)
    Weapon previous = speaker.GetEquippedWeapon(false)
    StorageUtil.SetIntValue(speaker, whip_main.KEY_IN_ACTION, 1)

    speaker.EquipItem(whip, false, true)
    if speaker.GetDistance(target) > 120.0
        speaker.PathToReference(target, 1.0)
    endif
    speaker.SetAngle(0.0, 0.0, speaker.GetAngleZ() + speaker.GetHeadingAngle(target))
    speaker.DrawWeapon()
    Utility.Wait(1.0)

    int done = 0
    int worst = -1 ; hit code with the highest level, for the summary narration
    while done < strikes && !target.IsDead() && !speaker.IsDead()
        Debug.SendAnimationEvent(speaker, "attackStart")
        Utility.Wait(0.45)
        int code = whip_main.Lash(speaker, target, false)
        if code >= 0 && (worst < 0 || code % 10 >= worst % 10)
            worst = code
        endif
        done += 1
        Utility.Wait(0.75)
    endwhile

    Utility.Wait(0.5) ; let a late enchantment hit from the last swing see the in-action flag
    StorageUtil.UnsetIntValue(speaker, whip_main.KEY_IN_ACTION)
    speaker.SheatheWeapon()
    if previous != None && previous != whip
        speaker.EquipItem(previous, false, true)
    else
        speaker.UnequipItem(whip, false, true)
    endif
    if given
        speaker.RemoveItem(whip, 1, true)
    endif

    if done > 0
        NarrateSummary(whip_main, speaker, target, done, worst)
    endif
EndFunction

; injuries/whipping.json narration.summary ("<speaker> whips <target> once/twice/several times/again and again ...")
; plus the hardest-hitting lash's narration. No digits: the actor would repeat them.
Function NarrateSummary(SkyrimNet_Whipped_Main whip_main, Actor speaker, Actor target, int done, int worst) global
    String msg = SkyrimNet_Whipped_Engine.ActionText(whip_main.SOURCE_WHIP, "narration.summary.text", speaker, target, done)
    if worst >= 0
        msg += " " + whip_main.LashText(speaker, target, worst)
    endif
    StorageUtil.SetFloatValue(target, whip_main.KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
    if whip_main.narration_enabled
        SkyrimNetApi.DirectNarration(msg, speaker, target)
    else
        SkyrimNetApi.RegisterEvent("whip_lash", msg, speaker, target)
    endif
EndFunction
