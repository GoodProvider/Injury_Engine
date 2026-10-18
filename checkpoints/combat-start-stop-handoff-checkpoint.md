# Handoff: `Stop_Fighting` / `Start_Fighting` LLM actions

Status: **planned, not started.** No code has changed. The user stopped at the plan stage and asked for this handoff.

## Request (user's words)
> add a LLM action that lets the actor choose to stop fighting. gated on in_combat, has the actor exit combat with a target. Add an LLM action that lets the actor start combat with a target

## Decisions the user made
- **Stop_Fighting** is gated on the speaker being in combat. After the LLM picks the action, ask SkyrimNet's decision model (Jev) **whether the target accepts**. Model this on the strategy decision in `../SkyrimNet_SexLab` (`SKSE_Source/src/StrategyDecision.cpp`).
  - **Target refuses:** light touch on both sides. `StopCombat` on the speaker, `StopCombat` on the target if it is fighting the speaker, `StopCombatAlarm` on the speaker. No Aggression change.
  - **Target accepts:** both sides stop, as above, and every NPC involved is calmed with Aggression 0.
- **Start_Fighting** has no combat gate, so it can also switch targets mid-fight. It still excludes the player and dead actors.

## Open choices the plan settles (change them if you disagree)
- **Target is the player:** no LLM is asked; the speaker side gets the accept path right away.
- **No decision possible** (API missing, no decision provider, request already in flight): treated as refused, so the light-touch path runs. Log the warning once.
- **Calm lasts** until 24 game hours pass (constant `TRUCE_HOURS`, no dashboard setting), or until `Start_Fighting` involves that actor.
- **Action descriptions and outcome text** are `AutoReadOnly` constants in `Injury_Engine_Actions.psc`, not injury JSON. They are combat text, not wound data. LLM-facing text has no numbers (standing rule).

## Implementation plan

### Papyrus: `Scripts/Source/Injury_Engine_Actions.psc`
Existing pattern: `Setup()` → `SkyrimNetApi.RegisterAction(name, desc, "Injury_Engine_Actions", "<X>_IsEligible", "Injury_Engine_Actions", "<X>_Execute", "", "PAPYRUS", 1, paramsJson)`. Eligibility and execute are `global` functions, and `Injury_Engine_Main.Get()` returns the main quest. `Setup()` runs on every load from `Main.Setup` (Main.psc:162).
- Register both actions with params `{"target": "Actor"}`.
- `StopFighting_IsEligible`: not the player, not dead, `IsInCombat()`.
- `StartFighting_IsEligible`: not the player, not dead.
- `StopFighting_Execute`:
  - Checks the target (not None, not the speaker, not dead).
  - If the target is the player, calls `ApplyTruce(speaker, target, true)` directly.
  - Otherwise calls the native `Injury_Engine_Engine.AskTruce(speaker, target)` (async).
- `StartFighting_Execute`:
  - Calls `RestoreAggression` on the speaker and on the target.
  - Then `speaker.StartCombat(target)`.
  - Then a short event (type `injury_combat`).
- Answer path:
  - `Setup` registers for mod event `Injury_Engine_Truce` (do `UnRegisterForModEvent` first, as Main does).
  - The handler drains `Engine.TakeTruces()` with the arrays `TruceSpeakers()`, `TruceTargets()` and `TruceAccepted()`. This follows the existing `TakeDeaths` / `DeathVictims` drain.
  - Then `ApplyTruce` for each answer.
- `ApplyTruce(speaker, target, accepted)`:
  - Always: `StopCombat` on both sides (the target only if it is an NPC) and `speaker.StopCombatAlarm()`.
  - If accepted: `Calm` on each NPC. `Calm` saves the original Aggression in StorageUtil float `injury_engine.truce_aggression` (only if not already saved), sets AV Aggression 0 and adds the actor to a StorageUtil form list on the quest.
  - Then `Injury_Engine_Main.ShortEvent(text, speaker, target, "combat", "truce_<speaker>_to_<target>", "injury_truce", (main.event_ttl*1000) as int)`.
- `RestoreAggression(actor)`: puts the saved value back, unsets the key and removes the actor from the list.
- Expiry: `RegisterForSingleUpdateGameTime(TRUCE_HOURS)`; `OnUpdateGameTime` restores everyone calmed.
  - Check first that no other script on the same quest (Main) uses `OnUpdateGameTime`.

### C++: new `SKSE_Source/src/TruceDecision.cpp` + `SKSE_Source/include/TruceDecision.h`
- **SkyrimNet public API:**
  - Header: `C:\Skyrim\dev\mods\skyrimnet devkit\CppAPI\PublicAPI.h`. SexLab uses `"${CMAKE_CURRENT_SOURCE_DIR}/../../SkyrimNet devkit/CppAPI"` in `target_include_directories`; add the same line to `SKSE_Source/CMakeLists.txt`.
  - The header **defines** its function-pointer globals, so include it (after `<Windows.h>`) in exactly one .cpp: TruceDecision.cpp.
  - Call `FindFunctions()` once at kDataLoaded.
  - The decision API is v12+, `PublicSendCustomDecisionToLLM`. Its callback runs on a SkyrimNet ThreadPool thread: copy the strings, then `SKSE::GetTaskInterface()->AddTask`. Make no RE:: calls in the callback.
  - The answer string for a `noul` question is `"true"` / `"false"` (yes when p >= 0.5). On failure, the json is `{"error": "<code>"}`; `no_decisions_route` means no provider is set up, so latch it and warn once (see `NoteFailure` in StrategyDecision.cpp).
  - SexLab also gates on `CrossDllStdStringSafe()`, which is false in `_DEBUG` builds. Do the same.
