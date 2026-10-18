# Truce guard: handoff checkpoint v02 (2026-10-16, code-review fixes)

Built and deployed: `make dll` → `SKSE/Plugins/Injury_Engine.dll`; pyro → `Scripts/Injury_Engine_Actions.pex`, `Injury_Engine_Engine.pex`. Not committed.

## Changed
- `Classifier.cpp/.h`: `g_truce` keyed by pair (`TruceKey`), expired pairs pruned, no wholesale clear at 512. New `ForgetTruces(actor)`, `ResetTruces()` (called on kPreLoadGame / kNewGame in `plugin.cpp`).
- `TruceDecision.cpp`: `g_inFlight` is speaker → {target, since}. `Busy()`: same target is ignored, another target is refused right away (`Push`), and a waiting request stops blocking after 60 s. `Done()` erases only a matching target.
- `InjuryEngine.cpp` `UpdateBleedout`: torture gone at health ≤ 0 → drop `g_downed` without `BleedOutStop` (the game owns that bleedout).
- `Injury_Engine_Actions.psc`: `Calm` no longer saves `KEY_TRUCE_WITH`; new `GuardTruce` saves the partner and end time on both NPCs and calls `NoteTruce` with the same end time (accepted only). `StartFighting` calls `ForgetTruces` on both actors.
- `Injury_Engine_Engine.psc`: native `ForgetTruces`.
- Docs: CLAUDE.md truce paragraph, KNOWLEDGEBASE truce / bleedout notes.

## Not changed (review items left)
- #9: guard is still rebuilt from StorageUtil on load (one partner per NPC in StorageUtil).
- #10: pending start / switch lines still wait up to break + reply_timeout (a deliberate change in this diff).

## In-game tests
1. NPC yields to an NPC that refuses → save → load → the refuser can still attack (`Injury_Engine.log` has no `truce: ... and ...` line for the pair after the load).
2. A truce with B, then A truce with C → both pairs are kept from fighting either way.
3. Two Stop_Fighting from one NPC at different targets quickly → log `still waiting on ...; request to ... refused`, and an `injury_truce` refusal event.
4. Load while the player Yes / No box is open → later truce requests from that NPC still work.
5. Torture an essential NPC into bleedout, finish it with a weapon → log `handed to the game's bleedout`, NPC stays down.
6. Start_Fighting A→C while A has a truce with B → log `truce: A drops 1 truce(s)`.
