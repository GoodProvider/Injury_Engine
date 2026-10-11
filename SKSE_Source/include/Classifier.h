#pragma once

// Classifies incoming damage into an injury source (InjuryConfig index): weapon type for physical hits
// (slashing, piercing, bludgeoning), magic effect resist for spells (burning, freezing, shocking),
// natural attacks of creatures (mauling), anything else "generic". Torture: unarmed NPC hits out of combat
// (punching); the Flames / Frostbite / Sparks (torture) spells of SkyrimNet_Whipped.esp give fire / frost /
// shock (vanilla spells are always real damage). Landing damage (no hit, no attacker, the character controller
// still falling) is the health half of falling.json. Sources without an injuries/<name>.json fall back to generic.
namespace Classifier
{
    // Registers the TESHitEvent sink (kDataLoaded).
    void Init();

    // Records a "(torture)" effect landing on `victim` (MagicTarget::AddTarget hook). Damage hooks must not walk
    // the active-effect list; Classify reads what this recorded.
    void NoteEffect(RE::Actor* victim, RE::Effect* effect);

    // Source index for damage to `victim` from `attacker` (may be null); -1 = ignore (a Padded Whip hit,
    // or nothing to classify into). When `physicalMelee` is non-null, set true for weapon / fist / unarmed-body
    // hits (one torture wound per HandleHealthDamage call); false for spell ticks and active torture effects.
    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee = nullptr);
}
