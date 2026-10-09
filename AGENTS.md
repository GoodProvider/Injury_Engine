# AGENTS.md

Agent guidance for SkyrimNet_Whipped (padded whip for SkyrimNet).

## What this is

Standalone padded whip for SkyrimNet. Each lash wounds a random body part; `SkyrimNet_Whipped.dll` (`InjuryEngine`) tracks injury per source (whipping plus classified slashing / piercing / bludgeoning / burning / freezing / shocking / mauling / generic damage) per part (co-saved), reads all wound data and text from `SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json`, Papyrus narrates hits and healing, prompts tell the LLM how wounds look and feel, and a PrismaUI overlay (PrismaUI is a required dependency; the overlay itself is a setting) shows the bars. Architecture and Form IDs: [CLAUDE.md](CLAUDE.md).

## Key paths

| Path | Role |
|------|------|
| `Scripts/Source/` | Papyrus source |
| `Scripts/` | Compiled `.pex` |
| `skyrimse.ppj` | Pyro project |
| `SKSE_Source/` | C++ SKSE plugin → `SKSE/Plugins/SkyrimNet_Whipped.dll` |
| `Scripts/Source/SkyrimNet_Whipped_Engine.psc` | Papyrus natives of the DLL |
| `Spriggit/SkyrimNet_Whipped/` | ESP source of truth (`SkyrimNet_Whipped.esp` is built) |
| `SKSE/Plugins/SkyrimNet_Whipped/injuries/` | Injury data + text per source (`whipping.json`, `slashing.json`, ...; schema in `guide.md`) |
| `SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/` | Beta 25 LLM plugin (prompts; folder name = `manifest.json` `id`) |
| `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml` | Dashboard settings schema (`Plugin_SkyrimNet_Whipped`, `whip.*`) |
| `PrismaUI/views/SkyrimNet_Whipped/overlay.html` | Whipping overlay page |

Repo root: `c:\Skyrim\dev\mods\SkyrimNet_Whip`.

### Logs (this machine)

Documents is OneDrive-redirected — not `%USERPROFILE%\Documents\...`.

| Log | Path |
|-----|------|
| Crash Logger | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\SKSE\crash-*.log` |
| SkyrimNet_Whipped (DLL, `plugin.cpp` spdlog) | `C:\Users\bhuff\OneDrive\Documents\my games\Skyrim Special Edition\SKSE\SkyrimNet_Whipped.log` |
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
| LLM action (`Whip_Target`) | [docs/authors/actions.md](docs/authors/actions.md) |
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
- Release: `make release` (version check, `info.json`, FOMOD, dll, esp, pack) → `versions/SkyrimNet_Whipped <version>.7z`. See [release-guide.md](release-guide.md).

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
