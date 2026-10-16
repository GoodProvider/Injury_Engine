# Actions

The mod's LLM actions: `Torture_Target` (it replaced `Whip_Target`; the rename resets the user's per-action settings), `Stop_Fighting`, `Stop_Fighting_by_Yielding` and `Start_Fighting`.

See also: [prompts.md](prompts.md), [../developers/papyrus.md](../developers/papyrus.md).

## Registration

All four are SkyrimNet YAML actions in `SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine/actions/` (filename = `name`). Nothing is registered from Papyrus any more. Each YAML calls a **member** function of `Injury_Engine_Actions` on quest `Injury_Engine` (0x800), with the `parameterMapping` entries as positional arguments (speaker first). Eligibility uses only the native `is_in_combat` decorator; the functions re-check the rest (not the player, not dead, a valid target).

| YAML | Function | Parameters | Eligibility |
|------|----------|------------|-------------|
| `Torture_Target.yaml` | `TortureTarget(speaker, target, type, strikes)` | `target`, `type` (`whip\|pinch\|slap\|pull\|punch` → `whipping`, `pinching`, `slapping`, `pulling`, `punching`), `strikes` (`1`..`10`) | not in combat |
| `Stop_Fighting.yaml` | `StopFighting(speaker, target)` | `target` | in combat |
| `Stop_Fighting_by_Yielding.yaml` | `StopFightingByYielding(speaker, target)` | `target` | in combat |
| `Start_Fighting.yaml` | `StartFighting(speaker, target)` | `target` | none |

The descriptions live in the YAML (`[speaker]`, `[target]`, `[type]`, `[strikes]` placeholders). The type list and strike range are fixed in `Torture_Target.yaml`: torture types added by other mods' injury JSON are not offered, an unknown type falls back to whip, and `injury_engine.action.max_strikes` only caps the chosen count. The "has no whip" event is `whipping.json` `action.no_whip`; the summary narration is `narration.summary` of the chosen type's file. Fire / frost / shock torture is not an action type: it comes from the Flames / Frostbite / Sparks (torture) spells and from `External_Torture`.

## Execution

The NPC walks to the target and tortures it `strikes` times.

- `whip`: equips the padded whip and swings (`attackStart`); each strike is `Main.Lash` (impact effect + sound → `Main.OnLash`). `injury_engine.in_action` stops the enchantment from counting the same hit twice. With `require_whip` on and no whip, a `whip_lash` event says so and nothing happens; with it off, a whip is handed over and taken back afterwards.
- `pinch`, `slap`, `pull`, `punch`: no weapon and no real swing (an unarmed attack would land as real damage through the hooks and start a fight). Each strike is `Main.Torture`, which plays the type's `action.animation` event when the JSON sets one (empty by default: vanilla has no fitting idles) and calls `Engine.Hit`.

One summary narration follows (DirectNarration, or event `torture_hit`).

## Stop_Fighting / Stop_Fighting_by_Yielding / Start_Fighting

The outcome lines are in `Injury_Engine_Actions.psc` (`ApplyTruce`, `StartFighting`), not injury JSON: they are combat text, not wound data. No numbers.

| Action | Execution |
|--------|-----------|
| `Stop_Fighting` | `Engine.AskTruce(speaker, target, false)`. Target is the player: a Yes / No message box "{name} wishes to stop fighting. Do you accept?" (Esc = No). Otherwise `Engine.AskTruce(speaker, target, false)` asks SkyrimNet's decision model (`TruceDecision.cpp`, template `prompts/decisions/injury_engine/accept_truce.prompt`) whether the target accepts; the answer comes back through mod event `Injury_Engine_Truce` → `OnTruce` → `ApplyTruce` |
| `Stop_Fighting_by_Yielding` | The speaker surrenders: it stands down (`Engine.StopFighting`), gets Aggression 0 (`Calm`) and sheathes its weapon at once. Target is the player: a Yes / No message box "{name} wishes to yield to you. Do you accept?" (Esc = No). Otherwise `Engine.AskTruce(speaker, target, true)` asks with template `accept_surrender.prompt`; same answer path |
| `Start_Fighting` | Ends a truce on either side (`RestoreAggression`), drops the torture calm between the pair (`Engine.ForgetCalm`), then `speaker.StartCombat(target)`. No combat gate, so it can switch targets mid-fight |

