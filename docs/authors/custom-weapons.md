# Adding your own weapon

How to make a weapon from your own mod produce its own wound descriptions in Injury_Engine. You need to be able to edit JSON and look up a form ID in xEdit or the Creation Kit. You don't need C++, Papyrus or any change to Injury_Engine's prompts.

For every key in an injury file, see the full schema in [injuries/guide.md](../../SKSE/Plugins/Injury_Engine/injuries/guide.md). This page covers the path from your weapon to your text.

## How your file gets called

Nothing calls your file by name, and there is nothing to register. Your file claims the weapon itself, through its `classify` block:

```text
Game start   Injury_Engine.dll reads every *.json in injuries/humanoid/
             Each file becomes one injury source; its "classify" rules go into one list
             weapon_file + weapon_id in a rule is turned into the weapon's form

A hit        The damage hook asks: which rule matches the weapon that struck?
             Your rule matches -> the wound is recorded under YOUR source, on one body part

The text     Every description of that wound is read from the file of its source:
             hit narration, how it looks, how it feels, killing blow, healing
```

So the `classify` block connects the weapon to the file, and the rest of the file is the text that gets used. SkyrimNet's prompts loop over whatever wounds an actor has, so a new source appears in them with no prompt edits.

## Do you need a file at all?

Maybe not. With no file of your own, weapons are classified by their weapon type:

| Weapon type | Injury source |
|-------------|---------------|
| Sword, one-handed axe, two-handed axe | `slashing` |
| Dagger | `piercing` |
| Bow, crossbow (a shot) | `arrow` |
| Mace, staff, warhammer, a bash with a bow or crossbow | `bludgeoning` |

Your weapon only needs a file if it should read differently from those, such as a scythe, a thorned lash or a saw blade.

A rule always lives in the file of the source it claims. Another mod cannot point a weapon at `slashing` from its own file; it would have to replace `slashing.json`. If you want your weapon to use the same words, copy the file and keep the text.

## Step by step

1. **Copy a shipped file** into your own mod, under the `humanoid` folder:

   ```text
   <YourMod>/SKSE/Plugins/Injury_Engine/injuries/humanoid/mymod_scythe.json
   ```

   - Copy `slashing.json` for a weapon that does real damage and can kill (model `health`).
   - Copy `slapping.json` for a torture implement that wounds without killing (model `torture`).

   The `humanoid` folder is required: a file directly in `injuries/` is not loaded. Your mod manager merges your folder with Injury_Engine's.

2. **Set the identity.** Use the same unique name for the file and for `id`, prefixed with your mod so it can't clash with another mod's file. Give it a `title`.

   ```json
   "id": "mymod_scythe",
   "title": "Scythe Wounds"
   ```

3. **Find your weapon's form ID** in xEdit. Drop the load-order prefix to get the file-local ID:

   | xEdit shows | Your plugin is | Write |
   |-------------|----------------|-------|
   | `0A000D62` | a normal ESP / ESM | `"0xD62"` |
   | `FE012803` | ESL or ESL-flagged | `"0x803"` (last three digits) |

4. **Replace the `classify` block** with a rule for your weapon (see the recipes below). Include the plugin's file extension, and keep the `0x`: a value written as `"803"` is read as decimal and finds the wrong form.

   ```json
   "classify": [
     { "priority": 100, "weapon_file": "MyWeapons.esp", "weapon_id": "0xD62" }
   ]
   ```

5. **Copying a torture file only:** change `action.alias` to your own short word, or delete the `action.alias` and `action.order` keys. Left as `"slap"`, your weapon's entry collides with the real slap.

