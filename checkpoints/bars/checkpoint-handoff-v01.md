# Handoff v01: overlay bar shows the last torture type, one color per type

Status: **Built (DLL 20:21, 2026-10-15), not committed.** No Papyrus or ESP changes. Follows killed v09 (independent topic).

## Problem
Torture is tracked per type (`State::torture[source][part]`), but the overlay bar summed each part's *worst* type over
the whole body and was always whipping red. Wanted: the bar shows the **current share of the last torture type**
that hit the actor, each type in **its own color**.

Decisions (developer):
- 100% = the part max of the parts that type can reach (its `part_weights` > 0, fitting the actor's sex, active parts
  only; none → every fitting part, same as `TortureHit`).
- Each type has light / medium / severe shades of its own hue; stage still darkens. Heal-session light blue and the
  darker "replaced" segment stay.

## Findings
- Every torture hit (`Hit`, `QueueHit`, hooked punches / spells) goes through `TortureHit` in `InjuryEngine.cpp`, so
  that is the one place to stamp "last hit by this type".
- `InjuryConfig` picks the body / defaults file by the text `"overlay"` (`InjuryConfig.cpp` `hasOverlay`,
  `WhipIndex`). A per-type `overlay` block would break that, hence the separate key `overlay_colors`
  (`"overlay_colors"` does not contain `"overlay"` with the closing quote).

## Changes
- `InjuryEngine.cpp`:
  - `g_lastTorture` (FormID → real seconds per type, transient; erased in `Erase`, cleared on co-save load and `Revert`),
    stamped in `TortureHit`.
  - `SourceReach(source, female, weight&)`: the parts a type reaches; `TortureHit` now uses it for its candidates
    (same behavior as before, extracted).
  - `OverlaySource(st, id)`: open type (points left) hit most recently; no times → most points; -1 when none.
  - `OverlayJson`: per actor `source`, `max` (reach part-max sum), `parts` = that type's points (0 off-reach);
    top level `source_colors` {id: {light, medium, severe}} from `overlay_colors`, whipping from `overlay.colors`.
- `InjuryEngine.h`: JSON comment.
- `overlay.html`: width = points / actor `max`; fill colored inline from `source_colors[source][stage]` (healing clears
  it so `.fill.healing` wins); a type switch counts as a hit for ordering; replaced segment clamped to `100 - fill`.
- Injury JSON `overlay_colors` (light / medium / severe):
  pinching `#c9a0e8 #8e44c9 #4a1a73`, slapping `#f4a6c8 #e0508f #8c1f52`, pulling `#f5c97a #e09a1e #8a5a0a`,
  punching `#9fb4d6 #4f6fa8 #23355e`, fire `#ffb27a #ff6a1a #a33500`, frost `#9fe8e0 #2fbfb0 #0e6b63`,
  shock `#f7ef8a #e3cf1a #8a7a00`, falling (torture half) `#c8c2b8 #8a8276 #4a443b`; whipping keeps its reds.
- Docs: CLAUDE.md overlay paragraph, README feature line, `docs/reference/json-keys.md`, `docs/developers/overlay.md`,
  `injuries/guide.md` key table.

## Known limits
- Hit times are not co-saved: right after a load the bar shows the type with the most points until the next hit.
- `replaced` is whole-body (all types) and is scaled to the shown type's reach, clamped so the bar never overflows.
- When the shown type heals out but another type is still open, the bar switches to that one (next most recent).
- Overlay JS was not run outside the game (no node on this machine); review only.

## Next step (developer), overlay on, verbose logging on
1. `Injury_Engine.log` at load: no `injuries: ... skipped` lines; body / defaults still from whipping.json.
2. Whip a neutral NPC: red bar, grows with each lash.
3. Pinch the same NPC: bar turns purple and shows pinching's share (starts small even if whipping is high).
4. Torture fire spell: orange; frost: teal; shock: yellow; punch: blue-grey.
5. Stop and let the last type heal out (or wait): the bar falls back to the other open type's color and share.
6. Healing Hands: light blue; hit again: the type's color returns.
7. Real damage (sword) on a tortured NPC: dark segment grows from the left, bar never overflows.
8. Save, load: bar shows the type with the most points; next hit switches to that hit's type.

## After it holds
Commit (exclude `checkpoints/`), e.g.:
`Overlay bar shows the last torture type in its own color`
- `InjuryEngine.cpp`: `g_lastTorture`, `SourceReach`, `OverlaySource`, `OverlayJson` source / max / source_colors
- `overlay.html`: per-actor max, per-type fill colors
- `injuries/*.json`: `overlay_colors` for every torture type
- docs: CLAUDE.md, README, json-keys, overlay.md, guide.md
Optional follow-up: tune colors; consider a KNOWLEDGEBASE note on the `"overlay"` text probe in `InjuryConfig`.
