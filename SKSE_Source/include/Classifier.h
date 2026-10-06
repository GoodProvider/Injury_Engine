#pragma once

// Classifies incoming damage into an injury source (InjuryConfig index): weapon type for physical hits
// (slashing, piercing, bludgeoning), magic effect resist for spells (burning, freezing, shocking),
// natural attacks of creatures (mauling), anything else "generic". Sources without an
// injuries/<name>.json fall back to generic.
namespace Classifier
{
    // Registers the TESHitEvent sink (kDataLoaded).
    void Init();

    // Source index for damage to `victim` from `attacker` (may be null); -1 = ignore (a Padded Whip hit,
    // or nothing to classify into).
    int Classify(RE::Actor* victim, RE::Actor* attacker);
}