6. **Rewrite the text** (see the table below). Keep every key and the number of entries in each array: a missing text is an empty sentence. Follow the [writing rules](../../SKSE/Plugins/Injury_Engine/injuries/guide.md#writing-rules), above all: no digits or percentages in anything the LLM reads.

7. **Restart the game.** Files are read once at game start, so loading a save is not enough.

## `classify` recipes

Each of these is the `classify` key of your file.

**One weapon:**

```json
"classify": [
  { "priority": 100, "weapon_file": "MyWeapons.esp", "weapon_id": "0xD62" }
]
```

**Several weapons**, one rule each. An enchanted variant made from a template is its own weapon record with its own ID, so list each one you want covered:

```json
"classify": [
  { "priority": 100, "weapon_file": "MyWeapons.esp", "weapon_id": "0xD62" },
  { "priority": 100, "weapon_file": "MyWeapons.esp", "weapon_id": "0xD63" },
  { "priority": 100, "weapon_file": "MyWeapons.esp", "weapon_id": "0xD64" }
]
```

**A whole family of weapons by keyword.** `weapon_types` is required, because a keyword alone matches nothing. The priority must be above 50, or the shipped `slashing` rule wins. Write the keyword's EditorID:

```json
"classify": [
  { "priority": 60, "weapon_types": ["OneHandSword", "TwoHandSword"], "keyword": "MyMod_WeapTypeScythe" }
]
```

Weapon types: `OneHandSword`, `TwoHandSword`, `OneHandAxe`, `TwoHandAxe`, `OneHandDagger`, `Bow`, `Crossbow`, `OneHandMace`, `Staff`, `HandToHandMelee`.

**A torture implement.** The rule looks the same as the first recipe; the difference is `"model": "torture"` in the file. Hits become torture points, can't kill, and don't start a fight on their own. `hook.points_per_hit` sets how many points one hit adds (default one).

A rule that names a weapon always beats the weapon-type rules, in or out of combat. `priority` between named rules only matters if two files name the same weapon. If your plugin isn't loaded, the rule is simply inactive, so the JSON is safe to ship.

## Which key produces which text

| Key | Where the LLM sees it |
|-----|-----------------------|
| `text.looks_heading`, `text.looks.<region>` | Character bio: what the wound looks like to others |
| `text.feels_intro`, `text.feels.<region>`, `text.feels_outro` | Final instructions: how the wound feels to the victim |
| `narration.form` | The weapon as a phrase in who-hit-whom lines and killing blows ("a keen cutting blade") |
| `narration.hit` | The line narrated when a hit lands, worded by how hurt the part already was (`levels`) |
| `death` | Killing blow, real-damage files only |
| `narration.heal_events` | Healing lines for spells, staves and potions |
| `narration.session`, `narration.heal.level` | Torture files only: combined torture scene lines and heal-session lines |
| `text.feels.torture.<region>`, `text.feels.heal.<region>` | Torture files only: how it feels while hurt, and while being healed |
| `overlay_colors` | Torture files only: the overlay bar colors for this type |

Regions are `head`, `body`, `arm` and `leg`. Each `looks` / `feels` region is an array with one sentence per stage, mildest first. Use `{name}` and the other [placeholders](../../SKSE/Plugins/Injury_Engine/injuries/guide.md#placeholders) rather than a character's name or gender.

Leave `narration.heal_events` and the body-related keys (`damage_max`, `thresholds`, `health_link`, `regions`, `parts`) as you copied them, unless you have a reason to change them.

## Other bodies and optional parts

**Creatures use their own wording.** Your file is the text for humanoid victims. A draugr, troll, dragon or other non-humanoid victim belongs to a race group, and that group's generic text replaces your text so wounds fit the body (no blood on a skeleton). To word your weapon for a group, add a file with only the keys you want to change. Objects merge and arrays replace:

```text
<YourMod>/SKSE/Plugins/Injury_Engine/injuries/humanoid_undead/mymod_scythe.json
```

Groups are listed in [guide.md](../../SKSE/Plugins/Injury_Engine/injuries/guide.md#groups). `classify` is read from the humanoid file only.

**Sexual parts have no text for a new source.** With the setting "Include sexual body parts" on, a wound there would be an empty sentence. The simplest fix is to keep your weapon off those parts:

```json
"part_weights": { "nipples": 0, "pussy": 0, "penis": 0, "lips": 0, "ass hole": 0 }
```

To write text for them instead, the block goes under your source ID in `humanoid/sexual_parts.json`. That is Injury_Engine's own file, so a separate mod would have to replace it, which is why the weights are the better choice for most weapons.

## Check that it works

1. Start the game and open `Injury_Engine.log` (in `Documents/My Games/Skyrim Special Edition/SKSE/`). Look for:

   ```text
   injuries: loaded mymod_scythe.json (mymod_scythe, model health)
   Classifier: weapon MyWeapons.esp 0xD62 -> 0A000D62 (mymod_scythe)
   ```

2. Hit an NPC hard. A real-damage wound has no text until it reaches the minor stage, so a scratch stays silent:
   - The part must hold at least a quarter of its real maximum, and the victim must have lost at least a quarter of its health.
   - Each attack lands on one randomly picked part, so small hits spread out. For a clear test, use a weak NPC, or a weapon strong enough to take a quarter of its health in one blow.
   - Torture weapons show text from the first hit.

3. To see each hit's classification, turn on the dashboard's verbose logging (`injury_engine.debug.verbose_logging`). You'll get lines like this:

   ```text
   classify 00013BB2: melee hit weapon 0A000D62 attacker 00000014 -> mymod_scythe
   ```

| You see | Cause |
|---------|-------|
| No `loaded` line for your file | The file isn't in `injuries/humanoid/`, or the mod isn't active |
| `injuries: mymod_scythe.json skipped: ...` | Invalid JSON, or fewer than three `stages`; the message names the problem |
| `Classifier: weapon ... not loaded, rule inactive` | The plugin isn't loaded, or `weapon_file` is misspelled, or `weapon_id` is the wrong form (check `0x`, drop the load-order prefix) |
| The `classify` line ends in `slashing` | Keyword rule with priority 50 or lower, a misspelled keyword, or `weapon_types` missing or wrong |
| Right source, empty sentence | A missing key or too few array entries; or a sexual part (see above) |
| Right source, no text at all | Real-damage wound still below the minor stage |
| A `more than 32 sources` warning | See limits below |

## Limits

- At most 32 sources load in total. Injury_Engine uses 19, and a `both` file counts as two.
- Rules name weapon records. A special arrow can't have its own text, because the bow is what the hit reports.
- A weapon's fire, frost or shock enchantment may be classified as `burning`, `freezing` or `shocking` rather than as your source. This has not been tested for custom weapons.
- The only shipped named-weapon rule is Diary of Mine's slap in `slapping.json`, a working example of the format.
- An effect rule (`effect_file` / `effect_id`) works for torture sources only. A real-damage spell is classified by its resist type.
