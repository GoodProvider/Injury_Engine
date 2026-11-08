Repo: `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` · baseline `f27c478` (Release 0.1.4) · nothing changed this session

# Goal

Playtest 11:25–11:29 (Bob + Skadi against bandits Trenlor and Olda Windrime). The user never noticed any injury or death narration.
This session found out why from the logs. No code changed.

# Results

Logs: [logs/2026-10-20-11-36-51/](logs/2026-10-20-11-36-51/) (`Injury_Engine.log`, `Papyrus.0.log`, `SkyrimNet.log`, `conversation_log.log`,
`openrouter_input.tail.log`, `MANIFEST.txt`). No crash this session (newest crash log is 2026-10-18).

Settings in force ([Papyrus.0.log:1961-1963](logs/2026-10-20-11-36-51/Papyrus.0.log#L1961)): narration on, all four modes `0` (Direct Narration),
`minor_min` **9** (the user lowered it from 25), verbose logging on.

**The DLL works.** All 4 deaths got a record and text ([Injury_Engine.log](logs/2026-10-20-11-36-51/Injury_Engine.log), grep `death`):
11:26:41 Trenlor, 11:26:48 Olda, 11:28:24 Trenlor, 11:28:35 Olda. Real hits reached Papyrus as `NarrateRealHits` lines.

**Papyrus sent almost everything as short-lived events, not DirectNarration.**
- Deaths: all 4 went out as `injury_death` events ([Papyrus.0.log:3123](logs/2026-10-20-11-36-51/Papyrus.0.log#L3123), 3442, 6450, 6842).
- Real hits: 3 DirectNarrations (2974, 3069, 6385), about 15 short-lived events.
- In `NarrateDeathBatch` / `NarrateHits` ([Injury_Engine_Main.psc:1401](../../Scripts/Source/Injury_Engine_Main.psc#L1401), 524) the event is the fallback when
  `DirectAllowed && PairVisible && ClaimNarration` (plus, for hits, `health_narration_cooloff` 6 s) fails. `DirectAllowed` is true (mode 0),
  so the failures are the reply gate or the cooloff.

**The 3 DirectNarrations were answered, but nobody hears the narration itself.** SkyrimNet makes the victim reply to it:
[SkyrimNet.log:4386](logs/2026-10-20-11-36-51/SkyrimNet.log#L4386) DirectNarration → 4581 `dialogue_response triggered by direct_narration` → Trenlor:
"Gah! You bast—!" ([conversation_log.log:14607](logs/2026-10-20-11-36-51/conversation_log.log#L14607)), the same at 14612 and 14614. In a fight these replies
sound like ordinary combat barks, so the injury narration feels missing even when it works.

# Findings

1. **Deaths are starved by the reply gate.** A killing blow usually comes right after a hit on the same pair that used the gate:
   - 11:26:39 DirectNarration (torture Fire) → 11:26:41 death: the reply was still speaking, so the gate was closed.
   - 11:28:24 DirectNarration (slashing) → 11:28:25 death: the same.
   - 11:26:48 / 11:28:35: the gate was closed again (combat speech, Skadi talking at 11:28:32). The exact reason is unknown because
     `ClaimNarration` logs only success ([Injury_Engine_Main.psc:178](../../Scripts/Source/Injury_Engine_Main.psc#L178)), and `PairVisible` is not logged
     on the death path.
   - The death is not retried, so it goes out as an event once and is gone.
2. **Hits are gated three ways:** the 6 s `health_narration_cooloff`, the reply gate (combat barks keep the speech queue busy), and the minor
   band. Many hits had `parts ... below minor, not narrated` even at `minor_min` 9 (spell ticks scatter over parts, a finding carried over from the
   injury-bands checkpoints).
3. **Short-lived events are silent by design.** The LLM only sees them in later prompts and does not react to them.
4. Side observation: at 11:26:48 [Injury_Engine.log](logs/2026-10-20-11-36-51/Injury_Engine.log) says `under torture, killer Bob ... last real hit: burning`, then
   `by Skadi`. The killer named in the torture line and the attacker of the last hit differ. Probably harmless (Skadi's fire landed last), but worth checking.

# Proposed fix (not done; for the user to choose)

- **Deaths take priority:** a death line skips the reply gate (or waits for it: keep it pending, as torture starts do, with a short expiry
  of about 10 s, and let `OnUpdate` retry), and a death replaces any pending hit line for the same victim.
- **Log gate failures in verbose mode:** in `ClaimNarration`, log `since / audio / queue -> closed` too. On the death and hit paths, log which
  of `PairVisible`, the cooloff or the claim failed.
- Optional: hit narration only for the first hit of a fight per pair, or for stage increases, so that it doesn't use up the gate right before a death.

# Not yet tested

- Bio looks and the healed line (carried over from injury-bands checkpoint-04).
- Torture: stages, overlay, bleedout (carried over).

# Next

1. Decide on the fix above (death priority with pending retry, plus verbose gate logging).
2. Implement in [Scripts/Source/Injury_Engine_Main.psc](../../Scripts/Source/Injury_Engine_Main.psc) (`NarrateDeathBatch`, `NarrateHits`, `ClaimNarration`), then
   run `compile: papyrus`. No DLL change is expected.
3. In game, with verbose logging on: fight 2 bandits, kill both in sight. In `Papyrus.0.log`, expect `DirectNarration` for each death (session `death`),
   or a logged gate reason. In `conversation_log.log`, expect the victim's or the followers' reaction to the death.
4. Snapshot the logs into `checkpoints/to-quite/logs/` and write the next checkpoint.
