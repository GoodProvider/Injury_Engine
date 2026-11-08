#include "InjuryConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include <windows.h>

namespace InjuryConfig
{
    namespace
    {
        std::mutex g_lock;
        // One body shape: its body and its copy of every source (same indices in every group; json = the humanoid
        // file with the group's own file merged over it). [0] is humanoid.
        struct Group
        {
            std::string id;
            Body body;
            std::vector<Source> sources;
        };
        std::vector<Group> g_groups(1, Group{ "humanoid", Body{}, {} });
        thread_local int t_group = kHumanoid;
        nlohmann::json g_session;  // injuries/session.json: shared templates of the combined torture narration
        std::vector<ClassifyRule> g_rules;

        Group& Cur()
        {
            return g_groups[t_group >= 0 && t_group < static_cast<int>(g_groups.size()) ? t_group : kHumanoid];
        }

        Group& Human()
        {
            return g_groups[kHumanoid];
        }

        std::uint32_t FormIdOf(const nlohmann::json& rule, const char* key)
        {
            const auto it = rule.find(key);
            if (it == rule.end() || it->is_null()) {
                return 0;
            }
            if (it->is_number_integer() || it->is_number_unsigned()) {
                return it->get<std::uint32_t>();
            }
            if (it->is_string()) {
                return static_cast<std::uint32_t>(std::strtoul(it->get<std::string>().c_str(), nullptr, 0));
            }
            return 0;
        }

        // -1 when the key is absent ("don't care"). A present value that is not a bool (`"npc": 1`) is an error: the
        // rule is skipped rather than silently matching everyone.
        int TriBool(const nlohmann::json& rule, const char* key)
        {
            const auto it = rule.find(key);
            if (it == rule.end()) {
                return -1;
            }
            if (!it->is_boolean()) {
                throw std::runtime_error(std::string("\"") + key + "\" must be true or false");
            }
            return it->get<bool>() ? 1 : 0;
        }

        void ReadClassify(int sourceIndex)
        {
            const auto it = Human().sources[sourceIndex].json.find("classify");
            if (it == Human().sources[sourceIndex].json.end() || !it->is_array()) {
                return;
            }
            for (const auto& rule : *it) {
                if (!rule.is_object()) {
                    SKSE::log::warn("injuries: {} classify: an entry is not an object, ignored", Human().sources[sourceIndex].id);
                    continue;
                }
                // A field of the wrong type (nlohmann throws type_error) skips this rule, not the whole load.
                try {
                    ClassifyRule r;
                    r.source = sourceIndex;
                    r.priority = rule.value("priority", 0);
                    if (const auto types = rule.find("weapon_types"); types != rule.end() && types->is_array()) {
                        for (const auto& name : *types) {
                            if (name.is_string()) {
                                r.weaponTypes.push_back(name.get<std::string>());
                            }
                        }
                    }
                    r.keyword = rule.value("keyword", "");
                    r.withoutKeyword = rule.value("without_keyword", "");
                    r.resist = rule.value("resist", "");
                    r.effectFile = rule.value("effect_file", "");
                    r.effectId = FormIdOf(rule, "effect_id");
                    r.hasEffect = r.effectId != 0 && !r.effectFile.empty();
                    r.ignoreFile = rule.value("ignore_weapon_file", "");
                    r.ignoreId = FormIdOf(rule, "ignore_weapon_id");
                    r.hasIgnore = r.ignoreId != 0 && !r.ignoreFile.empty();
                    r.weaponFile = rule.value("weapon_file", "");
                    r.weaponId = FormIdOf(rule, "weapon_id");
                    r.hasWeapon = r.weaponId != 0 && !r.weaponFile.empty();
                    r.unarmed = rule.value("unarmed", false);
                    r.npc = TriBool(rule, "npc");
                    r.combat = TriBool(rule, "combat");
                    r.falling = rule.value("falling", false);
                    r.minDrop = rule.value("min_drop", -1.0f);
                    r.drowning = rule.value("drowning", false);
                    r.fallback = rule.value("fallback", false);
                    g_rules.push_back(std::move(r));
                } catch (const std::exception& e) {
                    SKSE::log::error("injuries: {} classify rule skipped: {}", Human().sources[sourceIndex].id, e.what());
                }
            }
        }

