Scriptname Injury_Engine_Actions extends Quest

; LLM actions Torture_Target, Stop_Fighting, Stop_Fighting_by_Yielding and Start_Fighting. They are SkyrimNet YAML
; actions (SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/actions/<name>.yaml): eligibility is in the YAML,
; and each one calls the member function of the same role below with its positional parameters (speaker first).
; Torture texts: SKSE/Plugins/Injury_Engine/injuries/<type>.json (no_whip, summary).

Injury_Engine_Main Property main Auto

; A truce accepted by the target calms every NPC involved (Aggression 0) for this many game hours
Float Property TRUCE_HOURS = 24.0 AutoReadOnly
; StorageUtil: original Aggression and the expiry game time (days) per calmed actor; the calmed actors, on this quest
String Property KEY_TRUCE_AGGRESSION = "injury_engine.truce_aggression" AutoReadOnly
String Property KEY_TRUCE_UNTIL = "injury_engine.truce_until" AutoReadOnly
String Property KEY_TRUCE_LIST = "injury_engine.truce_actors" AutoReadOnly
; StorageUtil: the truce partner per calmed actor (the DLL's truce guard is noted again from it on load)
String Property KEY_TRUCE_WITH = "injury_engine.truce_with" AutoReadOnly
; TakeTruces / TruceSpeakers / ... share one C++ buffer: only one drain at a time.
Bool Property truces_draining = False Auto Hidden

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[Injury_Engine_Actions] " + func + ": " + msg)
    if notification
        Debug.Notification(msg)
    endif
EndFunction

Function Setup()
    truces_draining = False
    UnRegisterForModEvent("Injury_Engine_Truce")
    RegisterForModEvent("Injury_Engine_Truce", "OnTruce")
    RestoreExpiredTruces(True) ; a truce that ran out while the game was not running; the guard for the others
EndFunction

Injury_Engine_Actions Function GetActions() global
    return Game.GetFormFromFile(0x800, "Injury_Engine.esp") as Injury_Engine_Actions
EndFunction

; Action type -> injury source id ("" for an unknown type). Aliases live on each torture file as action.alias.
String Function SourceOfType(String type) global
    return Injury_Engine_Engine.SourceOfAlias(type)
EndFunction

; Torture_Target.yaml (out of combat). type: whip|pinch|slap|pull|punch; strikes: 1..10, clamped to max_strikes.
; Hand torture needs no item, so the whip requirement only matters once the type is chosen.
Function TortureTarget(Actor speaker, Actor target, String type, String strikeCount)
    Injury_Engine_Main whip_main = main
    if whip_main == None || speaker == None || speaker == Game.GetPlayer() || speaker.IsDead() || speaker.IsInCombat()
        Trace("TortureTarget", "speaker can't torture now: " + speaker)
        return
    endif
    if target == None || target == speaker || target.IsDead()
        Trace("TortureTarget", "no valid target: " + target)
        return
    endif
    String source = SourceOfType(type)
    if source == "" || !Injury_Engine_Engine.IsTorture(source)
        Trace("TortureTarget", "unknown or unloaded type '" + type + "', using whip")
        source = whip_main.SOURCE_WHIP
    endif
    int strikes = strikeCount as int
    if strikes < 1
        strikes = 3
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

    Trace("TortureTarget", speaker.GetDisplayName() + " -> " + target.GetDisplayName() + " " + source + " strikes:" + strikes + " given:" + given)
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
            Trace("TortureTarget", "[v] " + source + " strike " + done + "/" + strikes + " code:" + code \
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


;----------------------------------------------------------------------------------------------------
; Stop_Fighting.yaml (in combat): the speaker offers a truce; the decision model answers for the target
; (TruceDecision.cpp, template injury_engine/accept_truce). The player as target answers in a Yes / No message box.
;----------------------------------------------------------------------------------------------------
Function StopFighting(Actor speaker, Actor target)
    if speaker == None || speaker == Game.GetPlayer() || speaker.IsDead()
        return
    endif
    if target == None || target == speaker || target.IsDead()
        Trace("StopFighting", "no valid target: " + target)
        return
    endif
    Trace("StopFighting", speaker.GetDisplayName() + " offers " + target.GetDisplayName() + " to stop fighting")
    Injury_Engine_Engine.AskTruce(speaker, target, false) ; answer: mod event Injury_Engine_Truce
EndFunction

;----------------------------------------------------------------------------------------------------
; Stop_Fighting_by_Yielding.yaml (in combat): the speaker surrenders. It stands down and is calmed at once; the decision
; model answers for the target (template injury_engine/accept_surrender). The player as target answers in a Yes / No
; message box.
;----------------------------------------------------------------------------------------------------
Function StopFightingByYielding(Actor speaker, Actor target)
    if speaker == None || speaker == Game.GetPlayer() || speaker.IsDead()
        return
    endif
    if target == None || target == speaker || target.IsDead()
        Trace("StopFightingByYielding", "no valid target: " + target)
        return
    endif
    Trace("StopFightingByYielding", speaker.GetDisplayName() + " yields to " + target.GetDisplayName())
    Injury_Engine_Engine.StopFighting(speaker)
    Calm(speaker, target)
    speaker.SheatheWeapon()
    Injury_Engine_Engine.AskTruce(speaker, target, true) ; answer: mod event Injury_Engine_Truce
EndFunction

; Truce and surrender answers from the DLL (one per answered AskTruce; a repeat to the same target while one is
; waiting is ignored, one to another target is refused; refused when no decision could be asked).
Event OnTruce(String eventName, String strArg, Float numArg, Form sender)
    if truces_draining
        return
    endif
    truces_draining = True
    int n = Injury_Engine_Engine.TakeTruces()
    while n > 0
        Actor[] speakers = Injury_Engine_Engine.TruceSpeakers()
        Actor[] targets = Injury_Engine_Engine.TruceTargets()
        int[] accepted = Injury_Engine_Engine.TruceAccepted()
        int[] yields = Injury_Engine_Engine.TruceYields()
        int i = 0
        while i < n && i < speakers.Length && i < targets.Length && i < accepted.Length && i < yields.Length
            ApplyTruce(speakers[i], targets[i], accepted[i] != 0, yields[i] != 0)
            i += 1
        endwhile
        n = Injury_Engine_Engine.TakeTruces()
    endwhile
    truces_draining = False
EndEvent

; Truce refused: the speaker stands down (and its alarm), the target too if it is fighting the speaker; no Aggression
; change. Truce accepted: both stand down and every NPC involved is calmed (Aggression 0 for TRUCE_HOURS).
; Surrender (yielded): the speaker already stood down and is calmed. Accepted: the target stands down and is calmed
; too. Refused: the target is left alone and may keep attacking.
Function ApplyTruce(Actor speaker, Actor target, Bool accepted, Bool yielded)
    if speaker == None || target == None || speaker.IsDead()
        return
    endif
    Bool targetNpc = target != Game.GetPlayer() && !target.IsDead()
    Bool targetFights = targetNpc && target.GetCombatTarget() == speaker
    String text
    if yielded
        if accepted
            ; the first stop came before the answer (maybe while downed, where it may not last)
            Injury_Engine_Engine.StopFighting(speaker)
            if targetNpc
                Injury_Engine_Engine.StopFighting(target)
                Calm(target, speaker)
            endif
            GuardTruce(speaker, target)
            text = speaker.GetDisplayName() + " yields to " + target.GetDisplayName() + ", and " + target.GetDisplayName() + " accepts the surrender."
        else
            text = speaker.GetDisplayName() + " yields to " + target.GetDisplayName() + ", but " + target.GetDisplayName() + " refuses the surrender."
        endif
    else
        Injury_Engine_Engine.StopFighting(speaker)
        if targetNpc && (accepted || targetFights)
            Injury_Engine_Engine.StopFighting(target)
        endif
        if accepted
            Calm(speaker, target)
            if targetNpc
                Calm(target, speaker)
            endif
            GuardTruce(speaker, target)
            text = speaker.GetDisplayName() + " offers to stop fighting, and " + target.GetDisplayName() + " accepts. They lower their weapons."
        else
            text = speaker.GetDisplayName() + " offers to stop fighting, but " + target.GetDisplayName() + " refuses."
        endif
    endif
    Trace("ApplyTruce", text + " (target was fighting the speaker: " + targetFights + ")")
    String id = "truce_"
    if yielded
        id = "surrender_"
    endif
    Injury_Engine_Main.ShortEvent(text, speaker, target, "combat", id + speaker.GetFormID() + "_to_" + target.GetFormID(),         "injury_truce", (main.event_ttl * 1000.0) as int)
EndFunction

; Accepted truce or surrender: one end time for the DLL guard and both actors' saved truce (KEY_TRUCE_WITH /
; KEY_TRUCE_UNTIL, which RestoreExpiredTruces notes again on load). A refused surrender never gets here, so it leaves
; no guard behind.
Function GuardTruce(Actor speaker, Actor target)
    Float untilTime = Utility.GetCurrentGameTime() + TRUCE_HOURS / 24.0
    Actor[] pair = new Actor[2]
    pair[0] = speaker
    pair[1] = target
    int i = 0
    while i < 2
        Actor a = pair[i]
        if a != Game.GetPlayer() && StorageUtil.HasFloatValue(a, KEY_TRUCE_AGGRESSION)
            StorageUtil.SetFormValue(a, KEY_TRUCE_WITH, pair[1 - i])
            StorageUtil.SetFloatValue(a, KEY_TRUCE_UNTIL, untilTime)
        endif
        i += 1
    endwhile
    Injury_Engine_Engine.NoteTruce(speaker, target, untilTime)
EndFunction

; Aggression 0 until TRUCE_HOURS pass (or Start_Fighting involves the actor). The first saved value is kept. `other`
; is only for the trace: the guard partner is saved by GuardTruce once the truce is accepted.
Function Calm(Actor a, Actor other)
    if a == None || a == Game.GetPlayer()
        return
    endif
    if !StorageUtil.HasFloatValue(a, KEY_TRUCE_AGGRESSION)
        StorageUtil.SetFloatValue(a, KEY_TRUCE_AGGRESSION, a.GetBaseActorValue("Aggression"))
    endif
    StorageUtil.SetFloatValue(a, KEY_TRUCE_UNTIL, Utility.GetCurrentGameTime() + TRUCE_HOURS / 24.0)
    a.SetActorValue("Aggression", 0.0)
    StorageUtil.FormListAdd(self, KEY_TRUCE_LIST, a, false)
    if main.verbose_logging
        Trace("Calm", "[v] " + a.GetDisplayName() + " aggression 0 toward " + other + ", was " + StorageUtil.GetFloatValue(a, KEY_TRUCE_AGGRESSION))
    endif
EndFunction

Function RestoreAggression(Actor a)
    if a == None || !StorageUtil.HasFloatValue(a, KEY_TRUCE_AGGRESSION)
        return
    endif
    Float value = StorageUtil.GetFloatValue(a, KEY_TRUCE_AGGRESSION)
    a.SetActorValue("Aggression", value)
    StorageUtil.UnsetFloatValue(a, KEY_TRUCE_AGGRESSION)
    StorageUtil.UnsetFloatValue(a, KEY_TRUCE_UNTIL)
    StorageUtil.UnsetFormValue(a, KEY_TRUCE_WITH)
    StorageUtil.FormListRemove(self, KEY_TRUCE_LIST, a, true)
    Trace("RestoreAggression", a.GetDisplayName() + " aggression back to " + value)
EndFunction

; From Setup (every load; `noteGuard`: the DLL's truce guard is noted again for active truces) and
; Main.OnUpdateGameTime (every hours_per_point game hours).
Function RestoreExpiredTruces(Bool noteGuard = False)
    Float now = Utility.GetCurrentGameTime()
    int i = StorageUtil.FormListCount(self, KEY_TRUCE_LIST) - 1
    while i >= 0
        Actor a = StorageUtil.FormListGet(self, KEY_TRUCE_LIST, i) as Actor
        if a == None
            StorageUtil.FormListRemoveAt(self, KEY_TRUCE_LIST, i)
        elseif StorageUtil.GetFloatValue(a, KEY_TRUCE_UNTIL, 0.0) <= now
            RestoreAggression(a)
        elseif noteGuard
            Actor other = StorageUtil.GetFormValue(a, KEY_TRUCE_WITH) as Actor
            if other != None
                Injury_Engine_Engine.NoteTruce(a, other, StorageUtil.GetFloatValue(a, KEY_TRUCE_UNTIL))
            endif
        endif
        i -= 1
    endwhile
EndFunction

;----------------------------------------------------------------------------------------------------
; Start_Fighting.yaml: no combat gate, so it also switches targets mid-fight. Ends a truce on either side and the
; torture calm between the pair first, so the fight is not stopped right away.
;----------------------------------------------------------------------------------------------------
Function StartFighting(Actor speaker, Actor target)
    if speaker == None || speaker == Game.GetPlayer() || speaker.IsDead()
        return
    endif
    if target == None || target == speaker || target.IsDead()
        Trace("StartFighting", "no valid target: " + target)
        return
    endif
    RestoreAggression(speaker)
    RestoreAggression(target)
    ; RestoreAggression drops both actors' saved truces with anyone: drop the DLL guards to match
    Injury_Engine_Engine.ForgetTruces(speaker)
    Injury_Engine_Engine.ForgetTruces(target)
    Injury_Engine_Engine.ForgetCalm(speaker, target)
    speaker.StartCombat(target)
    String text = speaker.GetDisplayName() + " attacks " + target.GetDisplayName() + "."
    Trace("StartFighting", text)
    Injury_Engine_Main.ShortEvent(text, speaker, target, "combat", "fight_" + speaker.GetFormID() + "_to_" + target.GetFormID(), \
        "injury_combat", (main.event_ttl * 1000.0) as int)
EndFunction
