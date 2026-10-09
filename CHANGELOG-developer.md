https://github.com/GoodProvider/SkyrimNet_Whip/releases/tag/0.1.0

First release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **A padded whip for SkyrimNet.** Craft it at the forge or add it with the console; every real hit with it lashes the target.
- **Wounds on a random body part.** Face, back, arms and legs each take damage from light to severe, and the welt decal shows on the skin.
- **Wounds heal.** Over game time, and with healing spells, potions and natural regeneration, parts move through recovering and mostly healed to healed (scarred). Health stays held down while you are wounded.
- **NPCs react.** Hits and healing are narrated to the LLM, batched so a flurry of lashes does not flood it. Everyone who looks at the victim knows what the wounds look like, and the victim knows how they feel.
- **NPCs can whip someone.** The new `Whip_Target` action walks to the target and lashes them several times; by default the NPC must be carrying the whip.
- **Whipping overlay.** Shows a bar per wounded actor, damage first, then healing progress; it fades out by itself.
- **Settings.** Narration cooldown and range, strike limit, healing speed and regeneration, giving yourself Healing Hands, and the overlay are in the SkyrimNet dashboard under SkyrimNet_Whipped.
