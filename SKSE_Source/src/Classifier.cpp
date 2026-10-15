#include "Classifier.h"

#include "InjuryConfig.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>

namespace Classifier
{
    namespace
    {
        constexpr double kHitWindow = 0.6;  // real seconds a hit event stays usable for the damage that follows it
        constexpr double kTortureWindow = 2.0;  // real seconds a "(torture)" effect stays current after it was last seen
        constexpr double kQuietRecast = 1.0;  // real seconds between QuietMarker casts on one victim (the marker lasts 3 s)
        constexpr std::uint32_t kQuietSpellId = 0x81A;  // SPEL Injury_Engine_QuietSpell

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

        // A classify-rule match. source -2 = nothing, -1 = ignore this hit (the padded whip).
        struct Match
        {
            int source = -2;
            bool unarmed = false;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, Recent> g_recent;  // victim -> latest hit event
        // victim -> latest "(torture)" effect, from AddTarget and hit events. Damage hooks read this instead of walking
        // the active-effect list: they run inside the engine's own effect update, where a nested walk crashes.
        std::unordered_map<RE::FormID, TortureSeen> g_torture;
        std::unordered_map<RE::FormID, double> g_quiet;  // victim -> last QuietMarker cast
        // classify effect_file / effect_id -> runtime MGEF form id (ESL light masters need explicit resolution)
        std::unordered_map<std::string, RE::FormID> g_classifyEffectForms;

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::string_view IdOf(int index)
        {
            const auto* s = InjuryConfig::Find(index);
            return s ? std::string_view(s->id) : std::string_view(index == -1 ? "ignore" : "none");
        }

        std::optional<RE::WEAPON_TYPE> WeaponTypeOf(std::string_view name)
        {
            using Type = RE::WEAPON_TYPE;
            static const std::pair<std::string_view, Type> kTypes[] = {
                { "OneHandSword", Type::kOneHandSword },
                { "TwoHandSword", Type::kTwoHandSword },
                { "OneHandAxe", Type::kOneHandAxe },
                { "TwoHandAxe", Type::kTwoHandAxe },
                { "OneHandDagger", Type::kOneHandDagger },
                { "Bow", Type::kBow },
                { "Crossbow", Type::kCrossbow },
                { "OneHandMace", Type::kOneHandMace },
                { "Staff", Type::kStaff },
                { "HandToHandMelee", Type::kHandToHandMelee },
            };
            for (const auto& [n, t] : kTypes) {
                if (n == name) {
                    return t;
                }
            }
            return std::nullopt;
        }

        std::string_view ResistName(const RE::EffectSetting* mgef)
        {
            switch (mgef->data.resistVariable) {
            case RE::ActorValue::kResistFire:
                return "ResistFire";
            case RE::ActorValue::kResistFrost:
                return "ResistFrost";
            case RE::ActorValue::kResistShock:
                return "ResistShock";
            default:
                return {};
            }
        }

        RE::TESForm* LookupForm(std::string_view file, std::uint32_t id)
        {
            if (file.empty() || id == 0) {
                return nullptr;
            }
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm(id, file) : nullptr;
        }

        std::string EffectRuleKey(const InjuryConfig::ClassifyRule& rule)
        {
            return rule.effectFile + ":" + std::to_string(rule.effectId & 0x00FFFFFFu);
        }

        RE::FormID ResolveClassifyEffectForm(std::string_view file, std::uint32_t localId)
        {
            if (file.empty() || localId == 0) {
                return 0;
            }
            const auto local = localId & 0x00FFFFFFu;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) {
                return 0;
            }
            if (auto* form = data->LookupForm(local, file)) {
                return form->GetFormID();
            }
            const RE::TESFile* mod = data->LookupLoadedLightModByName(file);
            if (!mod) {
                mod = data->LookupLoadedModByName(file);
            }
            if (!mod) {
                return 0;
            }
            if (mod->IsLight()) {
                return 0xFE000000u | (static_cast<RE::FormID>(mod->GetSmallFileCompileIndex()) << 12) | (local & 0xFFFu);
            }
            return mod->GetFormID(local);
        }

