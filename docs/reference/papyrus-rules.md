# Papyrus rules

Canonical Papyrus language and naming rules for this repo.

## Case

Papyrus is case-insensitive (`aA` == `Aa`). Casing is for human readers only; match `EndFunction` / `EndEvent` to the declaration.

## Arrays vs None

Never compare an array to `None`. Use truthiness (`if a`), which means non-None and non-empty.

```papyrus
String[] a = None
; OK: non-None and non-empty
if a
; Runtime error
if a == None
```

## Default parameters

Default args are filled **at the caller's compile**, not at runtime. Adding a parameter (even with a default) requires recompiling **every** caller. Cross-script calls (`Actions` → `Main`, `LashEffect` → `Main`) should pass every argument explicitly so a stale `.pex` cannot log `Expected N, got M` and return None.

The same holds for natives in `SkyrimNet_Whipped_Engine.psc`: a changed signature needs the `.psc`, every caller and the DLL rebuilt together.

## Naming

| Kind | Rule | Example |
|------|------|---------|
| Constants | `UPPER_SNAKE` | `KEY_PENDING_PARTS` |
| Properties / variables | start lowercase; snake or camel | `narration_cooldown` |
| Functions / Events | start Upper case | `NarrateLash` |

## Debug traces

Each script has a global `Trace(func, msg, notification=False)` that writes `[ScriptName] func: msg` with `Debug.Trace` (Papyrus log). Prefix temporary debug messages with `"---"` (or tag them `; DEBUG-WHIP`) so they are easy to find and remove:

```papyrus
Trace("NarrateHealing", "--- no text for codes:" + codes)
```

Logs:

On the maintainer's machine Documents is OneDrive-redirected (`%USERPROFILE%\OneDrive\Documents\...`). Absolute paths: [AGENTS.md](../../AGENTS.md) (Logs).

| Log | Path |
|-----|------|
| Crash Logger | `Documents\my games\Skyrim Special Edition\SKSE\crash-*.log` |
| SkyrimNet_Whipped (DLL) | `Documents\my games\Skyrim Special Edition\SKSE\SkyrimNet_Whipped.log` |
| Papyrus (traces, None / stack) | `Documents\my games\Skyrim Special Edition\Logs\Script\Papyrus.0.log` |
| SkyrimNet (last resort; ask first) | `Documents\my games\Skyrim Special Edition\SKSE\SkyrimNet.log` |

When counting lines for reviews, include commented lines.

## Related

- JSON keys: [json-keys.md](json-keys.md)
- Developer paths / compile: [../developers/papyrus.md](../developers/papyrus.md)
