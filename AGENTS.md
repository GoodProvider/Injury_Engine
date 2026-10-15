# AGENTS.md

Agent guidance for Injury_Engine (torture damage and a padded whip for SkyrimNet).

## General instructions
The agent's job is not finished until the latest changes are ready for testing in Skyrim.
If this is possible step by step instructions should be left for the developer.

**Deploy before you stop.** This repo is the MO2 mod folder (`Injury_Engine`). After C++, Papyrus, or injury JSON work, always ship the files the game loads:

1. **DLL** — `make dll` → [SKSE/Plugins/Injury_Engine.dll](SKSE/Plugins/Injury_Engine.dll) (CMake copies the build output here; do not hand-copy from `build/` elsewhere).
2. **Injuries** — [SKSE/Plugins/Injury_Engine/injuries/*.json](SKSE/Plugins/Injury_Engine/injuries/) (edit in place; no separate packaging step).
3. **Papyrus** — `pyro skyrimse.ppj` or VS Code **`compile: pyro`** → [Scripts/*.pex](Scripts/) (Pyro writes into this repo, not into overwrite).

If a fix “does nothing” in-game, confirm MO2 is loading this folder (not a disabled release copy) and that overwrite has no stale `Injury_Engine` DLL or `.pex` shadowing it. See [KNOWLEDGEBASE.md](KNOWLEDGEBASE.md) (.pex vs .dll deploy).

## What this is

Standalone torture damage and padded whip for SkyrimNet. Each torture hit (whipping, pinching, slapping, pulling, punching, companion fire / frost / shock) wounds a body part; `Injury_Engine.dll` (`InjuryEngine`) tracks injury per source (torture types, each part counting its worst type, plus classified slashing / piercing / bludgeoning / burning / freezing / shocking / mauling / generic damage) per part (co-saved), reads all wound data and text from `SKSE/Plugins/Injury_Engine/injuries/*.json`, Papyrus narrates hits and healing, prompts tell the LLM how wounds look and feel, and a PrismaUI overlay (PrismaUI is a required dependency; the overlay itself is a setting) shows the bars. Architecture and Form IDs: [CLAUDE.md](CLAUDE.md).

## Key paths

| Path | Role |
|------|------|
| `Scripts/Source/` | Papyrus source |
| `Scripts/` | Compiled `.pex` |
| `skyrimse.ppj` | Pyro project |
| `SKSE_Source/` | C++ SKSE plugin → `SKSE/Plugins/Injury_Engine.dll` |
| `Scripts/Source/Injury_Engine_Engine.psc` | Papyrus natives of the DLL |
| `Spriggit/Injury_Engine/` | ESP source of truth (`Injury_Engine.esp` is built) |
| `SKSE/Plugins/Injury_Engine/injuries/` | Injury data + text per source (`whipping.json`, `slashing.json`, ...; schema in `guide.md`) |
| `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/` | Beta 25 LLM plugin (prompts; folder name = `manifest.json` `id`) |
| `SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml` | Dashboard settings schema (`Plugin_Injury_Engine`, `whip.*`) |
| `PrismaUI/views/Injury_Engine/overlay.html` | Whipping overlay page |

Repo root: `c:\Skyrim\dev\mods\SkyrimNet_Whip`.

### Logs (this machine)

Documents is OneDrive-redirected — not `%USERPROFILE%\Documents\...`.

| Log | Path |
|-----|------|
| Crash Logger | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\SKSE\crash-*.log` |
| Injury_Engine (DLL, `plugin.cpp` spdlog) | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\SKSE\Injury_Engine.log` |
| Papyrus (`Trace` → `Debug.Trace`) | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\Logs\Script\Papyrus.0.log` |
| SkyrimNet conversation (dialogue history) | `C:\Skyrim\dev\overwrite\SKSE\Plugins\SkyrimNet\logs\conversation_log.log` |
| SkyrimNet LLM requests (full prompts sent) | `C:\Skyrim\dev\overwrite\SKSE\Plugins\SkyrimNet\logs\openrouter_input.log` (rotated: `openrouter_input.<timestamp>.log`) |
| SkyrimNet LLM responses | `C:\Skyrim\dev\overwrite\SKSE\Plugins\SkyrimNet\logs\openrouter_output.log` (rotated: `openrouter_output.<timestamp>.log`) |
| SkyrimNet (last resort; ask first) | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\SKSE\SkyrimNet.log` |

The `SkyrimNet\logs\` files are several MB each; grep them or read the tail instead of loading them whole.

Relative paths for other machines: [docs/reference/papyrus-rules.md](docs/reference/papyrus-rules.md).

## Documentation map

| Job | Read |
|-----|------|
| Players / settings / install | [README.md](README.md) |
| Architecture, flow, Form IDs | [CLAUDE.md](CLAUDE.md) |
| Prompts | [docs/authors/prompts.md](docs/authors/prompts.md) |
| LLM action (`Torture_Target`) | [docs/authors/actions.md](docs/authors/actions.md) |
| Papyrus / ESP / settings | [docs/developers/papyrus.md](docs/developers/papyrus.md) |
| SKSE plugin / PrismaUI overlay | [docs/developers/overlay.md](docs/developers/overlay.md) |
| Quirks | [KNOWLEDGEBASE.md](KNOWLEDGEBASE.md) |
| Release (version sources, changelogs, packaging) | [release-guide.md](release-guide.md); agent skill `.claude/skills/release/SKILL.md` |

### Canonical contracts (do not restate elsewhere)

| Topic | File |
|-------|------|
| Papyrus language / naming / traces | [docs/reference/papyrus-rules.md](docs/reference/papyrus-rules.md) |
| JSON keys (decorator, `heal`, overlay) | [docs/reference/json-keys.md](docs/reference/json-keys.md) |
| No numbers in narration | [docs/authors/prompts.md](docs/authors/prompts.md#no-numbers) |

## Compile

- Papyrus: `pyro skyrimse.ppj` (VS Code task **`compile: pyro`** when present).
- SKSE: `make dll` (CMake + vcpkg, `VCPKG_ROOT` set; CommonLibSSE is MinLL/CommonLibVR 4.39.5 (MIT) from the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork`; a clean configure builds it, ~6 min). See [docs/developers/overlay.md](docs/developers/overlay.md).
- ESP: `make esp` (Spriggit → `.esp`); `make serialize` after xEdit edits.
- Release: `make release` (version check, `info.json`, FOMOD, dll, esp, pack) → `versions/Injury_Engine <version>.7z`. See [release-guide.md](release-guide.md).

Tools: Pyro, Spriggit (`SpriggitCLI/`, fetched by `updateSpriggit.bat`), `tools/patch_nif_paths.py`.

## Commit messages

First ~72 characters summarize the commit. Prefer a multi-line body with concrete bullets (paths, keys, setting names).

## Standing rules

- **Nexus first:** search the dependency's Nexus page before investigating blind.
- **Knowledgebase:** consult [KNOWLEDGEBASE.md](KNOWLEDGEBASE.md) before changes; append new quirks after sessions.
- **User scratch:** `z-*` files and directories (repo root) are local scratch. Never ingest, treat as ship set, changelog, or commit.
- **No numbers in narration:** LLM-facing text uses descriptive stages, never digits or percentages.
- **INI load order:** Skyrim.ini then SkyrimPrefs.ini (last wins).
- **SE ≠ VR** — never assume parity.

### Top gotchas (see KNOWLEDGEBASE for detail)

PrismaUI views load from `Data/PrismaUI/views/`; separate PrismaUI `Invoke`s are not applied in order; Papyrus default args are filled at the caller's compile; ESL FormIDs are signed in Papyrus; decorators return empty in action eligibility; `.pex` and `.dll` deploy in opposite directions; Wait() under 100ms unreliable; non-auto properties blank on load.

## Safety rules

### Never

- Delete game/config install dirs or Bethesda registry keys
- Write ESP/ESM/ESL/BSA/BA2 directly (use Spriggit: `make esp` / `make serialize`)

### Requires confirmation

- Edits in game/config directories; Skyrim INIs; SKSE plugin INIs; load-order files; destructive shell on those trees

### General

- Review diffs before applying; the user knows modding/INI; propose KNOWLEDGEBASE entries when risk patterns appear

## Confidence (mandatory before game/script/ESP changes)

1. State confidence 0–100% and assumptions
2. Investigate: KNOWLEDGEBASE, sources, web for SE/VR quirks
3. Target ≥ 90%; if lower, document gaps
4. Never assume SE = VR

| Range | Action |
|-------|--------|
| 95–100% | Proceed with confirmation |
| 80–94% | Proceed with caveats |
| 60–79% | Research more |
| < 60% | Do not proceed |

Checklist: KB → sources → web → rollback.
