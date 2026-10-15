Scriptname Injury_Engine_PlayerRef extends ReferenceAlias

Injury_Engine_Main Property main Auto

Event OnInit()
    OnPlayerLoadGame() ; new game: run setup without waiting for a reload
EndEvent

Event OnPlayerLoadGame()
    main.Setup()
EndEvent
