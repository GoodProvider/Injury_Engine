# Injury metadata guide (agent instructions)

You are improving the injury data of SkyrimNet_Whipped in a chat. Edit only the `*.json` files in this folder
(and this guide). Do **not** edit C++ (`SKSE_Source/`), Papyrus (`Scripts/Source/`) or prompts unless the user asks.
`guide.md` is not loaded by the game; every `*.json` here is.

## What these files are

One file = one injury **source**. `SkyrimNet_Whipped.dll` loads every `injuries/*.json` at game start
(`InjuryConfig`), tracks damage per source per body part (`InjuryEngine`), and uses the files for all text the LLM
sees (character bio, "how it feels", hit narration, healing narration), the Whip_Target action text and the
whip overlay. Restart the game to see edits. Look in `SkyrimNet_Whipped.log` (`injuries: ...` lines) for load errors:
a file that fails to parse is skipped and named there.

| File | Source | Fed by |
|------|--------|--------|
| `whipping.json` | model `whip`: damage then healing over game time, requires a SkyrimNet_Whipped whip | Whip_Target action, real Padded Whip hits |
| `slashing`, `piercing`, `bludgeoning` | model `health`: physical weapon hits | blades / daggers, arrows / bolts, maces / hammers / staves / fists |
| `burning`, `freezing`, `shocking` | model `health`: spell and enchantment damage | magic effects by resist type (fire, frost, shock) |
| `mauling` | model `health` | unarmed attacks by creatures (non-NPC attackers) |
| `generic` | model `health` | anything that cannot be classified; also the fallback for a class with no file; also supplies the common heal wording |
| `sexual` | (not written yet) | will require the victim to be in SexLab; do not create it unless asked |

A new injury type = a new `<id>.json` copied from a `health` file (not from `whipping.json`) plus a mapping in
`Classifier.cpp` (C++), which you cannot do here. Tell the user what hit class should map to it.

## How damage becomes injury (so you can judge the wording)

- Body: 6 parts (face, left arm, right arm, back, left leg, right leg). Regions: head, body, arm, leg.
- **Stages** (damage per part, 20 max): light 1-3, medium 4-10, severe 11-20. All health sources use the same scale;
  100% of max health lost = every part at 20.
- **whip** source: each lash adds a point to a random part; wounds hold health down (never below 10%), then heal over game
  time: damage drains, then healing rises through recovering, mostly healed, healed (scar).
- **health** sources: the health an attack takes becomes damage points, tagged with the classified source, on random parts
  (a part at max is skipped). Healing (potions, spells, rest) removes points at random across all sources; each source
  and each part is reported as it heals.
- **Real damage replaces whip damage** first (health is not lowered by that part), so a whipped actor only dies after the
  whip wounds are all replaced; the overlay shows replaced points as a darker bar.

## File format

Plain JSON (comments are tolerated by the parser but keep it valid JSON anyway). Keep keys lowercase.

```
id                 file name without .json (also the source id used in code)
title              human name
model              "whip" | "health"
requires           "whip" | "sexlab" | ""   (metadata; enforced in code)
damage_max, healing_max, thresholds, health_link, regions, parts
                   BODY BLOCK: must be identical in every file (only the first loaded file is read, whipping first).
                   Do not change numbers or part order without asking the user; the overlay and decorators depend on it.
stages             damage stage names, first 3 = light, medium, severe. whip adds recovering, mostly_healed, healed.
defaults           (whipping only) default values for dashboard settings; manifest.yaml must mirror them
overlay            (whipping only) title, bar order, colors (hex) incl. "replaced"
action             (whipping only) Whip_Target description ({target} and {strikes} are literal SkyrimNet placeholders, keep them),
                   no_whip line, impact_nodes list
text.looks_heading / looks   what each wounded part looks like  -> character bio ("### Whip Wounds" list)
text.feels_intro / feels / feels_outro   how it feels  -> final instructions
narration.list     how part names are joined ("face, back and left arm")
narration.hit      hit narration: lead + verb + times + by, then levels[0..3] by the worst part's state before the hit
                   (0 undamaged, 1 light, 2 medium, 3 severe)
narration.summary  (whipping) Whip_Target summary line
narration.heal     stage (text when a part reaches that stage while healing), source_healed, part_healed
narration.heal_events   (whipping and generic) spell / staff / potion healing: start, pain_gone, full, stopped
```

`text.looks.<region>` and `text.feels.<region>` are arrays indexed by stage, one entry per stage name in `stages`
(whipping 6, health sources 3). Regions: `head` (the face), `body` (the back), `arm`, `leg`.

### Placeholders

Use `{name}`, `{part}`, `{he}`, `{him}`, `{his}` in text, plus `{reflexive}`. Hit narration also has `{parts}`, `{verb}`,
`{times}`, `{by}`, `{attacker}`. Heal events also have `{caster}`, `{noun}`, `{who}`, `{obj}`, `{own}`, `{owner}`,
`{owner_lc}`, `{progress}`. Summary: `{speaker}`, `{target}`, `{times}`. Unknown `{tokens}` are left as typed, so do not
invent new ones (the code cannot fill them).

Hit narration is `lead + levels[n]`: `lead` is `"{name}'s {parts}{verb}{times}{by}"`, so `verb_one` / `verb_many`,
`times.*` and `by_*` carry their own leading space, and each `levels` entry starts with a comma, space or period as needed.

## Writing rules

1. **No numbers** in anything the LLM reads: no digits, percentages or point counts (`docs/authors/prompts.md#no-numbers`).
   Use stage words (light, medium, severe, recovering, healed) and plain description.
2. One sentence tone: concrete, sensory, specific to the damage type; escalate light -> medium -> severe; keep
   healing text moving toward scars/healed. Wounds the NPC would not survive must not read as trivial.
3. `feels` describes sensation, emotion and how it changes tone, posture and movement of the speaker; do not narrate
   actions for them.
4. Keep entries self-contained: each one is shown alone for its part. Avoid relying on another part's text.
5. Match gender-neutral pronoun placeholders; never hard-code he / she.
6. Do not mention game mechanics, "damage points", "stage" or file names in prose.
7. Whip-specific wording (welts, lash, whip) stays in `whipping.json`; other files must not mention whips.
8. Add content, do not delete keys: the engine reads every key above; a missing text is an empty sentence.

## Workflow for a chat session

1. Read the file(s) you will change and, for wording consistency, `whipping.json` (richest) and `generic.json`.
2. Propose changes in plain language first when the user's request is open-ended, then edit.
3. After editing, validate: `python -c "import json,sys;[json.load(open(f,encoding='utf-8')) for f in sys.argv[1:]]" injuries/*.json`
   and check every `looks` / `feels` region array has exactly one entry per stage (6 for whipping, 3 for others).
4. Report what changed per file and anything that needs a code change (new class, new placeholder, body numbers).
5. Do not touch the body block, `id`, `model`, or `requires` unless the user explicitly asks.
