# CLAUDE.md

SkyrimNet_Whipped: a standalone padded whip for SkyrimNet, modeled on `../SkyrimNet_SexLab`.

**Primary agent doc:** [AGENTS.md](AGENTS.md) (logs, docs map, standing rules, safety, confidence). Imported below so it is always loaded:

@AGENTS.md

## Key paths

- Papyrus: `Scripts/Source/` → `Scripts/`; project `skyrimse.ppj` (compile: pyro).
- SKSE plugin: `SKSE_Source/` → `make dll` → `SKSE/Plugins/SkyrimNet_Whipped.dll`. Natives: `SkyrimNet_Whipped_Engine.psc`.
- ESP source of truth: `Spriggit/SkyrimNet_Whipped/` → `make esp`. Never hand-edit `SkyrimNet_Whipped.esp`; after xEdit edits run `make serialize`.
- Injury data (all wound / healing numbers and text): `SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json`, guide for editing them: `injuries/guide.md`.
- LLM content: `SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/` (folder name = manifest `id`).
- Dashboard settings: `SKSE/Plugins/SkyrimNet/config/plugins/SkyrimNet_Whipped/manifest.yaml`, read as `Plugin_SkyrimNet_Whipped`.

## Form IDs (SkyrimNet_Whipped.esp, ESL)

| ID | Record |
|----|--------|
| 0x800 | QUST SkyrimNet_Whipped (scripts Main, Actions; alias PlayerRef) |
| 0x801 | GLOB skyrimnet_whipped_require_whip |
| 0x803 | WEAP SkyrimNet_Whipped_PaddedWhip |
| 0x804–0x80E | 1st-person STAT, IPDS, IPCTs, SNDRs, TXST decal (copied from DiaryOfMine.esm) |
| 0x80F | COBJ recipe |
| 0x810 / 0x811 | MGEF SkyrimNet_Whipped_LashEffect / ENCH SkyrimNet_Whipped_LashEnch |
| 0x812 | KYWD SkyrimNet_Whipped_WeapTypeWhip |

## Flow

