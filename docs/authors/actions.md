# Actions

The mod's one LLM action, `Whip_Target`.

See also: [prompts.md](prompts.md), [../developers/papyrus.md](../developers/papyrus.md).

## Registration

`Whip_Target` is registered from Papyrus (`SkyrimNet_Whipped_Actions.Setup` → `SkyrimNetApi.RegisterAction`), **not** from action YAML, so eligibility can check the NPC's inventory. There is no `actions/` folder in the plugin tree. The action description, the "has no whip" event and the summary narration are in `SKSE/Plugins/SkyrimNet_Whipped/injuries/whipping.json` (`action.*`, `narration.summary`); keep the `{target}` and `{strikes}` placeholders in the description.

| Part | Function |
|------|----------|
| Eligibility | `Whip_IsEligible`: not the player, not dead, not in combat; with `whip.action.require_whip` on, must carry the padded whip |
| Execution | `Whip_Execute(speaker, contextJson, paramsJson)` |
| Parameters | `target` (Actor), `strikes` (`1\|2\|…\|max_strikes`) |

`whip.action.max_strikes` is baked into the parameter list at registration, so a change needs a save and reload.

## Execution

The NPC walks to the target and lashes it `strikes` times via `Main.Lash` (impact effect + sound → `Main.OnLash`). `skyrimnet_whipped.in_action` stops the enchantment from counting the same hit twice. With `require_whip` off, a whip is handed over and taken back afterwards. One summary narration follows (DirectNarration, or event `whip_lash`).

## Events

| Type | When |
|------|------|
| `whip_lash` | Lash not narrated (narration off, too far, gate closed) |
| `whip_heal` | Healing not narrated (start / pain gone / stopped with the gate closed, fresh part heal with narration off or too far) |
| `whip_heal_<formid>_<n>` (short-lived, type `whip_heal`) | Parts healed while the gate is closed; batched into the next heal narration |

## Rules

- Do not use Papyrus decorators in eligibility — `CallDecoratorDirect` returns empty on a cache miss.
- Keep the action name stable; renaming resets the user's per-action settings.
- After changes: reload a save, then SkyrimNet webUI → Game Data Explorer → Refresh Actions.
