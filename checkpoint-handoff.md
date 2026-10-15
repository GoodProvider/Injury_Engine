# Checkpoint handoff

## Request
"When a leash needs third person and it's in VR mod, fail, don't try to go to 3rd person."

## Findings
- Injury_Engine has no leash or camera code. The leash code is in the sibling repo `c:\Skyrim\dev\mods\SkyrimNet_Leashed`.
- The only forced switch is `Game.ForceThirdPerson()` in `ApplyToHolder`, `Scripts/Source/SkyrimNet_Leashed_Actions.psc` (~line 1366-1370). It runs for the `holder_shield` fallback when a DD-hidden mesh and a first-person player coincide.
- `PlayerInFirstPerson` (line ~182) is `who == Game.GetPlayer() && Game.GetCameraState() == 0`.
- `WaitForLeashMesh` (~line 700) notifies "You must be in third person view for the leash to work" when no leash bones exist and the player is in first person.
- No VR detection exists in Leashed (no `IsVR` in Papyrus or `SKSE_Source/src`). Natives are registered in `SKSE_Source/src/Papyrus/Bridge.cpp` (~line 253) and declared in `SkyrimNet_Leashed_Native.psc`.

## Proposed plan (not approved, nothing implemented)
1. Add native `IsVR()` (`REL::Module::IsVR()`) to Bridge.cpp and Native.psc.
2. In `ApplyToHolder`, skip `Game.ForceThirdPerson()` on VR, trace it, and fall through to the existing failure path (`NarrateMeshFailed`).
3. VR-specific message in `WaitForLeashMesh`.
4. Notes in KNOWLEDGE.md, CHANGELOG.md and README.md; build with `make dll` and pyro.

Full plan file: `C:\Users\bhuff\.claude\plans\when-a-leash-needs-wobbly-pizza.md`.

## State
- The plan was rejected at ExitPlanMode; the user asked for this file only. No other files were changed.
- Open question: confirm the change belongs in SkyrimNet_Leashed rather than Injury Engine.
