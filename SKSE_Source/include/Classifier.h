#pragma once

// Classifies incoming damage into an injury source (InjuryConfig index) by the "classify" rules in
// injuries/*.json. Weapon types, keywords, spell resist, "(torture)" effect forms, unarmed hits, landing
// damage and the generic fallback are data. A source with no matching file is not a special case here:
// a rule that names a missing piece simply does not load.
namespace Classifier
{
    // Registers the TESHitEvent and TESCombatEvent sinks (kDataLoaded).
    void Init();

    // Records a "(torture)" effect landing on `victim` (MagicTarget::AddTarget hook). Damage hooks must not walk
    // the active-effect list; Classify reads what this recorded.
    void NoteEffect(RE::Actor* victim, RE::Effect* effect);

    // Torture source of a "(torture)" MGEF, or of the first such MGEF on a spell (so the spell's other effects,
    // e.g. the perk copies 0x822–0x825, count as torture too); -1 = not torture.
    int TortureSourceOfEffect(const RE::EffectSetting* mgef);
    int TortureSourceOfSpell(const RE::MagicItem* item);

    // Source index for damage to `victim` from `attacker` (may be null); -1 = ignore (a Padded Whip hit,
    // or nothing to classify into). When `physicalMelee` is non-null, set true for weapon / fist / unarmed-body
    // hits (one torture wound per HandleHealthDamage call); false for spell ticks and active torture effects.
    // When `unarmedForm` is non-null, set true when the matching rule is an unarmed rule (fists, not a mace).
    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee = nullptr, bool* unarmedForm = nullptr);

    // Puts the short-lived QuietMarker effect (SPEL 0x81A) on a torture victim so the QuietDialogue quest's silent
    // Hit / Assault / AssaultNC lines win over vanilla barks. Throttled per victim; the cast runs as an SKSE task
    // because damage hooks sit inside the engine's effect update. Padded Whip hits get the marker from their ENCH.
    void MarkQuiet(RE::Actor* victim);

    // Torture is not aggression: when `victim` enters combat against `attacker` within a few seconds of a torture
    // hit by them (either order), its combat is stopped. The hit event sink covers Padded Whip lashes and
    // "(torture)" spells; the damage hook calls this for every torture-classified hit (punches).
    void NoteCalm(RE::Actor* victim, RE::Actor* attacker);

    // Start_Fighting: drops the torture calm between `a` and `b` (both directions, and a pending combat start) and
    // releases the friendly-hits flag on both, so a fight the LLM starts right after a torture is not stopped.
    void ForgetCalm(RE::Actor* a, RE::Actor* b);

    // Stop_Fighting: the calm's stop for any actor (StopCombat + StopCombatAndAlarmOnActor), as an SKSE task.
    void StopFighting(RE::Actor* actor);

    // True when `attacker` holds the Padded Whip (a classify rule's ignore weapon) in either hand.
    bool IsWhipAttacker(RE::Actor* attacker);
    // Padded Whip, or bare fists whose unarmed rule is a torture source (punching out of combat).
    bool IsTortureAttacker(RE::Actor* attacker);

    // Setting injury_engine.body.punch_combat_health: true = a torture unarmed rule limited to out of combat
    // (punching) does not match during combat (punches fall to bludgeoning); false (default) = it matches anyway.
    void SetPunchCombatHealth(bool enabled);

    // A whip lash is a friendly hit, not an attack (GetFactionFightReaction hook): `victim` gets the ref flag
    // kIgnoreFriendlyHits, so the lashes neither count as friend hits nor start combat. Only a flag this set is
    // ever cleared; it is kept while lashes keep coming and released kCalmWindow after the last one.
    void SuppressFriendHits(RE::Actor* victim);

    // Clears the flags SuppressFriendHits set: those past kCalmWindow (an SKSE task; called from the TakeHits
    // native), or all of them right now (`all`, main thread: before a save, so the flag is never written).
    void ReleaseFriendHits(bool all);
}
