#include "InjuryConfig.h"
#include "InjuryEngine.h"
#include "Overlay.h"

#include <algorithm>
#include <string>
#include <vector>

// Natives for Scripts/Source/SkyrimNet_Whipped_Engine.psc
namespace
{
    std::int32_t Hit(RE::StaticFunctionTag*, RE::Actor* actor, RE::Actor* attacker, RE::BSFixedString sourceId)
    {
        const auto result = InjuryEngine::Hit(actor, attacker, sourceId.c_str());
        Overlay::Notify();
        return result;
    }

    std::int32_t HitPart(RE::StaticFunctionTag*, RE::Actor* actor, RE::Actor* attacker, RE::BSFixedString sourceId, std::int32_t part)
    {
        const auto result = InjuryEngine::HitPart(actor, attacker, sourceId.c_str(), part);
        Overlay::Notify();
        return result;
    }

    std::int32_t RealHit(RE::StaticFunctionTag*, RE::Actor* actor, RE::Actor* attacker, RE::BSFixedString sourceId, std::int32_t points)
    {
        const auto result = InjuryEngine::RealHit(actor, attacker, sourceId.c_str(), points);
        Overlay::Notify();
        return result;
    }

    bool HasHealth(RE::StaticFunctionTag*, RE::BSFixedString sourceId)
    {
        return InjuryConfig::IndexOf(sourceId.c_str(), false) >= 0;
    }

    std::int32_t HealPart(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t part, float health)
    {
        const auto result = InjuryEngine::HealPart(actor, part, health);
        Overlay::Notify();
        return result;
    }

    std::int32_t PartIndex(RE::StaticFunctionTag*, RE::BSFixedString name)
    {
        return InjuryEngine::PartIndex(name.c_str());
    }

    bool HasOpenWounds(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return InjuryEngine::HasOpenWounds(actor);
    }

    std::vector<RE::Actor*> TakeHealedActors(RE::StaticFunctionTag*)
    {
        auto result = InjuryEngine::TakeHealedActors();
        Overlay::Notify();
        return result;
    }

    std::vector<RE::Actor*> TakeQueuedHealActors(RE::StaticFunctionTag*)
    {
        return InjuryEngine::TakeQueuedHealActors();
    }

    std::vector<std::int32_t> TakeHealed(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return InjuryEngine::TakeHealed(actor);
    }

    std::vector<std::int32_t> TakeHits(RE::StaticFunctionTag*)
    {
        auto result = InjuryEngine::TakeHits();
        Overlay::Notify();
        return result;
    }

    std::vector<RE::Actor*> HitVictims(RE::StaticFunctionTag*)
    {
        return InjuryEngine::HitVictims();
    }

    std::vector<RE::Actor*> HitAttackers(RE::StaticFunctionTag*)
    {
        return InjuryEngine::HitAttackers();
    }

    std::vector<std::int32_t> TakeSyncEvents(RE::StaticFunctionTag*)
    {
        return InjuryEngine::TakeSyncEvents();
    }

    std::vector<std::int32_t> SyncHealth(RE::StaticFunctionTag*)
    {
        auto result = InjuryEngine::SyncHealth();
        Overlay::Notify();
        return result;
    }

    std::vector<RE::Actor*> SyncActors(RE::StaticFunctionTag*)
    {
        return InjuryEngine::SyncActors();
    }

    std::vector<RE::Actor*> SyncCasters(RE::StaticFunctionTag*)
    {
        return InjuryEngine::SyncCasters();
    }

    std::int32_t TrackedCount(RE::StaticFunctionTag*)
    {
        return InjuryEngine::TrackedCount();
    }

    RE::BSFixedString PartName(RE::StaticFunctionTag*, std::int32_t part)
    {
        return InjuryEngine::PartName(part);
    }

    RE::BSFixedString SourceId(RE::StaticFunctionTag*, std::int32_t source)
    {
        return InjuryEngine::SourceId(source);
    }

    std::int32_t SourceCount(RE::StaticFunctionTag*)
    {
        return InjuryEngine::SourceCount();
    }

    // True for a torture type (model "torture": whipping, pinching, ...), false for real damage or unknown ids.
    bool IsTorture(RE::StaticFunctionTag*, RE::BSFixedString sourceId)
    {
        return InjuryConfig::IsTorture(InjuryConfig::IndexOf(sourceId.c_str()));
    }

