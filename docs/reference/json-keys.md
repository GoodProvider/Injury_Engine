# JSON keys

Canonical rules for JSON that this mod emits for prompts and the overlay.

## Rule

Use **bare lowercase** keys only (`parts`, `stage`, `caster_is_target`). Do not invent Title Case or leading-`_` keys. Values shown to the LLM are words, never numbers — numeric fields are only for templates to turn into words ([../authors/prompts.md](../authors/prompts.md#no-numbers)).

## Decorator `skyrimnet_whipped_injuries(uuid)`

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
| `parts[].stage` | stage word of that source (`light`, `medium`, `severe`; whipping also `recovering`, `mostly_healed`, `healed`) |
| `parts[].look` / `feel` | the finished sentences for the character bio / final instructions |

## Injury files `injuries/<source>.json`

Keys and placeholders are documented for authors in `SKSE/Plugins/SkyrimNet_Whipped/injuries/guide.md`; code reads them by dotted path (`InjuryConfig::Str`, natives `Text` / `TextCount` / `Number`). Same bare-lowercase rule; values shown to the LLM contain no digits.

## Natives returning text

`HitText`, `HealText`, `SourceHealText`, `ActionText` render the JSON templates in C++ (placeholders `{name}`, `{he}`, `{him}`, `{his}`, `{part}`, `{parts}`, ... listed in `guide.md`). Codes passed in: hit code `fresh * 100000000 + source * 1000000 + part mask * 10 + level` (mask: bit per part, 11 parts); heal codes `source * 128 + part * 8 + stage`, `10000 + source` (source healed on every part), `20000 + part` (part has no injury left), plus `100000` when no healing source contributed (passive: silent `RegisterShortLivedEvent`, text wrapped in `narration.heal.passive`); sync events `kind * 10000 + healing source * 1000 + percent` (kind 0 start, 1 pain gone, 2 fully healed, 3 stopped; healing source 0 none, 1 spell, 2 staff, 3 potion, 4 other).

## Overlay (`whipUpdate`)

`InjuryEngine::OverlayJson`, pushed by `Overlay::Notify` when it changes:

```json
{"title":"Whip Damage","max":120,"part_max":[20,20,20,20,20,20],"healed":11,"medium":4,"severe":11,"mostly_healed":4,
 "colors":{"light":"#e8837a","replaced":"#3d0606"},
 "actors":[{"id":20,"name":"Nina","replaced":6,"parts":[[3,0],[0,11]]}]}
```

`parts` is six `[damage, healing]` pairs in bar order (`overlay.order` of `whipping.json`: head, body, right arm, left arm, right leg, left leg), eleven with "include sexual parts" (then nipples, pussy, penis, lips, ass hole); the example is cut short. `part_max` is each part's damage points in the same order; `max` is the whole-body capacity (one genital part counted). An unhit part is `[0, 11]` (healed). Each pair comes from the part's **worst** torture type (largest damage plus missing healing). `replaced` = torture damage points already replaced by real damage. Only loaded, living actors that still have torture damage or healing are listed (an actor whose torture is fully replaced drops out). The decorator, by contrast, lists every torture type separately, one source block each.
