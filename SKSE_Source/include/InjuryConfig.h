#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace RE
{
    class Actor;
}

// Injury data files: SKSE/Plugins/Injury_Engine/injuries/<group>/<source>.json (schema: injuries/guide.md).
// Every group folder is one body shape (humanoid, insectoid, ...) with its own body.json and text; every *.json in
// the humanoid folder is one injury source ("whipping", "slashing", ...), the same source index in every group.
// Loaded once at kDataLoaded. Lookups (GetBody, Find, Str, ...) read the calling thread's group (GroupScope).
namespace InjuryConfig
{
    // Parts per body: the hit code carries a 16-bit part mask (InjuryEngine::EncodeHit, Papyrus MaskOf).
    constexpr int kMaxParts = 16;
    constexpr int kMaxSources = 32;
    constexpr int kHumanoid = 0;  // group index of "humanoid" (always loaded first)

    // json::dump that never throws: actor names come from the game in its own code page (CP-1252 / CP-1251 custom
    // or localized names are not UTF-8), and the default strict handler throws type_error.316 out of a native.
    inline std::string Dump(const nlohmann::json& j)
    {
        return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    // Body model of one group (injuries/<group>/body.json; humanoid without body.json: the whipping.json body block
    // and sexual_parts.json parts). Parts are ordered core first, then sexual (setting "include sexual parts"), then
    // the lungs (at most one part; only sources whose part_weights name it land there). The defaults are humanoid's.
    struct Body
    {
        int damageMax = 20;      // damage points per body part at 100%
        int medium = 4;          // damage at which a part is "medium"
        int severe = 11;         // ... "severe"
        float woundShare = 0.9f; // torture wounds hold health down by this share of max health
        float floor = 0.1f;      // torture wounds never take health below this share of max
        float bleedoutBelow = 0.15f;  // torture at or below this share of max health holds the actor in bleedout
        bool bleedout = true;    // torture bleedout can happen to this group at all
        int partCount = 12;
        int coreCount = 6;
        int sexualCount = 5;     // parts coreCount .. coreCount + sexualCount - 1
        int lungs = 11;          // index of the lungs part, -1 when the group has none
        std::array<std::string, kMaxParts> partName{ "face", "left arm", "right arm", "back", "left leg", "right leg",
            "nipples", "pussy", "penis", "lips", "ass hole", "lungs" };
        std::array<int, kMaxParts> partRegion{ 0, 2, 2, 1, 3, 3, 4, 5, 6, 7, 8, 9 };
        std::array<int, kMaxParts> partSex{ 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 0 };  // 0 any, 1 female only, 2 male only
        std::vector<std::string> regionName{ "head", "body", "arm", "leg", "nipple", "vulva", "penis", "lips", "anus", "lungs" };
        std::array<int, kMaxParts> overlayOrder{ 0, 3, 2, 1, 5, 4, 6, 7, 8, 9, 10, 11 };
        // Damage points a part holds at 100% (sexual parts are small), the chance weight of being picked,
        // and the damage at which it is "medium" / "severe" (scaled from the common thresholds by partMax).
        // Lungs: weight 0 (only a source's part_weights reach them), no cap (partMax unused).
        std::array<int, kMaxParts> partMax{ 20, 20, 20, 20, 20, 20, 8, 8, 8, 6, 6, 20 };
        std::array<float, kMaxParts> partWeight{ 1, 1, 1, 1, 1, 1, 0.2f, 0.2f, 0.2f, 0.25f, 0.15f, 0 };
        std::array<int, kMaxParts> partMedium{};
        std::array<int, kMaxParts> partSevere{};
        // Real (health model) points a part holds at 100%: one real point is 1% of max health on any part; the
        // stage is points / realMax against the Injury Ranges settings. Torture keeps partMax.
        std::array<int, kMaxParts> realMax{ 100, 100, 100, 100, 100, 100, 10, 10, 10, 5, 5, 100 };
        // Lungs stage: lung damage as a share of max health (body.json "lungs": {"medium", "severe"}).
        float lungMedium = 0.25f;
        float lungSevere = 0.5f;
        // body.json disabled_sources (file ids): a torture source there is ignored on this group (no wound, no
        // health lost); a health source passes through untracked.
        std::vector<std::string> disabledSources;

        bool IsSexual(int part) const { return part >= coreCount && part < coreCount + sexualCount; }
    };

    struct Source
    {
        std::string id;        // unique key (SourceId, co-save); "<base>_real" for the health half of a "both" file
        std::string base;      // the file's id: what callers pass with a torture flag (IndexOf(base, torture))
        std::string model;     // "torture" (damage then healing over game time; "whip" is read as torture)
                               // or "health" (damage = health lost); a "both" file loads as one of each
        std::string requires_; // "whip", "sexlab" or ""
        std::string file;      // file name stem in the group folder ("whipping")
        nlohmann::json json;
        std::vector<std::string> stages;  // stage names, damage stages first
        // Torture: pick weight per part (part_weights by part name, else the body's weight; 0 = never picked),
        // and the share of max health hooked damage (punches, spells) needs for one torture hit.
        std::array<float, kMaxParts> partWeight{};
        float pointsPerSecond = 2.0f;  // hook.points_per_second: spell torture points per real second
        int pointsPerHit = 1;          // hook.points_per_hit: points one melee / lash hit adds to its part

        bool unreachable = false;  // part_weights name only parts this group lacks (drowning without lungs)

        bool IsTorture() const { return model == "torture"; }
    };

    // One entry of a source's "classify" array. The classifier matches these; it does not name sources itself.
    // npc / combat: -1 means "either", 0 false, 1 true. Rules are tried highest priority first.
    struct ClassifyRule
    {
        int source = -1;
        int priority = 0;
        std::vector<std::string> weaponTypes;
        std::string keyword;
        std::string withoutKeyword;
        std::string resist;  // "ResistFire", "ResistFrost", "ResistShock"
        std::string effectFile;
        std::uint32_t effectId = 0;
        bool hasEffect = false;
        std::string ignoreFile;
        std::uint32_t ignoreId = 0;
        bool hasIgnore = false;
        std::string weaponFile;  // one weapon form (another mod's), this source in or out of combat
        std::uint32_t weaponId = 0;
        bool hasWeapon = false;
        bool unarmed = false;
        int npc = -1;
        int combat = -1;
        int bash = -1;  // 1: only a bash hit (bash flag), 0: never a bash hit (bow / crossbow: the arrow), -1: either
        bool falling = false;
        float minDrop = -1.0f;  // falling: minimum drop in units (-1: half of fJumpFallHeightMin)
        bool drowning = false;
        bool fallback = false;
    };

    // Reads every injuries/<group>/*.json and races.json. Safe to call again (reload).
    void Load();

    // ---- groups ---------------------------------------------------------------------------------------
    // The calling thread's group: GetBody, Find, Str, Count, Number and Json read it. Default humanoid.
    int CurrentGroup();
    void SetGroup(int group);  // out-of-range: humanoid
    // Sets the thread's group for a scope and restores the previous one after it (no argument: only restores).
    struct GroupScope
    {
        int previous;
        GroupScope() : previous(CurrentGroup()) {}
        explicit GroupScope(int group) : previous(CurrentGroup()) { SetGroup(group); }
        ~GroupScope() { SetGroup(previous); }
        GroupScope(const GroupScope&) = delete;
        GroupScope& operator=(const GroupScope&) = delete;
    };
    int GroupCount();
    std::string GroupName(int group);
    int GroupIndex(std::string_view name);  // -1 when no such group folder was loaded
    // The actor's group by races.json: actor override, race EditorID, race file + form, race EditorID contains,
    // keywords, default_group; a group with no folder falls back to humanoid. Race results are cached.
    int GroupOf(RE::Actor* actor);
    // The source (by index) is in the current group's disabled_sources.
    bool IsDisabled(int source);
    // Writes every loaded race with its group to the log (verbose logging; call after kDataLoaded).
    void DumpRaces();

    const Body& GetBody();
    int SourceCount();
    const Source* Find(int index);
    // Case-insensitive name match: Papyrus strings come from the case-insensitive BSFixedString pool, so "pussy"
    // can arrive as "Pussy" when another mod interned it first.
    bool IEquals(std::string_view a, std::string_view b);
    int IndexOf(std::string_view id);  // by unique id, -1 when not loaded
    int IndexOf(std::string_view base, bool torture);  // by file id and model (torture / health), -1 when not loaded
    int WhipIndex();                   // the source that carries "overlay" (body, defaults, whip action), -1 when not loaded
    bool IsTorture(int index);         // model "torture"
    int GenericIndex();                // the source whose classify rule is "fallback", -1 when not loaded
    const std::vector<ClassifyRule>& ClassifyRules();  // highest priority first; empty before Load

    // Dotted path into a source's JSON ("narration.hit.lead", "text.looks.head.2"); fallback when absent.
    std::string Str(int source, std::string_view path, std::string_view fallback = "");
    // Dotted path into injuries/session.json (shared scene templates); fallback when absent.
    std::string SessionStr(std::string_view path, std::string_view fallback = "");
    // session.json heal_feed.races: the beast races whose self-heal is feeding (werewolf, werebear); key names the
    // heal_feed line ("heal_feed.<key>"). id is the form id local to file.
    struct FeedRace
    {
        std::string file;
        std::uint32_t id = 0;
        std::string key;
    };
    std::vector<FeedRace> FeedRaces();
    int Count(int source, std::string_view path);  // array / object size, 0 when absent
    float Number(int source, std::string_view path, float fallback);
    nlohmann::json Json(int source, std::string_view path);  // null when absent

    // Replaces "{key}" tokens present in vars; unknown tokens stay as written.
    std::string Render(std::string_view tmpl, const std::map<std::string, std::string>& vars);
}
