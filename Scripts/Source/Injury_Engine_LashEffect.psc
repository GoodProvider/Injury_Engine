Scriptname Injury_Engine_LashEffect extends ActiveMagicEffect

; Carried by the whip's enchantment: fires once per real weapon hit.

Injury_Engine_Main Property main Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if main == None
        return
    endif
    ; Scripted strikes from the LLM action already called OnLash; don't count them twice.
    if akCaster != None && StorageUtil.GetIntValue(akCaster, main.KEY_IN_ACTION, 0) > 0
        return
    endif
    main.OnLash(akCaster, akTarget)
EndEvent