        void CacheClassifyEffectForms()
        {
            g_classifyEffectForms.clear();
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (!rule.hasEffect) {
                    continue;
                }
                const auto key = EffectRuleKey(rule);
                if (g_classifyEffectForms.contains(key)) {
                    continue;
                }
                const RE::FormID id = ResolveClassifyEffectForm(rule.effectFile, rule.effectId);
                if (!id) {
                    SKSE::log::warn("injuries: classify effect {} 0x{:X} not resolved", rule.effectFile, rule.effectId);
                    continue;
                }
                g_classifyEffectForms.emplace(key, id);
                if (const auto* conf = InjuryConfig::Find(rule.source); conf && conf->IsTorture()) {
                    SKSE::log::info("Classifier: torture MGEF {} 0x{:X} -> {:08X} ({})", rule.effectFile, rule.effectId, id,
                        conf->id);
                }
            }
        }

        bool EffectMatchesRule(const RE::EffectSetting* mgef, const InjuryConfig::ClassifyRule& rule)
        {
            if (!rule.hasEffect || !mgef) {
                return false;
            }
            if (LookupForm(rule.effectFile, rule.effectId) == mgef) {
                return true;
            }
            if (const auto it = g_classifyEffectForms.find(EffectRuleKey(rule)); it != g_classifyEffectForms.end()) {
                return mgef->GetFormID() == it->second;
            }
            return false;
        }

