#include "Classifier.h"

#include "InjuryConfig.h"

#include <chrono>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace Classifier
{
    namespace
    {
        constexpr double kHitWindow = 0.6;  // real seconds a hit event stays usable for the damage that follows it

        struct Recent
        {
            RE::FormID source = 0;  // weapon or spell
            double when = 0.0;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Recent> g_recent;  // victim -> latest hit event
        RE::TESObjectWEAP* g_whip = nullptr;
        bool g_whipLooked = false;

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        bool IsWhip(const RE::TESObjectWEAP* weapon)
        {
            if (!g_whipLooked) {
                g_whipLooked = true;
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    g_whip = data->LookupForm<RE::TESObjectWEAP>(0x803, "SkyrimNet_Whipped.esp");
                }
            }
            return g_whip && weapon == g_whip;
        }

        // Class id -> source index, generic when that class has no file.
        int Source(std::string_view id)
        {
            if (const int index = InjuryConfig::IndexOf(id); index >= 0) {
                return index;
            }
            return InjuryConfig::GenericIndex();
        }

        int ClassifyWeapon(const RE::TESObjectWEAP* weapon)
        {
            using Type = RE::WEAPON_TYPE;
            switch (weapon->GetWeaponType()) {
            case Type::kOneHandSword:
            case Type::kTwoHandSword:
            case Type::kOneHandAxe:
                return Source("slashing");
            case Type::kTwoHandAxe:
                // war hammers share the two-hand axe animations
                return Source(weapon->HasKeywordString("WeapTypeWarhammer") ? "bludgeoning" : "slashing");
            case Type::kOneHandDagger:
            case Type::kBow:
            case Type::kCrossbow:
                return Source("piercing");
            case Type::kOneHandMace:
            case Type::kStaff:
            case Type::kHandToHandMelee:
                return Source("bludgeoning");
            default:
                return InjuryConfig::GenericIndex();
            }
        }

        int ClassifyMagic(const RE::MagicItem* item)
        {
            for (const auto* effect : item->effects) {
                if (!effect || !effect->baseEffect) {
                    continue;
                }
                switch (effect->baseEffect->data.resistVariable) {
                case RE::ActorValue::kResistFire:
                    return Source("burning");
                case RE::ActorValue::kResistFrost:
                    return Source("freezing");
                case RE::ActorValue::kResistShock:
                    return Source("shocking");
                default:
                    break;
                }
            }
            return InjuryConfig::GenericIndex();
        }

        // -2 = nothing recognised (caller decides), -1 = ignore, else a source index.
        int ClassifyForm(RE::TESForm* form)
        {
            if (!form) {
                return -2;
            }
            if (const auto* weapon = form->As<RE::TESObjectWEAP>()) {
                return IsWhip(weapon) ? -1 : ClassifyWeapon(weapon);
            }
            if (const auto* magic = form->As<RE::MagicItem>()) {
                return ClassifyMagic(magic);
            }
            return -2;
        }

        class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                if (event && event->target) {
                    std::lock_guard lock(g_lock);
                    if (g_recent.size() > 512) {
                        g_recent.clear();
                    }
                    g_recent[event->target->GetFormID()] = { event->source, NowSeconds() };
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Init()
    {
        static HitSink sink;
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESHitEvent>(&sink);
            SKSE::log::info("Classifier: hit event sink registered");
        }
    }

    int Classify(RE::Actor* victim, RE::Actor* attacker)
    {
        if (!victim) {
            return -1;
        }
        // 1. the latest hit event on this victim (weapon or spell that landed)
        RE::FormID sourceId = 0;
        {
            std::lock_guard lock(g_lock);
            if (const auto it = g_recent.find(victim->GetFormID()); it != g_recent.end() && NowSeconds() - it->second.when <= kHitWindow) {
                sourceId = it->second.source;
            }
        }
        if (sourceId) {
            const int result = ClassifyForm(RE::TESForm::LookupByID(sourceId));
            if (result != -2) {
                return result;
            }
        }
        // 2. what the attacker is holding (the hit event may not have fired yet)
        if (attacker) {
            const int result = ClassifyForm(attacker->GetEquippedObject(false));
            if (result != -2) {
                return result;
            }
            // 3. unarmed: people punch, animals bite and claw
            if (!attacker->GetEquippedObject(false)) {
                return attacker->HasKeywordString("ActorTypeNPC") ? Source("bludgeoning") : Source("mauling");
            }
        }
        return InjuryConfig::GenericIndex();
    }
}
