# Actions

The mod's one LLM action, `Torture_Target` (it replaced `Whip_Target`; the rename resets the user's per-action settings).

See also: [prompts.md](prompts.md), [../developers/papyrus.md](../developers/papyrus.md).

## Registration

`Torture_Target` is registered from Papyrus (`SkyrimNet_Whipped_Actions.Setup` → `SkyrimNetApi.RegisterAction`), **not** from action YAML, so eligibility can check the NPC's inventory. There is no `actions/` folder in the plugin tree. The action description is `action.description` in `SKSE/Plugins/SkyrimNet_Whipped/injuries/whipping.json` (keep the `{target}`, `{type}` and `{strikes}` placeholders); the "has no whip" event is `whipping.json` `action.no_whip`; the summary narration is `narration.summary` of the chosen type's file.

| Part | Function |
|------|----------|
| Eligibility | `Torture_IsEligible`: not the player, not dead, not in combat (the whip requirement is checked at execution, since hand torture needs nothing) |
| Execution | `Torture_Execute(speaker, contextJson, paramsJson)` |
| Parameters | `target` (Actor), `type` (`whip\|pinch\|slap\|pull\|punch` → `whipping`, `pinching`, `slapping`, `pulling`, `punching`), `strikes` (`1\|2\|…\|max_strikes`) |

`whip.action.max_strikes` is baked into the parameter list at registration, so a change needs a save and reload. Fire / frost / shock torture is not an action type: it comes from the Flames / Frostbite / Sparks (torture) spells and from `External_Torture`.

## Execution

The NPC walks to the target and tortures it `strikes` times.

- `whip`: equips the padded whip and swings (`attackStart`); each strike is `Main.Lash` (impact effect + sound → `Main.OnLash`). `skyrimnet_whipped.in_action` stops the enchantment from counting the same hit twice. With `require_whip` on and no whip, a `whip_lash` event says so and nothing happens; with it off, a whip is handed over and taken back afterwards.
- `pinch`, `slap`, `pull`, `punch`: no weapon and no real swing (an unarmed attack would land as real damage through the hooks and start a fight). Each strike is `Main.Torture`, which plays the type's `action.animation` event when the JSON sets one (empty by default: vanilla has no fitting idles) and calls `Engine.Hit`.

One summary narration follows (DirectNarration, or event `torture_hit`).

## Events

| Type | When |
|------|------|
| `whip_lash` | `Torture_Target` with `require_whip` on and the NPC has no whip (`whip_lash_no_whip_<speaker>`; repeat overwrites) |
| `torture_hit` | Torture not sent as DirectNarration: hits between scene lines, narration off, or speech queue busy. One short-lived event per torturer→victim pair (`torture_hit_<attacker>_to_<victim>`, attacker `0` when none); each register overwrites and refreshes TTL (like SkyrimNet `combat_hit`) |
| `injury_hit` | Real-damage hit (always an event) |
| `whip_heal` | Healing not narrated (start / pain gone / stopped with the gate closed, fresh part heal with narration off or too far) |
| `whip_heal_<formid>_<n>` (short-lived, type `whip_heal`) | Parts healed while the gate is closed; batched into the next heal narration |

## Rules

- Do not use Papyrus decorators in eligibility — `CallDecoratorDirect` returns empty on a cache miss.
- Keep the action name stable; renaming resets the user's per-action settings.
- After changes: reload a save, then SkyrimNet webUI → Game Data Explorer → Refresh Actions.
