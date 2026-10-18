# Handoff: `Stop_Fighting` / `Start_Fighting` LLM actions (v01)

Status: **planned, not started.** No code has changed. Baseline commit: `c385cae` (Add External_PartPoints). v01 lines up the plan with the code after bb8b669 … c385cae (torture can't kill, the calm on combat start, the fight-reaction hook). The main change: the Classifier calm now affects both actions (see "Interaction with the torture calm").

## Request (user's words)
> add a LLM action that lets the actor choose to stop fighting. gated on in_combat, has the actor exit combat with a target. Add an LLM action that lets the actor start combat with a target

## Decisions the user made
- **Stop_Fighting** is gated on the speaker being in combat. After the LLM picks the action, ask SkyrimNet's decision model (Jev) **whether the target accepts**. Model this on the strategy decision in `../SkyrimNet_SexLab` (`SKSE_Source/src/StrategyDecision.cpp`).
  - **Target refuses:** light touch on both sides. Stop combat on the speaker, on the target if it is fighting the speaker, and clear the speaker's alarm. No Aggression change.
  - **Target accepts:** both sides stop, as above, and every NPC involved is calmed with Aggression 0.
- **Start_Fighting** has no combat gate, so it can also switch targets mid-fight. It still excludes the player and dead actors.

## Open choices the plan settles (change them if you disagree)
- **Target is the player:** no LLM is asked; the speaker side gets the accept path right away.
- **No decision possible** (API missing, no decision provider, request already in flight): treated as refused, so the light-touch path runs. Log the warning once.
- **Calm lasts** until 24 game hours pass (constant `TRUCE_HOURS`, no dashboard setting), or until `Start_Fighting` involves that actor.
- **Action descriptions and outcome text** are `AutoReadOnly` constants in `Injury_Engine_Actions.psc`, not injury JSON. They are combat text, not wound data. LLM-facing text has no numbers (standing rule).

## Interaction with the torture calm (new since the first handoff)
- `Classifier` already stops combat after torture. Its `TESCombatEvent` sink (`Classifier.cpp:459-482`) ends a victim's combat against its torturer when it starts within 3 s (`kCalmWindow`) of a torture hit, in either order. `StopCombatLater` (`Classifier.cpp:426-443`) does the stop: `StopCombat` + `ProcessLists::StopCombatAndAlarmOnActor`, as an SKSE task.
- `SuppressFriendHits` (`Classifier.cpp:579`) also sets `kIgnoreFriendlyHits` on a lashed or punched victim for 3 s.
- **Start_Fighting:** right after a torture, a victim told to fight its torturer is stopped by the calm.
  - Fix: add `Classifier::ForgetCalm(a, b)`. It erases the `g_calm` / `g_combatStart` entries for the pair (both directions) and releases the friendly-hits flag.
  - Papyrus native: `Injury_Engine_Engine.ForgetCalm(Actor, Actor)`. `StartFighting_Execute` calls it before `StartCombat`.
- **Stop_Fighting:** reuse the calm's stronger stop rather than Papyrus `StopCombat` + `StopCombatAlarm` alone.
  - Expose the body of `StopCombatLater` as `Classifier::StopFighting(actor)`.
  - Papyrus native: `Injury_Engine_Engine.StopFighting(Actor)`. `ApplyTruce` calls it.
- The fight-reaction hook does not block Start_Fighting: combat started by SkyrimNet actions does not pass through the hit handler (KB "Torture punches and combat").

## Implementation plan

### Papyrus: `Scripts/Source/Injury_Engine_Actions.psc`
Existing pattern: `Setup()` → `SkyrimNetApi.RegisterAction(name, desc, "Injury_Engine_Actions", "<X>_IsEligible", "Injury_Engine_Actions", "<X>_Execute", "", "PAPYRUS", 1, paramsJson)` (Actions.psc:20-39). Eligibility and execute are `global` functions, and `Injury_Engine_Main.Get()` returns the main quest. Follow the style of `Torture_IsEligible` / `Torture_Execute` (Actions.psc:47-150): `[v]` traces gated on `verbose_logging`, and `ShortEvent(..., (main.event_ttl * 1000.0) as int)`. `Main.Setup` (Main.psc:130) runs on every load and calls `actions.Setup()` at Main.psc:160-165. It registers its mod events at Main.psc:139-142; copy that `UnRegisterForModEvent` + `RegisterForModEvent` pattern.
- Register both actions with params `{"target": "Actor"}`.
- `StopFighting_IsEligible`: not the player, not dead, `IsInCombat()`.
- `StartFighting_IsEligible`: not the player, not dead.
- `StopFighting_Execute`:
  - Checks the target (not None, not the speaker, not dead).
  - If the target is the player, calls `ApplyTruce(speaker, target, true)` directly.
  - Otherwise calls the native `Injury_Engine_Engine.AskTruce(speaker, target)` (async).
- `StartFighting_Execute`:
  - Calls `RestoreAggression` on the speaker and on the target.
  - Calls `Injury_Engine_Engine.ForgetCalm(speaker, target)`.
  - Then `speaker.StartCombat(target)`.
  - Then a short event (type `injury_combat`).
- Answer path:
  - `Setup` registers for mod event `Injury_Engine_Truce` (do `UnRegisterForModEvent` first).
  - The handler drains `Engine.TakeTruces()` with the arrays `TruceSpeakers()`, `TruceTargets()` and `TruceAccepted()`. This follows the existing `TakeDeaths` / `DeathVictims` drain (`Main.NarrateDeaths`).
  - Then `ApplyTruce` for each answer.
- `ApplyTruce(speaker, target, accepted)`:
  - Always: `Injury_Engine_Engine.StopFighting` on the speaker and on the target (the target only if it is an NPC).
  - If accepted: `Calm` on each NPC. `Calm` saves the original Aggression in StorageUtil float `injury_engine.truce_aggression` (only if none is saved yet), saves the expiry game time in StorageUtil float `injury_engine.truce_until`, sets AV Aggression 0 and adds the actor to a StorageUtil form list on the quest.
  - Then `Injury_Engine_Main.ShortEvent(text, speaker, target, "combat", "truce_<speaker>_to_<target>", "injury_truce", (main.event_ttl * 1000.0) as int)`.
- `RestoreAggression(actor)`: puts the saved value back, unsets both keys and removes the actor from the list.
- Expiry:
  - `Main` already has `OnUpdateGameTime` (Main.psc:928, for heal sync; it re-registers every `hours_per_point`).
  - Update registrations are per script, so Actions gets its own `RegisterForSingleUpdateGameTime(TRUCE_HOURS)` and its own `OnUpdateGameTime`. That handler restores every actor in the list whose `truce_until` has passed and re-registers for the next one.
  - `Actions.Setup` also restores expired truces on load, so a missed update cannot leave Aggression at 0 for good.

### C++: new `SKSE_Source/src/TruceDecision.cpp` + `SKSE_Source/include/TruceDecision.h`
- **SkyrimNet public API:**
  - Header: `C:\Skyrim\dev\mods\skyrimnet devkit\CppAPI\PublicAPI.h`. Add `"${CMAKE_CURRENT_SOURCE_DIR}/../../SkyrimNet devkit/CppAPI"` to the existing `target_include_directories` block (`SKSE_Source/CMakeLists.txt:28-32`; SexLab has the same line at its CMakeLists.txt:84).
  - The header **defines** its function-pointer globals, so include it (after `<Windows.h>`) in exactly one .cpp: TruceDecision.cpp.
  - Call `FindFunctions()` once at kDataLoaded.
  - The decision API is v12+, `PublicSendCustomDecisionToLLM` (PublicAPI.h ~877-883). Its callback runs on a SkyrimNet ThreadPool thread: copy the strings, then `SKSE::GetTaskInterface()->AddTask`. Make no RE:: calls in the callback.
  - The answer string for a `noul` question is `"true"` / `"false"` (yes when p >= 0.5). On failure, the json is `{"error": "<code>"}`; `no_decisions_route` means no provider is set up, so latch it and warn once (see `NoteFailure` in StrategyDecision.cpp).
  - SexLab also gates on `CrossDllStdStringSafe()`, which is false in `_DEBUG` builds. Do the same.
- **API:**
  - `Init()`: in the kDataLoaded block of `plugin.cpp` `OnMessage` (plugin.cpp:59-64, after `InjuryEngine::InitDeaths()`).
  - `Reset()`: in the kNewGame / kPostLoadGame case (plugin.cpp:65-68, beside `Overlay::Reset()`). Clears the pending map and the no-route latch.
  - `Ask(speaker, target)`: one request per speaker in flight.
  - Result queue: `{speaker FormID, target FormID, accepted}`. On push, send `SKSE::ModCallbackEvent{"Injury_Engine_Truce", "", 0.0f, nullptr}` through `SKSE::GetModCallbackEventSource()`. This copies the pattern of `NotifyPapyrus` (`InjuryEngine.cpp:888-893`), a helper in an anonymous namespace there, so write a local copy rather than call it.
- **Classifier additions** (`SKSE_Source/include/Classifier.h`, `SKSE_Source/src/Classifier.cpp`): `ForgetCalm(RE::Actor*, RE::Actor*)` and `StopFighting(RE::Actor*)`, as described in "Interaction with the torture calm".
- **Context JSON:**
  - `speaker_uuid`, `speaker_name`, `target_uuid`, `target_name` (`PublicFormIDToUUID`).
  - `relationship` label: copy `Relationship()` from SexLab's StrategyDecision.cpp.
  - `speaker_health` / `target_health` as words (unhurt / lightly hurt / badly hurt / near death, from the Health AV percentage).
  - `speaker_is_follower` / `target_is_follower`.
  - `timeline`: the target's recent events through `PublicGetRecentEvents`, a reduced copy of SexLab's `BuildTimeline` / `SplitSpeech` (skip `hit` / `combat` / `spell`).
- **Natives** in `SKSE_Source/src/Papyrus_Engine.cpp`: register them beside `TakeDeaths` … `DeathText` (Papyrus_Engine.cpp:374-378). Declare them in `Scripts/Source/Injury_Engine_Engine.psc` beside the death natives (Engine.psc:94-98).
  - `AskTruce(Actor, Actor)`.
  - `int TakeTruces()`.
  - `Actor[] TruceSpeakers()`, `Actor[] TruceTargets()`, `Bool[] TruceAccepted()`.
  - `ForgetCalm(Actor, Actor)`, `StopFighting(Actor)`.

### Decision template
- New file `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/prompts/decisions/injury_engine/accept_truce.prompt`. Template name `injury_engine/accept_truce`. The `decisions/` folder is new: `prompts/` holds only `submodules/` today.
- Format reference: `../SkyrimNet_SexLab/SKSE/Plugins/SkyrimNet/external/goodprovider.sexlab/prompts/decisions/sexlab/minigame_strategy.prompt`. It uses a `{# #}` header comment, `[ state <id> ]` / `[ end state ]` blocks and `[ question <id> noul ]` / `[ end question ]`. A yes/no example is the `speak` question in `minigame_player_turn.prompt`.
- States: the target (`render_character_profile("short_inline", target_uuid)`), the speaker, the fight (relationship, both health words) and the timeline.
- Question: `[ question accept noul ]` "Does {{ target_name }} accept {{ speaker_name }}'s offer to stop fighting right now?"
  - Leans yes: losing or badly hurt, friends or allies, a misunderstanding.
  - Leans no: sworn enemies, winning, a grudge in the timeline.

### Docs to update
- `docs/authors/actions.md:3` says "The mod's one LLM action". Rewrite it, and add both actions, the truce decision, the `injury_truce` / `injury_combat` events and the 24 h calm.
- `CLAUDE.md` Flow: one paragraph, including how the actions use the Classifier calm (`ForgetCalm` / `StopFighting`).
- `README.md` features.
- `docs/developers/papyrus.md`: the Actions.psc row (line 12) and the StorageUtil key table (around line 65): `injury_engine.truce_aggression`, `injury_engine.truce_until`, the calmed-actor list.
- `docs/developers/overlay.md`: the devkit include path and the API v12 requirement.
- `CHANGELOG-developer.md` / `CHANGELOG-player.md`.
- `KNOWLEDGEBASE.md`, next to "Torture is not aggression" (line 29):
  - Papyrus `StopCombat` does not stick on hostile NPCs unless Aggression is lowered.
  - The torture calm would cancel a Start_Fighting right after torture, which is why `ForgetCalm` exists.

## Reference material found
- Prior art for the combat calls: SeverActions `ceasefighting.yaml` / `attacktarget.yaml` and `Source/Scripts/SeverActions_Combat.psc` (`CeaseFire_Execute`, `AttackTarget_Execute`). Their ceasefire zeroes aggression natively. Action names here must not collide with `CeaseFighting` / `AttackTarget`.
- Devkit docs for the decision template format: `C:\Skyrim\dev\mods\skyrimnet devkit\docs\modding\WORKFLOW_PROMPTS.md`, and the `PublicSendCustomDecisionToLLM` comment in `PublicAPI.h` (~line 877).
- In-repo stop code to reuse: `Classifier::StopCombatLater` (`Classifier.cpp:426`).

## Confidence
About 85%. Risks:
- Whether `StartCombat` sticks for a follower attacking an ally. The NPC may drop combat at once; lowering the relationship rank may be needed.
- Whether SkyrimNet resolves decision templates from `external/<id>/prompts/decisions/` for this plugin the same way it does for SexLab.
- The torture calm: if `ForgetCalm` misses a case, a Start_Fighting within 3 s of a torture hit is cancelled. Also unverified in game: whether `StopCombatAndAlarmOnActor` clears a bounty alarm on the player (KB "Torture is not aggression").
- SE only; VR is not tested.

## Deploy / test once implemented
1. `make dll` → `SKSE/Plugins/Injury_Engine.dll`.
2. `pyro skyrimse.ppj` → `Scripts/*.pex`.
3. No ESP change.
4. In game: load a save, then in the SkyrimNet webUI go to Game Data Explorer → Refresh Actions. Then test:
   - `Start_Fighting` on a bandit.
   - Torture an NPC, then within 3 s `Start_Fighting` between the pair: the fight starts and stays (no `calm:` line in `Injury_Engine.log` with verbose logging on).
   - Out of combat, `Stop_Fighting` is not offered.
   - Mid-fight, `Stop_Fighting`: `Injury_Engine.log` shows the decision send and answer. On accept, the fight ends and does not restart (`GetAV Aggression` = 0). On refuse, it may resume.
   - Without a decision provider, the light-touch path runs.
   - After 24 game hours, Aggression is restored. Also save during a truce, wait past the expiry, then reload: it is restored on load.