    // action.alias ("whip") or the source id itself ("whipping"), torture sources only.
    RE::BSFixedString SourceOfAlias(RE::StaticFunctionTag*, RE::BSFixedString alias)
    {
        if (alias.empty()) {
            return "";
        }
        for (int i = 0; i < InjuryConfig::SourceCount(); ++i) {
            if (!InjuryConfig::IsTorture(i)) {
                continue;
            }
            const auto* s = InjuryConfig::Find(i);
            if (!s) {
                continue;
            }
            if (InjuryConfig::IEquals(s->id, alias.c_str()) || InjuryConfig::IEquals(InjuryConfig::Str(i, "action.alias"), alias.c_str())) {
                return s->id;
            }
        }
        return "";
    }

    RE::BSFixedString ActionAliases(RE::StaticFunctionTag*)
    {
        struct Item
        {
            float order;
            int index;
            std::string alias;
        };
        std::vector<Item> items;
        for (int i = 0; i < InjuryConfig::SourceCount(); ++i) {
            if (!InjuryConfig::IsTorture(i)) {
                continue;
            }
            const std::string alias = InjuryConfig::Str(i, "action.alias");
            if (alias.empty()) {
                continue;
            }
            items.push_back({ InjuryConfig::Number(i, "action.order", 1000.0f), i, alias });
        }
        std::ranges::sort(items, [](const Item& a, const Item& b) {
            return a.order < b.order || (a.order == b.order && a.index < b.index);
        });
        std::string out;
        for (const auto& item : items) {
            if (!out.empty()) {
                out += '|';
            }
            out += item.alias;
        }
        return out;
    }

    RE::BSFixedString GetStateJson(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        return InjuryEngine::StateJson(actor);
    }

    RE::BSFixedString HitText(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::Actor* attacker, RE::Actor* victim,
        std::int32_t mask, std::int32_t level, std::int32_t count, bool noCause, bool unarmedForm)
    {
        return InjuryEngine::HitText(sourceId.c_str(), attacker, victim, mask, level, count, noCause, unarmedForm);
    }

    bool HitUnarmed(RE::StaticFunctionTag*, std::int32_t index)
    {
        return InjuryEngine::HitUnarmed(static_cast<std::size_t>(index));
    }

    void LogLine(RE::StaticFunctionTag*, RE::BSFixedString kind, RE::BSFixedString text)
    {
        InjuryEngine::LogLine(kind.c_str(), text.c_str());
    }

    RE::BSFixedString LevelText(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::Actor* victim, std::int32_t level)
    {
        return InjuryEngine::LevelText(sourceId.c_str(), victim, level);
    }

    RE::BSFixedString HealText(RE::StaticFunctionTag*, RE::Actor* victim, std::vector<std::int32_t> codes)
    {
        return InjuryEngine::HealText(victim, codes);
    }

    RE::BSFixedString SourceHealText(RE::StaticFunctionTag*, std::int32_t kind, std::int32_t source, std::int32_t percent,
        RE::Actor* caster, RE::Actor* target)
    {
        return InjuryEngine::SourceHealText(kind, source, percent, caster, target);
    }

    RE::BSFixedString ActionText(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::BSFixedString path, RE::Actor* speaker,
        RE::Actor* target, std::int32_t count)
    {
        return InjuryEngine::ActionText(sourceId.c_str(), path.c_str(), speaker, target, count);
    }

    RE::BSFixedString SceneText(RE::StaticFunctionTag*, RE::BSFixedString phase, std::vector<RE::TESForm*> attackers,
        std::vector<RE::TESForm*> victims, std::vector<RE::BSFixedString> sources, std::vector<std::int32_t> masks,
        std::vector<std::int32_t> levels)
    {
        std::vector<std::string> ids;
        for (const auto& id : sources) {
            ids.emplace_back(id.c_str());
        }
        std::vector<RE::Actor*> atk;
        std::vector<RE::Actor*> vic;
        for (auto* form : attackers) {
            atk.push_back(form ? form->As<RE::Actor>() : nullptr);
        }
        for (auto* form : victims) {
            vic.push_back(form ? form->As<RE::Actor>() : nullptr);
        }
        return InjuryEngine::SceneText(phase.c_str(), atk, vic, ids, masks, levels);
    }

    // Raw JSON string at a dotted path of injuries/<source>.json ("" when absent), no placeholders replaced.
    RE::BSFixedString Text(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::BSFixedString path)
    {
        return InjuryConfig::Str(InjuryConfig::IndexOf(sourceId.c_str()), path.c_str());
    }

    std::int32_t TextCount(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::BSFixedString path)
    {
        return InjuryConfig::Count(InjuryConfig::IndexOf(sourceId.c_str()), path.c_str());
    }

    float Number(RE::StaticFunctionTag*, RE::BSFixedString sourceId, RE::BSFixedString path, float fallback)
    {
        return InjuryConfig::Number(InjuryConfig::IndexOf(sourceId.c_str()), path.c_str(), fallback);
    }

