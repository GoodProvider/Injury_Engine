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
        constexpr double kCalmWindow = 3.0;  // real seconds a torture hit keeps its victim from fighting back
        constexpr double kCombatPending = 1.0;  // real seconds a combat start waits for the torture hit that caused it

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

        struct Pair
        {
            RE::FormID other = 0;  // torture attacker / combat target
            double when = 0.0;
        };
        std::unordered_map<RE::FormID, Pair> g_calm;  // victim -> latest torture hit (attacker)
        std::unordered_map<RE::FormID, Pair> g_combatStart;  // actor -> latest combat start (target)
        // victim -> last whip lash, for the kIgnoreFriendlyHits flags SuppressFriendHits set (own lock: the fight
        // reaction hook runs inside the engine's hit handling)
        std::mutex g_friendLock;
        std::atomic<bool> g_punchCombatHealth{ false };
        std::unordered_map<RE::FormID, double> g_friendFlag;
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
                // Setting injury_engine.body.punch_combat_health off: torture punches stay torture in combat.
                const auto* conf = g_punchCombatHealth.load() ? nullptr : InjuryConfig::Find(rule.source);
                if (!conf || !conf->IsTorture()) {
                    return false;
                }
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

        // The padded whip (a classify rule's ignore weapon).
        bool IsIgnoredWeapon(RE::FormID source)
        {
            const auto* weapon = source ? RE::TESForm::LookupByID<RE::TESObjectWEAP>(source) : nullptr;
            if (!weapon) {
                return false;
            }
            for (const auto& rule : InjuryConfig::ClassifyRules()) {
                if (rule.hasIgnore && LookupForm(rule.ignoreFile, rule.ignoreId) == weapon) {
                    return true;
                }
            }
            return false;
        }

        // Ends the combat a torture hit started: the victim (and its alarm on the player) stands down. SKSE task,
        // because combat and hit events arrive inside the engine's own update.
        // Main thread. The actor (and its alarm on the player) stands down.
        bool StopCombatNow(RE::FormID actorId)
        {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            if (!actor || actor->IsDead()) {
                return false;
            }
            actor->StopCombat();
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                lists->StopCombatAndAlarmOnActor(actor, false);
            }
            return true;
        }

        void StopCombatLater(RE::FormID victimId, RE::FormID attackerId)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                return;
            }
            tasks->AddTask([victimId, attackerId]() {
                if (StopCombatNow(victimId)) {
                    SKSE::log::debug("calm: victim {:08X} stops fighting torture attacker {:08X}", victimId, attackerId);
                }
            });
        }

        // Caller holds g_lock. A combat start that came before this hit (same attacker) is ended now.
        void NoteCalmLocked(RE::FormID victimId, RE::FormID attackerId, double now)
        {
            if (g_calm.size() > 512) {
                g_calm.clear();
            }
            g_calm[victimId] = { attackerId, now };
            if (const auto it = g_combatStart.find(victimId);
                it != g_combatStart.end() && it->second.other == attackerId && now - it->second.when <= kCombatPending) {
                g_combatStart.erase(it);
                StopCombatLater(victimId, attackerId);
            }
        }

        class CombatSink : public RE::BSTEventSink<RE::TESCombatEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* event, RE::BSTEventSource<RE::TESCombatEvent>*) override
            {
                if (!event || !event->actor || !event->targetActor || *event->newState != RE::ACTOR_COMBAT_STATE::kCombat) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                const RE::FormID actorId = event->actor->GetFormID();
                const RE::FormID targetId = event->targetActor->GetFormID();
                const double now = NowSeconds();
                std::lock_guard lock(g_lock);
                if (const auto it = g_calm.find(actorId); it != g_calm.end() && it->second.other == targetId && now - it->second.when <= kCalmWindow) {
                    StopCombatLater(actorId, targetId);
                    return RE::BSEventNotifyControl::kContinue;
                }
                // The torture hit may be reported after the combat start: keep it for NoteCalmLocked.
                if (g_combatStart.size() > 512) {
                    g_combatStart.clear();
                }
                g_combatStart[actorId] = { targetId, now };
                return RE::BSEventNotifyControl::kContinue;
            }
        };

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
                    const bool whip = torture < 0 && IsIgnoredWeapon(event->source);
                    SKSE::log::debug("hit event target:{:08X} source:{:08X} cause:{:08X} torture:{} whip:{}", event->target->GetFormID(),
                        event->source, event->cause ? event->cause->GetFormID() : 0, IdOf(torture), whip);
                    std::lock_guard lock(g_lock);
                    if ((torture >= 0 || whip) && event->cause && event->target->Is(RE::FormType::ActorCharacter)) {
                        NoteCalmLocked(event->target->GetFormID(), event->cause->GetFormID(), NowSeconds());
                    }
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
        static CombatSink combatSink;
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESHitEvent>(&sink);
            holder->AddEventSink<RE::TESCombatEvent>(&combatSink);
            SKSE::log::info("Classifier: hit and combat event sinks registered");
        }
    }

    void NoteCalm(RE::Actor* victim, RE::Actor* attacker)
    {
        if (!victim || !attacker) {
            return;
        }
        std::lock_guard lock(g_lock);
        NoteCalmLocked(victim->GetFormID(), attacker->GetFormID(), NowSeconds());
    }

    bool IsWhipAttacker(RE::Actor* attacker)
    {
        if (!attacker) {
            return false;
        }
        for (const bool left : { false, true }) {
            if (const auto* equipped = attacker->GetEquippedObject(left); equipped && IsIgnoredWeapon(equipped->GetFormID())) {
                return true;
            }
        }
        return false;
    }

    void SetPunchCombatHealth(bool enabled)
    {
        g_punchCombatHealth.store(enabled);
    }

    bool IsTortureAttacker(RE::Actor* attacker)
    {
        if (IsWhipAttacker(attacker)) {
            return true;
        }
        if (!attacker || attacker->GetEquippedObject(false) || attacker->GetEquippedObject(true)) {
            return false;
        }
        // out-of-combat fists (punching): the same unarmed rule Classify uses
        const Match hit = MatchUnarmed(attacker);
        const auto* conf = hit.source >= 0 ? InjuryConfig::Find(hit.source) : nullptr;
        return conf && conf->IsTorture();
    }

    void SuppressFriendHits(RE::Actor* victim)
    {
        if (!victim) {
            return;
        }
        constexpr auto kFlag = static_cast<std::uint32_t>(RE::TESObjectREFR::RecordFlags::kIgnoreFriendlyHits);
        std::lock_guard lock(g_friendLock);
        const RE::FormID victimId = victim->GetFormID();
        if (const auto it = g_friendFlag.find(victimId); it != g_friendFlag.end()) {
            it->second = NowSeconds();
            victim->formFlags |= kFlag;
            return;
        }
        if (victim->formFlags & kFlag) {
            return;  // set by the game or another mod: not ours to clear
        }
        victim->formFlags |= kFlag;
        g_friendFlag[victimId] = NowSeconds();
        SKSE::log::debug("calm: victim {:08X} ignores friendly hits while whipped", victimId);
    }

    void ReleaseFriendHits(bool all)
    {
        const auto release = [](bool everything) {
            constexpr auto kFlag = static_cast<std::uint32_t>(RE::TESObjectREFR::RecordFlags::kIgnoreFriendlyHits);
            std::lock_guard lock(g_friendLock);
            const double now = NowSeconds();
            for (auto it = g_friendFlag.begin(); it != g_friendFlag.end();) {
                if (!everything && now - it->second <= kCalmWindow) {
                    ++it;
                    continue;
                }
                if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first)) {
                    actor->formFlags &= ~kFlag;
                }
                SKSE::log::debug("calm: victim {:08X} counts friendly hits again", it->first);
                it = g_friendFlag.erase(it);
            }
        };
        if (all) {
            release(true);
            return;
        }
        {
            std::lock_guard lock(g_friendLock);
            if (g_friendFlag.empty()) {
                return;
            }
        }
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask([release]() { release(false); });
        }
    }

    void ForgetCalm(RE::Actor* a, RE::Actor* b)
    {
        if (!a || !b) {
            return;
        }
        const RE::FormID aId = a->GetFormID();
        const RE::FormID bId = b->GetFormID();
        {
            std::lock_guard lock(g_lock);
            const auto erase = [](std::unordered_map<RE::FormID, Pair>& map, RE::FormID key, RE::FormID other) {
                if (const auto it = map.find(key); it != map.end() && it->second.other == other) {
                    map.erase(it);
                }
            };
            erase(g_calm, aId, bId);
            erase(g_calm, bId, aId);
            erase(g_combatStart, aId, bId);
            erase(g_combatStart, bId, aId);
        }
        constexpr auto kFlag = static_cast<std::uint32_t>(RE::TESObjectREFR::RecordFlags::kIgnoreFriendlyHits);
        std::lock_guard lock(g_friendLock);
        for (auto* actor : { a, b }) {
            if (const auto it = g_friendFlag.find(actor->GetFormID()); it != g_friendFlag.end()) {
                actor->formFlags &= ~kFlag;
                g_friendFlag.erase(it);
            }
        }
        SKSE::log::debug("calm: forgotten between {:08X} and {:08X}", aId, bId);
    }

    void StopFighting(RE::Actor* actor)
    {
        auto* tasks = actor ? SKSE::GetTaskInterface() : nullptr;
        if (!tasks) {
            return;
        }
        tasks->AddTask([id = actor->GetFormID()]() {
            if (StopCombatNow(id)) {
                SKSE::log::info("truce: {:08X} stops fighting", id);
            }
        });
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

    int TortureSourceOfEffect(const RE::EffectSetting* mgef)
    {
        const int source = TortureSourceOf(mgef);
        return source >= 0 ? source : -1;
    }

    int TortureSourceOfSpell(const RE::MagicItem* item)
    {
        if (!item) {
            return -1;
        }
        for (const auto* effect : item->effects) {
            if (const int source = effect ? TortureSourceOf(effect->baseEffect) : -2; source >= 0) {
                return source;
            }
        }
        return -1;
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
            // Both hands first: a Padded Whip in one hand must not swallow the beam of a "(torture)" spell in the
            // other (the whip's ignore result lets the game apply the spell damage untouched, so torture could kill).
            RE::TESForm* hands[2] = { attacker->GetEquippedObject(false), attacker->GetEquippedObject(true) };
            Match matches[2] = { ClassifyForm(hands[0], attacker), ClassifyForm(hands[1], attacker) };
            const int active = ClassifyActiveTorture(victim);
            int pick = -1;
            for (int i = 0; i < 2 && pick < 0; ++i) {
                // the victim burns from this hand's torture spell right now: a beam tick
                if (active >= 0 && matches[i].source == active && hands[i] && !hands[i]->IsWeapon()) {
                    pick = i;
                }
            }
            for (int i = 0; i < 2 && pick < 0; ++i) {
                if (matches[i].source >= 0) {
                    pick = i;
                }
            }
            if (pick >= 0) {
                SKSE::log::debug("classify {:08X}: attacker {:08X} equipped {:08X} -> {}", victimId, attacker->GetFormID(),
                    hands[pick]->GetFormID(), IdOf(matches[pick].source));
                // an equipped spell (Sparks (torture), ...) ticks every frame: only a weapon is a melee hit
                return take(matches[pick], hands[pick]->IsWeapon());
            }
            if (matches[0].source == -1 || matches[1].source == -1) {
                if (active != -2) {
                    SKSE::log::debug("classify {:08X}: attacker {:08X} holds the padded whip but torture effect -> {}", victimId,
                        attacker->GetFormID(), IdOf(active));
                    return take({ active, false }, false);
                }
                SKSE::log::debug("classify {:08X}: attacker {:08X} padded whip -> ignore", victimId, attacker->GetFormID());
                return take({ -1, false }, true);
            }
            if (!hands[0] && !hands[1]) {
                if (active != -2) {
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
