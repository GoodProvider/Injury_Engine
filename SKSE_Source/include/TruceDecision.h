#pragma once

#include <vector>

// Stop_Fighting / Stop_Fighting_by_Yielding: asks SkyrimNet's decision model (template injury_engine/accept_truce,
// or injury_engine/accept_surrender for a yield) whether the target accepts the speaker's offer to stop fighting
// or its surrender. The answer is queued for Papyrus and announced with mod event
// "Injury_Engine_Truce"; Injury_Engine_Actions drains it (TakeTruces) and applies the truce. When no decision
// can be asked (no SkyrimNet API v12+, no decision provider, a request already in flight for the speaker, a
// failed request), the answer is "refused" right away. When the target is the player, a Yes / No message box asks
// the player instead ("<speaker> wishes to stop fighting / yield to you. Do you accept?"; Esc = No).
namespace TruceDecision
{
    struct Answer
    {
        RE::FormID speaker = 0;
        RE::FormID target = 0;
        bool accepted = false;
        bool yielded = false;  // a surrender (Stop_Fighting_by_Yielding), not a truce offer
    };

    // kDataLoaded: resolves the SkyrimNet public API.
    void Init();
    // New game / load: drops pending requests and the no-provider latch.
    void Reset();
    // Main thread (Papyrus native). One request per speaker in flight. yield: a surrender, not a truce offer.
    void Ask(RE::Actor* speaker, RE::Actor* target, bool yield);
    // Answers since the last call, oldest first.
    std::vector<Answer> Take();
}