    void SetHoursPerPoint(RE::StaticFunctionTag*, float hours)
    {
        InjuryEngine::SetHoursPerPoint(hours);
    }

    void SetRegenRate(RE::StaticFunctionTag*, float percent)
    {
        InjuryEngine::SetRegenRate(percent);
    }

    void SetOverlayEnabled(RE::StaticFunctionTag*, bool enabled)
    {
        Overlay::SetEnabled(enabled);
    }

    void SetIncludeSexualParts(RE::StaticFunctionTag*, bool enabled)
    {
        InjuryEngine::SetIncludeSexualParts(enabled);
        Overlay::Notify();
    }

    // "Verbose logging": debug lines in SkyrimNet_Whipped.log (SKSE::log::debug is dropped at info level).
    void SetVerboseLogging(RE::StaticFunctionTag*, bool enabled)
    {
        const auto level = enabled ? spdlog::level::debug : spdlog::level::info;
        if (auto logger = spdlog::default_logger(); logger && logger->level() != level) {
            logger->set_level(level);
            logger->flush_on(level);
            SKSE::log::info("verbose logging {}", enabled ? "on" : "off");
        }
    }

    void Forget(RE::StaticFunctionTag*, RE::Actor* actor)
    {
        InjuryEngine::Forget(actor);
        Overlay::Notify();
    }
}

bool Register_Engine_Functions(RE::BSScript::IVirtualMachine* a_vm)
{
    constexpr std::string_view s = "SkyrimNet_Whipped_Engine";
    a_vm->RegisterFunction("Hit", s, Hit);
    a_vm->RegisterFunction("HitPart", s, HitPart);
    a_vm->RegisterFunction("RealHit", s, RealHit);
    a_vm->RegisterFunction("HasHealth", s, HasHealth);
    a_vm->RegisterFunction("HealPart", s, HealPart);
    a_vm->RegisterFunction("PartIndex", s, PartIndex);
    a_vm->RegisterFunction("HasOpenWounds", s, HasOpenWounds);
    a_vm->RegisterFunction("TakeHealedActors", s, TakeHealedActors);
    a_vm->RegisterFunction("TakeQueuedHealActors", s, TakeQueuedHealActors);
    a_vm->RegisterFunction("TakeHealed", s, TakeHealed);
    a_vm->RegisterFunction("TakeHits", s, TakeHits);
    a_vm->RegisterFunction("HitVictims", s, HitVictims);
    a_vm->RegisterFunction("HitAttackers", s, HitAttackers);
    a_vm->RegisterFunction("TakeSyncEvents", s, TakeSyncEvents);
    a_vm->RegisterFunction("SyncHealth", s, SyncHealth);
    a_vm->RegisterFunction("SyncActors", s, SyncActors);
    a_vm->RegisterFunction("SyncCasters", s, SyncCasters);
    a_vm->RegisterFunction("TrackedCount", s, TrackedCount);
    a_vm->RegisterFunction("PartName", s, PartName);
    a_vm->RegisterFunction("SourceId", s, SourceId);
    a_vm->RegisterFunction("SourceCount", s, SourceCount);
    a_vm->RegisterFunction("IsTorture", s, IsTorture);
    a_vm->RegisterFunction("SourceOfAlias", s, SourceOfAlias);
    a_vm->RegisterFunction("ActionAliases", s, ActionAliases);
    a_vm->RegisterFunction("GetStateJson", s, GetStateJson);
    a_vm->RegisterFunction("HitText", s, HitText);
    a_vm->RegisterFunction("HitUnarmed", s, HitUnarmed);
    a_vm->RegisterFunction("LogLine", s, LogLine);
    a_vm->RegisterFunction("LevelText", s, LevelText);
    a_vm->RegisterFunction("HealText", s, HealText);
    a_vm->RegisterFunction("SourceHealText", s, SourceHealText);
    a_vm->RegisterFunction("ActionText", s, ActionText);
    a_vm->RegisterFunction("SceneText", s, SceneText);
    a_vm->RegisterFunction("Text", s, Text);
    a_vm->RegisterFunction("TextCount", s, TextCount);
    a_vm->RegisterFunction("Number", s, Number);
    a_vm->RegisterFunction("SetHoursPerPoint", s, SetHoursPerPoint);
    a_vm->RegisterFunction("SetRegenRate", s, SetRegenRate);
    a_vm->RegisterFunction("SetOverlayEnabled", s, SetOverlayEnabled);
    a_vm->RegisterFunction("SetIncludeSexualParts", s, SetIncludeSexualParts);
    a_vm->RegisterFunction("SetVerboseLogging", s, SetVerboseLogging);
    a_vm->RegisterFunction("Forget", s, Forget);
    return true;
}
