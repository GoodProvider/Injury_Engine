#pragma once

#include <string>
#include <string_view>
#include <vector>

// Per-actor, per-body-part, per-source injury tracking. Data and text come from InjuryConfig
// (injuries/<source>.json).
//
// Two models:
//   torture Whipping, pinching, slapping, pulling, punching, fire / frost / shock torture. Each hit damages one
//           part (weighted by the type's part_weights): damage, then healing over game time, tracked per type
//           per part as one deficit (damage + missing healing points). A part counts only its largest type:
//           wounds hold health down by the sum of the part maxima * RatioTorture, never below the floor.
//           Healing a part lowers every type on it by one.
//   health  Real damage (slashing, freezing, ...): the health the attack takes becomes damage points
//           (100% of max health = every part at damage_max), tagged with the classified source.
// Real damage first replaces torture points (the hit does not lower health by that part; the part's maximum
// drops by one and every type above it is clamped down), then takes health as normal, so a tortured actor only
// dies after the torture is all replaced.
// Health above the wounded level is healing: it is converted back into points one at a time on random
// parts (torture and health sources alike). Time is applied lazily on every access (no tick thread).
namespace InjuryEngine
{
    constexpr std::uint32_t kRecord = 'DHEN';
    constexpr std::uint32_t kRecordVersion = 4;  // 4: torture per type; 3: whipping only, 11 parts; 2: 6 parts (both still loaded)

    constexpr int kCoreParts = 6;    // face, left arm, right arm, back, left leg, right leg
    constexpr int kPartCount = 11;   // + nipples, pussy, penis, lips, ass hole (only used with "include sexual parts")

    // Hit levels (the part's state before the hit), used to pick the narration.
    enum HitLevel : int { kUndamaged = 0, kLight = 1, kMedium = 2, kSevere = 3 };

    // SyncHealth events and the healing source attached to them.
    enum SyncKind : int { kHealStart = 0, kPainGone = 1, kFullyHealed = 2, kHealStopped = 3 };
    enum SourceKind : int { kSourceNone = 0, kSourceSpell = 1, kSourceStaff = 2, kSourcePotion = 3, kSourceMagic = 4 };

    // Hit code: fresh * 100000000 + source * 1000000 + partMask * 10 + level
    //   fresh: the victim had no open wounds before; level: worst part stage before the hit (HitLevel).
    constexpr std::int32_t EncodeHit(bool fresh, int source, int mask, int level)
    {
        return (fresh ? 100000000 : 0) + source * 1000000 + mask * 10 + level;
    }

    // Heal codes (TakeHealed): source * 128 + part * 8 + stage for a part that reached a new stage
    // (0 light .. 4 mostly healed, 5 healed); kHealSourceBase + source when a source has healed on every part;
    // kHealPartBase + part when a part has no injury left from any source.
    // Plus kHealPassive when no healing source contributed (game time, natural regen): narrated silently.
    constexpr std::int32_t kHealSourceBase = 10000;
    constexpr std::int32_t kHealPartBase = 20000;
    constexpr std::int32_t kHealPassive = 100000;

    // True while the engine itself is changing health: the damage hooks must ignore it.
    bool IsInternalDamage();

    // Torture hit on a random part (source model "torture"). Takes the matching health (never below the floor).
    // Returns a hit code, or -1 for None/dead actors and unknown / non-torture sources.
    std::int32_t Hit(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId);
    // Hit on one part (PartIndex); -1, a part outside the active set, or one the actor's sex lacks: random.
    std::int32_t HitPart(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId, int part);
    // Scripted real damage: `points` damage points of the health source with file id `base` (a "health" file, or the
    // health half of a "both" file), applied like a hooked hit (the part is picked as for an attack; the hit is
    // queued for narration). Returns the source index, -1 for None/dead actors or no such health source.
    std::int32_t RealHit(RE::Actor* actor, RE::Actor* attacker, std::string_view base, int points);

