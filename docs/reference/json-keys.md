# JSON keys

Canonical rules for JSON that this mod emits for prompts and the overlay.

## Rule

Use **bare lowercase** keys only (`parts`, `stage`, `caster_is_target`). Do not invent Title Case or leading-`_` keys. Values shown to the LLM are words, never numbers — numeric fields are only for templates to turn into words ([../authors/prompts.md](../authors/prompts.md#no-numbers)).

## Decorator `injury_engine_injuries(uuid)`

`Main.Injuries_Decorator` -> native `GetStateJson` -> `InjuryEngine::StateJson`. One entry per source that has injuries; the text is already resolved from `injuries/<source>.json` for this actor (name, pronouns, part):

```json
{"sources":[{"id":"whipping","looks_heading":"### Whip Wounds","feels_intro":"How Nina's whip wounds feel...","feels_outro":"Answer must be creative...",
  "parts":[{"part":"left arm","region":"arm","stage":"medium","look":"Nina's left arm: ...","feel":"Nina's left arm throbs..."}]}]}
```

| Key | Meaning |
|-----|---------|
| `sources[].id` | source id (file name without `.json`) |
| `looks_heading`, `feels_intro`, `feels_outro` | headings / framing text from the source's `text` block |
| `parts[].part` / `region` | body part name / region (head, body, arm, leg; with "include sexual parts" also nipple, vulva, penis, lips, anus) |
| `parts[].stage` | stage word of that source (`light`, `medium`, `severe`); torture parts during a heal session: `high`, `mid`, `low` |
| `parts[].look` / `feel` | the finished sentences for the character bio / final instructions (torture `feel` from `text.feels.torture`, or `text.feels.heal` during a heal session) |
| `healed` | only when `sources` is empty and the actor is alive: `session.json` `healed` ("{name} is completely healed."), printed by the character bio so the LLM drops wounds left in the dialogue history |

## Injury files `injuries/<source>.json`

Keys and placeholders are documented for authors in `SKSE/Plugins/Injury_Engine/injuries/guide.md`; code reads them by dotted path (`InjuryConfig::Str`, natives `Text` / `TextCount` / `Number`). Same bare-lowercase rule; values shown to the LLM contain no digits.

## Natives returning text

`HitText`, `HealMapText`, `SourceHealText`, `ActionText`, `DeathText` render the JSON templates in C++ (placeholders `{name}`, `{he}`, `{him}`, `{his}`, `{part}`, `{parts}`, ... listed in `guide.md`). Codes passed in: hit code `fresh * 100000000 + source * 1000000 + part mask * 10 + level` (mask: bit per part, 11 parts); `HealMapText(actor)` takes no codes (it reads the actor's torture points); sync events `kind * 10000 + healing source * 1000 + percent` (kind 0 start, 1 pain gone, 2 fully healed, 3 stopped; healing source 0 none, 1 spell, 2 staff, 3 potion, 4 other).

## Overlay (`injuryEngineUpdate`)

`InjuryEngine::OverlayJson`, pushed by `Overlay::Notify` when it changes:

```json
{"title":"Torture Damage","max":120,"part_max":[20,20,20,20,20,20],"medium":4,"severe":11,
 "colors":{"light":"#a8dc8c","healing":"#8fd3ff","replaced":"#c8201e"},
 "source_colors":{"whipping":{"light":"#a8dc8c","medium":"#4caf3a","severe":"#1f5e17"},"pinching":{"light":"#c9a0e8"}},
 "actors":[{"id":20,"name":"Nina","source":"pinching","max":100,"replaced":6,"healing":false,"parts":[3,0]}]}
```

`parts` is six torture point counts in bar order (`overlay.order` of `whipping.json`: head, body, right arm, left arm, right leg, left leg), eleven with "include sexual parts" (then nipples, pussy, penis, lips, ass hole); the example is cut short. `part_max` is each part's damage points in the same order; `max` is the whole-body capacity (one genital part counted). An unhit or healed part is `0`. Each bar shows one torture type, `source`: the actor's open type hit most recently (after a load, which keeps no hit times, the one with the most points); `parts` are that type's points (`0` on parts it does not reach) and the actor's `max` is the `part_max` sum of the parts it reaches (its `part_weights` above 0, fitting the actor's sex), so the bar is that type's share of what it can wound. The top-level `max` is the whole-body capacity (fallback). `source_colors` maps each torture type id to its `light` / `medium` / `severe` bar colors (`overlay_colors` of its injury file, whipping's `overlay.colors`); the fill uses the stage color of the shown type. `healing` is true while a healing source works on the actor (heal session): the bar is drawn in `colors.healing`. `replaced` = torture damage points already replaced by real damage. Only loaded, living actors that still have torture points are listed (an actor whose torture is fully replaced drops out). The decorator, by contrast, lists every torture type separately, one source block each.