        int TortureSourceOf(const RE::EffectSetting* mgef)
        {
            if (!mgef) {
                return -2;
            }
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (!rule.hasEffect) {
                    continue;
                }
                if (!EffectMatchesRule(mgef, rule)) {
                    continue;
                }
                if (const auto* conf = InjuryConfig::Find(rule.source); conf && conf->IsTorture()) {
                    return rule.source;
                }
            }
            return -2;
        }

        int ResistHealthSourceOf(const RE::EffectSetting* mgef)
        {
            if (!mgef) {
                return -2;
            }
            if (TortureSourceOf(mgef) >= 0) {
                return -2;
            }
            const auto resist = ResistName(mgef);
            if (resist.empty()) {
                return -2;
            }
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (rule.hasEffect || rule.resist != resist) {
                    continue;
                }
                return rule.source;
            }
            return InjuryConfig::GenericIndex();
        }

        bool Npc(RE::Actor* actor)
        {
            return actor && actor->HasKeywordString("ActorTypeNPC");
        }

        bool UnarmedMatches(const InjuryConfig::ClassifyRule& rule, RE::Actor* attacker)
        {
            if (!rule.unarmed) {
                return false;
            }
            const bool npc = Npc(attacker);
            if (rule.npc == 1 && !npc) {
                return false;
            }
            if (rule.npc == 0 && npc) {
                return false;
            }
            const bool combat = attacker && attacker->IsInCombat();
            if (rule.combat == 1 && !combat) {
                return false;
            }
            if (rule.combat == 0 && combat) {
                return false;
            }
            return true;
        }

        Match MatchUnarmed(RE::Actor* attacker)
        {
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (UnarmedMatches(rule, attacker)) {
                    return { rule.source, true };
                }
            }
            return {};
        }

        bool WeaponTypeListed(const InjuryConfig::ClassifyRule& rule, RE::WEAPON_TYPE type)
        {
            for (const auto& name : rule.weaponTypes) {
                if (const auto parsed = WeaponTypeOf(name); parsed && *parsed == type) {
                    return true;
                }
            }
            return false;
        }

        Match MatchWeapon(const RE::TESObjectWEAP* weapon, RE::Actor* attacker)
        {
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (!rule.hasIgnore) {
                    continue;
                }
                if (auto* form = LookupForm(rule.ignoreFile, rule.ignoreId); form && form == weapon) {
                    return { -1, false };
                }
            }
            // A fist weapon is an unarmed hit when an unarmed rule claims this attacker. A mace stays a weapon rule.
            if (weapon->GetWeaponType() == RE::WEAPON_TYPE::kHandToHandMelee) {
                if (const Match hit = MatchUnarmed(attacker); hit.source >= 0) {
                    return hit;
                }
            }
            const auto type = weapon->GetWeaponType();
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (!WeaponTypeListed(rule, type)) {
                    continue;
                }
                if (!rule.keyword.empty() && !weapon->HasKeywordString(rule.keyword.c_str())) {
                    continue;
                }
                if (!rule.withoutKeyword.empty() && weapon->HasKeywordString(rule.withoutKeyword.c_str())) {
                    continue;
                }
                return { rule.source, false };
            }
            return {};
        }

        // Torture "(torture)" MGEF first, else health resist match, else -2.
        int EffectSource(const RE::EffectSetting* mgef)
        {
            if (const int torture = TortureSourceOf(mgef); torture >= 0) {
                return torture;
            }
            return ResistHealthSourceOf(mgef);
        }

        Match MatchMagic(const RE::MagicItem* item)
        {
            Match deferred{};
            for (const auto* effect : item->effects) {
                if (!effect || !effect->baseEffect) {
                    continue;
                }
                const auto* mgef = effect->baseEffect;
                if (const int torture = TortureSourceOf(mgef); torture >= 0) {
                    return { torture, false };
                }
                if (deferred.source == -2) {
                    const int health = ResistHealthSourceOf(mgef);
                    if (health != -2) {
                        deferred = { health, false };
                    }
                }
            }
            return deferred;
        }

        int TortureIndexOf(const RE::EffectSetting* mgef)
        {
            return TortureSourceOf(mgef);
        }

        void NoteTortureLocked(RE::FormID victimId, int index)
        {
            if (g_torture.size() > 512) {
                g_torture.clear();
            }
            g_torture[victimId] = { index, NowSeconds() };
        }

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

        Match ClassifyForm(RE::TESForm* form, RE::Actor* attacker)
        {
            if (!form) {
                return {};
            }
            if (const auto* weapon = form->As<RE::TESObjectWEAP>()) {
                return MatchWeapon(weapon, attacker);
            }
            if (const auto* magic = form->As<RE::MagicItem>()) {
                return MatchMagic(magic);
            }
            return {};
        }

        int FallingSource()
        {
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (rule.falling) {
                    return rule.source;
                }
            }
            return -2;
        }

        int ClassifyFall(RE::Actor* victim)
        {
            const int falling = FallingSource();
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
        CacheClassifyEffectForms();
        for (const auto& rule : InjuryConfig::ClassifyRules()) {
            for (const auto& name : rule.weaponTypes) {
                if (!WeaponTypeOf(name)) {
                    SKSE::log::warn("injuries: {} classify weapon_types: unknown \"{}\"", IdOf(rule.source), name);
                }
            }
        }
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
            const RE::FormID victimId = victim->GetFormID();
            const auto it = g_torture.find(victimId);
            const bool announce = it == g_torture.end() || NowSeconds() - it->second.when > kTortureWindow || it->second.index != index;
            NoteTortureLocked(victimId, index);
            if (announce) {
                SKSE::log::info("torture effect: victim {:08X} source {}", victimId, IdOf(index));
            }
        }
    }

    int Classify(RE::Actor* victim, RE::Actor* attacker, bool* physicalMelee, bool* unarmedForm)
    {
        if (!victim) {
            return -1;
        }
        const auto setPhysical = [&](bool value) {
            if (physicalMelee) {
                *physicalMelee = value;
            }
        };
        if (unarmedForm) {
            *unarmedForm = false;
        }
        const auto take = [&](const Match& hit, bool physical) {
            setPhysical(physical);
            if (unarmedForm) {
                *unarmedForm = hit.unarmed;
            }
            return hit.source;
        };
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
            const Match hit = ClassifyForm(form, hitter);
            if (hit.source != -2) {
                SKSE::log::debug("classify {:08X}: hit event form {:08X} -> {}", victimId, sourceId, IdOf(hit.source));
                return take(hit, form && form->IsWeapon());
            }
            if (const int active = ClassifyActiveTorture(victim); active != -2) {
                SKSE::log::debug("classify {:08X}: hit form {:08X} unknown, torture effect -> {}", victimId, sourceId, IdOf(active));
                return take({ active, false }, false);
            }
        }
        if (attacker) {
            for (const bool left : { false, true }) {
                auto* equipped = attacker->GetEquippedObject(left);
                const Match hit = ClassifyForm(equipped, attacker);
                if (hit.source != -2) {
                    SKSE::log::debug("classify {:08X}: attacker {:08X} equipped {:08X} -> {}", victimId, attacker->GetFormID(),
                        equipped ? equipped->GetFormID() : 0, IdOf(hit.source));
                    // an equipped spell (Sparks (torture), ...) ticks every frame: only a weapon is a melee hit
                    return take(hit, equipped && equipped->IsWeapon());
                }
            }
            if (!attacker->GetEquippedObject(false) && !attacker->GetEquippedObject(true)) {
                if (const int active = ClassifyActiveTorture(victim); active != -2) {
                    SKSE::log::debug("classify {:08X}: attacker {:08X} unarmed but torture effect -> {}", victimId, attacker->GetFormID(),
                        IdOf(active));
                    return take({ active, false }, false);
                }
                const Match hit = MatchUnarmed(attacker);
                SKSE::log::debug("classify {:08X}: attacker {:08X} unarmed {} -> {}", victimId, attacker->GetFormID(),
                    Npc(attacker) ? "npc" : "creature", IdOf(hit.source >= 0 ? hit.source : InjuryConfig::GenericIndex()));
                if (hit.source >= 0) {
                    return take(hit, true);
                }
                return take({ InjuryConfig::GenericIndex(), false }, true);
            }
        }
        if (const int active = ClassifyActiveTorture(victim); active != -2) {
            SKSE::log::debug("classify {:08X}: torture spell effect active -> {}", victimId, IdOf(active));
            return take({ active, false }, false);
        }
        if (!sourceId && !attacker) {
            if (const int fall = ClassifyFall(victim); fall != -2) {
                SKSE::log::debug("classify {:08X}: landing -> {}", victimId, IdOf(fall));
                return take({ fall, false }, false);
            }
        }
        SKSE::log::debug("classify {:08X}: no hit event or attacker weapon (hit form {:08X}) -> generic", victimId, sourceId);
        return take({ InjuryConfig::GenericIndex(), false }, false);
    }

    void MarkQuiet(RE::Actor* victim)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!victim || !tasks) {
            return;
        }
        {
            std::lock_guard lock(g_lock);
            const double now = NowSeconds();
            if (g_quiet.size() > 512) {
                g_quiet.clear();
            }
            double& last = g_quiet[victim->GetFormID()];
            if (now - last < kQuietRecast) {
                return;
            }
            last = now;
        }
        tasks->AddTask([handle = victim->GetHandle()]() {
            static auto* spell = skyrim_cast<RE::SpellItem*>(LookupForm("Injury_Engine.esp", kQuietSpellId));
            auto ref = handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            auto* caster = actor && !actor->IsDead() ? actor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
            if (!spell || !caster) {
                return;
            }
            caster->CastSpellImmediate(spell, true, actor, 1.0f, false, 0.0f, nullptr);
            SKSE::log::debug("quiet marker: victim {:08X}", actor->GetFormID());
        });
    }
}