Real weapon hit → ENCH → `SkyrimNet_Whipped_LashEffect.OnEffectStart` → `Main.OnLash`.
LLM action `Whip_Target` (registered in Papyrus so eligibility can check inventory) → `Main.Lash` (PlayImpactEffect + sound) → `Main.OnLash`. The `skyrimnet_whipped.in_action` StorageUtil flag stops double counting.
Injury data and text: `SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json` (`whipping`, `slashing`, `piercing`, `bludgeoning`, `burning`, `freezing`, `shocking`, `mauling`, `generic`; sexual assault later) loaded by `InjuryConfig` at kDataLoaded; the schema and an agent guide for improving them are in `injuries/guide.md`. Nothing about wounds is hard-coded any more: stage thresholds, part and region names, narration prose, healing text, bio / feels tables, Whip_Target text, overlay title and colors, setting defaults all live in those files (`manifest.yaml` still carries the dashboard schema and mirrors the defaults).
`InjuryEngine` (C++, co-saved version 2) tracks injury per actor, per body part, per source. Model `whip` (`whipping`): `OnLash` calls `SkyrimNet_Whipped_Engine.Hit(victim, "whipping")` (random body part, damage then healing over game time); requires a SkyrimNet_Whipped whip. Model `health` (everything else): `Hooks.cpp` hooks `Actor::HandleHealthDamage` (SE / AE) and `ActorValueOwner::ModActorValue` (kDamage on Health) on Character and PlayerCharacter, `Classifier` picks the source (TESHitEvent + the attacker's weapon: blades slashing, daggers and arrows piercing, maces / staves / fists bludgeoning, spell resist fire / frost / shock burning / freezing / shocking, creature attacks mauling, else `generic`; Padded Whip hits ignored) and `InjuryEngine::OnDamage` turns the health the attack takes into points (capacity 6 parts × 20 = 100% of max health, random part not at max). Real damage first replaces whip points (`absorbed = min(damage, whip hold)`: the hook reduces the damage so health does not drop by that part, whip points are removed and the same health worth of real points added) and only then takes health, so a whipped actor dies only after all whip wounds are replaced; the 10% floor applies only while whip points exist. The hook queues the hit and fires mod event `SkyrimNet_Whipped_Injury`, which makes `Main.OnInjury` start the sync loop; `Main.NarrateRealHits` drains `TakeHits` and narrates through the same `NarrateInjury`.
`NarrateInjury` batches hits per victim and source (StorageUtil `skyrimnet_whipped.pending_*.<source>`): the first hit on a victim with no open wounds always narrates (RegisterEvent if narration is off or too far); later hits send one combined DirectNarration (all parts since the last narration, worst level) when `GetSpeechQueueSize() == 0` and the cooldown has passed, otherwise a RegisterShortLivedEvent per hit. Healing mirrors hitting: `TakeHealedActors`/`TakeHealed` return heal codes (a part reached a new stage, a source healed on every part it touched, a part with no injury left from any source; transient C++ queue, filled by game time for whip points and by health conversion for all), drained by `OnUpdateGameTime` and every `OnUpdate` tick. `NarrateHealing` batches them per victim (StorageUtil `skyrimnet_whipped.heal_*`): the first heal since the last hit always narrates (RegisterEvent if narration is off or too far); later ones send one combined DirectNarration when the gate allows, otherwise a RegisterShortLivedEvent `whip_heal_*` per take; a new hit clears the heal batch. Health link: whip wounds hold health down by deficit (damage + missing healing points) × `0.9 × MaxHealth / 186`, real points by `MaxHealth / 120` each; the wounded level is `max − both`, never below 10% max while whip points exist. `Main.OnUpdate` runs every 0.2 s while `TrackedCount() > 0`, calling `SyncHealth`: health above the wounded level converts back to points (random whip / real point, random part, damage first), fully while a healing source is active or the actor has no whip wounds, otherwise natural regen at `whip.healing.regen_rate`% (the rest is taken back). Healing sources come from a `MagicTarget::AddTarget` vtable hook (`Hooks.cpp`) plus an active-effect scan; events (heal start, pain gone, fully healed, heal stopped) carry source kind + caster and are narrated by `Main.NarrateSourceHeal`: fully healed is always a DirectNarration (while narration is on) and clears the heal batch; start, pain gone and stopped go through the usual gate or a `whip_heal` event. Setting `whip.healing.give_healing_hands` adds Healing Hands (Skyrim.esm 0x4D3F2) to the player.
Narration text is rendered in C++ from the JSON files (`InjuryEngine::HitText`, `HealText`, `SourceHealText`, `ActionText`, natives of the same names), no prompt is involved. Prompt text for the LLM: `character_bio/0420_injury_looks.prompt` and `user_final_instructions/0420_injury_feels.prompt` loop over decorator `skyrimnet_whipped_injuries(uuid)` (`Main.Injuries_Decorator`, `GetStateJson`), whose `look` / `feel` strings are already resolved for the actor.
Overlay (`whip.overlay.enabled`, "Show whipping overlay"): PrismaUI view `PrismaUI/views/SkyrimNet_Whipped/overlay.html`, created by `Overlay::Init` on kDataLoaded (`Overlay.cpp`, header vendored in `SKSE_Source/lib/PrismaUI/`). The Papyrus natives `Hit`, `SyncHealth`, `TakeHealedActors`, `TakeHits` and `Forget` call `Overlay::Notify`, which pushes `InjuryEngine::OverlayJson` to `whipUpdate()` when it changes. `#WhippedPanel` shows a white "Whip Damage" title above `.actor-whipped-damage-bars`: one name-only bar per whipped actor, most recently hit first (whole-body whip damage total, or healing progress once no damage is left; color = average stage); real damage that replaces whip damage grows a darker red segment from the left; an actor whose whip damage is fully replaced is dropped and the panel hides when no actor has whip damage; the whole overlay fades 3 s after the last change.
