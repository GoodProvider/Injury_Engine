#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// Injury data files: SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json (schema: injuries/guide.md).
// Every *.json is one injury source ("whipping", "slashing", ...). Loaded once at kDataLoaded.
namespace InjuryConfig
{
    constexpr int kCoreParts = 6;   // face, left arm, right arm, back, left leg, right leg
    constexpr int kPartCount = 11;  // + nipples, pussy, penis, lips, ass hole (setting "include sexual parts")
    constexpr int kMaxSources = 32;

    // Body model shared by every source (read from the first loaded file, whipping first).
    struct Body
    {
        int damageMax = 20;      // damage points per body part at 100%
        int healingMax = 11;     // torture only: healing points at "healed"
        int medium = 4;          // damage at which a part is "medium"
        int severe = 11;         // ... "severe"
        int mostlyHealed = 4;    // torture only: healing at "mostly healed"
        float woundShare = 0.9f; // torture wounds hold health down by this share of max health
        float floor = 0.1f;      // torture wounds never take health below this share of max
        std::array<std::string, kPartCount> partName{ "face", "left arm", "right arm", "back", "left leg", "right leg",
            "nipples", "pussy", "penis", "lips", "ass hole" };
        std::array<int, kPartCount> partRegion{ 0, 2, 2, 1, 3, 3, 4, 5, 6, 7, 8 };
        std::vector<std::string> regionName{ "head", "body", "arm", "leg", "nipple", "vulva", "penis", "lips", "anus" };
        std::array<int, kPartCount> overlayOrder{ 0, 3, 2, 1, 5, 4, 6, 7, 8, 9, 10 };
        // Damage points a part holds at 100% (sexual parts are small), the chance weight of being picked,
        // and the damage at which it is "medium" / "severe" (scaled from the common thresholds by partMax).
        std::array<int, kPartCount> partMax{ 20, 20, 20, 20, 20, 20, 8, 8, 8, 6, 6 };
        std::array<float, kPartCount> partWeight{ 1, 1, 1, 1, 1, 1, 0.2f, 0.2f, 0.2f, 0.25f, 0.15f };
        std::array<int, kPartCount> partMedium{};
        std::array<int, kPartCount> partSevere{};
    };

    struct Source
    {
        std::string id;        // unique key (SourceId, co-save); "<base>_real" for the health half of a "both" file
        std::string base;      // the file's id: what callers pass with a torture flag (IndexOf(base, torture))
        std::string model;     // "torture" (damage then healing over game time; "whip" is read as torture)
                               // or "health" (damage = health lost); a "both" file loads as one of each
        std::string requires_; // "whip", "sexlab" or ""
        nlohmann::json json;
        std::vector<std::string> stages;  // stage names, damage stages first
        // Torture: pick weight per part (part_weights by part name, else the body's weight; 0 = never picked),
        // and the share of max health hooked damage (punches, spells) needs for one torture hit.
        std::array<float, kPartCount> partWeight{};
        float pointsPerSecond = 2.0f;  // hook.points_per_second: spell torture points per real second
        int pointsPerHit = 1;          // hook.points_per_hit: points one melee / lash hit adds to its part

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
        bool unarmed = false;
        int npc = -1;
        int combat = -1;
        bool falling = false;
        bool fallback = false;
    };

    // Reads every injuries/*.json. Safe to call again (reload).
    void Load();

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
    int Count(int source, std::string_view path);  // array / object size, 0 when absent
    float Number(int source, std::string_view path, float fallback);
    nlohmann::json Json(int source, std::string_view path);  // null when absent

    // Replaces "{key}" tokens present in vars; unknown tokens stay as written.
    std::string Render(std::string_view tmpl, const std::map<std::string, std::string>& vars);
}
