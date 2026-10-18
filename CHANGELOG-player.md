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

### Installer
- Installer option: fight / stop / yield actions (on by default; Torture_Target always installs).
- Installer option: torture overlay (on by default; off sets its dashboard default to off).

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
