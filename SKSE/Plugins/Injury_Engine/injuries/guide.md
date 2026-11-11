# Injury metadata guide (agent instructions)

You are improving the injury data of Injury_Engine in a chat. Edit only the `*.json` files in this folder, its group
folders and `../races.json` (and this guide). Do **not** edit C++ (`SKSE_Source/`), Papyrus (`Scripts/Source/`) or prompts unless the user asks.
`guide.md` is not loaded by the game; every `*.json` here is.

## What these files are

Every folder here is one race **group** (body shape, see "Groups" below); the source files live in
`injuries/humanoid/`, and `session.json` and this guide stay at the root. One file = one injury **source**.
`Injury_Engine.dll` loads every `injuries/humanoid/*.json` at game start
(`InjuryConfig`), tracks damage per source per body part (`InjuryEngine`), and uses the files for all text the LLM
sees (character bio, "how it feels", hit narration, healing narration), the Torture_Target action text and the
torture overlay. Restart the game to see edits. Look in `Injury_Engine.log` (`injuries: ...` lines) for load errors:
a file that fails to parse is skipped and named there.

| File | Source | Fed by |
|------|--------|--------|
| `whipping.json` | model `torture`: damage then healing over game time, requires a Injury_Engine whip; also holds the body block, setting defaults and overlay | Torture_Target `whip`, real Torture Padded Whip hits |
| `pinching`, `slapping`, `pulling` | model `torture` | Torture_Target `pinch` / `slap` / `pull`, `External_Torture` |
| `punching` | model `torture` | Torture_Target `punch`, unarmed NPC / player hits while the attacker is out of combat |
| `fire`, `frost`, `shock` | model `torture` | the spells Torture Flames / Frostbite / Sparks (Injury_Engine.esp; vanilla spells are real damage) |
| `slashing`, `piercing`, `bludgeoning` | model `health`: physical weapon hits | blades / daggers / maces, hammers, staves, fists and bow bashes |
| `arrow` | model `health`: bow and crossbow shots | arrows and bolts (`narration.session` words the hit as a shot) |
| `burning`, `freezing`, `shocking` | model `health`: spell and enchantment damage | magic effects by resist type (fire, frost, shock) |
| `mauling` | model `health` | unarmed attacks by creatures (non-NPC attackers) |
| `falling` | model `both`: torture "Falls and Scrapes" + health "Fall Injuries" (source `falling_real`) | torture: `External_Injure(..., "falling", ..., torture = True)` (SkyrimNet_Leashed ragdoll pulls); health: landing damage (no hit, no attacker, still falling) |
| `leash_dragging` | model `torture`; **shipped by SkyrimNet_Leashed**, not this mod (see "Injury types from other mods") | SkyrimNet_Leashed, each second a knocked-down actor is dragged |
| `drowning` | model `health`: lands on the **lungs** only | drowning damage (no hit, no attacker, under water without Water Breathing) |
| `generic` | model `health` | anything that cannot be classified; also the fallback for a class with no file; also supplies the common heal wording |
| `sexual_parts.json` | **not a source** | optional parts (setting "Include sexual body parts"): part names, size and hit chance, plus their `looks` / `feels` text per source id (see below) |
| `sexual` | (not written yet) | will require the victim to be in SexLab; do not create it unless asked |

A new injury type = a new `<id>.json` copied from a `health` file (not from `whipping.json`) plus a `classify`
array in that file (see below). A type that only scripts call (a `torture` file used through `External_Injure` /
`External_Torture`) needs no `classify` block.

### Injury types from other mods

Another mod can add its own types: it ships `SKSE/Plugins/Injury_Engine/injuries/humanoid/<id>.json` in its own archive
(a file directly in `injuries/` is not loaded),
and the mod manager merges that folder with this one at runtime (nothing to register). Then it calls
`Injury_Engine_Main.External_Injure(attacker, victim, text, "<id>", count, part, narrate, torture)` (see
`docs/developers/papyrus.md`), usually only when `Injury_Engine.esp` is loaded. Rules for such a file:

