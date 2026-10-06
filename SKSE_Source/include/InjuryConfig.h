#pragma once

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

// Injury data files: SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json (schema: injuries/guide.md).
// Every *.json is one injury source ("whipping", "slashing", ...). Loaded once at kDataLoaded.
namespace InjuryConfig
{
    constexpr int kPartCount = 6;  // face, left arm, right arm, back, left leg, right leg
    constexpr int kMaxSources = 16;

    // Body model shared by every source (read from the first loaded file, whipping first).
    struct Body
    {
        int damageMax = 20;      // damage points per body part at 100%
        int healingMax = 11;     // whip only: healing points at "healed"
        int medium = 4;          // damage at which a part is "medium"
        int severe = 11;         // ... "severe"
        int mostlyHealed = 4;    // whip only: healing at "mostly healed"
        float woundShare = 0.9f; // whip wounds hold health down by this share of max health
        float floor = 0.1f;      // whip wounds never take health below this share of max
        std::array<std::string, kPartCount> partName{ "face", "left arm", "right arm", "back", "left leg", "right leg" };
        std::array<int, kPartCount> partRegion{ 0, 2, 2, 1, 3, 3 };
        std::vector<std::string> regionName{ "head", "body", "arm", "leg" };
        std::array<int, kPartCount> overlayOrder{ 0, 3, 2, 1, 5, 4 };
    };

    struct Source
    {
        std::string id;
        std::string model;     // "whip" (damage then healing over game time) or "health" (damage = health lost)
        std::string requires_; // "whip", "sexlab" or ""
        nlohmann::json json;
        std::vector<std::string> stages;  // stage names, damage stages first
    };

    // Reads every injuries/*.json. Safe to call again (reload).
    void Load();

    const Body& GetBody();
    int SourceCount();
    const Source* Find(int index);
    int IndexOf(std::string_view id);  // -1 when not loaded
    int WhipIndex();                   // first source with model "whip", -1 when none
    int GenericIndex();                // "generic", -1 when not loaded

    // Dotted path into a source's JSON ("narration.hit.lead", "text.looks.head.2"); fallback when absent.
    std::string Str(int source, std::string_view path, std::string_view fallback = "");
    int Count(int source, std::string_view path);  // array / object size, 0 when absent
    float Number(int source, std::string_view path, float fallback);
    nlohmann::json Json(int source, std::string_view path);  // null when absent

    // Replaces "{key}" tokens present in vars; unknown tokens stay as written.
    std::string Render(std::string_view tmpl, const std::map<std::string, std::string>& vars);
}
