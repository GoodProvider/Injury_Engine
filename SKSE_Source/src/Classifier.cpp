#include "Classifier.h"

#include "InjuryConfig.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace Classifier
{
    namespace
    {
        constexpr double kHitWindow = 0.6;  // real seconds a hit event stays usable for the damage that follows it
        constexpr double kTortureWindow = 2.0;  // real seconds a "(torture)" effect stays current after it was last seen

        struct Recent
        {
            RE::FormID source = 0;  // weapon or spell
            RE::ObjectRefHandle cause;  // attacker / caster
            double when = 0.0;
        };

        struct TortureSeen
        {
            int index = -2;  // torture source index
            double when = 0.0;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Recent> g_recent;  // victim -> latest hit event
        // victim -> latest "(torture)" effect, from AddTarget and hit events. Damage hooks read this instead of walking
        // the active-effect list: they run inside the engine's own effect update, where a nested walk crashes.
        std::unordered_map<RE::FormID, TortureSeen> g_torture;
        RE::TESObjectWEAP* g_whip = nullptr;
        bool g_whipLooked = false;
        RE::EffectSetting* g_tortureEffects[3] = {};  // Flames / Frostbite / Sparks (torture) effects
        bool g_tortureLooked = false;
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

        // Source index as its id for the log ("ignore" for -1).
        std::string_view IdOf(int index)
        {
            const auto* s = InjuryConfig::Find(index);
            return s ? std::string_view(s->id) : std::string_view(index == -1 ? "ignore" : "none");
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

        // Torture rather than a fight: a person (NPC or the player) attacking while not in combat.
        bool OutOfCombat(RE::Actor* attacker)
        {
            return attacker && !attacker->IsInCombat() && attacker->HasKeywordString("ActorTypeNPC");
        }

        // One of the "(torture)" spell effects of SkyrimNet_Whipped.esp (in or out of combat).
        bool IsTortureEffect(const RE::EffectSetting* mgef)
        {
            if (!g_tortureLooked) {
                g_tortureLooked = true;
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    for (RE::FormID i = 0; i < 3; ++i) {
                        g_tortureEffects[i] = data->LookupForm<RE::EffectSetting>(0x813 + i, "SkyrimNet_Whipped.esp");
                    }
                }
            }
            return mgef && std::find(std::begin(g_tortureEffects), std::end(g_tortureEffects), mgef) != std::end(g_tortureEffects);
        }

        // Torture id for a fire / frost / shock effect, nullptr for any other.
        const char* TortureId(const RE::EffectSetting* mgef)
        {
            switch (mgef->data.resistVariable) {
            case RE::ActorValue::kResistFire:
                return "fire";
            case RE::ActorValue::kResistFrost:
                return "frost";
            case RE::ActorValue::kResistShock:
                return "shock";
            default:
                return nullptr;
            }
        }

        // Fire / frost / shock: torture (fire, frost, shock) for a "(torture)" spell, else real damage
        // (burning, freezing, shocking).
        int ClassifyMagic(const RE::MagicItem* item)
        {
            for (const auto* effect : item->effects) {
                if (!effect || !effect->baseEffect) {
                    continue;
                }
                const bool torture = IsTortureEffect(effect->baseEffect);
                const auto pick = [&](std::string_view tortureId, std::string_view realId) {
                    const int t = torture ? InjuryConfig::IndexOf(tortureId) : -1;
                    return t >= 0 ? t : Source(realId);
                };
                switch (effect->baseEffect->data.resistVariable) {
                case RE::ActorValue::kResistFire:
                    return pick("fire", "burning");
                case RE::ActorValue::kResistFrost:
                    return pick("frost", "freezing");
                case RE::ActorValue::kResistShock:
                    return pick("shock", "shocking");
                default:
                    break;
                }
            }
            return InjuryConfig::GenericIndex();
        }

        // Torture source index of a "(torture)" effect, -2 for any other effect.
        int TortureIndexOf(const RE::EffectSetting* mgef)
        {
            if (!mgef || !IsTortureEffect(mgef)) {
                return -2;
            }
            const char* id = TortureId(mgef);
            const int index = id ? InjuryConfig::IndexOf(id) : -1;
            return index >= 0 ? index : -2;
        }

        // Caller holds g_lock.
        void NoteTortureLocked(RE::FormID victimId, int index)
        {
            if (g_torture.size() > 512) {
                g_torture.clear();
            }
            g_torture[victimId] = { index, NowSeconds() };
        }

        // Spell ticks with no hit event in the window: a "(torture)" effect seen on the victim recently.
        // -2 when there is none. Never walks the active-effect list (see g_torture).
        int ClassifyActiveTorture(RE::Actor* victim)
        {
            std::lock_guard lock(g_lock);
            const auto it = g_torture.find(victim->GetFormID());
            if (it == g_torture.end()) {
                return -2;
            }
            if (NowSeconds() - it->second.when > kTortureWindow) {
                g_torture.erase(it);
                return -2;
            }
            return it->second.index;
        }

        // -2 = nothing recognised (caller decides), -1 = ignore, else a source index.
        int ClassifyForm(RE::TESForm* form, RE::Actor* attacker)
        {
            if (!form) {
                return -2;
            }
            if (const auto* weapon = form->As<RE::TESObjectWEAP>()) {
                if (IsWhip(weapon)) {
                    return -1;
                }
                // fists out of combat: a beating, torture punching
                if (weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee && OutOfCombat(attacker) &&
                    InjuryConfig::IndexOf("punching") >= 0) {
                    return InjuryConfig::IndexOf("punching");
                }
                return ClassifyWeapon(weapon);
            }
            if (const auto* magic = form->As<RE::MagicItem>()) {
                return ClassifyMagic(magic);
            }
            return -2;
        }

        // Landing damage: the character controller still holds the fall when the damage is applied.
        // The health half of falling.json; -2 when not falling or falling.json is not loaded.
        int ClassifyFall(RE::Actor* victim)
        {
            const int falling = InjuryConfig::IndexOf("falling", false);
            auto* controller = falling >= 0 ? victim->GetCharController() : nullptr;
            if (!controller) {
                return -2;
            }
            const float drop = controller->fallStartHeight - victim->GetPositionZ();
            SKSE::log::debug("classify {:08X}: fallTime:{:.2f} drop:{:.0f}", victim->GetFormID(), controller->fallTime, drop);
            return controller->fallTime > 0.0f ? falling : -2;
        }

        class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                if (event && event->target) {
                    // concentration beams keep hitting while held: keep the torture entry fresh
                    int torture = -2;
                    if (const auto* item = event->source ? RE::TESForm::LookupByID<RE::MagicItem>(event->source) : nullptr) {
                        for (const auto* effect : item->effects) {
                            if (effect && (torture = TortureIndexOf(effect->baseEffect)) >= 0) {
                                break;
                            }
                        }
                    }
                    std::lock_guard lock(g_lock);
                    if (g_recent.size() > 512) {
                        g_recent.clear();
                    }
                    g_recent[event->target->GetFormID()] = { event->source, event->cause ? event->cause->GetHandle() : RE::ObjectRefHandle{},
                        NowSeconds() };
                    if (torture >= 0) {
                        NoteTortureLocked(event->target->GetFormID(), torture);
                    }
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

    void NoteEffect(RE::Actor* victim, RE::Effect* effect)
    {
        if (!victim || !effect) {
            return;
        }
        if (const int index = TortureIndexOf(effect->baseEffect); index >= 0) {
            std::lock_guard lock(g_lock);
            NoteTortureLocked(victim->GetFormID(), index);
        }
    }

    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee)
    {
        if (!victim) {
            return -1;
        }
        const auto setPhysical = [&](bool value) {
            if (physicalMelee) {
                *physicalMelee = value;
            }
        };
        // 1. the latest hit event on this victim (weapon or spell that landed)
        RE::FormID sourceId = 0;
        RE::ObjectRefHandle cause;
        {
            std::lock_guard lock(g_lock);
            if (const auto it = g_recent.find(victim->GetFormID()); it != g_recent.end() && NowSeconds() - it->second.when <= kHitWindow) {
                sourceId = it->second.source;
                cause = it->second.cause;
            }
        }
        const RE::FormID victimId = victim->GetFormID();
        if (sourceId) {
            auto causeRef = cause.get();
            auto* hitter = attacker ? attacker : causeRef ? causeRef->As<RE::Actor>() : nullptr;
            auto* form = RE::TESForm::LookupByID(sourceId);
            const int result = ClassifyForm(form, hitter);
            if (result != -2) {
                setPhysical(form && form->IsWeapon());
                SKSE::log::debug("classify {:08X}: hit event form {:08X} -> {}", victimId, sourceId, IdOf(result));
                return result;
            }
            // Projectile / other hit forms: do not guess unarmed while a torture spell is on the victim.
            if (const int active = ClassifyActiveTorture(victim); active != -2) {
                setPhysical(false);
                SKSE::log::debug("classify {:08X}: hit form {:08X} unknown, torture effect -> {}", victimId, sourceId, IdOf(active));
                return active;
            }
        }
        // 2. what the attacker is holding (the hit event may not have fired yet)
        if (attacker) {
            for (const bool left : { false, true }) {
                auto* equipped = attacker->GetEquippedObject(left);
                const int result = ClassifyForm(equipped, attacker);
                if (result != -2) {
                    setPhysical(true);
                    SKSE::log::debug("classify {:08X}: attacker {:08X} equipped {:08X} -> {}", victimId, attacker->GetFormID(),
                        equipped ? equipped->GetFormID() : 0, IdOf(result));
                    return result;
                }
            }
            // 3. unarmed: people punch (torture punching out of combat), animals bite and claw
            if (!attacker->GetEquippedObject(false) && !attacker->GetEquippedObject(true)) {
                if (const int active = ClassifyActiveTorture(victim); active != -2) {
                    setPhysical(false);
                    SKSE::log::debug("classify {:08X}: attacker {:08X} unarmed but torture effect -> {}", victimId, attacker->GetFormID(),
                        IdOf(active));
                    return active;
                }
                const bool npc = attacker->HasKeywordString("ActorTypeNPC");
                const int punching = InjuryConfig::IndexOf("punching");
                const int unarmed = !npc                                          ? Source("mauling")
                                    : OutOfCombat(attacker) && punching >= 0  ? punching
                                                                               : Source("bludgeoning");
                setPhysical(true);
                SKSE::log::debug("classify {:08X}: attacker {:08X} unarmed {} -> {}", victimId, attacker->GetFormID(), npc ? "npc" : "creature",
                    IdOf(unarmed));
                return unarmed;
            }
        }
        // 4. spell ticks after the hit event window: a "(torture)" spell still running is torture
        if (const int active = ClassifyActiveTorture(victim); active != -2) {
            setPhysical(false);
            SKSE::log::debug("classify {:08X}: torture spell effect active -> {}", victimId, IdOf(active));
            return active;
        }
        // 5. no hit and no attacker while falling: landing damage
        if (!sourceId && !attacker) {
            if (const int fall = ClassifyFall(victim); fall != -2) {
                setPhysical(false);
                SKSE::log::debug("classify {:08X}: landing -> {}", victimId, IdOf(fall));
                return fall;
            }
        }
        setPhysical(false);
        SKSE::log::debug("classify {:08X}: no hit event or attacker weapon (hit form {:08X}) -> generic", victimId, sourceId);
        return InjuryConfig::GenericIndex();
    }
}