`ApplyTruce` for a truce offer:

- **Refused** (also when no decision can be asked: SkyrimNet API below v12, no decision provider set up, a request for that speaker already in flight, a failed request): the speaker stands down (`Engine.StopFighting`: `StopCombat` + `StopCombatAndAlarmOnActor`), and the target too if it is fighting the speaker. No Aggression change, so the fight may resume.
- **Accepted**: both stand down, and every NPC involved gets Aggression 0 for 24 game hours (`TRUCE_HOURS`). The original value is kept in StorageUtil and put back when the time runs out (`Main.OnUpdateGameTime` and every load call `RestoreExpiredTruces`) or when `Start_Fighting` involves that actor.

`ApplyTruce` for a surrender (the speaker has already stood down and is calmed):

- **Accepted**: the target stands down and gets Aggression 0 too.
- **Refused** (also when no decision can be asked): the target is left alone and may keep attacking. The calmed speaker may still defend itself when hit.

Truce guard (any accepted truce or surrender): `Engine.NoteTruce(speaker, target, until)` tells the DLL that for `TRUCE_HOURS` any combat start between the pair, either way, is stopped right away (`Classifier` combat sink). A downed actor under a truce that stands up from torture bleedout stands down again (a `StopCombat` during bleedout may not last). Each truce stop also clears the actor's `kAngryWithPlayer` flag. Aggression 0 alone doesn't do this, because an actor that is already hostile to the player fights again. The guard isn't saved: `Setup` notes active truces again from StorageUtil (`injury_engine.truce_with`). `Start_Fighting` drops it (`ForgetCalm`).

The decision context: both names and uuids, the relationship rank as a word, each side's health as a word, whether each is a follower, whether the target is fighting the speaker, and the target's recent events (hits, combat and spells left out). Truce: the question leans yes when the target is losing, hurt, a friend or ally, or the fight looks like a misunderstanding, and no for enemies, a winning target, or a grudge. Surrender: it leans yes for a merciful target, friends or followers, or a helpless speaker, and no for sworn foes, bloodthirsty bandits or monsters, or a grudge.

## Events

| Type | When |
|------|------|
| `whip_lash` | `Torture_Target` with `require_whip` on and the NPC has no whip (`whip_lash_no_whip_<speaker>`; repeat overwrites) |
| `torture_hit` | Every torture hit registers immediately (including the first, method switches, and out of sight). Id `torturing_<method>_<torturer>_to_<victim>` (`method` = injury file id, e.g. `whipping`, `shock`; missing torturer `0`). Re-register overwrites and refreshes TTL. Combined scene lines when narration is off or the speech queue is busy use the same id. |
| `injury_hit` | Every real-damage hit registers immediately. Id `health_<method>_<attacker>_to_<victim>`. |
| `injury_truce` | `Stop_Fighting` answer: "{speaker} offers to stop fighting, and {target} accepts ..." or "... but {target} refuses." Id `truce_<speaker>_to_<target>`. `Stop_Fighting_by_Yielding` answer: "{speaker} yields to {target}, and {target} accepts the surrender." or "... but {target} refuses the surrender." Id `surrender_<speaker>_to_<target>` |
| `injury_combat` | `Start_Fighting`: "{speaker} attacks {target}." Id `fight_<speaker>_to_<target>` |
| `whip_heal` | Every heal registers immediately (part heals, passive, heal start / pain gone / stopped). Id `healing_<method>_<caster>_to_<victim>` (`method` = wound source id, `part`, or heal-sync source `spell` / `staff` / `potion` / `other` / `none`). Session fallbacks when narration is off or the queue is busy use the same id. |

## Rules

- Do not use Papyrus decorators in YAML eligibility — `CallDecoratorDirect` returns empty on a cache miss. Use native decorators (`is_in_combat`, factions, globals) and re-check in the function.
- `executionFunctionName` must be a non-global function of `Injury_Engine_Actions` whose parameters match `parameterMapping` in order.
- Keep the action name stable; renaming resets the user's per-action settings.
- After YAML changes: restart the game, then SkyrimNet webUI → Game Data Explorer → Refresh Actions.
