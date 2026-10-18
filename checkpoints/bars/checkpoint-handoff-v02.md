# Handoff v02: red means real damage, whipping is green

Status: **Edited, not committed.** Follows v01. JSON / page only: the DLL from v01 (20:21) is current, no rebuild.

## Problem
Red was used for both whipping torture and (dark red) the real-damage "replaced" segment. Developer: red should mean
real damage only; whipping gets a different color.

## Changes
- `injuries/whipping.json` `overlay.colors`: whipping light / medium / severe `#a8dc8c / #4caf3a / #1f5e17` (green,
  the one hue no other torture type or the heal blue uses); `replaced` `#c8201e` (clear red, was `#3d0606`).
- `overlay.html`: CSS fallback vars match (`--light`, `--medium`, `--severe`, `--replaced`); comment says "red segment".
- Docs: CLAUDE.md, README (feature line + replacement paragraph), `docs/developers/overlay.md`,
  `docs/reference/json-keys.md` example, `injuries/guide.md` ("red bar"; missing `overlay_colors` falls back to
  whipping's green).
- Untouched: `CHANGELOG-player.md` (released history still says "darker bar").

Current palette: whipping green, pinching purple, slapping pink, pulling amber, punching blue-grey, fire orange,
frost teal, shock yellow, falling grey, healing light blue, real damage (replaced) red.

## Known limits
Same as v01. A torture type without `overlay_colors` now shows green (whipping's colors), not red.

## Next step (developer), overlay on
1. Whip an NPC: bar is green, darker green at higher stages.
2. Hit the whipped NPC with a sword: a red segment grows from the left while the green shrinks.
3. Fire torture: orange, distinct from the red segment.
4. If a red tone is too close to fire / slapping in game, tweak `whipping.json` `overlay.colors.replaced`
   (restart the game; JSON loads at kDataLoaded).

## After it holds
Commit v01 + v02 together (exclude `checkpoints/`), e.g.:
`Overlay bar shows the last torture type in its own color`
- `InjuryEngine.cpp`: `g_lastTorture`, `SourceReach`, `OverlaySource`, `OverlayJson` source / max / source_colors
- `overlay.html`: per-actor max, per-type fill colors, red replaced segment
- `injuries/*.json`: `overlay_colors` per torture type; whipping green, replaced red
- docs: CLAUDE.md, README, json-keys, overlay.md, guide.md
