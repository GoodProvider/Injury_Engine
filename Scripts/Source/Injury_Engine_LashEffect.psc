Scriptname Injury_Engine_LashEffect extends ActiveMagicEffect

; Carried by the whip's enchantment: fires once per real weapon hit.

Injury_Engine_Main Property main Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if main == None
        return
    endif
    ; Scripted strikes from the LLM action already called OnLash; don't count them twice.
    if akCaster != None && StorageUtil.GetIntValue(akCaster, main.KEY_IN_ACTION, 0) > 0
        float now = Utility.GetCurrentRealTime()
        float stamp = StorageUtil.GetFloatValue(akCaster, main.KEY_IN_ACTION_TIME, -1000.0)
        if now >= stamp && now - stamp <= main.IN_ACTION_STALE
            return
        endif
        ; left over from a Torture_Target that a save / load cut short: this is a real lash
        StorageUtil.UnsetIntValue(akCaster, main.KEY_IN_ACTION)
        StorageUtil.UnsetFloatValue(akCaster, main.KEY_IN_ACTION_TIME)
    endif
    main.OnLash(akCaster, akTarget)
EndEvent
