# 01 — torture gear rename

Repo `c:\Skyrim\dev\mods\Injury_Engine` · branch `bands` (holds unrelated, uncommitted band work) · baseline `e887881` · logs: [logs/2026-10-20-10-29-36/](logs/2026-10-20-10-29-36/) (none copied, nothing needed)

## Goal
Rename the torture spells and the whip to "Torture <name>" so they sort together in the inventory and magic menus.

## Decisions
- Only the display name (`FULL`) changes, on the same records. FormIDs and EditorIDs stay the same, so existing saves show the new names and need no load-time swap (user confirmed "Same records, no swap").
- Whip: **Torture Padded Whip**. Spells and their main effects: **Torture Flames / Torture Frostbite / Torture Sparks**.
- These keep their vanilla-style names: side effects 0x822–0x825 (Intense Flames Fear, Slow, Deep Freeze Paralyze, Disintegrate), QuietMarker / QuietSpell ("Torture quiet"), LashEffect ("Whip lash") and the ENCH ("Lash").
- The lowercase noun "padded whip" in LLM text (`Torture_Target.yaml`) and in setting descriptions stays as it is.

## Results
- Spriggit `"Name"` changed in 7 files: WEAP 0x803, SPEL 0x816–0x818, MGEF 0x813–0x815. `make esp` (run from PowerShell; it fails from Git Bash because of the `if not exist` cmd syntax) rebuilt `Injury_Engine.esp`.
- Text renamed ("X (torture)" → "Torture X", `"(torture)"` → `"Torture"`, "Padded Whip" → "Torture Padded Whip") in CLAUDE.md, README.md, KNOWLEDGEBASE.md, docs/authors/actions.md, docs/developers/papyrus.md, guides/skyrimnet-attack-short-term-events.md, injuries/guide.md, manifest.yaml (`give_gear` description), plus comments in Classifier.h/.cpp, Hooks.cpp, InjuryEngine.cpp and Injury_Engine_Main.psc. Those code edits are comments only, so the DLL and .pex were not rebuilt.
- CHANGELOGs untouched: 0.1.3 is released and there is no unreleased section. The next release needs a player line: "Torture spells and the whip are renamed Torture Flames / Frostbite / Sparks and Torture Padded Whip so they sort together."
- Remaining `(torture)` matches are unrelated words (e.g. "last hit code (torture)").

## Not yet tested
- In game: the names, and that torture still works (nothing matches by name, so no change is expected).

## Next
1. Deploy: the ESP is already in the mod folder; restart Skyrim.
2. Load an existing save. Inventory → Weapons shows "Torture Padded Whip". Magic → Destruction lists Torture Flames / Frostbite / Sparks together.
3. Cast each on an NPC: the active effect shows the new name, torture points land (overlay bar) and there is no real damage. Lash with the whip: torture as before.
4. At the next release, add the changelog line above (release skill).