        std::filesystem::path DllDir()
        {
            HMODULE module = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&DllDir), &module)) {
                wchar_t buffer[MAX_PATH]{};
                if (GetModuleFileNameW(module, buffer, MAX_PATH) > 0) {
                    return std::filesystem::path(buffer).parent_path();
                }
            }
            return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins";
        }

        std::filesystem::path InjuryDir()
        {
            auto dir = DllDir() / "Injury_Engine" / "injuries";
            if (!std::filesystem::exists(dir)) {
                auto fallback = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "Injury_Engine" / "injuries";
                if (std::filesystem::exists(fallback)) {
                    return fallback;
                }
            }
            return dir;
        }

        const nlohmann::json* Walk(const nlohmann::json& root, std::string_view path)
        {
            const nlohmann::json* node = &root;
            while (!path.empty()) {
                const auto dot = path.find('.');
                const std::string key(path.substr(0, dot));
                path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
                if (node->is_object()) {
                    const auto it = node->find(key);
                    if (it == node->end()) {
                        return nullptr;
                    }
                    node = &*it;
                } else if (node->is_array()) {
                    std::size_t index = 0;
                    try {
                        index = static_cast<std::size_t>(std::stoul(key));
                    } catch (...) {
                        return nullptr;
                    }
                    if (index >= node->size()) {
                        return nullptr;
                    }
                    node = &(*node)[index];
                } else {
                    return nullptr;
                }
            }
            return node;
        }

        int RegionIndex(Body& b, const std::string& region)
        {
            auto it = std::ranges::find(b.regionName, region);
            if (it == b.regionName.end()) {
                b.regionName.push_back(region);
                it = b.regionName.end() - 1;
            }
            return static_cast<int>(it - b.regionName.begin());
        }

        // Humanoid without body.json: the body block of the first loaded file (whipping) and lungs at index 11.
        void ReadLegacyBody(Body& b, const nlohmann::json& j)
        {
            b = Body{};
            b.damageMax = j.value("damage_max", b.damageMax);
            if (const auto t = j.find("thresholds"); t != j.end() && t->is_object()) {
                b.medium = t->value("medium", b.medium);
                b.severe = t->value("severe", b.severe);
            }
            if (const auto h = j.find("health_link"); h != j.end() && h->is_object()) {
                b.woundShare = h->value("wound_share", b.woundShare);
                b.floor = h->value("floor", b.floor);
                b.bleedoutBelow = h->value("bleedout_below", b.bleedoutBelow);
            }
            if (const auto r = j.find("regions"); r != j.end() && r->is_array() && !r->empty()) {
                b.regionName.clear();
                for (const auto& name : *r) {
                    b.regionName.push_back(name.get<std::string>());
                }
            }
            if (const auto p = j.find("parts"); p != j.end() && p->is_array() && p->size() == static_cast<std::size_t>(b.coreCount)) {
                for (int i = 0; i < b.coreCount; ++i) {
                    b.partName[i] = (*p)[i].value("name", b.partName[i]);
                    const std::string region = (*p)[i].value("region", "");
                    const auto it = std::ranges::find(b.regionName, region);
                    if (it != b.regionName.end()) {
                        b.partRegion[i] = static_cast<int>(it - b.regionName.begin());
                    }
                }
            }
            if (const auto o = j.find("overlay"); o != j.end() && o->is_object()) {
                if (const auto order = o->find("order"); order != o->end() && order->is_array() &&
                                                           order->size() == static_cast<std::size_t>(b.coreCount)) {
                    for (int i = 0; i < b.coreCount; ++i) {
                        const int part = (*order)[i].is_number_integer() ? (*order)[i].get<int>() : -1;
                        if (part < 0 || part >= b.coreCount) {
                            SKSE::log::warn("injuries: overlay.order[{}] is not a part index 0..{}; default kept", i, b.coreCount - 1);
                            continue;
                        }
                        b.overlayOrder[i] = part;
                    }
                }
            }
            b.partRegion[b.lungs] = RegionIndex(b, "lungs");
            for (int i = 0; i < b.partCount; ++i) {
                b.partMax[i] = b.damageMax;
            }
            const int realMax = std::max(1, j.value("real_max", 100));
            for (int i = 0; i < b.coreCount; ++i) {
                b.realMax[i] = realMax;
            }
        }

        // Humanoid without body.json: lungs stage from the first source with a top-level "lungs" block.
        void ReadLegacyLungs(Group& g)
        {
            for (const auto& s : g.sources) {
                const auto l = s.json.find("lungs");
                if (l == s.json.end() || !l->is_object()) {
                    continue;
                }
                g.body.lungMedium = std::clamp(l->value("medium", g.body.lungMedium), 0.01f, 1.0f);
                g.body.lungSevere = std::clamp(l->value("severe", g.body.lungSevere), g.body.lungMedium, 1.0f);
                return;
            }
        }

        // Humanoid without body.json: sexual_parts.json "parts" (5 entries) define parts 6..10.
        void ReadLegacySexualParts(Body& b, const nlohmann::json& j)
        {
            const auto p = j.find("parts");
            if (p == j.end() || !p->is_array() || p->size() != static_cast<std::size_t>(b.sexualCount)) {
                return;
            }
            for (int i = b.coreCount; i < b.coreCount + b.sexualCount; ++i) {
                const auto& e = (*p)[i - b.coreCount];
                b.partName[i] = e.value("name", b.partName[i]);
                b.partMax[i] = std::max(3, e.value("max", b.partMax[i]));
                b.realMax[i] = std::max(1, e.value("real_max", b.realMax[i]));
                b.partWeight[i] = std::max(0.01f, e.value("weight", b.partWeight[i]));
                const std::string region = e.contains("region") ? e.value("region", std::string{})
                                           : b.partRegion[i] < static_cast<int>(b.regionName.size()) ? b.regionName[b.partRegion[i]]
                                                                                                     : std::string("body");
                b.partRegion[i] = RegionIndex(b, region);
            }
        }

        // injuries/<group>/body.json (schema: guide.md "Groups"). False: the group is not loaded.
        bool ReadBodyJson(Body& b, const nlohmann::json& j, const std::string& group)
        {
            b = Body{};
            b.damageMax = std::max(1, j.value("damage_max", b.damageMax));
            if (const auto t = j.find("thresholds"); t != j.end() && t->is_object()) {
                b.medium = t->value("medium", b.medium);
                b.severe = t->value("severe", b.severe);
            }
            if (const auto h = j.find("health_link"); h != j.end() && h->is_object()) {
                b.woundShare = h->value("wound_share", b.woundShare);
                b.floor = h->value("floor", b.floor);
                b.bleedoutBelow = h->value("bleedout_below", b.bleedoutBelow);
            }
            b.bleedout = j.value("bleedout", true);
            if (const auto l = j.find("lungs"); l != j.end() && l->is_object()) {
                b.lungMedium = std::clamp(l->value("medium", b.lungMedium), 0.01f, 1.0f);
                b.lungSevere = std::clamp(l->value("severe", b.lungSevere), b.lungMedium, 1.0f);
            }
            const int realMax = std::max(1, j.value("real_max", 100));
            b.regionName.clear();
            if (const auto r = j.find("regions"); r != j.end() && r->is_array()) {
                for (const auto& name : *r) {
                    if (name.is_string()) {
                        b.regionName.push_back(name.get<std::string>());
                    }
                }
            }
            struct Def
            {
                std::string name, region;
                int max, realMax, sex, kind;  // kind 0 core, 1 sexual, 2 lungs
                float weight;
            };
            std::vector<Def> defs;
            const auto p = j.find("parts");
            if (p == j.end() || !p->is_array() || p->empty()) {
                SKSE::log::error("injuries: group {} body.json has no parts: group not loaded", group);
                return false;
            }
            for (const auto& e : *p) {
                if (!e.is_object() || !e.contains("name")) {
                    SKSE::log::error("injuries: group {} body.json: a part has no name: group not loaded", group);
                    return false;
                }
                Def d;
                d.name = e.value("name", std::string{});
                d.kind = e.value("lungs", false) ? 2 : e.value("sexual", false) ? 1 : 0;
                d.region = e.value("region", d.kind == 2 ? std::string("lungs") : std::string("body"));
                d.max = std::max(3, e.value("max", b.damageMax));
                d.realMax = std::max(1, e.value("real_max", d.kind == 1 ? 10 : realMax));
                d.weight = std::max(0.0f, e.value("weight", d.kind == 2 ? 0.0f : d.kind == 1 ? 0.2f : 1.0f));
                const std::string sex = e.value("sex", std::string{});
                d.sex = sex == "female" ? 1 : sex == "male" ? 2 : 0;
                defs.push_back(std::move(d));
            }
            if (defs.size() > static_cast<std::size_t>(kMaxParts)) {
                SKSE::log::error("injuries: group {} body.json has {} parts, at most {}: group not loaded", group, defs.size(), kMaxParts);
                return false;
            }
            if (std::ranges::count_if(defs, [](const Def& d) { return d.kind == 2; }) > 1) {
                SKSE::log::error("injuries: group {} body.json has more than one lungs part: group not loaded", group);
                return false;
            }
            std::ranges::stable_sort(defs, {}, &Def::kind);  // core, sexual, lungs
            b.partCount = static_cast<int>(defs.size());
            b.coreCount = static_cast<int>(std::ranges::count_if(defs, [](const Def& d) { return d.kind == 0; }));
            b.sexualCount = static_cast<int>(std::ranges::count_if(defs, [](const Def& d) { return d.kind == 1; }));
            b.lungs = -1;
            if (b.coreCount == 0) {
                SKSE::log::error("injuries: group {} body.json has no core part: group not loaded", group);
                return false;
            }
            b.partName.fill({});
            b.partRegion.fill(0);
            b.partSex.fill(0);
            b.partMax.fill(b.damageMax);
            b.partWeight.fill(0.0f);
            b.realMax.fill(realMax);
            for (int i = 0; i < b.partCount; ++i) {
                const Def& d = defs[i];
                b.partName[i] = d.name;
                b.partRegion[i] = RegionIndex(b, d.region);
                b.partSex[i] = d.sex;
                b.partMax[i] = d.max;
                b.realMax[i] = d.realMax;
                b.partWeight[i] = d.weight;
                if (d.kind == 2) {
                    b.lungs = i;
                }
            }
            // overlay_order: part names, top to bottom; parts not named follow in body order
            std::vector<int> order;
            if (const auto o = j.find("overlay_order"); o != j.end() && o->is_array()) {
                for (const auto& name : *o) {
                    if (!name.is_string()) {
                        continue;
                    }
                    for (int i = 0; i < b.partCount; ++i) {
                        if (IEquals(b.partName[i], name.get<std::string>()) && std::ranges::find(order, i) == order.end()) {
                            order.push_back(i);
                        }
                    }
                }
            }
            for (int i = 0; i < b.partCount; ++i) {
                if (std::ranges::find(order, i) == order.end()) {
                    order.push_back(i);
                }
            }
            // the overlay shows the active parts (core, then sexual) as its first entries
            std::ranges::stable_sort(order, {}, [&](int i) { return i < b.coreCount ? 0 : b.IsSexual(i) ? 1 : 2; });
            b.overlayOrder.fill(0);
            for (int i = 0; i < b.partCount; ++i) {
                b.overlayOrder[i] = order[i];
            }
            if (const auto d = j.find("disabled_sources"); d != j.end() && d->is_array()) {
                for (const auto& id : *d) {
                    if (id.is_string()) {
                        b.disabledSources.push_back(id.get<std::string>());
                    }
                }
            }
            return true;
        }

        // sexual_parts.json "text": {"<source id>": {"looks": {region: [..]}, "feels": {...}}} merged into each source
        // of the group (objects merge, region arrays replace; a "both" file's health half falls back to the file id).
        void MergeSexualText(Group& g, const nlohmann::json& j)
        {
            const auto t = j.find("text");
            if (t == j.end() || !t->is_object()) {
                return;
            }
            for (auto& s : g.sources) {
                auto st = t->find(s.id);
                if (st == t->end()) {
                    st = t->find(s.base);
                }
                if (st == t->end() || !st->is_object()) {
                    continue;
                }
                for (const char* kind : { "looks", "feels" }) {
                    const auto regions = st->find(kind);
                    if (regions == st->end() || !regions->is_object()) {
                        continue;
                    }
                    s.json["text"][kind].merge_patch(*regions);
                }
            }
        }

        // Sexual parts are small: medium / severe scale down with partMax (never below 2 / medium + 1), so every
        // part still covers light, medium and severe.
        void FinishBody(Body& b)
        {
            for (int i = 0; i < b.partCount; ++i) {
                if (b.partMax[i] == b.damageMax) {
                    b.partMedium[i] = b.medium;
                    b.partSevere[i] = b.severe;
                    continue;
                }
                const float scale = static_cast<float>(b.partMax[i]) / static_cast<float>(b.damageMax);
                b.partMedium[i] = std::max(2, static_cast<int>(std::lround(b.medium * scale)));
                b.partSevere[i] = std::max(b.partMedium[i] + 1, static_cast<int>(std::lround(b.severe * scale)));
                b.partSevere[i] = std::min(b.partSevere[i], b.partMax[i]);
            }
        }

        // part_weights {"part name": weight} over the body's weights (0 = never picked); hook.points_per_second,
        // hook.points_per_hit. A source whose part_weights give weight only to parts this group lacks (drowning: the
        // lungs) reaches nothing here. Runs after the body is final.
        void FinishSources(Group& g)
        {
            const Body& b = g.body;
            for (auto& s : g.sources) {
                s.partWeight = b.partWeight;
                s.unreachable = false;
                try {
                    s.pointsPerSecond = std::max(0.1f, s.json.value("/hook/points_per_second"_json_pointer, 2.0f));
                    s.pointsPerHit = std::clamp(s.json.value("/hook/points_per_hit"_json_pointer, 1), 1, 10);
                    const auto w = s.json.find("part_weights");
                    if (w == s.json.end() || !w->is_object()) {
                        continue;
                    }
                    bool missingWeighted = false;
                    bool anyWeighted = false;
                    for (const auto& [name, value] : w->items()) {
                        if (!value.is_number()) {
                            continue;
                        }
                        int index = -1;
                        for (int i = 0; i < b.partCount; ++i) {
                            if (IEquals(b.partName[i], name)) {
                                index = i;
                                break;
                            }
                        }
                        if (index < 0) {
                            missingWeighted = missingWeighted || value.get<float>() > 0.0f;
                            if (g.id == "humanoid") {
                                SKSE::log::warn("injuries: {} part_weights: unknown part \"{}\" ignored", s.id, name);
                            }
                            continue;
                        }
                        s.partWeight[index] = std::max(0.0f, value.get<float>());
                        anyWeighted = anyWeighted || s.partWeight[index] > 0.0f;
                    }
                    if (missingWeighted && !anyWeighted && !s.IsTorture()) {
                        s.partWeight.fill(0.0f);  // SourceReach falls back to the body; OnDamage checks Reaches
                        s.unreachable = true;
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("injuries: {} {} hook / part_weights skipped: {}", g.id, s.id, e.what());
                }
            }
        }

        // Every part must name a region that exists.
        void CheckRegions(Body& b, const std::string& group)
        {
            if (b.regionName.empty()) {
                b.regionName.push_back("body");
            }
            const auto body = std::ranges::find(b.regionName, std::string("body"));
            const int fallback = body != b.regionName.end() ? static_cast<int>(body - b.regionName.begin()) : 0;
            for (int i = 0; i < b.partCount; ++i) {
                if (b.partRegion[i] < 0 || b.partRegion[i] >= static_cast<int>(b.regionName.size())) {
                    SKSE::log::warn("injuries: {} part {} has no region (index {}); using \"{}\"", group, b.partName[i], b.partRegion[i],
                        b.regionName[fallback]);
                    b.partRegion[i] = fallback;
                }
            }
        }

        nlohmann::json ReadJson(const std::filesystem::path& file)
        {
            std::ifstream in(file);
            if (!in) {
                return nullptr;
            }
            return nlohmann::json::parse(in, nullptr, true, true);
        }

        // The humanoid sources: every *.json in `dir` except body / sexual_parts / session. Returns false when none.
        bool LoadHumanoidSources(const std::filesystem::path& dir, nlohmann::json& firstJson)
        {
            auto& sources = Human().sources;
            std::error_code ec;
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                const auto stem = entry.path().stem().string();
                if (entry.is_regular_file() && entry.path().extension() == ".json" && stem != "sexual_parts" && stem != "session" &&
                    stem != "body" && stem != "common") {
                    files.push_back(entry.path());
                }
            }
            if (ec || files.empty()) {
                SKSE::log::error("injuries: no *.json found in {}", dir.string());
                return false;
            }
            // The file that carries "overlay" loads first (its body block wins without body.json), then alphabetical.
            const auto hasOverlay = [](const std::filesystem::path& path) {
                std::ifstream in(path);
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                return text.find("\"overlay\"") != std::string::npos;
            };
            std::ranges::sort(files, [&](const auto& a, const auto& b) {
                const bool aw = hasOverlay(a);
                const bool bw = hasOverlay(b);
                return aw != bw ? aw : a.filename() < b.filename();
            });
            for (const auto& file : files) {
                if (static_cast<int>(sources.size()) >= kMaxSources) {
                    SKSE::log::warn("injuries: more than {} sources, ignoring {}", kMaxSources, file.filename().string());
                    break;
                }
                try {
                    const auto j = ReadJson(file);
                    const std::string base = j.value("id", file.stem().string());
                    std::string model = j.value("model", "health");
                    if (model == "whip") {
                        model = "torture";  // before torture types, whipping was the only "whip" source
                    }
                    // "both": one torture and one health source from the shared fields, each with its own block
                    // ("torture" / "health") merged over them. The health half's id is "<id>_real".
                    std::vector<Source> loaded;
                    if (model == "both") {
                        if (static_cast<int>(sources.size()) + 2 > kMaxSources) {
                            SKSE::log::warn("injuries: more than {} sources, ignoring {}", kMaxSources, file.filename().string());
                            break;
                        }
                        nlohmann::json shared = j;
                        shared.erase("torture");
                        shared.erase("health");
                        for (const char* half : { "torture", "health" }) {
                            Source s;
                            s.json = shared;
                            if (const auto block = j.find(half); block != j.end() && block->is_object()) {
                                s.json.merge_patch(*block);
                            }
                            s.model = half;
                            s.id = s.model == "torture" ? base : base + "_real";
                            s.json["id"] = s.id;
                            s.json["model"] = s.model;
                            s.json["_both"] = true;
                            loaded.push_back(std::move(s));
                        }
                    } else {
                        Source s;
                        s.json = j;
                        s.model = model;
                        s.id = base;
                        loaded.push_back(std::move(s));
                    }
                    for (auto& s : loaded) {
                        s.base = base;
                        s.file = file.stem().string();
                        s.requires_ = s.json.value("requires", "");
                        if (const auto st = s.json.find("stages"); st != s.json.end() && st->is_array()) {
                            for (const auto& name : *st) {
                                s.stages.push_back(name.get<std::string>());
                            }
                        }
                        if (s.stages.size() < 3) {
                            throw std::runtime_error("\"stages\" needs at least light, medium, severe");
                        }
                    }
                    if (firstJson.is_null()) {
                        firstJson = loaded.front().json;
                    }
                    for (auto& s : loaded) {
                        SKSE::log::info("injuries: loaded {} ({}, model {})", file.filename().string(), s.id, s.model);
                        sources.push_back(std::move(s));
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("injuries: {} skipped: {}", file.filename().string(), e.what());
                }
            }
            return !sources.empty();
        }

        bool Disabled(const Body& b, const Source& s)
        {
            return std::ranges::any_of(b.disabledSources, [&](const std::string& id) { return IEquals(id, s.base) || IEquals(id, s.id); });
        }

        // Another group: body.json, then every humanoid source with the group's own file merged over it.
        void LoadGroup(const std::filesystem::path& dir, const nlohmann::json& humanSexual)
        {
            Group g;
            g.id = dir.filename().string();
            try {
                const auto body = ReadJson(dir / "body.json");
                if (body.is_null()) {
                    SKSE::log::error("injuries: group {} has no body.json: not loaded (its races use humanoid)", g.id);
                    return;
                }
                if (!ReadBodyJson(g.body, body, g.id)) {
                    return;
                }
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: group {} body.json: {}: not loaded", g.id, e.what());
                return;
            }
            // common.json: the group's text for every source without its own file (shared keys, then the block of the
            // source's model, "torture" or "health"); sources in its "skip" list keep humanoid text (drowning: lungs are lungs)
            nlohmann::json common;
            try {
                common = ReadJson(dir / "common.json");
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: group {} common.json skipped: {}", g.id, e.what());
            }
            int own = 0;
            std::vector<std::string> missing;
            for (const auto& h : Human().sources) {
                Source s = h;
                const bool off = Disabled(g.body, s);
                if (common.is_object()) {
                    const auto skip = common.find("skip");
                    const bool skipped = skip != common.end() && skip->is_array() &&
                                         std::ranges::any_of(*skip, [&](const nlohmann::json& id) {
                                             return id.is_string() && (IEquals(id.get<std::string>(), s.base) || IEquals(id.get<std::string>(), s.id));
                                         });
                    if (!skipped) {
                        nlohmann::json shared = common;
                        shared.erase("torture");
                        shared.erase("health");
                        shared.erase("skip");
                        shared.erase("_note");
                        s.json.merge_patch(shared);
                        if (const auto block = common.find(s.model); block != common.end() && block->is_object()) {
                            s.json.merge_patch(*block);
                        }
                    }
                }
                try {
                    const auto patch = s.file == "common" || s.file == "body" ? nlohmann::json() : ReadJson(dir / (s.file + ".json"));
                    if (patch.is_object()) {
                        nlohmann::json shared = patch;
                        if (s.json.value("_both", false)) {
                            shared.erase("torture");
                            shared.erase("health");
                        }
                        s.json.merge_patch(shared);
                        if (s.json.value("_both", false)) {
                            if (const auto block = patch.find(s.model); block != patch.end() && block->is_object()) {
                                s.json.merge_patch(*block);
                            }
                        }
                        s.json["id"] = s.id;
                        s.json["model"] = s.model;
                        if (s.base == h.base && s.id == h.id) {
                            ++own;
                        }
                    } else if (!off && !common.is_object() && std::ranges::find(missing, s.file) == missing.end()) {
                        missing.push_back(s.file);
                    }
                } catch (const std::exception& e) {
                    SKSE::log::error("injuries: group {} {}.json skipped (humanoid text used): {}", g.id, s.file, e.what());
                }
                g.sources.push_back(std::move(s));
            }
            try {
                const auto sexual = ReadJson(dir / "sexual_parts.json");
                if (sexual.is_object()) {
                    MergeSexualText(g, sexual);
                } else if (g.body.sexualCount > 0) {
                    SKSE::log::warn("injuries: group {} has no sexual_parts.json: its sexual parts use humanoid text", g.id);
                }
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: group {} sexual_parts.json skipped: {}", g.id, e.what());
            }
            (void)humanSexual;
            FinishBody(g.body);
            FinishSources(g);
            CheckRegions(g.body, g.id);
            for (const auto& file : missing) {
                SKSE::log::warn("injuries: group {} has no {}.json: humanoid text used", g.id, file);
            }
            SKSE::log::info("injuries: group {} loaded ({} parts: {} core, {} sexual, lungs {}; {} own files; disabled {})", g.id,
                g.body.partCount, g.body.coreCount, g.body.sexualCount, g.body.lungs >= 0 ? "yes" : "no", own,
                g.body.disabledSources.size());
            g_groups.push_back(std::move(g));
        }

        // ---- races.json --------------------------------------------------------------------------------
        struct FormRule
        {
            std::string file;
            std::uint32_t id = 0;
            std::string group;
        };
        struct ContainsRule
        {
            std::string text;
            std::string group;
        };
        struct KeywordRule
        {
            std::vector<std::string> all;
            std::string group;
        };
        struct RaceRules
        {
            std::string defaultGroup = "humanoid";
            std::vector<FormRule> actors;
            std::vector<std::pair<std::string, std::string>> races;  // EditorID -> group
            std::vector<FormRule> raceForms;
            std::vector<ContainsRule> contains;
            std::vector<KeywordRule> keywords;
        };
        RaceRules g_races;
        std::mutex g_raceLock;
        std::unordered_map<RE::FormID, int> g_raceCache;
        std::unordered_map<RE::FormID, int> g_actorGroups;  // resolved races.json "actors"
        bool g_actorsResolved = false;
        std::vector<std::string> g_warnedGroups;

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        void LoadRaces(const std::filesystem::path& file)
        {
            g_races = RaceRules{};
            try {
                const auto j = ReadJson(file);
                if (j.is_null()) {
                    SKSE::log::warn("injuries: races.json missing ({}): every actor uses humanoid", file.string());
                    return;
                }
                g_races.defaultGroup = j.value("default_group", std::string("humanoid"));
                const auto forms = [](const nlohmann::json& list, std::vector<FormRule>& out) {
                    if (!list.is_array()) {
                        return;
                    }
                    for (const auto& e : list) {
                        if (!e.is_object()) {
                            continue;
                        }
                        FormRule r{ e.value("file", std::string{}), FormIdOf(e, "id"), e.value("group", std::string{}) };
                        if (!r.file.empty() && r.id != 0 && !r.group.empty()) {
                            out.push_back(std::move(r));
                        }
                    }
                };
                forms(j.value("actors", nlohmann::json()), g_races.actors);
                forms(j.value("race_forms", nlohmann::json()), g_races.raceForms);
                if (const auto r = j.find("races"); r != j.end() && r->is_object()) {
                    for (const auto& [group, list] : r->items()) {
                        if (group.starts_with("_") || !list.is_array()) {
                            continue;
                        }
                        for (const auto& edid : list) {
                            if (edid.is_string()) {
                                g_races.races.emplace_back(Lower(edid.get<std::string>()), group);
                            }
                        }
                    }
                }
                if (const auto c = j.find("race_contains"); c != j.end() && c->is_array()) {
                    for (const auto& e : *c) {
                        if (e.is_object() && e.contains("text") && e.contains("group")) {
                            g_races.contains.push_back({ Lower(e.value("text", std::string{})), e.value("group", std::string{}) });
                        }
                    }
                }
                if (const auto k = j.find("keywords"); k != j.end() && k->is_array()) {
                    for (const auto& e : *k) {
                        if (!e.is_object()) {
                            continue;
                        }
                        KeywordRule r;
                        r.group = e.value("group", std::string{});
                        if (const auto all = e.find("all"); all != e.end() && all->is_array()) {
                            for (const auto& kw : *all) {
                                if (kw.is_string()) {
                                    r.all.push_back(kw.get<std::string>());
                                }
                            }
                        }
                        if (!r.group.empty() && !r.all.empty()) {
                            g_races.keywords.push_back(std::move(r));
                        }
                    }
                }
                SKSE::log::info("injuries: races.json: {} races, {} race forms, {} contains rules, {} keyword rules, {} actors, default {}",
                    g_races.races.size(), g_races.raceForms.size(), g_races.contains.size(), g_races.keywords.size(), g_races.actors.size(),
                    g_races.defaultGroup);
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: races.json skipped: {}", e.what());
            }
        }

        // Group name -> loaded index; a group with no folder is humanoid (warned once). Caller holds g_raceLock.
        int ResolveGroup(const std::string& name)
        {
            for (int i = 0; i < static_cast<int>(g_groups.size()); ++i) {
                if (IEquals(g_groups[i].id, name)) {
                    return i;
                }
            }
            if (std::ranges::find(g_warnedGroups, name) == g_warnedGroups.end()) {
                g_warnedGroups.push_back(name);
                SKSE::log::warn("injuries: group {} is not loaded (no injuries/{}/ folder): humanoid used", name, name);
            }
            return kHumanoid;
        }

        // The races.json group name of a race and the rule that picked it. Caller holds g_raceLock.
        std::string RaceGroupName(RE::TESRace* race, std::string& rule)
        {
            const char* edidRaw = race->GetFormEditorID();
            const std::string edid = Lower(edidRaw ? edidRaw : "");
            if (!edid.empty()) {
                for (const auto& [name, group] : g_races.races) {
                    if (name == edid) {
                        rule = "race";
                        return group;
                    }
                }
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            for (const auto& r : g_races.raceForms) {
                const auto* form = data ? data->LookupForm<RE::TESRace>(r.id, r.file) : nullptr;
                if (form && form->GetFormID() == race->GetFormID()) {
                    rule = "race_form";
                    return r.group;
                }
            }
            if (!edid.empty()) {
                for (const auto& r : g_races.contains) {
                    if (!r.text.empty() && edid.find(r.text) != std::string::npos) {
                        rule = "contains " + r.text;
                        return r.group;
                    }
                }
            }
            for (const auto& r : g_races.keywords) {
                if (std::ranges::all_of(r.all, [&](const std::string& kw) { return race->HasKeywordString(kw); })) {
                    rule = "keywords";
                    return r.group;
                }
            }
            rule = "default";
            return g_races.defaultGroup;
        }

        int RaceGroup(RE::TESRace* race)
        {
            if (!race) {
                return kHumanoid;
            }
            if (const auto it = g_raceCache.find(race->GetFormID()); it != g_raceCache.end()) {
                return it->second;
            }
            std::string rule;
            const std::string name = RaceGroupName(race, rule);
            const int group = ResolveGroup(name);
            if (g_raceCache.size() > 4096) {
                g_raceCache.clear();
            }
            g_raceCache[race->GetFormID()] = group;
            const char* edid = race->GetFormEditorID();
            SKSE::log::info("race {:08X} {} -> {} ({}){}", race->GetFormID(), edid && *edid ? edid : "(no EditorID)", name, rule,
                group == kHumanoid && !IEquals(name, "humanoid") ? " -> humanoid (not loaded)" : "");
            return group;
        }
    }

    void Load()
    {
        std::lock_guard lock(g_lock);
        g_groups.assign(1, Group{ "humanoid", Body{}, {} });
        g_rules.clear();
        const auto dir = InjuryDir();
        auto humanDir = dir / "humanoid";
        if (!std::filesystem::exists(humanDir)) {
            SKSE::log::warn("injuries: no injuries/humanoid/ folder, reading humanoid files from {}", dir.string());
            humanDir = dir;
        }
        SKSE::log::info("injuries: loading from {}", dir.string());
        nlohmann::json firstJson;
        const bool any = LoadHumanoidSources(humanDir, firstJson);
        g_session = nlohmann::json::object();
        try {
            const auto session = ReadJson(dir / "session.json");
            if (session.is_object()) {
                g_session = session;
            } else {
                SKSE::log::warn("injuries: session.json missing: torture scenes have no text");
            }
        } catch (const std::exception& e) {
            SKSE::log::error("injuries: session.json skipped: {}", e.what());
        }
        nlohmann::json humanSexual;
        Group& human = Human();
        if (any) {
            bool bodyOk = false;
            try {
                const auto body = ReadJson(humanDir / "body.json");
                if (body.is_object()) {
                    bodyOk = ReadBodyJson(human.body, body, "humanoid");
                }
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: humanoid body.json skipped: {}", e.what());
            }
            try {
                humanSexual = ReadJson(humanDir / "sexual_parts.json");
                if (humanSexual.is_null()) {
                    SKSE::log::warn("injuries: sexual_parts.json missing: sexual parts have no text");
                }
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: sexual_parts.json skipped: {}", e.what());
                humanSexual = nullptr;
            }
            if (!bodyOk) {
                ReadLegacyBody(human.body, firstJson);
                if (humanSexual.is_object()) {
                    ReadLegacySexualParts(human.body, humanSexual);
                }
                ReadLegacyLungs(human);
            }
            if (humanSexual.is_object()) {
                MergeSexualText(human, humanSexual);
            }
            FinishBody(human.body);
            FinishSources(human);
        }
        CheckRegions(human.body, "humanoid");
        for (int i = 0; i < static_cast<int>(human.sources.size()); ++i) {
            ReadClassify(i);
        }
        std::stable_sort(g_rules.begin(), g_rules.end(), [](const ClassifyRule& a, const ClassifyRule& b) {
            return a.priority > b.priority;
        });
        if (g_rules.empty()) {
            SKSE::log::warn("injuries: no classify rules in {}: torture spells and real damage are not tracked (stale copy in MO2 overwrite?)",
                humanDir.string());
        } else {
            SKSE::log::info("injuries: {} classify rules", g_rules.size());
        }
        // other groups: every folder next to humanoid/ (alphabetical)
        if (any && humanDir != dir) {
            std::error_code ec;
            std::vector<std::filesystem::path> dirs;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (entry.is_directory() && entry.path().filename() != "humanoid") {
                    dirs.push_back(entry.path());
                }
            }
            std::ranges::sort(dirs);
            for (const auto& d : dirs) {
                LoadGroup(d, humanSexual);
            }
        }
        {
            std::lock_guard raceLock(g_raceLock);
            g_raceCache.clear();
            g_actorGroups.clear();
            g_actorsResolved = false;
            g_warnedGroups.clear();
            LoadRaces(dir.parent_path() / "races.json");
        }
        if (WhipIndex() < 0) {
            SKSE::log::warn("injuries: whipping.json missing or invalid: whipping disabled, body / overlay / setting defaults fall back");
        }
        if (GenericIndex() < 0) {
            SKSE::log::warn("injuries: generic.json missing: damage that cannot be classified is not tracked");
        }
    }

    int CurrentGroup()
    {
        return t_group;
    }

    void SetGroup(int group)
    {
        t_group = group >= 0 && group < static_cast<int>(g_groups.size()) ? group : kHumanoid;
    }

    int GroupCount()
    {
        return static_cast<int>(g_groups.size());
    }

    std::string GroupName(int group)
    {
        return group >= 0 && group < GroupCount() ? g_groups[group].id : std::string("humanoid");
    }

    int GroupIndex(std::string_view name)
    {
        for (int i = 0; i < GroupCount(); ++i) {
            if (IEquals(g_groups[i].id, name)) {
                return i;
            }
        }
        return -1;
    }

    int GroupOf(RE::Actor* actor)
    {
        if (!actor || g_groups.size() <= 1) {
            return kHumanoid;
        }
        std::lock_guard lock(g_raceLock);
        if (!g_actorsResolved) {
            g_actorsResolved = true;
            auto* data = RE::TESDataHandler::GetSingleton();
            for (const auto& r : g_races.actors) {
                const auto* form = data ? data->LookupForm(r.id, r.file) : nullptr;
                if (form) {
                    g_actorGroups[form->GetFormID()] = ResolveGroup(r.group);
                } else {
                    SKSE::log::warn("injuries: races.json actor {}:{:06X} not found", r.file, r.id);
                }
            }
        }
        if (!g_actorGroups.empty()) {
            if (const auto it = g_actorGroups.find(actor->GetFormID()); it != g_actorGroups.end()) {
                return it->second;
            }
            if (const auto* base = actor->GetActorBase()) {
                if (const auto it = g_actorGroups.find(base->GetFormID()); it != g_actorGroups.end()) {
                    return it->second;
                }
            }
        }
        return RaceGroup(actor->GetRace());
    }

    bool IsDisabled(int source)
    {
        const auto* s = Find(source);
        return s && Disabled(Cur().body, *s);
    }

    void DumpRaces()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return;
        }
        std::lock_guard lock(g_raceLock);
        int count = 0;
        for (auto* race : data->GetFormArray<RE::TESRace>()) {
            if (!race) {
                continue;
            }
            std::string rule;
            const std::string name = RaceGroupName(race, rule);
            const auto* file = race->GetFile(0);
            const char* edid = race->GetFormEditorID();
            SKSE::log::info("race dump: {} {:08X} {} -> {} ({}){}", file ? file->GetFilename() : "?", race->GetFormID(),
                edid && *edid ? edid : "(no EditorID)", name, rule, GroupIndex(name) < 0 ? " [not loaded: humanoid]" : "");
            ++count;
        }
        SKSE::log::info("race dump: {} races", count);
    }

    const Body& GetBody()
    {
        return Cur().body;
    }

    int SourceCount()
    {
        return static_cast<int>(Human().sources.size());
    }

    const Source* Find(int index)
    {
        auto& sources = Cur().sources;
        return index >= 0 && index < static_cast<int>(sources.size()) ? &sources[index] : nullptr;
    }

    bool IEquals(std::string_view a, std::string_view b)
    {
        return std::ranges::equal(a, b, [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
        });
    }

    int IndexOf(std::string_view id)
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (IEquals(Human().sources[i].id, id)) {
                return i;
            }
        }
        return -1;
    }

    int IndexOf(std::string_view base, bool torture)
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (IEquals(Human().sources[i].base, base) && Human().sources[i].IsTorture() == torture) {
                return i;
            }
        }
        return -1;
    }

    int WhipIndex()
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (Human().sources[i].json.contains("overlay")) {
                return i;
            }
        }
        return -1;
    }

    bool IsTorture(int index)
    {
        const auto* s = Find(index);
        return s && s->IsTorture();
    }

    int GenericIndex()
    {
        for (const auto& rule : g_rules) {
            if (rule.fallback && rule.source >= 0) {
                return rule.source;
            }
        }
        return -1;
    }

    const std::vector<ClassifyRule>& ClassifyRules()
    {
        return g_rules;
    }

    nlohmann::json Json(int source, std::string_view path)
    {
        const auto* s = Find(source);
        if (!s) {
            return nullptr;
        }
        const auto* node = Walk(s->json, path);
        return node ? *node : nlohmann::json(nullptr);
    }

    std::string Str(int source, std::string_view path, std::string_view fallback)
    {
        const auto* s = Find(source);
        const auto* node = s ? Walk(s->json, path) : nullptr;
        if (node && node->is_string()) {
            return node->get<std::string>();
        }
        return std::string(fallback);
    }

    std::string SessionStr(std::string_view path, std::string_view fallback)
    {
        const auto* node = Walk(g_session, path);
        return node && node->is_string() ? node->get<std::string>() : std::string(fallback);
    }

    std::vector<FeedRace> FeedRaces()
    {
        std::vector<FeedRace> list;
        const auto* node = Walk(g_session, "heal_feed.races");
        if (!node || !node->is_array()) {
            return list;
        }
        for (const auto& r : *node) {
            if (!r.is_object()) {
                continue;
            }
            FeedRace f;
            f.file = r.value("file", "");
            f.id = FormIdOf(r, "id");
            f.key = r.value("key", "");
            if (!f.file.empty() && f.id != 0 && !f.key.empty()) {
                list.push_back(std::move(f));
            } else {
                SKSE::log::warn("injuries: session.json heal_feed.races entry skipped (needs file, id and key)");
            }
        }
        return list;
    }

    int Count(int source, std::string_view path)
    {
        const auto* s = Find(source);
        const auto* node = s ? Walk(s->json, path) : nullptr;
        return node && (node->is_array() || node->is_object()) ? static_cast<int>(node->size()) : 0;
    }

    float Number(int source, std::string_view path, float fallback)
    {
        const auto* s = Find(source);
        const auto* node = s ? Walk(s->json, path) : nullptr;
        if (node && node->is_boolean()) {
            return node->get<bool>() ? 1.0f : 0.0f;
        }
        return node && node->is_number() ? node->get<float>() : fallback;
    }

    std::string Render(std::string_view tmpl, const std::map<std::string, std::string>& vars)
    {
        std::string out;
        out.reserve(tmpl.size() + 32);
        std::size_t i = 0;
        while (i < tmpl.size()) {
            if (tmpl[i] == '{') {
                const auto close = tmpl.find('}', i);
                if (close != std::string_view::npos) {
                    const auto it = vars.find(std::string(tmpl.substr(i + 1, close - i - 1)));
                    if (it != vars.end()) {
                        out += it->second;
                        i = close + 1;
                        continue;
                    }
                }
            }
            out += tmpl[i++];
        }
        return out;
    }
}
