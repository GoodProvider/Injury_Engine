#pragma once

#include <string>
#include <string_view>
#include <vector>

// Per-actor, per-body-part, per-source injury tracking. Data and text come from InjuryConfig
// (injuries/<source>.json).
//
// Two models:
//   torture Whipping, pinching, slapping, pulling, punching, fire / frost / shock torture. Each hit damages one
//           part (weighted by the type's part_weights): torture points per type per part (0 .. part max) go
//           up with damage and down with healing (game time, healing sources). A part counts only its largest
//           type: its points give the torture stage (light / medium / severe) and, inverted, the heal level
//           (high / mid / low). Wounds hold health down by the sum of the part maxima * RatioTorture, never
//           below the floor. Healing a part lowers every type on it by one.
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
    // 7: real points on their own scale (1 point = 1% of max health; 6 and older are rescaled from the old
    // 1 / capacity points); 6: 12 parts (lungs); 5: torture points per type, 11 parts; 4: per-type deficit (healing_max + damage);
    // 3: whipping only, 11 parts; 2: 6 parts (5 loads as is, 4, 3 and 2 keep the damage only)
    constexpr std::uint32_t kRecordVersion = 7;

    constexpr int kCoreParts = 6;    // face, left arm, right arm, back, left leg, right leg
    constexpr int kPartCount = 12;   // + nipples, pussy, penis, lips, ass hole (only used with "include sexual parts"), lungs
    constexpr int kPartLungs = 11;   // drowning only: no cap, outside the health capacity, stage by share of max health

    // Hit levels (the part's state before the hit), used to pick the narration.
    enum HitLevel : int { kUndamaged = 0, kLight = 1, kMedium = 2, kSevere = 3 };

    // SyncHealth events and the healing source attached to them.
    enum SyncKind : int { kHealStart = 0, kPainGone = 1, kFullyHealed = 2, kHealStopped = 3 };
    enum SourceKind : int { kSourceNone = 0, kSourceSpell = 1, kSourceStaff = 2, kSourcePotion = 3, kSourceMagic = 4, kSourceFood = 5 };

    // Hit code: fresh * 100000000 + source * 1000000 + partMask * 10 + level
    //   fresh: the victim had no open wounds before; level: worst part stage before the hit (HitLevel).
    constexpr std::int32_t EncodeHit(bool fresh, int source, int mask, int level)
    {
        return (fresh ? 100000000 : 0) + source * 1000000 + mask * 10 + level;
    }

    // True while the engine itself is changing health: the damage hooks must ignore it.
    bool IsInternalDamage();

    // Gives back health the game already took (HandleHealthDamage reports damage after it is applied), as internal
    // damage so the hooks ignore it. No-op on a dead actor or a non-positive amount.
    void RestoreHealth(RE::Actor* actor, float amount);

    // Uncaused damage (Classify's `uncaused`: no attacker, no hit event, no real fall) the game has already applied,
    // health now `healthAfter`. Held about 1.5 s; SyncHealth lands what did not come back when it reaches
    // SetUncausedMinPercent of max health, else drops it.
    void NoteUncaused(RE::Actor* victim, float amount, int source, float healthAfter);

    // Torture hit on a random part (source model "torture"). Takes the matching health (never below the floor).
    // Returns a hit code, or -1 for None/dead actors and unknown / non-torture sources.
    std::int32_t Hit(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId);
    // Hit on one part (PartIndex); -1, a part outside the active set, or one the actor's sex lacks: random.
    std::int32_t HitPart(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId, int part);
    // Scripted real damage: `points` real points (1% of max health each) of the health source with file id `base` (a "health" file, or the
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
    // for spell ticks and active torture effects (land hook.points_per_second points per second). Returns the damage the game
    // should still apply: torture sources none (they become torture hits), health sources what torture wounds
    // do not absorb.
    // unarmedForm: the hit matched an "unarmed" classify rule, so narration uses narration.form_unarmed when that key exists.
    // applied: the game has already taken `amount` (HandleHealthDamage, held uncaused damage); false when it is taken
    // after this call (ModActorValue, RealHit). Real points are capped by the health missing after the hit.
    float OnDamage(RE::Actor* victim, RE::Actor* attacker, float amount, int source, bool physicalMelee, bool unarmedForm = false,
        bool applied = true);

    // True if the actor has any damage left (any source).
    bool HasOpenWounds(RE::Actor* actor);

    // A healing source (spell, staff, potion) is working on the actor right now: its heal session is open.
    // Opens with kHealStart; a torture or real hit, heal stopped or fully healed closes it.
    bool IsHealing(RE::Actor* actor);

    // Real hits not yet narrated: hit codes; HitVictims / HitAttackers are parallel to the last TakeHits result.
    // Hits of one victim, attacker and source merge while queued and leave at most once per second (a health hit also releases its victim's held hits).
    std::vector<std::int32_t> TakeHits();
    std::vector<RE::Actor*> HitVictims();
    std::vector<RE::Actor*> HitAttackers();
    // Parallel to the last TakeHits result (same index).
    bool HitUnarmed(std::size_t index);
    // Parallel to the last TakeHits result: hits merged into that entry (1 when alone).
    int HitCount(std::size_t index);
    // Queues a hit code Papyrus already got from Hit (scripted torture), so it is merged and gated like hooked hits.
    void QueueHit(RE::Actor* victim, RE::Actor* attacker, std::int32_t code);

    // TESDeathEvent sink (kDataLoaded): a death within a few seconds of the victim's last real hit queues that
    // source's DeathText. TakeDeaths returns the texts; DeathVictims / DeathAttackers / DeathSources are parallel to it.
    void InitDeaths();
    // kDataLoaded: resolves the SexLab / OStim scene factions; an actor in one is never held in torture bleedout.
    void InitSceneFactions();
    std::vector<std::string> TakeDeaths();
    std::vector<RE::Actor*> DeathVictims();
    std::vector<RE::Actor*> DeathAttackers();
    std::vector<std::string> DeathSources();

    // Called from the MagicTarget::AddTarget hook for every effect applied to an actor.
    void NoteHealingSource(RE::Actor* target, RE::TESObjectREFR* caster, RE::MagicItem* item, RE::Effect* effect);

    // Pending sync events (heal start / pain gone / ...) not yet taken; does not run health conversion.
    // kind * 10000 + source * 1000 + percent. SyncActors/SyncCasters are parallel to the last result.
    std::vector<std::int32_t> TakeSyncEvents();

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
    // Torture parts: stage light / medium / severe and text.feels.torture, or during a heal session high / mid / low
    // and text.feels.heal.
    std::string StateJson(RE::Actor* actor);

    // Torture overlay snapshot (no time applied). Each bar shows the actor's latest torture type ("source") alone:
    // its points per part, out of "max" = the part max of the parts that type reaches.
    // {"title","max","part_max","medium","severe","colors":{...},"source_colors":{"<id>":{light,medium,severe}},
    //  "actors":[{"id","name","source","max","replaced","healing","parts":[points x active parts in overlay order]}]}
    std::string OverlayJson();

    // Torture points per part (time applied): each part the actor's sex has, points = the part's torture (largest type),
    // out of its part max; total = torture points over all parts, out of max = the active parts' capacity.
    // {"total","max","parts":[{"name","points","max"}]}
    std::string PartPointsJson(RE::Actor* actor);

    // Narration text rendered from the JSON files.
    std::string HitText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int mask, int level, int count,
        bool noCause = false, bool unarmedForm = false);
    // Killing-blow line (death.lead / lead_none + a random death.regions.<region> line + maybe death.soil) for a health
    // source; part -1 = random main part, a sexual part uses its nearest main region. "" when the file has no death text.
    std::string DeathText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int part);
    // The feeling sentence of a hit level (narration.hit.levels), rendered for the victim.
    std::string LevelText(std::string_view sourceId, RE::Actor* victim, int level);
    // Heal session line: every part with torture points, at its heal level (high / mid / low) from its largest type,
    // grouped by (type, level) into narration.heal.level.<level> sentences. "" when nothing is left.
    std::string HealMapText(RE::Actor* victim);
    std::string SourceHealText(int kind, int healingSource, int percent, RE::Actor* caster, RE::Actor* target);
    // One line for several targets healed by the same cast (heal_group in session.json); empty for food.
    std::string SourceHealGroupText(int healingSource, RE::Actor* caster, const std::vector<RE::Actor*>& targets);
    // Form id of the spell / staff / potion of the actor's last heal start (0 none): heal starts group only on the same item.
    RE::FormID HealItem(RE::Actor* actor);
    // Renders the string at `path` with {speaker}, {target} and {times} (words for count).
    // Combined torture narration for the pairs (parallel lists) of one phase: start, again, or switch.
    // Pairs are grouped by victim; same method / mixed methods and one or many attackers are worded from session.json.
    std::string SceneText(std::string_view phase, const std::vector<RE::Actor*>& attackers, const std::vector<RE::Actor*>& victims,
        const std::vector<std::string>& sources, const std::vector<int>& masks, const std::vector<int>& levels);
    std::string ActionText(std::string_view sourceId, std::string_view path, RE::Actor* speaker, RE::Actor* target, int count);

    // Setting "include sexual parts": new hits and prompts use the 5 extra small parts; existing wounds on them
    // keep healing while it is off.
    void SetIncludeSexualParts(bool enabled);
    // Setting injury_engine.body.torture_bleedout: torture at or below body.bleedout_below of max health holds the
    // actor in the bleedout idle until health is above it again; off lets every downed actor get up.
    void SetTortureBleedout(bool enabled);
    void SetHoursPerPoint(float hours);
    // Percent of natural regeneration that heals torture wounds (the rest of the regen is taken back).
    void SetRegenRate(float percent);
    // Setting injury_engine.body.uncaused_min_percent (default 5; 0 = any uncaused damage that sticks counts).
    void SetUncausedMinPercent(float percent);
    // Settings injury_engine.ranges.*: the percent of a part's real max where a real wound becomes minor / major /
    // severe (below minor: no stage, no narration). Clamped to 1..100 and minor < major < severe.
    void SetInjuryRanges(int minor, int major, int severe);
    void Forget(RE::Actor* actor);

    // Writes a narration line to Injury_Engine.log: "[hh:mm:ss] <category>:<narrationType> <text>"
    // (category torture / heal / health; narrationType direct / event / short; empty text is ignored).
    void LogLine(std::string_view category, std::string_view narrationType, std::string_view sessionType, std::string_view text);

    void Save(SKSE::SerializationInterface* intfc);
    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
    void Revert();
}
