#include "InjuryConfig.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include <windows.h>

namespace InjuryConfig
{
    namespace
    {
        std::mutex g_lock;
        Body g_body;
        nlohmann::json g_session;  // injuries/session.json: shared templates of the combined torture narration
        std::vector<Source> g_sources;
        std::vector<ClassifyRule> g_rules;

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

        // -1 when the key is absent: the rule does not care. 0 / 1 when it is a bool.
        int TriBool(const nlohmann::json& rule, const char* key)
        {
            const auto it = rule.find(key);
            if (it == rule.end() || !it->is_boolean()) {
                return -1;
            }
            return it->get<bool>() ? 1 : 0;
        }

        void ReadClassify(int sourceIndex)
        {
            const auto it = g_sources[sourceIndex].json.find("classify");
            if (it == g_sources[sourceIndex].json.end() || !it->is_array()) {
                return;
            }
            for (const auto& rule : *it) {
                if (!rule.is_object()) {
                    SKSE::log::warn("injuries: {} classify: an entry is not an object, ignored", g_sources[sourceIndex].id);
                    continue;
                }
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
                r.unarmed = rule.value("unarmed", false);
                r.npc = TriBool(rule, "npc");
                r.combat = TriBool(rule, "combat");
                r.falling = rule.value("falling", false);
                r.fallback = rule.value("fallback", false);
                g_rules.push_back(std::move(r));
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
            auto dir = DllDir() / "SkyrimNet_Whipped" / "injuries";
            if (!std::filesystem::exists(dir)) {
                auto fallback = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "SkyrimNet_Whipped" / "injuries";
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

        void ReadBody(const nlohmann::json& j)
        {
            Body b;
            b.damageMax = j.value("damage_max", b.damageMax);
            if (const auto t = j.find("thresholds"); t != j.end() && t->is_object()) {
                b.medium = t->value("medium", b.medium);
                b.severe = t->value("severe", b.severe);
            }
            if (const auto h = j.find("health_link"); h != j.end() && h->is_object()) {
                b.woundShare = h->value("wound_share", b.woundShare);
                b.floor = h->value("floor", b.floor);
            }
            if (const auto r = j.find("regions"); r != j.end() && r->is_array() && !r->empty()) {
                b.regionName.clear();
                for (const auto& name : *r) {
                    b.regionName.push_back(name.get<std::string>());
                }
            }
            if (const auto p = j.find("parts"); p != j.end() && p->is_array() && p->size() == kCoreParts) {
                for (int i = 0; i < kCoreParts; ++i) {
                    b.partName[i] = (*p)[i].value("name", b.partName[i]);
                    const std::string region = (*p)[i].value("region", "");
                    const auto it = std::ranges::find(b.regionName, region);
                    if (it != b.regionName.end()) {
                        b.partRegion[i] = static_cast<int>(it - b.regionName.begin());
                    }
                }
            }
            if (const auto o = j.find("overlay"); o != j.end() && o->is_object()) {
                if (const auto order = o->find("order"); order != o->end() && order->is_array() && order->size() == kCoreParts) {
                    for (int i = 0; i < kCoreParts; ++i) {
                        b.overlayOrder[i] = (*order)[i].get<int>();
                    }
                }
            }
            b.partMax.fill(b.damageMax);
            g_body = b;
        }

        // injuries/sexual_parts.json: the optional parts 6..10 (name, region, max, weight) and their text per source id
        // (a "both" file's health half falls back to the file id):
        // {"parts":[{name,region,max,weight} x5], "text":{"<source id>":{"looks":{region:[..]},"feels":{...}}}}
        // (torture feels: {"torture":{region:[..]},"heal":{region:[..]}}; health feels: {region:[..]})
        void ReadSexualParts(const std::filesystem::path& file)
        {
            std::ifstream in(file);
            if (!in) {
                SKSE::log::warn("injuries: sexual_parts.json missing: sexual parts have no text");
                return;
            }
            const auto j = nlohmann::json::parse(in, nullptr, true, true);
            Body& b = g_body;
            const auto p = j.find("parts");
            if (p != j.end() && p->is_array() && p->size() == kPartCount - kCoreParts) {
                for (int i = kCoreParts; i < kPartCount; ++i) {
                    const auto& e = (*p)[i - kCoreParts];
                    b.partName[i] = e.value("name", b.partName[i]);
                    b.partMax[i] = std::max(3, e.value("max", b.partMax[i]));
                    b.partWeight[i] = std::max(0.01f, e.value("weight", b.partWeight[i]));
                    const std::string region = e.value("region", b.regionName[b.partRegion[i]]);
                    auto it = std::ranges::find(b.regionName, region);
                    if (it == b.regionName.end()) {
                        b.regionName.push_back(region);
                        it = b.regionName.end() - 1;
                    }
                    b.partRegion[i] = static_cast<int>(it - b.regionName.begin());
                }
            }
            // text.looks / text.feels merged into each source's own JSON (objects merge, region arrays are added)
            const auto t = j.find("text");
            if (t == j.end() || !t->is_object()) {
                return;
            }
            for (auto& s : g_sources) {
                auto st = t->find(s.id);
                if (st == t->end()) {
                    st = t->find(s.base);  // the health half of a "both" file shares its text
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
        void FinishBody()
        {
            Body& b = g_body;
            for (int i = 0; i < kPartCount; ++i) {
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

        // Torture sources: part_weights {"part name": weight} over the body's weights (0 = never picked);
        // hook.points_per_second, hook.points_per_hit. Runs after the body is final (sexual parts read).
        void FinishSources()
        {
            const Body& b = g_body;
            for (auto& s : g_sources) {
                s.partWeight = b.partWeight;
                s.pointsPerSecond = std::max(0.1f, s.json.value("/hook/points_per_second"_json_pointer, 2.0f));
                s.pointsPerHit = std::clamp(s.json.value("/hook/points_per_hit"_json_pointer, 1), 1, 10);
                const auto w = s.json.find("part_weights");
                if (w == s.json.end() || !w->is_object()) {
                    continue;
                }
                for (const auto& [name, value] : w->items()) {
                    const auto it = std::ranges::find(b.partName, name);
                    if (it == b.partName.end() || !value.is_number()) {
                        SKSE::log::warn("injuries: {} part_weights: unknown part \"{}\" ignored", s.id, name);
                        continue;
                    }
                    s.partWeight[it - b.partName.begin()] = std::max(0.0f, value.get<float>());
                }
            }
        }
    }

    void Load()
    {
        std::lock_guard lock(g_lock);
        g_sources.clear();
        g_rules.clear();
        g_body = Body{};
        const auto dir = InjuryDir();
        SKSE::log::info("injuries: loading from {}", dir.string());
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json" && entry.path().stem() != "sexual_parts" &&
                entry.path().stem() != "session") {
                files.push_back(entry.path());
            }
        }
        if (ec || files.empty()) {
            SKSE::log::error("injuries: no *.json found in {}", dir.string());
            return;
        }
        // The file that carries "overlay" loads first (its body block wins), then alphabetical.
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
        bool bodyRead = false;
        for (const auto& file : files) {
            if (static_cast<int>(g_sources.size()) >= kMaxSources) {
                SKSE::log::warn("injuries: more than {} sources, ignoring {}", kMaxSources, file.filename().string());
                break;
            }
            try {
                std::ifstream in(file);
                const auto j = nlohmann::json::parse(in, nullptr, true, true);
                const std::string base = j.value("id", file.stem().string());
                std::string model = j.value("model", "health");
                if (model == "whip") {
                    model = "torture";  // before torture types, whipping was the only "whip" source
                }
                // "both": one torture and one health source from the shared fields, each with its own block
                // ("torture" / "health") merged over them. The health half's id is "<id>_real".
                std::vector<Source> loaded;
                if (model == "both") {
                    if (static_cast<int>(g_sources.size()) + 2 > kMaxSources) {
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
                if (!bodyRead) {
                    ReadBody(loaded.front().json);
                    bodyRead = true;
                }
                for (auto& s : loaded) {
                    SKSE::log::info("injuries: loaded {} ({}, model {})", file.filename().string(), s.id, s.model);
                    g_sources.push_back(std::move(s));
                }
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: {} skipped: {}", file.filename().string(), e.what());
            }
        }
        g_session = nlohmann::json::object();
        try {
            std::ifstream in(dir / "session.json");
            if (in) {
                g_session = nlohmann::json::parse(in, nullptr, true, true);
            } else {
                SKSE::log::warn("injuries: session.json missing: torture scenes have no text");
            }
        } catch (const std::exception& e) {
            SKSE::log::error("injuries: session.json skipped: {}", e.what());
        }
        if (bodyRead) {
            try {
                ReadSexualParts(dir / "sexual_parts.json");
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: sexual_parts.json skipped: {}", e.what());
            }
            FinishBody();
            FinishSources();
        }
        for (int i = 0; i < static_cast<int>(g_sources.size()); ++i) {
            ReadClassify(i);
        }
        std::stable_sort(g_rules.begin(), g_rules.end(), [](const ClassifyRule& a, const ClassifyRule& b) {
            return a.priority > b.priority;
        });
        if (g_rules.empty()) {
            SKSE::log::warn("injuries: no classify rules in {}: torture spells and real damage are not tracked (stale copy in MO2 overwrite?)",
                dir.string());
        } else {
            SKSE::log::info("injuries: {} classify rules", g_rules.size());
        }
        if (WhipIndex() < 0) {
            SKSE::log::warn("injuries: whipping.json missing or invalid: whipping disabled, body / overlay / setting defaults fall back");
        }
        if (GenericIndex() < 0) {
            SKSE::log::warn("injuries: generic.json missing: damage that cannot be classified is not tracked");
        }
    }

    const Body& GetBody()
    {
        return g_body;
    }

    int SourceCount()
    {
        return static_cast<int>(g_sources.size());
    }

    const Source* Find(int index)
    {
        return index >= 0 && index < SourceCount() ? &g_sources[index] : nullptr;
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
            if (IEquals(g_sources[i].id, id)) {
                return i;
            }
        }
        return -1;
    }

    int IndexOf(std::string_view base, bool torture)
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (IEquals(g_sources[i].base, base) && g_sources[i].IsTorture() == torture) {
                return i;
            }
        }
        return -1;
    }

    int WhipIndex()
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (g_sources[i].json.contains("overlay")) {
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