- **API:**
  - `Init()`: kDataLoaded, in `plugin.cpp` `OnMessage`.
  - `Reset()`: kNewGame / kPostLoadGame. Clears the pending map and the no-route latch.
  - `Ask(speaker, target)`: one request per speaker in flight.
  - Result queue: `{speaker FormID, target FormID, accepted}`. On push, send `SKSE::ModCallbackEvent{"Injury_Engine_Truce", "", 0.0f, nullptr}`, the same as `NotifyPapyrus` in `InjuryEngine.cpp:800`.
- **Context JSON:**
  - `speaker_uuid`, `speaker_name`, `target_uuid`, `target_name` (`PublicFormIDToUUID`).
  - `relationship` label: copy `Relationship()` from SexLab's StrategyDecision.cpp.
  - `speaker_health` / `target_health` as words (unhurt / lightly hurt / badly hurt / near death, from the Health AV percentage).
  - `speaker_is_follower` / `target_is_follower`.
  - `timeline`: the target's recent events through `PublicGetRecentEvents`, a reduced copy of SexLab's `BuildTimeline` / `SplitSpeech` (skip `hit` / `combat` / `spell`).
- **Natives** in `SKSE_Source/src/Papyrus_Engine.cpp` (register beside `TakeDeaths`, around line 357), declared in `Scripts/Source/Injury_Engine_Engine.psc`:
  - `AskTruce(Actor, Actor)`.
  - `int TakeTruces()`.
  - `Actor[] TruceSpeakers()`, `Actor[] TruceTargets()`, `Bool[] TruceAccepted()`.

### Decision template
- New file `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/prompts/decisions/injury_engine/accept_truce.prompt`. Template name `injury_engine/accept_truce`.
- Format reference: `../SkyrimNet_SexLab/SKSE/Plugins/SkyrimNet/external/goodprovider.sexlab/prompts/decisions/sexlab/minigame_strategy.prompt`. It uses a `{# #}` header comment, `[ state <id> ]` / `[ end state ]` blocks and `[ question <id> noul ]` / `[ end question ]`. A yes/no example is the `speak` question in `minigame_player_turn.prompt`.
- States: the target (`render_character_profile("short_inline", target_uuid)`), the speaker, the fight (relationship, both health words) and the timeline.
- Question: `[ question accept noul ]` "Does {{ target_name }} accept {{ speaker_name }}'s offer to stop fighting right now?"
  - Leans yes: losing or badly hurt, friends or allies, a misunderstanding.
  - Leans no: sworn enemies, winning, a grudge in the timeline.

### Docs to update
- `docs/authors/actions.md`: it currently says "the mod's one LLM action". Add both actions, the truce decision, the `injury_truce` / `injury_combat` events and the 24 h calm.
- `CLAUDE.md` Flow: one paragraph.
- `README.md` features.
- `docs/developers/papyrus.md`: the StorageUtil keys.
- `docs/developers/overlay.md`: the devkit include path and the API v12 requirement.
- `CHANGELOG-developer.md` / `CHANGELOG-player.md`.
- `KNOWLEDGEBASE.md`: Papyrus `StopCombat` does not stick on hostile NPCs unless Aggression is lowered.

## Reference material found
- Prior art for the combat calls: SeverActions `ceasefighting.yaml` / `attacktarget.yaml` and `Source/Scripts/SeverActions_Combat.psc` (`CeaseFire_Execute`, `AttackTarget_Execute`). Their ceasefire zeroes aggression natively. Action names here must not collide with `CeaseFighting` / `AttackTarget`.
- Devkit docs for the decision template format: `C:\Skyrim\dev\mods\skyrimnet devkit\docs\modding\WORKFLOW_PROMPTS.md`, and the `PublicSendCustomDecisionToLLM` comment in `PublicAPI.h` (~line 850).

## Confidence
About 85%. Risks:
- Whether `StartCombat` sticks for a follower attacking an ally. The NPC may drop combat at once; lowering the relationship rank may be needed.
- Whether SkyrimNet resolves decision templates from `external/<id>/prompts/decisions/` for this plugin the same way it does for SexLab.
- SE only; VR is not tested.

## Deploy / test once implemented
1. `make dll` → `SKSE/Plugins/Injury_Engine.dll`.
2. `pyro skyrimse.ppj` → `Scripts/*.pex`.
3. No ESP change.
4. In game: load a save, then in the SkyrimNet webUI go to Game Data Explorer → Refresh Actions. Then test:
   - `Start_Fighting` on a bandit.
   - Out of combat, `Stop_Fighting` is not offered.
   - Mid-fight, `Stop_Fighting`: `Injury_Engine.log` shows the decision send and answer. On accept, the fight ends and does not restart (`GetAV Aggression` = 0). On refuse, it may resume.
   - Without a decision provider, the light-touch path runs.
   - After 24 game hours, Aggression is restored.
