# Release guide

Align docs and version metadata with what will ship as `next_version`. Packaging, git tags, and GitHub Releases are a separate maintainer step.

Agents: use the project `release` skill (`.claude/skills/release/SKILL.md`). Session state: [release-checkpoint.xml](release-checkpoint.xml). Contracts: [docs/reference/](docs/reference/) — do not restate them here.

## Who does what

| Actor | Owns | Does not do unless asked |
|-------|------|--------------------------|
| Agent | Changelogs, freshness-matrix docs, version bump in all sources, checkpoint | `pyro`, `make release`, git tag, GitHub Release, feature work |
| Maintainer | Compile, package, tag, publish | Inventing changelog bullets the tree does not support |

**Agent done-when:** `CHANGELOG.md`, `CHANGELOG-developer.md` and `CHANGELOG-player.md` cover `next_version`; freshness-matrix files match the delta; `check-version.py` passes; checkpoint is current. Packaging is not part of done.

## Version sources

One release version. `make release` runs `python_scripts/check-version.py -v ${VERSION}` first and stops on any mismatch.

| Source | Field | Role |
|--------|-------|------|
| [Makefile](Makefile) | `VERSION` | Authoritative; names the 7z |
| `SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/manifest.json` | `version` | LLM plugin bundle |
| `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml` | `version` | Dashboard settings |
| `SKSE_Source/CMakeLists.txt` | `project(... VERSION)` | DLL |
| `SKSE_Source/version.rc` | FILEVERSION / PRODUCTVERSION / strings (`x,y,z,0`, `x.y.z.0`) | DLL resource |
| `SKSE/Plugins/SkyrimNet_Whipped/info.json` | `version` | Written by `make release` (`python_scripts/info.py`) |
| `FOMOD/info.xml`, `FOMOD/ModuleConfig.xml` | `Version` | Generated from `FOMOD_source/` by `fomod-update-name-version.py` (git-ignored) |
| git tag | bare `VERSION` (e.g. `0.1.0`) | Created only when the maintainer ships |

Releases URL: `https://github.com/GoodProvider/SkyrimNet_Whip/releases/tag/{VERSION}` (matches the current `origin`; update if the repo is renamed).

## Freshness matrix

Update only the rows the delta touches.

| When the delta includes | Update |
|-------------------------|--------|
| Prompts, decorator JSON, heal text | `docs/authors/prompts.md`; `docs/reference/json-keys.md`; changelogs |
| `Whip_Target` action | `docs/authors/actions.md`; changelogs |
| Papyrus / ESP / settings | `docs/developers/papyrus.md`; README settings table; `CLAUDE.md` Flow / Form IDs; changelogs |
| SKSE plugin / overlay | `docs/developers/overlay.md`; changelogs |
| Install / requirements | `README.md` (Requirements, Install); `FOMOD_source/` |
| Doc layout or agent map | `AGENTS.md`; `release-checkpoint.xml` `doc_files` |

## Changelog rules

Ground truth is the git delta since `base_tag` (none before 0.1.0) plus working-tree files that will ship. Every bullet must be verifiable. Changelogs link to `docs/reference/…`, never paste a contract. The "no numbers" rule applies to LLM-facing text, not changelogs.

### `CHANGELOG.md`

```markdown
## [VERSION](https://github.com/GoodProvider/SkyrimNet_Whip/releases/tag/VERSION) — since [BASE](https://github.com/GoodProvider/SkyrimNet_Whip/releases/tag/BASE)
```

H3 themes, only when non-empty: Whip / action, Wounds / healing, Narration / prompts, Papyrus, SKSE / overlay, Install / settings, Docs. Prefer concrete identifiers (`whip.*` keys, prompt files, StorageUtil keys, form names).

### `CHANGELOG-developer.md`

First line = releases URL for `next_version`. Then the SkyrimNet requirement and 5–12 plain-English bullets, none missing from `CHANGELOG.md`.

### `CHANGELOG-player.md`

Bullets only: the player-facing items of `CHANGELOG.md` (no build, license, docs or internals), each 100 characters or less, none missing from `CHANGELOG.md`. Shipped in the 7z (staged into `core/`) and used as the GitHub Release body.

## Noise (do not changelog)

`z-*`, `SSEEdit Cache/`, `versions/`, `core/`, `FOMOD/`, `SpriggitCLI/`, `SKSE_Source/build/`, `SKSE_Source/lib/` (vendored), crash logs, `Papyrus.0.log`.

## Packaging (maintainer)

Prereqs: `Scripts/*.pex` current (`pyro skyrimse.ppj`); `VCPKG_ROOT` set; `Spriggit/` JSON is the ESP source of truth; `7z`, `python3`, `powershell` on PATH.

`make release` = `check-version.py` → `info.json` → `FOMOD/*.xml` from `FOMOD_source/` → `make dll` → `make esp` → stage `core/` (Scripts, SKSE, meshes, textures, Sound, PrismaUI, `SkyrimNet_Whipped.esp`, `CHANGELOG-player.md`) → `7z a "versions/SkyrimNet_Whipped ${VERSION}.7z" FOMOD core` → remove `core/`. The FOMOD installs `core` unconditionally (no options); PrismaUI is a documented requirement, not an installer step.

Ship order: bump version sources → changelogs → commit → `pyro` → `make release` → inspect `7z l` output → git tag `VERSION` → GitHub Release (attach the 7z, body = `CHANGELOG-player.md`).