- The file name is the `id`, and the id must be unique across mods (prefix it with the mod, e.g. `leash_dragging`):
  two mods shipping the same file name overwrite each other in the mod manager.
- The body block must match `whipping.json` exactly (copy it).
- Use `model` `torture` to call it with `torture = True` (`health` / `both` for real damage, `torture = False`).
- `sexual_parts.json` has no text for it: set the five sexual parts to `0` in `part_weights` (torture types).
- At most 32 sources load in total (a `both` file counts twice); the log names any that are skipped.
- Uninstalling the other mod is safe: on the next load its wounds are dropped from the save with a warning.

A weapon from another mod needs no script: a `classify` rule in the new file claims it, and every description of its
wounds then comes from that file. Walkthrough: `docs/authors/custom-weapons.md` (in the mod's source repository). In short:

- `"weapon_file": "MyWeapons.esp", "weapon_id": "0xD62"`: the file-local form ID as a `0x` string (no load-order
  prefix; for an ESL, the last three digits). Without `0x` the value is read as decimal.
- `keyword` only works together with `weapon_types`, and needs a `priority` above 50 to beat the shipped rules.
- If copied from a torture file, change or remove `action.alias`, or it collides with the original.

## How damage becomes injury (so you can judge the wording)

- Body: 6 parts (face, left arm, right arm, back, left leg, right leg). Regions: head, body, arm, leg.
- **Sexual parts** (only with the dashboard setting "Include sexual body parts", off by default): nipples, pussy (women
  only), penis (men only), lips, ass hole, regions `nipple`, `vulva`, `penis`, `lips`, `anus`. They are small: each has
  its own `max` (fewer damage points) and `weight` (chance of being picked; the six main parts weigh one each). Their
  medium / severe thresholds scale down with `max`, so they still go light, medium, severe.
- **Lungs** (part `lungs`, region `lungs`): an internal part only sources whose `part_weights` name it land on
  (drowning). It has no cap (it takes as much damage as the source deals), is not part of the 100% health scale
  below, and its stage is the lung damage as a share of max health: the `lungs` block (`medium`, `severe`, shares
  0..1; drowning.json ships 0.25 / 0.5). It never takes torture and never shows on the overlay.
- **Torture stages** (torture points per part, 20 max, `damage_max` / `thresholds`): light 1-3, medium 4-10, severe 11-20.
- **Real (health) stages**: real points have their own scale: one point is 1% of max health on any part, a main part
  holds `real_max` (100), sexual parts their `real_max` in sexual_parts.json (10 / 5). The stage is the points as a
  percent of the part's real max against the dashboard **Injury Ranges** (`defaults.range_minor / range_major /
  range_severe`, 25 / 75 / 90): below minor the wound counts (it holds health) but has no stage, no narration and no
  looks / feels text; then the source's three stages (minor, major, severe) in order. No part can hold a larger share
  of its real max than the body has lost health (while health is between 75% and 26% no part passes minor), and parts
  are trimmed back when health comes back. The `thresholds` blocks in health files are not read.
- **torture** sources: each hit adds a point of that type to one part (picked by the type's `part_weights`); a healing
  wound of that type that is hit again just gains one point on top of where it had healed down to (one value per part and type:
  hits raise it, healing lowers it). Each type is tracked separately per part, but a part counts
  only its **worst** type: that alone holds health down (never below 10%) and shows on the overlay. Wounds heal over game
  time, and every point of healing lowers **every** type on the part by one (slap light, pinch severe, fire light: all
  heal together). The points give the torture stage light / medium / severe and, while a healing source works on the
  actor (heal session), the heal level high / mid / low (severe = low, medium = mid, light = high).
- **health** sources: the health an attack takes becomes real points, tagged with the classified source, all on one random part
  (whipped parts first) until that part reaches its cap (the body's missing health), then on another. Healing (potions, spells, rest) removes points at random across all sources; each source
  and nothing is narrated per stage.
- **Real damage replaces torture damage** first, on the same part the attack lands on (health is not lowered by that part): the
  part's worst type drops by one per point and every other type above it is clamped down to it, so a tortured actor only dies
  after the torture is all replaced; the overlay shows replaced points as a red bar.

## File format

Plain JSON (comments are tolerated by the parser but keep it valid JSON anyway). Keep keys lowercase.

```
id                 file name without .json (also the source id used in code)
title              human name
model              "torture" | "health" | "both"   ("whip" is still read as "torture")
                   "both": one torture and one health source from one file. Shared keys (id, body block) at the top,
                   everything else in a "torture": {...} and a "health": {...} block, each merged over the shared
                   keys (title, stages, part_weights, action, text, narration per half). The health half's source id
                   is "<id>_real" (logs, co-save, sexual_parts.json falls back to <id>); callers pass <id> plus a
                   torture flag. Example: falling.json.
requires           "whip" | "sexlab" | ""   (metadata only; stored, not enforced)
damage_max, thresholds, health_link, regions, parts
                   BODY BLOCK: legacy, read only when humanoid/body.json is missing (then from the first loaded file,
                   whipping first). The body of every group is its body.json (see "Groups").
                   Do not change numbers or part order without asking the user; the overlay and decorators depend on it.
                   health_link: wound_share (share of max health torture holds down), floor (torture never takes
                   health below it), bleedout_below (torture at or below it holds the victim in bleedout until health
                   is above it again; setting injury_engine.body.torture_bleedout). session.json "collapse" is added
                   once to the next scene line after a victim goes down.
stages             damage stage names: light, medium, severe.
part_weights       (optional) {"part name": weight}: chance of the type landing there; 0 = never; missing parts keep
                   the body's weight (main parts one, sexual parts their sexual_parts.json weight, lungs 0). Torture
                   types and health sources alike (a health file puts it at the top: drowning.json, lungs only)
lungs              (optional, read from the first file that has it) {"medium": 0.25, "severe": 0.5}: lung damage as a
                   share of max health at which the lungs are medium / severe
classify           (optional) how incoming damage picks this source. Highest `priority` wins. The C++ classifier
                   has no source names of its own; it only reads these rules. A "both" file puts the rule in the
                   half it belongs to (falling's landing rule is in `health`).
                   weapon_types     Skyrim weapon type names: OneHandSword, TwoHandSword, OneHandAxe, TwoHandAxe,
                                    OneHandDagger, Bow, Crossbow, OneHandMace, Staff, HandToHandMelee
                   keyword          the weapon must have this keyword (WeapTypeWarhammer on a two-hand axe)
                   without_keyword  the weapon must not have this keyword
                   resist           ResistFire, ResistFrost or ResistShock (health spell damage)
                   effect_file / effect_id   this exact magic effect is the torture spell (local form id, hex string)
                   ignore_weapon_file / ignore_weapon_id   this weapon is not injury damage (the padded whip)
                   weapon_file / weapon_id   this exact weapon is this source, in or out of combat, before its weapon
                                    type (slapping: DiaryOfMine.esm 0x404724 "Slap with hand", a OneHandSword); a
                                    plugin that is not loaded leaves the rule inactive
                   bash             true: only a bash (the game's bash flag on the hit event or the
                                    melee hit data), false: never a bash (a bow shot), omitted: either.
                                    arrow.json claims Bow / Crossbow with false, bludgeoning.json with true
                   unarmed          true: no weapon, or the fist weapon. npc / combat are true, false, or omitted (either)
                   falling          true: landing damage (no hit, no attacker, still falling)
                   drowning         true: damage with no hit and no attacker while under water (engine underwater
                                    flag or head submerged) without Water Breathing; tried before falling
                   fallback         true: anything that matched nothing else (generic.json)
hook.points_per_second  (torture, optional, default 2) points per real second hooked spell damage (fire / frost /
                   shock) adds, whatever the spell's own damage. fire / frost / shock ship 8: the 6-part body (120 points) is full in about 15 s
hook.points_per_hit     (torture, optional, default 1) points one lash / punch / hand torture adds to the part it hits
action.alias / action.order   (torture, optional) short name for Torture_Target ("whip", "pinch", ...) and its
                   place in the type list. The source id itself is always accepted too.
action.verb, action.animation   (torture) log verb; animation event the NPC plays per Torture_Target strike ("" = none)
defaults           (whipping only) default values for dashboard settings; manifest.yaml must mirror them
overlay            (whipping only) title, bar order, colors (hex) incl. "replaced" and "healing" (heal session bar);
                   colors.light / medium / severe are also whipping's bar colors
overlay_colors     (torture, optional) this type's overlay bar colors (hex) light / medium / severe; the bar shows the
                   type that hit last. Not named "overlay": that key marks the whipping file. Missing: whipping's green
action             (whipping) no_whip line, impact_nodes list (the Torture_Target description is in the SkyrimNet
                   plugin's actions/Torture_Target.yaml)
text.looks_heading / looks   what each wounded part looks like  -> character bio ("### Whip Wounds" list)
text.feels_intro / feels / feels_outro   how it feels  -> final instructions
                   torture: feels.torture.<region> [light, medium, severe] and feels.heal.<region> [high, mid, low]
                   (used while a healing source works on the actor); health: feels.<region> [light, medium, severe]
narration.list     how part names are joined ("face, back and left arm")
narration.form     short phrase for who-hit-whom narration ("a keen cutting blade"); bludgeoning may add form_unarmed
narration.hit      hit narration: lead + verb + times + by when no attacker is named; otherwise session.json hit
                   templates plus levels[0..3] by the worst part's state before the hit
                   (0 undamaged, 1 light, 2 medium, 3 severe)
narration.summary  (torture) times words (once / twice / several / many)
narration.session  (torture) switch, end, hit_noun; shared hit / mixed_hit / no_attacker_hit / inline templates
                   in injuries/session.json (not a source); a source may override clause, hit.* and
                   no_attacker_hit.* here (falling: "{target} {what}" + "hits {form}" = "Nia hits the rough ground")
                   session.json "healed" ("{name} is completely healed.") is the character bio line of a living
                   actor with no wound left to show.
                   session.json "heal_food" eat / drink / eat_unknown ({name}, {item}): heal start line when food
                   or an ingredient heals an actor (no caster; replaces narration.heal_events.start); drink when the
                   food uses the sound most potions are drunk with.
                   session.json "heal_feed": races ([{file, id, key}], the beast races whose self-heal is feeding:
                   WerewolfBeastRace "werewolf", DLC2WerebearBeastRace "werebear"), one line per key ({name}) as the
                   lead of the feed heal start line (source 6, no caster, followed by the heal map), and "wounds"
                   ({name}) added after it when the actor has only real wounds (the heal map is torture only).
                   Feeding at full health has no line.
narration.heal.level   (torture) high / mid / low: one heal-session sentence for the parts ({parts}) of this type
                   at that heal level, e.g. "The wounds on {name}'s {parts} have scabbed over." (low)
narration.heal_events   (whipping and generic) spell / staff / potion healing: start, start_unhurt (target has no wounds), pain_gone (no longer sent, kept for older files), full, stopped
death              (health files, and the health half of a "both" file) killing-blow narration, sent once when the
                   victim dies within a few seconds of a hit of this source (setting "Narrate killing blows"):
                   lead          "{attacker} kills {name} with {form}: " (an attacker other than the victim)
                   lead_none     "{name} dies from {form}: " (no attacker)
                   regions       head / body / arm / leg: arrays of lines, one picked at random, appended to the lead;
                                 a sexual part uses head (lips) or body (the rest)
                   soil, soil_chance   lines (one picked) added after the region line with this chance (0..1)
                   {form} is narration.form (form_unarmed for an unarmed hit when present); {part} is the part hit.
```

`text.looks.<region>` is an array indexed by stage (light, medium, severe). Health sources use `text.feels.<region>` the
same way; torture sources split feelings into `text.feels.torture.<region>` (light, medium, severe) and
`text.feels.heal.<region>` (high, mid, low). Regions: `head` (the face), `body` (the back), `arm`, `leg`.

### sexual_parts.json

```
parts              legacy (read only when humanoid/body.json is missing; body.json defines the parts now):
                   exactly 5 entries in this order: nipples, pussy, penis, lips, ass hole
                   {name, region, max (damage points, at least 3), weight (hit chance; main parts are 1)}
text.<source id>   {looks: {region: [...]}, feels: {...}} merged into that source's text.looks / text.feels, in the
                   same shape (torture feels: {torture: {region: [...]}, heal: {region: [...]}}). A source with no entry here has no text
                   for these parts (empty sentence). A new source file needs a block here too.
```

The writing rules below apply; keep the prose about the wound and the pain, not about sex.

### Placeholders

Use `{name}`, `{part}`, `{he}`, `{him}`, `{his}` in text, plus `{reflexive}`. Pronouns are for `text.*` (looks / feels) only:
narration, death and heal event text (everything under `narration` and `death`) names every actor (`{name}`, `{attacker}`,
`{target}`, `{caster}`) and never uses a pronoun, because SkyrimNet followers read a bare "she" as the player. There the
engine renders `{he}` / `{him}` / `{reflexive}` as the name and `{his}` as "name's" anyway. Hit narration also has `{parts}`, `{verb}`,
`{times}`, `{by}`, `{attacker}`. Heal levels have `{parts}`. Heal events also have `{caster}`, `{noun}`, `{who}`, `{obj}`, `{own}`, `{owner}`,
`{owner_lc}`, `{progress}`. Summary: `{speaker}`, `{target}`, `{times}`. Unknown `{tokens}` are left as typed, so do not
invent new ones (the code cannot fill them).

Death text is `death.lead` (or `lead_none`) + one `death.regions.<region>` line (+ maybe one `soil` line), so each
region line starts lowercase mid-sentence (after the colon) and is a full death: the wound, how it looks, how it smells,
the last moment. Torture files have no `death` block (torture never kills).

Hit narration is `lead + levels[n]`: `lead` is `"{name}'s {parts}{verb}{times}{by}"`, so `verb_one` / `verb_many`,
`times.*` and `by_*` carry their own leading space, and each `levels` entry starts with a comma, space or period as needed.

## Groups

`../races.json` maps every race to a group; each group is a folder `injuries/<group>/` with its own body and text.
On a hit the victim's group is looked up and the damage lands on that group's parts with that group's text. An
actor keeps the group it had when its first wound was tracked until it is fully healed (a werewolf transformation
does not remap open wounds). Groups with no folder use humanoid (warned once in `Injury_Engine.log`).

Folders shipped: `humanoid` (all source files, `body.json`, `sexual_parts.json`), `humanoid_undead`, `humanoid_werewolf`,
`humanoid_vampire` (vampire lord), `insectoid`, `quadruped`, `beast_humanoid` (trolls, lurkers), `plant` (spriggans),
`dragon`, `dragon_undead` (Durnehviir, by actor), `dragon_skeletal`, `spectral_dragon`, `construct` (Dwarven machines),
`elemental` (atronachs, gargoyles, ash guardians), `spirit` (wisps, ice wraiths, ghosts), `tentacled` (netch, seekers),
`crustacean` (mudcrabs), `fish` (slaughterfish), `bird`. Sexual parts exist only in humanoid, humanoid_undead,
humanoid_werewolf, humanoid_vampire and insectoid. Not shipped: quadruped_undead (no race maps to it).

`race_contains` is a plain case-insensitive substring test: a short text matches inside unrelated EditorIDs ("cat"
matched `GoblinRaceDuplicate`). Prefer long texts and check every new rule against the `race dump:` lines.

A group folder holds:

```
body.json          REQUIRED. The group's body (a group without it is not loaded):
  damage_max, real_max, thresholds {medium, severe}, health_link {wound_share, floor, bleedout_below}
  bleedout         true | false: torture bleedout can happen to this group at all
  lungs            {medium, severe}: lung stage as a share of max health (only with a lungs part)
  regions          region names (more are added from the parts)
  parts            up to 16: {name, region, max (torture points, default damage_max), real_max (default real_max,
                   sexual 10), weight (pick chance, default 1, sexual 0.2, lungs 0), sexual: true (only with the
                   setting include_sexual_parts), sex: "female" | "male", lungs: true (one part at most: drowning
                   lands only there; no cap, outside the capacity)}. Loaded order: core, sexual, lungs. Keep the order
                   once shipped: co-saves store points by part index. More than 16 parts: the group is not loaded
                   (the hit code carries a 16-bit part mask).
  overlay_order    part names, top of the overlay first (others follow in body order)
  disabled_sources source file ids. A torture source listed here is ignored on this group (no wound, no health
                   lost); a health source passes through untracked (the game's damage lands as normal).
common.json        (optional) the group's text for every source WITHOUT its own file here: shared keys, then a
                   "health": {...} and a "torture": {...} block merged by the source's model; "skip": [ids] keeps
                   humanoid text for those sources (drowning: lungs are lungs). Write it material-true and neutral
                   enough for every damage type (no blood on undead, chitin on insects).
<source>.json      (optional) merged over the humanoid file of the same name (after common.json): objects merge,
                   arrays replace. Usually text, narration.hit.levels, death and part_weights by this group's part
                   names. A "both" file (falling) takes "torture" / "health" blocks like the humanoid file.
sexual_parts.json  (optional) "text" (same shape as humanoid's: per source id) and / or "health": {looks, feels} and
                   "torture": {looks, feels: {torture, heal}}, one block for every source of that model without its
                   own "text" entry. Without the file the sexual parts keep humanoid text (warned in the log).
```

A source whose part_weights give weight only to parts the group lacks (drowning without lungs) is not tracked there:
its damage passes through. `classify` rules are read from the humanoid files only. Part names from Papyrus
(`External_Torture`, `Torture_Target`, `HealPart`) resolve by name in the victim's group, else by region
("face" -> "muzzle"), else random. The Papyrus log names parts with `PartNameFor(actor, part)` (the victim's group).

`../races.json` order: `actors` (`{file, id, group}`: an actor or actor base form, e.g. one dragon on a shared race),
`races` (`{"<group>": [EditorIDs]}`, case-insensitive), `race_forms` (`{file, id, group}`), `race_contains`
(`{text, group}`: the race EditorID contains the text, first match wins: specific texts before general ones),
`keywords` (`{all: [race keywords], group}`), `default_group`. With the DLL loaded, `Injury_Engine.log` lists every
race with its group (`race dump:` lines at game start, `race <id> ... -> <group> (<rule>)` on first use).

## Writing rules

1. **No numbers** in anything the LLM reads: no digits, percentages or point counts (`docs/authors/prompts.md#no-numbers`).
   Use stage words (light, medium, severe; healing high, mid, low) and plain description.
2. One sentence tone: concrete, sensory, specific to the damage type; escalate light -> medium -> severe; keep
   healing text moving toward scars/healed. Wounds the NPC would not survive must not read as trivial.
3. `feels` describes sensation, emotion and how it changes tone, posture and movement of the speaker; do not narrate
   actions for them.
4. Keep entries self-contained: each one is shown alone for its part. Avoid relying on another part's text.
5. Match gender-neutral pronoun placeholders; never hard-code he / she.
6. Do not mention game mechanics, "damage points", "stage" or file names in prose.
7. Type-specific wording stays in its own file: welts, lash, whip only in `whipping.json`; other files must not mention whips.
8. Add content, do not delete keys: the engine reads every key above; a missing text is an empty sentence.

## Workflow for a chat session

1. Read the file(s) you will change and, for wording consistency, `whipping.json` (richest) and `generic.json`.
2. Propose changes in plain language first when the user's request is open-ended, then edit.
3. After editing, validate: `python -c "import json,sys;[json.load(open(f,encoding='utf-8')) for f in sys.argv[1:]]" injuries/*/*.json ../races.json`
   and check every `looks` / `feels` region array has exactly one entry per stage (6 for whipping, 3 for others),
   including the per-source blocks in `sexual_parts.json`. Every health file's `death.regions` needs head, body, arm
   and leg, each with at least one line (two or more so deaths do not repeat); drowning.json lands only on the
   lungs, so it needs `death.regions.lungs` (and `looks` / `feels` `lungs`) instead.
4. Report what changed per file and anything that needs a code change (new class, new placeholder, body numbers).
5. Do not touch the body block, `id`, `model`, or `requires` unless the user explicitly asks.
