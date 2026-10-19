#pragma once

// Classifies incoming damage into an injury source (InjuryConfig index) by the "classify" rules in
// injuries/*.json. Weapon types, keywords, spell resist, "(torture)" effect forms, unarmed hits, landing
// damage and the generic fallback are data. A source with no matching file is not a special case here:
// a rule that names a missing piece simply does not load.
namespace Classifier
{
    // Registers the TESHitEvent and TESCombatEvent sinks (kDataLoaded).
    void Init();

    // Records a "(torture)" effect, or a real health-damage spell effect and its caster, landing on `victim`
    // (MagicTarget::AddTarget hook). Damage hooks must not walk the active-effect list; Classify reads what this
    // recorded.
    void NoteEffect(RE::Actor* victim, RE::Effect* effect, RE::TESObjectREFR* caster);

    // Torture source of a "(torture)" MGEF, or of the first such MGEF on a spell (so the spell's other effects,
    // e.g. the perk copies 0x822–0x825, count as torture too); -1 = not torture.
    int TortureSourceOfEffect(const RE::EffectSetting* mgef);
    int TortureSourceOfSpell(const RE::MagicItem* item);

    // Source index for damage to `victim` from `attacker` (may be null); -1 = ignore (a Padded Whip hit,
    // or nothing to classify into). When `physicalMelee` is non-null, set true for weapon / fist / unarmed-body
    // hits (one torture wound per HandleHealthDamage call); false for spell ticks and active torture effects.
    // When `unarmedForm` is non-null, set true when the matching rule is an unarmed rule (fists, not a mace).
    // When `uncaused` is non-null, set true for generic damage with no hit event, no attacker and no real fall
    // (scripted damage from other mods): the hooks hold it with InjuryEngine::NoteUncaused.
    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee = nullptr, bool* unarmedForm = nullptr,
        bool* uncaused = nullptr);

    // The melee hit that connected (ProcessHitData): classified from the hit's own weapon (null = fists) only, so a
    // live "(torture)" spell effect or a sword in the other hand can't replace a punch or a whip swing. -1 = the
    // Padded Whip, -2 = no rule.
    int ClassifyMelee(RE::Actor* victim, RE::Actor* attacker, const RE::TESObjectWEAP* weapon, bool* unarmedForm = nullptr);

    // Under-water state of an actor (drowning classification and the always-on drowning probe in Hooks.cpp).
    struct DrownSignals
    {
        bool underwater = false;  // Actor::BOOL_FLAGS::kUnderwater
        bool inWater = false;     // BOOL_BITS::kInWater
        bool swimming = false;    // BOOL_BITS::kSwimming
        float timer = 0.0f;       // underWaterTimer
        float submerged = 0.0f;   // GetSubmergedLevel: 0 dry .. 1 under
        float waterBreathing = 0.0f;

        // Any sign of water at all (the probe logs only then).
        bool Wet() const { return underwater || inWater || swimming || timer > 0.0f || submerged >= 0.5f; }
        std::string Text() const;
    };
    DrownSignals ReadDrownSignals(RE::Actor* actor);

    // Puts the short-lived QuietMarker effect (SPEL 0x81A) on a torture victim so the QuietDialogue quest's silent
    // Hit / Assault / AssaultNC lines win over vanilla barks. Throttled per victim; the cast runs as an SKSE task
    // because damage hooks sit inside the engine's effect update. Padded Whip hits get the marker from their ENCH.
    void MarkQuiet(RE::Actor* victim);

    // A max-health buff changed on `actor` (a Recover-flagged Health effect such as Fortify Health ending, or the
    // temporary Health modifier dropping). The health it takes is not a wound: the hooks pass uncaused damage
    // within `seconds` of such a change through untouched, and held uncaused damage is dropped.
    void NoteBuffChange(RE::Actor* actor);
    bool RecentBuffChange(RE::Actor* actor, double seconds);

    // Torture is not aggression: when `victim` enters combat against `attacker` within a few seconds of a torture
    // hit by them (either order), its combat is stopped. The hit event sink covers Padded Whip lashes and
    // "(torture)" spells; the damage hook calls this for every torture-classified hit (punches).
    void NoteCalm(RE::Actor* victim, RE::Actor* attacker);

    // Start_Fighting: drops the torture calm and the truce guard between `a` and `b` (both directions, and a pending
    // combat start) and releases the friendly-hits flag on both, so a fight the LLM starts right after is not stopped.
    void ForgetCalm(RE::Actor* a, RE::Actor* b);

    // Stop_Fighting: the calm's stop for any actor (StopCombat + StopCombatAndAlarmOnActor, and the actor is no longer
    // angry with the player), as an SKSE task.
    void StopFighting(RE::Actor* actor);

    // Accepted truce / surrender: until game time `untilGameDays` a combat start between `a` and `b` (either way) is
    // stopped right away (not saved; Papyrus notes active truces again on load).
    void NoteTruce(RE::Actor* a, RE::Actor* b, float untilGameDays);

    // Load / new game: drops every truce guard (Papyrus notes the save's active truces again).
    void ResetTruces();

    // Load / new game: drops the truce guards and every session cache (hit events, torture and spell effects, calm
    // windows, quiet casts) and releases every kIgnoreFriendlyHits flag this set. Main thread.
    void ResetSession();

    // Start_Fighting: drops every truce guard `actor` holds (with anyone), matching the Papyrus truce keys it clears.
    void ForgetTruces(RE::Actor* actor);

    // A downed actor stood up: an actor under an active truce stands down again (a StopCombat during bleedout may not
    // last).
    void OnStoodUp(RE::Actor* actor);

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
