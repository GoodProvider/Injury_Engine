https://github.com/GoodProvider/Injury_Engine/releases/tag/0.1.0

First release. Requires SkyrimNet 0.25.0 (Beta 25) or later, SKSE, Address Library, PapyrusUtil SE and PrismaUI.

- **Injuries from all damage.** Swords, arrows, spells, creatures, falls and torture each wound a body part, which moves from light to medium to severe and heals over time.
- **Torture that can't kill.** Whipping, pinching, slapping, pulling, punching and fire / frost / shock torture spells hold health down but never kill; a victim worn down far enough collapses until healed.
- **Padded whip and torture spells.** Craft the whip or get it, with Flames / Frostbite / Sparks (torture), from "Add gear to player". Torture starts no fight.
- **Real damage replaces torture first.** A tortured actor only dies once its torture wounds are replaced by real ones.
- **Healing.** Game time, regeneration, and healing spells, staffs and potions; a heal session describes how far each wound has healed.
- **Narration.** Torture, healing, real hits and killing blows are narrated to the LLM, combined into one line per scene, and each narration waits for SkyrimNet's reply to the last one.
- **Prompts.** Everyone sees what the wounds look like; the victim knows how they feel.
- **LLM actions.** `Torture_Target`, `Stop_Fighting`, `Stop_Fighting_by_Yielding` and `Start_Fighting` as SkyrimNet YAML actions; offers made to the player show a Yes / No box.
- **Overlay.** A PrismaUI "Torture Damage" bar per tortured actor that fades out by itself.
- **Extensible.** Other mods can add injury types as JSON and use the External API (SkyrimNet_SexLab, SkyrimNet_Leashed).
