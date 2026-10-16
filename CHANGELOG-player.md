## Unreleased

- When an NPC offers you to stop fighting or yields to you, a Yes / No box asks whether you accept.
- NPCs can now yield in a fight (Stop_Fighting_by_Yielding): they put their weapon away and surrender, and the other side decides whether to accept the surrender or keep attacking.
- The mod's LLM actions now ship as SkyrimNet action files, so they show and can be tuned in SkyrimNet's action settings like other plugins' actions.
- NPCs can now offer to stop a fight (Stop_Fighting). The other side decides whether to accept, using SkyrimNet's decision model (set up a decision provider in SkyrimNet, otherwise offers are refused). If they accept, both stand down and stay calm for a day; if not, the fight may go on. NPCs can also start a fight with someone, or turn on someone mid-fight (Start_Fighting).
- Other mods can now read how hurt each body part is (SkyrimNet_SexLab's Aid and Punish panels show it as a bar).
- Fixed SkyrimNet_SexLab's "enable torture sexual parts" Yes only lasting until the next load; it now saves the setting.
- **Renamed to Injury Engine.** The mod now tracks injuries from all damage (combat, spells, creatures, falls and torture), not just whipping. Old saves start fresh: wounds and settings from SkyrimNet_Whipped are not carried over.
- Injury narration now says who hits whom and with what (a lash, a blade, flames, and so on). The mod log records each wound point and the narration or short-lived event text SkyrimNet receives.

- **Continuous torture narration.** Torture is narrated as it starts, every few seconds while it goes on, when the torturer changes method, and when it stops. Everyone torturing at the same time is combined into one narration. Nothing is narrated out of sight; a memory is stored when it ends. New settings: Torture maximum break, Torture direct narration gap.

- Torture now has a clear shape: when it begins the NPC reacts at once (queued speech is cleared), and two seconds after the last hit they get one summary of the whole beating and how the last hit felt ("Nina was whipped several times, the last lash ..."). The torture narration interval setting is gone.
- Fixed a crash when an effect dealt damage while other effects were ending (for example right after an armbinder was removed).
- With SkyrimNet_SexLab, a punishment line is now followed by how the hit feels, in the same message.
- A slap, pinch or lash is no longer followed seconds later by "ordinary healing, nothing to remark on"; light marks still heal, just without that note while the torture is going on.
- Fixed hits aimed at a body part landing somewhere random ("slaps her pussy… her right arm is slapped").
- Falling from a height now leaves fall injuries (sprains, bruises, jarred joints) that heal like other real damage.
- Other mods can add their own injury types. SkyrimNet_Leashed uses this: being tripped by a leash scrapes you up, and being dragged along the ground leaves abrasions; neither can kill.
- Whipping is now one kind of torture: NPCs can also pinch, slap, pull and punch with the new Torture_Target action (it replaces Whip_Target).
- Punching someone outside a fight counts as torture. New spells Flames (torture), Frostbite (torture) and Sparks (torture) cause fire, frost and shock torture; the normal spells are unchanged. "Add whip to player" is now "Add gear to player" and also teaches you the three torture spells.
- Torture can't kill (it never takes health below 10%); each body part counts only its worst torture, and healing heals every kind on that part at once.
- Real damage still replaces torture damage first, shown as the darker bar on the "Torture Damage" overlay.
- Old saves keep their whip wounds.
- Hitting a wound that is still healing adds one step to where it had healed down to, instead of jumping back up to medium.
- Torture narration follows a rhythm: after a pause (setting "Torture narration window", 30 s) the next hit is narrated on its own, hits a few seconds apart ("Torture narration interval", 6 s) are combined into one narration, and quicker hits are only noted. A narration waits for nobody else to be speaking, otherwise it is only noted. The narration cooldown now applies to healing only.

- Craft a padded whip at the forge; every real hit with it lashes the target.
- Each lash wounds a random body part and shows a welt decal on the skin.
- Wounds heal over game time, with healing spells and potions, and by natural regeneration.
- Health stays held down while you are wounded.
- NPCs react: hits and healing are narrated to the LLM, and everyone sees the victim's wounds.
- The new Whip_Target action lets an NPC walk up and whip someone, if it carries the whip.
- A whipping overlay shows a bar per wounded actor and fades out by itself.
- Settings in the SkyrimNet dashboard: narration, strikes, healing speed, Healing Hands, overlay.
- Other mods can whip or heal a chosen body part (SkyrimNet_SexLab Force / Aid).
- Requires SkyrimNet 0.25.0, SKSE, Address Library, PapyrusUtil SE and PrismaUI.