    // A caller's heal (spell, potion): `health` becomes torture points healed on `part` first (-1: random parts),
    // then any part (each point lowers every type on its part); health rises by the points healed.
    // Returns points healed, 0 when nothing was open, -1 bad args.
    std::int32_t HealPart(RE::Actor* actor, int part, float health);

    // Part index by part name ("left arm") or region ("head" -> face); -1 when unknown or "".
    int PartIndex(std::string_view name);

    // Damage taken (health points, positive) from the damage hooks. `source` is the classified source index;
    // physicalMelee: weapon / fist / unarmed-body hits from HandleHealthDamage (one torture hit per call); false
    // for spell ticks and active torture effects (accumulate per hook.hit_share). Returns the damage the game
    // should still apply: torture sources none (they become torture hits), health sources what torture wounds
    // do not absorb.
    // unarmedForm: the hit matched an "unarmed" classify rule, so narration uses narration.form_unarmed when that key exists.
    float OnDamage(RE::Actor* victim, RE::Actor* attacker, float amount, int source, bool physicalMelee, bool unarmedForm = false);

    // True if the actor has any damage left (any source).
    bool HasOpenWounds(RE::Actor* actor);

    // Applies time to every tracked actor; returns those with parts that healed to a new stage since the last TakeHealed.
    std::vector<RE::Actor*> TakeHealedActors();
    std::vector<std::int32_t> TakeHealed(RE::Actor* actor);

    // Real hits not yet narrated: hit codes; HitVictims / HitAttackers are parallel to the last TakeHits result.
    std::vector<std::int32_t> TakeHits();
    std::vector<RE::Actor*> HitVictims();
    std::vector<RE::Actor*> HitAttackers();
    // Parallel to the last TakeHits result (same index).
    bool HitUnarmed(std::size_t index);

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

    // Torture overlay snapshot (no time applied), each part from its largest torture type:
    // {"title","colors":{...},"actors":[{"id","name","replaced","parts":[[damage,healing] x active parts in overlay order]}]}
    std::string OverlayJson();

    // Narration text rendered from the JSON files.
    std::string HitText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int mask, int level, int count,
        bool noCause = false, bool unarmedForm = false);
    // The feeling sentence of a hit level (narration.hit.levels), rendered for the victim.
    std::string LevelText(std::string_view sourceId, RE::Actor* victim, int level);
    // Heal codes all passive, or a source-heal event without a source: wrapped in narration.heal.passive.
    std::string HealText(RE::Actor* victim, const std::vector<std::int32_t>& codes);
    std::string SourceHealText(int kind, int healingSource, int percent, RE::Actor* caster, RE::Actor* target);
    // Renders the string at `path` with {speaker}, {target} and {times} (words for count).
    // Combined torture narration for the pairs (parallel lists) of one phase: start, again, switch or end.
    // Pairs are grouped by victim; same method / mixed methods and one or many attackers are worded from session.json.
    std::string SceneText(std::string_view phase, const std::vector<RE::Actor*>& attackers, const std::vector<RE::Actor*>& victims,
        const std::vector<std::string>& sources, const std::vector<int>& masks, const std::vector<int>& levels);
    std::string ActionText(std::string_view sourceId, std::string_view path, RE::Actor* speaker, RE::Actor* target, int count);

    // Setting "include sexual parts": new hits and prompts use the 5 extra small parts; existing wounds on them
    // keep healing while it is off.
    void SetIncludeSexualParts(bool enabled);
    void SetHoursPerPoint(float hours);
    // Percent of natural regeneration that heals torture wounds (the rest of the regen is taken back).
    void SetRegenRate(float percent);
    void Forget(RE::Actor* actor);

    // Writes an info line to SkyrimNet_Whipped.log: "<kind>: <text>" (empty text is ignored).
    void LogLine(std::string_view kind, std::string_view text);

    void Save(SKSE::SerializationInterface* intfc);
    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
    void Revert();
}
