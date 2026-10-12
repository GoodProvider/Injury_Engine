#pragma once

// Classifies incoming damage into an injury source (InjuryConfig index) by the "classify" rules in
// injuries/*.json. Weapon types, keywords, spell resist, "(torture)" effect forms, unarmed hits, landing
// damage and the generic fallback are data. A source with no matching file is not a special case here:
// a rule that names a missing piece simply does not load.
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
    // When `unarmedForm` is non-null, set true when the matching rule is an unarmed rule (fists, not a mace).
    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee = nullptr, bool* unarmedForm = nullptr);

    // Puts the short-lived QuietMarker effect (SPEL 0x81A) on a torture victim so the QuietDialogue quest's silent
    // Hit / Assault / AssaultNC lines win over vanilla barks. Throttled per victim; the cast runs as an SKSE task
    // because damage hooks sit inside the engine's effect update. Padded Whip hits get the marker from their ENCH.
    void MarkQuiet(RE::Actor* victim);
}
