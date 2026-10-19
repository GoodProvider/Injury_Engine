## 0.1.2

### Fixes
- A buff wearing off (Courage, Fortify Health and the like) no longer counts as a fall or a wound.
- Walking down steps or slopes can no longer be taken for a hard landing.
- Loading a save no longer leaves old torture scenes, heal sessions or truces running.
- Loading a save during an NPC's torture action no longer stops later lashes from counting.
- Punches and weapon hits are judged by the actual swing, not by what is equipped.
- A recent whip lash no longer hides other damage that follows it.
- Torture narration no longer gets cancelled when you look away for a moment.
- Heal start narration is no longer lost while SkyrimNet is still replying.
- The overlay no longer shows an older state after quick updates.

### Fighting
- An NPC can keep several truces at once, each with its own end time.
- A late answer to an expired truce request is ignored.

### Wounds
- A mistake in an injury file now skips only that entry instead of the whole file.

## 0.1.1

### Fixes
- Fixed a game freeze when spells hit a badly tortured actor.
- Fire spells cast with empty hands no longer count as punches.
- Body parts that are switched off no longer get real wounds.

### Wounds
- Drowning now wounds the lungs.
- New setting: minimum health loss from other mods' scripts that counts as a wound.

### Narration
- Narration names attacker and victim instead of "he" / "she", so followers don't think you were hurt.
- Real hits in fights out of sight are no longer narrated (they stay short events).

### FOMOD
- FOMOD option: fight / stop / yield actions (on by default; Torture_Target always installs).
- FOMOD option: torture overlay (on by default; off sets its dashboard default to off).

## 0.1.0

### Requirements 
- Requires SkyrimNet 0.25.0, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

### Pain and Healing 
- Every hit wounds a body part: swords, arrows, spells, creatures, falls and torture.
- Wounds go from light to medium to severe and heal over time, with regeneration and healing magic.
- Torture, healing, hits and killing blows are narrated, and NPCs see and feel the wounds.
- Other mods can add injury types and wound or heal body parts.

### Torture 
- Torture (whip, pinch, slap, pull, punch, fire, frost, shock) hurts but can't kill.
- Victims tortured far enough collapse until they are healed (setting).
- Craft the padded whip at the forge; "Add gear to player" also gives the three torture spells.
- Torture and lashes don't start fights.
- NPCs can torture someone with the Torture_Target action.
- A torture overlay shows a bar per tortured actor and fades out by itself.
- Optional damageable sexual body parts (setting).

### Real Damage 
- Real damage replaces torture wounds first, so a tortured victim dies only after that.

### Combat start, stop, yield
- NPCs can offer to stop a fight or yield; offers to you show a Yes / No box.
- NPCs can start a fight with Start_Fighting.
