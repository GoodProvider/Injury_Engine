# Papyrus compile switch: handoff checkpoint v01

Status: **done, compiled, deployed, not tested in game, not committed.** All 5 scripts rebuilt with `papyrus.exe` (0 errors). No DLL / ESP / JSON change.

## Problem
Docs and memory told agents to build with `pyro skyrimse.ppj` / `compile: pyro`. Pyro is not on PATH and the repo had no `.vscode/tasks.json`.

## Findings
- `c:/Skyrim/tools/papyrus-compiler/papyrus.exe compile` takes repeated `-h` header dirs; on a script found in several dirs the **last** `-h` wins (the reverse of Pyro's import order).
- SKSE64 first and vanilla last gave 93 errors (`Math`, `GetDisplayName`, `RegisterForModEvent` undefined). Vanilla first and SKSE64 last compiles cleanly.
- A failed compile writes no `.pex`.

## Current fix
- New `.vscode/tasks.json`: task `compile: papyrus` (default build task), header order vanilla → PapyrusUtil → SkyrimNet → SKSE64.
- Switched compile instructions in `AGENTS.md`, `CLAUDE.md`, `README.md`, `release-guide.md`, `docs/developers/papyrus.md`, `KNOWLEDGEBASE.md` (plus a new entry on the header order). `skyrimse.ppj` stays as a Pyro fallback.
- Memory: `build-commands.md` rewritten, new `papyrus-compile-task.md`.

## Known limits
- The `.pex` files now come from a different compiler than the Pyro (CK compiler) builds. They are smaller (e.g. Main 60662 → 48662 bytes); the behavior should be the same but has not been checked in game.
- Header dirs are absolute paths for this machine.

## Next step for the developer
1. In VS Code: Terminal → Run Build Task → `compile: papyrus` and confirm "finish" with no errors.
2. Load a save in MO2 and do a quick check: a whip lash, a torture punch, a `Torture_Target` action, and a truce. Look for Papyrus errors in `Papyrus.0.log`.

## After it holds
Commit the docs, `.vscode/tasks.json` and the rebuilt `.pex` together with the open truce-guard work, or separately as "Compile Papyrus with papyrus-compiler".
