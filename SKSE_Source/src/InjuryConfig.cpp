#include "InjuryConfig.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <windows.h>

namespace InjuryConfig
{
    namespace
    {
        std::mutex g_lock;
        Body g_body;
        std::vector<Source> g_sources;

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
            b.healingMax = j.value("healing_max", b.healingMax);
            if (const auto t = j.find("thresholds"); t != j.end() && t->is_object()) {
                b.medium = t->value("medium", b.medium);
                b.severe = t->value("severe", b.severe);
                b.mostlyHealed = t->value("mostly_healed", b.mostlyHealed);
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
            if (const auto p = j.find("parts"); p != j.end() && p->is_array() && p->size() == kPartCount) {
                for (int i = 0; i < kPartCount; ++i) {
                    b.partName[i] = (*p)[i].value("name", b.partName[i]);
                    const std::string region = (*p)[i].value("region", "");
                    const auto it = std::ranges::find(b.regionName, region);
                    if (it != b.regionName.end()) {
                        b.partRegion[i] = static_cast<int>(it - b.regionName.begin());
                    }
                }
            }
            if (const auto o = j.find("overlay"); o != j.end() && o->is_object()) {
                if (const auto order = o->find("order"); order != o->end() && order->is_array() && order->size() == kPartCount) {
                    for (int i = 0; i < kPartCount; ++i) {
                        b.overlayOrder[i] = (*order)[i].get<int>();
                    }
                }
            }
            g_body = b;
        }
    }

    void Load()
    {
        std::lock_guard lock(g_lock);
        g_sources.clear();
        g_body = Body{};
        const auto dir = InjuryDir();
        SKSE::log::info("injuries: loading from {}", dir.string());
        std::error_code ec;
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        if (ec || files.empty()) {
            SKSE::log::error("injuries: no *.json found in {}", dir.string());
            return;
        }
        // whipping first (its body block wins), then alphabetical; stable source indices for a session
        std::ranges::sort(files, [](const auto& a, const auto& b) {
            const bool aw = a.stem() == "whipping";
            const bool bw = b.stem() == "whipping";
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
                Source s;
                s.json = nlohmann::json::parse(in, nullptr, true, true);
                s.id = s.json.value("id", file.stem().string());
                s.model = s.json.value("model", "health");
                s.requires_ = s.json.value("requires", "");
                if (const auto st = s.json.find("stages"); st != s.json.end() && st->is_array()) {
                    for (const auto& name : *st) {
                        s.stages.push_back(name.get<std::string>());
                    }
                }
                if (s.stages.size() < 3) {
                    throw std::runtime_error("\"stages\" needs at least light, medium, severe");
                }
                if (!bodyRead) {
                    ReadBody(s.json);
                    bodyRead = true;
                }
                SKSE::log::info("injuries: loaded {} ({}, model {})", file.filename().string(), s.id, s.model);
                g_sources.push_back(std::move(s));
            } catch (const std::exception& e) {
                SKSE::log::error("injuries: {} skipped: {}", file.filename().string(), e.what());
            }
        }
        if (WhipIndex() < 0) {
            SKSE::log::warn("injuries: no whip-model source loaded (whipping.json missing or invalid): whipping disabled");
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

    int IndexOf(std::string_view id)
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (g_sources[i].id == id) {
                return i;
            }
        }
        return -1;
    }

    int WhipIndex()
    {
        for (int i = 0; i < SourceCount(); ++i) {
            if (g_sources[i].model == "whip") {
                return i;
            }
        }
        return -1;
    }

    int GenericIndex()
    {
        return IndexOf("generic");
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
