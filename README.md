# SkyrimNet W.H.I.P.P.E.D.

![image of person whipping, healing, and injured](images/SkyrimNet_Whipped.jpg)
Whipped Healing Injury: Pleasure & Pain Experience Details

A standalone padded whip for [SkyrimNet](https://github.com/MinLL/SkyrimNet-GamePlugin). Each lash wounds a random body part (face, back, left/right arm, left/right leg). The hit is narrated to the LLM, and while the wounds heal, every character who looks at the victim knows what they look like and the victim knows how they feel.

The whip, its welt decal, and its sounds come from **PAH Diary Of Mine**. This mod does not need DOM.

## Features

- **Padded whip** (`SkyrimNet_Whipped_PaddedWhip`): craftable at the forge with DOM's recipe (leather strips plus one other vanilla item), or `help "Padded whip"` → `player.additem`.
- **Welt decal on hit**: the same engine impact chain as DOM (weapon → impact data set → flesh impact → `SNW_WhipMarkDecal.dds`).
- **Direct Narration on hit**: "Nina's face is hit by Ulfric's whip, a sudden, sharp slap of raw shock…", worded by how damaged that part already was (undamaged, light, medium, severe). After the first hit, a hit more than the torture narration window after the last one is narrated on its own, hits between the interval and the window are combined into one narration, and quicker hits are recorded as events; a busy speech queue or a distance limit also turns a narration into an event.
- **Wound prompts**: a character-bio section with what each wounded part looks like, and a final-instructions section with how it feels to the speaker. Both follow each part from light, medium and severe damage through recovering, mostly healed and healed (scarred).
- **Torture_Target LLM action**: an NPC walks to the target and whips, pinches, slaps, pulls or punches them 1–N times, followed by one summary narration. Whipping needs the padded whip.
- **Torture damage**: whipping, pinching, slapping, pulling and punching (fists outside combat), plus fire, frost and shock torture from the spells Flames (torture), Frostbite (torture) and Sparks (torture) (given to you by "Add gear to player"; the normal spells stay real damage). Torture holds health down but never below 10%, so it can't kill. Each type is tracked per body part, but a part counts only its worst type; healing a part heals every type on it at once.

## Settings (SkyrimNet dashboard → SkyrimNet_Whipped)

| Key | Default | Meaning |
|-----|---------|---------|
| `whip.action.require_whip` | on | The NPC must already carry the padded whip to whip with Torture_Target. Off: one is handed over for the lashing. |
| `whip.action.max_strikes` | 10 | Most strikes per action (applies after a reload) |
| `whip.healing.hours_per_point` | 1 | Game hours per point of damage drained, then per healing point gained |
| `whip.narration.enabled` | on | Narrate lashes (off = events only) |
| `whip.narration.torture_narration_window_size` | 30 s | After a torture hit, passive healing is not reported for this long |
| `whip.narration.cooldown` | 8 s | Per-victim healing narration cooldown |
| `whip.narration.max_distance` | 15 m | Farther lashes become events |
| `whip.body.include_sexual_parts` | off | Include sexual body parts (nipples, pussy, penis, lips, and ass hole): small parts, hit less often and holding less damage, but still wounded light, medium and severe. Pussy only on women, penis only on men. |
| `whip.debug.verbose_logging` | off | Extra debug lines in `SkyrimNet_Whipped.log` and `Papyrus.0.log` (for troubleshooting) |

## Requirements

SKSE, Address Library for SKSE Plugins, SkyrimNet (Beta 25+), PapyrusUtil SE, and **PrismaUI** (required: the whipping overlay is built on it).

## Install

Install `SkyrimNet_Whipped <version>.7z` with your mod manager (it is a FOMOD with no options; everything is required). Enable `SkyrimNet_Whipped.esp`.

## How wounds are tracked

The engine owns decals: they can vanish on a cell change or reload, and scripts can't query them. So `SkyrimNet_Whipped.dll` (InjuryEngine, saved in the SKSE co-save) tracks each actor's six body parts, per injury source (whip wounds plus classified real damage, see below):

| Damage | Stage | | Healing (damage 0) | Stage |
|---|---|---|---|---|
| 1–3 | light | | 0–3 | recovering |
| 4–10 | medium | | 4–10 | mostly healed |
| 11+ | severe | | 11 | healed (scar) |

Each lash adds 1 damage to a random part; a part that is already healing reopens at damage 4. Every `hours_per_point` game hours a part loses 1 damage, then gains 1 healing; health restored by spells, potions or regeneration heals random parts one point at a time. Each part that heals to its next stage is narrated like a hit (first one always, later ones batched behind the cooldown or kept as short-lived events); only "fully healed" is always a Direct Narration. An actor is forgotten once every hit part is healed. Decorator `skyrimnet_whipped_injuries(uuid)` returns the injuries of every source with their descriptions already written for that actor.

### Other injuries

Ordinary damage is tracked too, classified by what hit the actor: slashing (swords, axes), piercing (daggers, arrows), bludgeoning (maces, hammers, staves, fists), burning, freezing and shocking (fire, frost and shock spells), mauling (creatures), falling (landing from a height) and generic (anything else). Each health point lost becomes damage on a random body part (all parts at maximum = no health left) with the same light / medium / severe stages, and health regained heals random injuries one point at a time; a type, a body part and the whole body are each reported as they finish healing. Real damage first replaces torture damage: while an actor still carries torture wounds the hit does not lower health (the part's worst type drops and the others are clamped to it), and the overlay shows the replaced part as a darker red bar; once all torture is replaced, further damage is normal and can kill. Torture wounds themselves keep their special rules (damage, then healing over time, never below 10% health).

### Editing the injury text and numbers

Everything the LLM reads about injuries, the Torture_Target text, the overlay title and colors and the setting defaults are in `SKSE/Plugins/SkyrimNet_Whipped/injuries/*.json` (one file per source). `injuries/guide.md` is a ready-made instruction sheet you can give to a chat assistant to improve or extend them.

## Building

- SKSE plugin: `make dll` (CMake + vcpkg, `VCPKG_ROOT` set; CommonLibSSE is MinLL/CommonLibVR 4.39.5 (MIT) from the overlay port `SKSE_Source/vcpkg-ports/commonlibsse-ng-fork`; a clean configure builds it, ~6 min). The .dll is copied to `SKSE/Plugins/`.
- Scripts: `pyro skyrimse.ppj` (VS Code task "compile: pyro").
- ESP: `make esp` builds `SkyrimNet_Whipped.esp` from `Spriggit/SkyrimNet_Whipped/`. After editing in xEdit, run `make serialize`.
- Release: `make release` → `versions/SkyrimNet_Whipped <version>.7z` (FOMOD + core). Procedure: [release-guide.md](release-guide.md).
- `tools/patch_nif_paths.py` repoints copied DOM meshes to `textures\weapons\SNW\` (already applied).

## Credits

Whip mesh, textures, decal and sounds: PAH Diary Of Mine.

## License

MIT, see [LICENSE](LICENSE). The SKSE plugin statically links [MinLL/CommonLibVR](https://github.com/MinLL/CommonLibVR) 4.39.5 (MIT) and [OpenVR](https://github.com/ValveSoftware/openvr) (BSD-3-Clause).
