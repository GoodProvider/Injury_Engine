# Changelog

## Unreleased

- New `Injury_Engine_Engine.PartPointsJson(Actor)` native (`InjuryEngine::PartPointsJson`, time applied) and `Injury_Engine_Main.External_PartPoints(Actor)`: torture points per part (`{"total","max","parts":[{"name","points","max"}]}`), used by SkyrimNet_SexLab's Aid / Punish torture bar.
- Build against MinLL/CommonLibVR 4.39.5 (MIT, SE 1.7.99 / 1.7.104) through the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork` (copy of SkyrimNet_SexLab's) instead of SexLab's alandtse/CommonLibSSE-NG v11.0.0 checkout (GPL-3.0). `find_package(CommonLibSSE)`; the colorglass registry and `-DCommonLibPath` are gone. No plugin code changes.
- `LICENSE` added (MIT); README gains a License section.

## [0.1.0](https://github.com/GoodProvider/SkyrimNet_Whip/releases/tag/0.1.0) — initial release

### Whip / action
- Padded whip `Injury_Engine_PaddedWhip` (WEAP 0x803) with COBJ recipe, enchantment `Injury_Engine_LashEnch` and effect `Injury_Engine_LashEffect` (`OnEffectStart` → `Main.OnLash`); weapon, impact data and welt decal `SNW_WhipMarkDecal.dds` come from PAH Diary Of Mine
- LLM action `Whip_Target`, registered in Papyrus so eligibility can check inventory (`injury_engine.action.require_whip`, `injury_engine.action.give_to_player`, `injury_engine.action.max_strikes`). The `injury_engine.in_action` StorageUtil flag stops double counting. See [docs/authors/actions.md](docs/authors/actions.md)

### Wounds / healing
- `Injury_Engine.dll` (`DamageHealing_Engine`, co-saved) tracks six body parts per actor; each lash damages a random part, then damage drains and healing accrues over game time (`injury_engine.healing.hours_per_point`)
- Wounds hold health down by deficit; health regained above the wounded level converts back into healing (`injury_engine.healing.regen_rate` for natural regeneration). Healing sources are detected by a `MagicTarget::AddTarget` hook plus an active-effect scan; `injury_engine.healing.give_healing_hands` adds Healing Hands to the player
- Natives in `Injury_Engine_Engine.psc`: `HasOpenWounds`, `Hit`, `SyncHealth`, `TakeHealedActors`, `TakeHealed`, `Forget`

### Narration / prompts
- Batched hit narration per victim (`NarrateLash`, StorageUtil `injury_engine.pending_*`) and healing narration (`NarrateHealing`, `NarrateSourceHeal`, `injury_engine.heal_*`); settings `injury_engine.narration.enabled`, `.cooldown`, `.max_distance`, `.event_ttl`
- Prompts `character_bio/0420_whip_welts.prompt`, `user_final_instructions/0420_whip_pain.prompt` and `helpers/healing.prompt` (rendered by `Main.RenderHealPrompt`) under `external/goodprovider.injuryengine/`; decorator `injury_engine_welts(uuid)`. See [docs/authors/prompts.md](docs/authors/prompts.md) and [docs/reference/json-keys.md](docs/reference/json-keys.md)

### SKSE / overlay
- PrismaUI overlay `PrismaUI/views/Injury_Engine/overlay.html` (`injury_engine.overlay.enabled`): one bar per tracked actor, fades 3 s after the last change. See [docs/developers/overlay.md](docs/developers/overlay.md)

### Install / settings
- Packaged as a FOMOD (core only) by `make release`; `python_scripts/check-version.py` verifies the version in the Makefile, both manifests, `CMakeLists.txt` and `version.rc`. Requires SkyrimNet Beta 25 (0.25.0), SKSE, Address Library, PapyrusUtil SE and PrismaUI
- Dashboard settings under `config/plugins/Injury_Engine/manifest.yaml` (`whip.*`)

### Docs
- README, AGENTS.md, KNOWLEDGEBASE.md, `docs/`, [release-guide.md](release-guide.md)
