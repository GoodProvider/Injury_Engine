#pragma once

#include <string>
#include <string_view>
#include <vector>

// Per-actor, per-body-part, per-source injury tracking. Data and text come from InjuryConfig
// (injuries/<source>.json).
//
// Two models:
//   whip    Each hit damages one random part (damage, then healing over game time). Wounds hold health down:
//           the deficit (damage + missing healing points over hit parts) * RatioWhip, never below the floor.
//   health  Real damage (slashing, freezing, ...): the health the attack takes becomes damage points
//           (100% of max health = every part at damage_max), tagged with the classified source.
// Real damage first replaces whip points (the hit does not lower health by that part), then takes health
// as normal, so an actor with whip wounds only dies after the whip wounds are all replaced.
// Health above the wounded level is healing: it is converted back into points one at a time on random
// parts (whip and health sources alike). Time is applied lazily on every access (no tick thread).
namespace InjuryEngine
{
    constexpr std::uint32_t kRecord = 'DHEN';
    constexpr std::uint32_t kRecordVersion = 2;

    constexpr int kPartCount = 6;

    // Hit levels (the part's state before the hit), used to pick the narration.
    enum HitLevel : int { kUndamaged = 0, kLight = 1, kMedium = 2, kSevere = 3 };

    // SyncHealth events and the healing source attached to them.
    enum SyncKind : int { kHealStart = 0, kPainGone = 1, kFullyHealed = 2, kHealStopped = 3 };
    enum SourceKind : int { kSourceNone = 0, kSourceSpell = 1, kSourceStaff = 2, kSourcePotion = 3, kSourceMagic = 4 };

    // Hit code: fresh * 1000000 + source * 10000 + partMask * 10 + level
    //   fresh: the victim had no open wounds before; level: worst part stage before the hit (HitLevel).
    constexpr std::int32_t EncodeHit(bool fresh, int source, int mask, int level)
    {
        return (fresh ? 1000000 : 0) + source * 10000 + mask * 10 + level;
    }

    // Heal codes (TakeHealed): source * 64 + part * 8 + stage for a part that reached a new stage
    // (0 light .. 4 mostly healed, 5 healed); kHealSourceBase + source when a source has healed on every part;
    // kHealPartBase + part when a part has no injury left from any source.
    constexpr std::int32_t kHealSourceBase = 10000;
    constexpr std::int32_t kHealPartBase = 20000;

    // True while the engine itself is changing health: the damage hooks must ignore it.
    bool IsInternalDamage();

    // Whip hit on a random part (source model "whip"). Takes the matching health (never below the floor).
    // Returns a hit code, or -1 for None/dead actors and unknown / non-whip sources.
    std::int32_t Hit(RE::Actor* actor, std::string_view sourceId);

    // Real damage taken (health points, positive) from the damage hooks. `source` is the classified source
    // index. Returns the damage the game should still apply (whip wounds absorb their share).
    float OnDamage(RE::Actor* victim, RE::Actor* attacker, float amount, int source);

    // True if the actor has any damage left (any source).
    bool HasOpenWounds(RE::Actor* actor);

    // Applies time to every tracked actor; returns those with parts that healed to a new stage since the last TakeHealed.
    std::vector<RE::Actor*> TakeHealedActors();
    std::vector<std::int32_t> TakeHealed(RE::Actor* actor);

    // Real hits not yet narrated: hit codes; HitVictims / HitAttackers are parallel to the last TakeHits result.
    std::vector<std::int32_t> TakeHits();
    std::vector<RE::Actor*> HitVictims();
    std::vector<RE::Actor*> HitAttackers();

    // Called from the MagicTarget::AddTarget hook for every effect applied to an actor.
    void NoteHealingSource(RE::Actor* target, RE::TESObjectREFR* caster, RE::MagicItem* item, RE::Effect* effect);

    // Converts healing on loaded tracked actors into points and returns the queued events as
    // kind * 10000 + source * 1000 + percent. SyncActors/SyncCasters are parallel to the last result.
    std::vector<std::int32_t> SyncHealth();
    std::vector<RE::Actor*> SyncActors();
    std::vector<RE::Actor*> SyncCasters();

    // Tracked actors plus events not yet taken.
    int TrackedCount();

    std::string PartName(int part);
    std::string SourceId(int source);
    int SourceCount();

    // Decorator data, text resolved from the JSON files for this actor:
    // {"sources":[{"id","looks_heading","feels_intro","feels_outro","parts":[{"part","region","stage","look","feel"}]}]}
    std::string StateJson(RE::Actor* actor);

    // Whip-only overlay snapshot (no time applied):
    // {"title","colors":{...},"actors":[{"id","name","replaced","parts":[[damage,healing] x6 in overlay order]}]}
    std::string OverlayJson();

    // Narration text rendered from the JSON files.
    std::string HitText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int mask, int level, int count);
    std::string HealText(RE::Actor* victim, const std::vector<std::int32_t>& codes);
    std::string SourceHealText(int kind, int healingSource, int percent, RE::Actor* caster, RE::Actor* target);
    // Renders the string at `path` with {speaker}, {target} and {times} (words for count).
    std::string ActionText(std::string_view sourceId, std::string_view path, RE::Actor* speaker, RE::Actor* target, int count);

    void SetHoursPerPoint(float hours);
    // Percent of natural regeneration that heals whip wounds (the rest of the regen is taken back).
    void SetRegenRate(float percent);
    void Forget(RE::Actor* actor);

    void Save(SKSE::SerializationInterface* intfc);
    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
    void Revert();
}
