#include "InjuryEngine.h"

#include "InjuryConfig.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

namespace InjuryEngine
{
    namespace
    {
        using InjuryConfig::GetBody;
        constexpr int kMaxSources = InjuryConfig::kMaxSources;
        constexpr int kStageHealed = 5;
        constexpr int kStageSourceHealed = 6;  // HealItem only: the source has healed on every part
        constexpr int kStagePartHealed = 7;    // HealItem only: the part has no injury left
        constexpr double kSourceLinger = 1.0;  // real seconds a healing source stays active after last seen

        struct Part  // whip model
        {
            bool hit = false;
            std::int32_t damage = 0;
            std::int32_t healing = 0;
            double lastHours = 0.0;  // game hours when time was last applied
        };
        using Parts = std::array<Part, kPartCount>;
        using Real = std::array<std::array<std::int32_t, kPartCount>, kMaxSources>;  // health model: [source][part] damage

        struct State
        {
            Parts whip{};
            Real real{};
            std::int32_t replaced = 0;  // whip damage points replaced by real damage (overlay)
            float carry = 0.0f;         // real damage (health) not yet a whole point
            // Transient: which sources injured a part / which parts a source injured, since it was last clear.
            std::array<std::uint32_t, kPartCount> partSources{};
            std::array<std::uint32_t, kMaxSources> sourceParts{};
        };

        // The healing source (spell, staff, potion, ...) currently working on an actor. Transient.
        struct Healer
        {
            RE::ActorHandle caster;
            int source = kSourceNone;
            int startPoints = 0;
            double lastSeen = 0.0;  // real seconds
        };

        struct Event
        {
            RE::FormID actor = 0;
            RE::ActorHandle caster;
            int kind = 0;
            int source = kSourceNone;
            int percent = 0;
        };

        struct HealItem
        {
            int source = 0;
            int part = 0;
            int stage = 0;
        };

        struct HitEntry
        {
            RE::FormID victim = 0;
            RE::ActorHandle attacker;
            int source = 0;
            int mask = 0;
            int level = 0;
            bool fresh = false;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, State> g_actors;
        std::unordered_map<RE::FormID, std::vector<HealItem>> g_healed;  // not yet taken by Papyrus. Transient.
        std::unordered_map<RE::FormID, Healer> g_healers;
        std::unordered_map<RE::FormID, bool> g_pained;      // had damage at the last sync
        std::unordered_map<RE::FormID, float> g_regenBank;  // regen health not yet a whole point
        std::vector<Event> g_events;                        // not yet taken by SyncHealth
        std::vector<HitEntry> g_hits;                       // real hits not yet taken by Papyrus
        std::vector<RE::Actor*> g_syncActors;
        std::vector<RE::Actor*> g_syncCasters;
        std::vector<RE::Actor*> g_hitVictims;
        std::vector<RE::Actor*> g_hitAttackers;
        float g_hoursPerPoint = 1.0f;
        float g_regenRate = 10.0f;
        thread_local int g_internal = 0;  // >0 while the engine itself changes health

        struct InternalScope
        {
            InternalScope() { ++g_internal; }
            ~InternalScope() { --g_internal; }
        };

        double NowHours()
        {
            auto* cal = RE::Calendar::GetSingleton();
            return cal ? static_cast<double>(cal->GetHoursPassed()) : 0.0;
        }

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        std::mt19937& Rng()
        {
            static std::mt19937 rng{ std::random_device{}() };
            return rng;
        }

        int RandomIndex(int count)
        {
            return std::uniform_int_distribution<int>(0, count - 1)(Rng());
        }

        int WhipIndex()
        {
            return InjuryConfig::WhipIndex();
        }

        // ---- stages and totals -------------------------------------------------------------------------

        int DamageStage(int damage)
        {
            const auto& b = GetBody();
            if (damage >= b.severe) return 2;
            if (damage >= b.medium) return 1;
            if (damage >= 1) return 0;
            return -1;
        }

        int StageIndex(const Part& p)
        {
            const auto& b = GetBody();
            if (const int stage = DamageStage(p.damage); stage >= 0) return stage;
            if (p.healing >= b.healingMax) return kStageHealed;
            if (p.healing >= b.mostlyHealed) return 4;
            return 3;
        }

        // Damage plus missing healing points over all hit whip parts.
        int Deficit(const Parts& parts)
        {
            const int healed = GetBody().healingMax;
            int total = 0;
            for (const auto& p : parts) {
                if (p.hit) {
                    total += p.damage + (healed - p.healing);
                }
            }
            return total;
        }

        int WhipDamage(const Parts& parts)
        {
            int total = 0;
            for (const auto& p : parts) {
                if (p.hit) {
                    total += p.damage;
                }
            }
            return total;
        }

        bool AnyWhipHit(const Parts& parts)
        {
            return std::ranges::any_of(parts, [](const Part& p) { return p.hit; });
        }

        int RealTotal(const State& st)
        {
            int total = 0;
            for (const auto& source : st.real) {
                for (const int d : source) {
                    total += d;
                }
            }
            return total;
        }

        int RealOnPart(const State& st, int part)
        {
            int total = 0;
            for (const auto& source : st.real) {
                total += source[part];
            }
            return total;
        }

        int RealOnSource(const State& st, int source)
        {
            int total = 0;
            for (const int d : st.real[source]) {
                total += d;
            }
            return total;
        }

        int PartDamage(const State& st, int part)
        {
            return (st.whip[part].hit ? st.whip[part].damage : 0) + RealOnPart(st, part);
        }

        bool HasWhip(const State& st)
        {
            return Deficit(st.whip) > 0;
        }

        bool HasOpenDamage(const State& st)
        {
            return WhipDamage(st.whip) > 0 || RealTotal(st) > 0;
        }

        int TotalPoints(const State& st)
        {
            return Deficit(st.whip) + RealTotal(st);
        }

        bool AnyInjury(const State& st)
        {
            return AnyWhipHit(st.whip) || RealTotal(st) > 0;
        }

        bool PartClear(const State& st, int part)
        {
            const Part& p = st.whip[part];
            return (!p.hit || (p.damage == 0 && p.healing >= GetBody().healingMax)) && RealOnPart(st, part) == 0;
        }

        // Health per whip point / per real point.
        float RatioWhip(float maxHealth)
        {
            const auto& b = GetBody();
            return b.woundShare * maxHealth / static_cast<float>(kPartCount * (b.damageMax + b.healingMax));
        }

        float RatioReal(float maxHealth)
        {
            return maxHealth / static_cast<float>(kPartCount * GetBody().damageMax);
        }

        // Health the wounds hold health down to (whip wounds never take more than the floor allows).
        float WoundedLevel(const State& st, float maxHealth)
        {
            float level = maxHealth - Deficit(st.whip) * RatioWhip(maxHealth) - RealTotal(st) * RatioReal(maxHealth);
            if (HasWhip(st)) {
                level = std::max(level, GetBody().floor * maxHealth);
            }
            return std::max(level, 0.0f);
        }

        void MarkTouched(State& st, int source, int part)
        {
            st.partSources[part] |= 1u << source;
            st.sourceParts[source] |= 1u << part;
        }

        // ---- heal queue --------------------------------------------------------------------------------

        // Latest stage per (source, part) wins; source / part heal items are kept once.
        void QueueHealed(RE::FormID id, int source, int part, int stage)
        {
            auto& items = g_healed[id];
            for (auto& item : items) {
                const bool same = stage == kStageSourceHealed ? item.stage == stage && item.source == source
                                  : stage == kStagePartHealed ? item.stage == stage && item.part == part
                                                              : item.stage < kStageSourceHealed && item.source == source && item.part == part;
                if (same) {
                    item.stage = stage;
                    return;
                }
            }
            items.push_back({ source, part, stage });
        }

        // A new hit on a part overrides any of that source's heals not yet narrated.
        void DropHealed(RE::FormID id, int source, int part)
        {
            const auto it = g_healed.find(id);
            if (it == g_healed.end()) {
                return;
            }
            std::erase_if(it->second, [&](const HealItem& i) { return i.stage < kStageSourceHealed && i.source == source && i.part == part; });
        }

        // A part may now be clear of every source: queue "part healed" when more than one source injured it.
        void AfterHeal(RE::FormID id, State& st, int part)
        {
            if (!PartClear(st, part)) {
                return;
            }
            if (std::popcount(st.partSources[part]) >= 2) {
                QueueHealed(id, 0, part, kStagePartHealed);
            }
            st.partSources[part] = 0;
        }

        // ---- whip model --------------------------------------------------------------------------------

        // Spend elapsed heal points: first drain damage to 0, then raise healing.
        void Advance(RE::FormID id, State& st, double now)
        {
            const int whip = WhipIndex();
            const int healed = GetBody().healingMax;
            const double step = g_hoursPerPoint;
            for (int i = 0; i < kPartCount; ++i) {
                Part& p = st.whip[i];
                if (!p.hit) {
                    continue;
                }
                if ((p.damage == 0 && p.healing >= healed) || now < p.lastHours) {
                    p.lastHours = now;
                    continue;
                }
                const int points = static_cast<int>(std::floor((now - p.lastHours) / step));
                if (points <= 0) {
                    continue;
                }
                const int before = StageIndex(p);
                p.lastHours += points * step;
                const int drained = std::min(points, static_cast<int>(p.damage));
                p.damage -= drained;
                p.healing = std::min(healed, p.healing + (points - drained));
                if (const int after = StageIndex(p); after != before && whip >= 0) {
                    QueueHealed(id, whip, i, after);
                    if (after == kStageHealed) {
                        AfterHeal(id, st, i);
                    }
                }
            }
        }

        // One whip point healed on a random part that still needs it. False when nothing is left.
        bool HealWhipPoint(RE::FormID id, State& st, double now)
        {
            const int healed = GetBody().healingMax;
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                const Part& p = st.whip[i];
                if (p.hit && (p.damage > 0 || p.healing < healed)) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                return false;
            }
            const int index = open[RandomIndex(count)];
            Part& p = st.whip[index];
            const int before = StageIndex(p);
            if (p.damage > 0) {
                --p.damage;
            } else {
                ++p.healing;
            }
            p.lastHours = now;
            if (const int after = StageIndex(p); after != before && WhipIndex() >= 0) {
                QueueHealed(id, WhipIndex(), index, after);
                if (after == kStageHealed) {
                    AfterHeal(id, st, index);
                }
            }
            return true;
        }

        // A real hit replaces one whip point: damage first, then a missing healing point. True if one was replaced.
        bool ReplaceWhipPoint(State& st, double now)
        {
            const int healed = GetBody().healingMax;
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                if (st.whip[i].hit && st.whip[i].damage > 0) {
                    open[count++] = i;
                }
            }
            if (count > 0) {
                --st.whip[open[RandomIndex(count)]].damage;
                ++st.replaced;
                return true;
            }
            for (int i = 0; i < kPartCount; ++i) {
                if (st.whip[i].hit && st.whip[i].healing < healed) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                return false;
            }
            Part& p = st.whip[open[RandomIndex(count)]];
            ++p.healing;
            p.lastHours = now;
            return true;
        }

        // Whip fully healed or replaced: clear it. queue: narrate "source healed" (not for replacement).
        void ClearWhipIfDone(RE::FormID id, State& st, bool queue)
        {
            if (!AnyWhipHit(st.whip) || HasWhip(st)) {
                return;
            }
            const int whip = WhipIndex();
            if (queue && whip >= 0 && std::popcount(st.sourceParts[whip]) >= 2) {
                QueueHealed(id, whip, 0, kStageSourceHealed);
            }
            if (whip >= 0) {
                st.sourceParts[whip] = 0;
            }
            for (int i = 0; i < kPartCount; ++i) {
                if (st.whip[i].hit) {
                    st.partSources[i] &= ~(whip >= 0 ? 1u << whip : 0u);
                }
            }
            st.whip = Parts{};
            st.replaced = 0;
        }

        // ---- real (health model) -----------------------------------------------------------------------

        // One point on a random part that is not at max damage; -1 when every part is full.
        int AddRealPoint(State& st, int source)
        {
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                if (PartDamage(st, i) < GetBody().damageMax) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                return -1;
            }
            const int part = open[RandomIndex(count)];
            ++st.real[source][part];
            MarkTouched(st, source, part);
            return part;
        }

        // One real point healed on a random (source, part) that has damage. False when nothing is left.
        bool HealRealPoint(RE::FormID id, State& st)
        {
            std::array<std::pair<int, int>, kMaxSources * kPartCount> open{};
            int count = 0;
            for (int s = 0; s < kMaxSources; ++s) {
                for (int p = 0; p < kPartCount; ++p) {
                    if (st.real[s][p] > 0) {
                        open[count++] = { s, p };
                    }
                }
            }
            if (count == 0) {
                return false;
            }
            const auto [source, part] = open[RandomIndex(count)];
            const int before = DamageStage(st.real[source][part]);
            --st.real[source][part];
            const int after = DamageStage(st.real[source][part]);
            if (after < 0) {
                QueueHealed(id, source, part, kStageHealed);
                if (RealOnSource(st, source) == 0) {
                    if (std::popcount(st.sourceParts[source]) >= 2) {
                        QueueHealed(id, source, 0, kStageSourceHealed);
                    }
                    st.sourceParts[source] = 0;
                }
                AfterHeal(id, st, part);
            } else if (after != before) {
                QueueHealed(id, source, part, after);
            }
            return true;
        }

        // ---- healing sources, events -------------------------------------------------------------------

        int Percent(const Healer& healer, int points)
        {
            if (healer.startPoints <= 0) {
                return 100;
            }
            return std::clamp(100 * (healer.startPoints - points) / healer.startPoints, 0, 100);
        }

        Healer* ActiveHealer(RE::FormID id, double now)
        {
            const auto it = g_healers.find(id);
            if (it == g_healers.end() || now - it->second.lastSeen >= kSourceLinger) {
                return nullptr;
            }
            return &it->second;
        }

        void PushEvent(RE::FormID id, int kind, const Healer* healer, int percent)
        {
            Event e;
            e.actor = id;
            e.kind = kind;
            e.percent = percent;
            if (healer) {
                e.caster = healer->caster;
                e.source = healer->source;
            }
            g_events.push_back(e);
        }

        void Erase(RE::FormID id)
        {
            g_actors.erase(id);
            g_healed.erase(id);
            g_healers.erase(id);
            g_pained.erase(id);
            g_regenBank.erase(id);
        }

        // Applies time to the actor's entry; drops it when every injury is healed (queuing kFullyHealed) or
        // the actor is dead. Returns the entry or nullptr. Caller holds g_lock.
        State* Current(RE::Actor* actor, double now)
        {
            const RE::FormID id = actor->GetFormID();
            const auto it = g_actors.find(id);
            if (it == g_actors.end()) {
                return nullptr;
            }
            State& st = it->second;
            Advance(id, st, now);
            if (actor->IsDead()) {
                SKSE::log::info("forget {:08X} (dead)", id);
                Erase(id);
                return nullptr;
            }
            ClearWhipIfDone(id, st, true);
            if (!AnyInjury(st)) {
                SKSE::log::info("forget {:08X} (healed)", id);
                PushEvent(id, kFullyHealed, ActiveHealer(id, NowSeconds()), 100);
                Erase(id);
                return nullptr;
            }
            return &st;
        }

        bool IsHealing(const RE::EffectSetting* mgef, float magnitude)
        {
            using Flag = RE::EffectSetting::EffectSettingData::Flag;
            return mgef && magnitude > 0.0f &&
                   mgef->GetArchetype() == RE::EffectSetting::Archetype::kValueModifier &&
                   mgef->data.primaryAV == RE::ActorValue::kHealth && !mgef->IsDetrimental() &&
                   !mgef->data.flags.any(Flag::kRecover) &&
                   mgef->data.castingType != RE::MagicSystem::CastingType::kConstantEffect;
        }

        int SourceOf(RE::MagicItem* item)
        {
            if (!item) {
                return kSourceMagic;
            }
            using Type = RE::MagicSystem::SpellType;
            switch (item->GetSpellType()) {
            case Type::kSpell:
            case Type::kPower:
            case Type::kLesserPower:
            case Type::kVoicePower:
            case Type::kScroll:
                return kSourceSpell;
            case Type::kStaffEnchantment:
                return kSourceStaff;
            case Type::kPotion:
                return item->IsFood() ? kSourceMagic : kSourcePotion;
            default:
                return kSourceMagic;
            }
        }

        // A healing source touched a tracked actor: refresh it, or start one (queuing kHealStart).
        void Seen(RE::FormID id, const State& st, RE::Actor* caster, RE::MagicItem* item, double now)
        {
            if (const auto it = g_healers.find(id); it != g_healers.end()) {
                it->second.lastSeen = now;
                return;
            }
            const int points = TotalPoints(st);
            if (points <= 0) {
                return;
            }
            Healer healer;
            if (caster) {
                healer.caster = caster->GetHandle();
            }
            healer.source = SourceOf(item);
            healer.startPoints = points;
            healer.lastSeen = now;
            g_healers[id] = healer;
            PushEvent(id, kHealStart, &healer, 0);
            SKSE::log::info("heal start {:08X} source:{} caster:{:08X} points:{}", id, healer.source,
                caster ? caster->GetFormID() : 0, points);
        }

        // Healing effects still running on the actor (concentration beams, heal over time).
        void ScanActiveEffects(RE::Actor* actor, RE::FormID id, const State& st, double now)
        {
            auto* target = actor->AsMagicTarget();
            if (!target) {
                return;
            }
            // The engine's own walk: GetActiveEffectList() is nullptr on VR since CommonLibSSE-NG v11.
            using Flag = RE::ActiveEffect::Flag;
            target->VisitActiveEffects([&](RE::ActiveEffect* ae) {
                if (ae && !ae->flags.any(Flag::kInactive, Flag::kDispelled) && IsHealing(ae->GetBaseObject(), ae->magnitude)) {
                    Seen(id, st, ae->caster.get().get(), ae->spell, now);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        }

        // Health above the wounded level is healing: turn it into points on random parts (whip and real alike).
        // A healing source, or an actor without whip wounds, converts all of it; natural regen with whip wounds
        // only g_regenRate percent, the rest is taken back.
        void ConvertHealth(RE::Actor* actor, RE::FormID id, State& st, double now, bool sourceActive)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            if (maxHealth <= 0.0f) {
                return;
            }
            const float ratioW = RatioWhip(maxHealth);
            const float ratioR = RatioReal(maxHealth);
            const float excess = av->GetActorValue(RE::ActorValue::kHealth) - WoundedLevel(st, maxHealth);
            if (excess <= 0.0f) {
                return;
            }
            const bool fullSpeed = sourceActive || !HasWhip(st);
            float budget = excess;
            if (!fullSpeed) {
                float& bank = g_regenBank[id];
                bank += excess * g_regenRate / 100.0f;
                budget = bank;
            }
            float spent = 0.0f;
            while (true) {
                const bool whip = HasWhip(st);
                const bool real = RealTotal(st) > 0;
                if (!whip && !real) {
                    break;
                }
                const bool pickWhip = whip && (!real || RandomIndex(2) == 0);
                const float cost = pickWhip ? ratioW : ratioR;
                if (budget - spent < cost) {
                    break;
                }
                if (pickWhip ? !HealWhipPoint(id, st, now) : !HealRealPoint(id, st)) {
                    break;
                }
                spent += cost;
            }
            if (!fullSpeed) {
                g_regenBank[id] = std::max(0.0f, budget - spent);
                if (const float extra = excess - spent; extra > 0.0f) {
                    InternalScope scope;
                    av->DamageActorValue(RE::ActorValue::kHealth, extra);
                }
            }
        }

        // Health for whip points just added, never below the floor (whipping can't kill).
        void TakeHealth(RE::Actor* actor, int points)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            const float health = av->GetActorValue(RE::ActorValue::kHealth);
            const float floor = GetBody().floor * maxHealth;
            if (points <= 0 || maxHealth <= 0.0f || health <= floor) {
                return;
            }
            const float loss = std::min(points * RatioWhip(maxHealth), health - floor);
            InternalScope scope;
            av->DamageActorValue(RE::ActorValue::kHealth, loss);
        }

        void NotifyPapyrus()
        {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent event{ "SkyrimNet_Whipped_Injury", "", 0.0f, nullptr };
                source->SendEvent(&event);
            }
        }

        // ---- text --------------------------------------------------------------------------------------

        std::string NameOf(RE::Actor* actor)
        {
            if (!actor) {
                return "Someone";
            }
            const char* name = actor->GetDisplayFullName();
            return name && *name ? name : "Someone";
        }

        std::map<std::string, std::string> ActorVars(RE::Actor* actor)
        {
            const auto* base = actor ? actor->GetActorBase() : nullptr;
            const bool female = base && base->IsFemale();
            return { { "name", NameOf(actor) },
                { "he", female ? "she" : "he" },
                { "him", female ? "her" : "him" },
                { "his", female ? "her" : "his" },
                { "reflexive", female ? "herself" : "himself" } };
        }

        const char* StageName(int source, int stage)
        {
            static const char* kFixed[] = { "light", "medium", "severe", "recovering", "mostly_healed", "healed" };
            const auto* s = InjuryConfig::Find(source);
            if (s && stage >= 0 && stage < static_cast<int>(s->stages.size())) {
                return s->stages[stage].c_str();
            }
            return stage >= 0 && stage < 6 ? kFixed[stage] : "healed";
        }

        // "face", "back and left arm", "face, back and left arm" for a part bitmask.
        std::string PartsText(int source, int mask)
        {
            const std::string andWord = InjuryConfig::Str(source, "narration.list.and", " and ");
            const std::string comma = InjuryConfig::Str(source, "narration.list.comma", ", ");
            std::string text;
            int remaining = mask;
            for (int i = 0; i < kPartCount && remaining != 0; ++i) {
                const int bit = 1 << i;
                if (!(remaining & bit)) {
                    continue;
                }
                remaining &= ~bit;
                const std::string& part = GetBody().partName[i];
                text += text.empty() ? part : (remaining == 0 ? andWord : comma) + part;
            }
            return text.empty() ? InjuryConfig::Str(source, "narration.list.fallback", "body") : text;
        }

        // The source whose heal_events wording fits the actor: whip-only wounds use whipping's, anything else generic's.
        int HealEventsSource(const State* st)
        {
            const int whip = WhipIndex();
            const int generic = InjuryConfig::GenericIndex();
            const bool whipOnly = st && RealTotal(*st) == 0 && AnyWhipHit(st->whip);
            if (whipOnly && whip >= 0) {
                return whip;
            }
            return generic >= 0 ? generic : whip;
        }
    }

    bool IsInternalDamage()
    {
        return g_internal > 0;
    }

    std::int32_t Hit(RE::Actor* actor, std::string_view sourceId)
    {
        const int source = InjuryConfig::IndexOf(sourceId);
        const auto* conf = InjuryConfig::Find(source);
        if (!actor || actor->IsDead() || !conf || conf->model != "whip") {
            return -1;
        }
        std::int32_t code;
        {
            std::lock_guard lock(g_lock);
            const double now = NowHours();
            const RE::FormID id = actor->GetFormID();
            State* existing = Current(actor, now);
            State& st = existing ? *existing : g_actors[id];
            const bool fresh = !HasOpenDamage(st);
            const int deficitBefore = Deficit(st.whip);

            const int index = RandomIndex(kPartCount);
            Part& p = st.whip[index];
            DropHealed(id, source, index);
            int level;
            if (!p.hit) {
                level = kUndamaged;
                p.hit = true;
                p.damage = 1;
                p.healing = 0;
                p.lastHours = now;
            } else if (p.damage == 0) {
                // A healing part reopens straight to medium damage.
                level = kLight;
                p.damage = GetBody().medium;
                p.healing = 0;
                p.lastHours = now;
            } else {
                level = DamageStage(p.damage) + 1;
                p.damage = std::min(GetBody().damageMax, p.damage + 1);
            }
            MarkTouched(st, source, index);
            g_pained[id] = true;
            TakeHealth(actor, Deficit(st.whip) - deficitBefore);
            SKSE::log::info("hit {:08X} {} part:{} level:{} damage:{}", id, conf->id, GetBody().partName[index], level, p.damage);
            code = EncodeHit(fresh, source, 1 << index, level);
        }
        return code;
    }

    float OnDamage(RE::Actor* victim, RE::Actor* attacker, float amount, int source)
    {
        const auto* conf = InjuryConfig::Find(source);
        if (!victim || amount <= 0.0f || !conf || conf->model != "health" || victim->IsDead()) {
            return amount;
        }
        auto* av = victim->AsActorValueOwner();
        const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
        if (maxHealth <= 0.0f) {
            return amount;
        }
        float pass = amount;
        {
            std::lock_guard lock(g_lock);
            const double now = NowHours();
            const RE::FormID id = victim->GetFormID();
            State* existing = Current(victim, now);
            State& st = existing ? *existing : g_actors[id];
            const bool fresh = !HasOpenDamage(st);
            const float ratioW = RatioWhip(maxHealth);
            const float ratioR = RatioReal(maxHealth);

            // Whip points hold health down: real damage replaces them first and does not lower health by that part.
            int removeWhip = 0;
            float absorbed = 0.0f;
            if (HasWhip(st)) {
                const int deficit = Deficit(st.whip);
                removeWhip = std::min(deficit, static_cast<int>(std::floor(std::min(amount, deficit * ratioW) / ratioW + 1e-3f)));
                absorbed = removeWhip * ratioW;
                pass = amount - absorbed;
            }
            const float convertible = st.carry + pass;
            const int passPoints = static_cast<int>(std::floor(convertible / ratioR + 1e-4f));
            st.carry = convertible - passPoints * ratioR;
            const int points = static_cast<int>(std::lround(absorbed / ratioR)) + passPoints;

            int replacedNow = 0;
            for (int i = 0; i < removeWhip; ++i) {
                if (!ReplaceWhipPoint(st, now)) {
                    break;
                }
                ++replacedNow;
            }
            ClearWhipIfDone(id, st, false);

            int mask = 0;
            int level = 0;
            for (int i = 0; i < points; ++i) {
                // the part's stage before the point lands: 0 undamaged .. 3 severe
                std::array<int, kPartCount> stageBefore{};
                for (int p = 0; p < kPartCount; ++p) {
                    stageBefore[p] = DamageStage(PartDamage(st, p)) + 1;
                }
                const int part = AddRealPoint(st, source);
                if (part < 0) {
                    break;
                }
                mask |= 1 << part;
                level = std::max(level, stageBefore[part]);
                DropHealed(id, source, part);
            }
            g_pained[id] = true;
            if (mask != 0) {
                if (g_hits.size() < 256) {
                    HitEntry e;
                    e.victim = id;
                    if (attacker) {
                        e.attacker = attacker->GetHandle();
                    }
                    e.source = source;
                    e.mask = mask;
                    e.level = level;
                    e.fresh = fresh;
                    g_hits.push_back(e);
                }
                SKSE::log::info("damage {:08X} {} amount:{:.1f} absorbed:{:.1f} whip_replaced:{} points:{} mask:{} level:{}", id,
                    conf->id, amount, absorbed, replacedNow, points, mask, level);
            }
            if (!AnyInjury(st)) {
                g_actors.erase(id);
            }
        }
        NotifyPapyrus();
        return pass;
    }

    bool HasOpenWounds(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }
        std::lock_guard lock(g_lock);
        const State* st = Current(actor, NowHours());
        return st && HasOpenDamage(*st);
    }

    std::vector<RE::Actor*> TakeHealedActors()
    {
        std::lock_guard lock(g_lock);
        const double now = NowHours();
        std::vector<RE::FormID> ids;
        ids.reserve(g_actors.size());
        for (const auto& [id, st] : g_actors) {
            ids.push_back(id);
        }
        for (const auto id : ids) {
            if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
                Current(actor, now);  // applies time; may queue heals and forget the actor
            }
        }
        std::vector<RE::Actor*> result;
        for (auto it = g_healed.begin(); it != g_healed.end();) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
            if (!actor || it->second.empty()) {
                it = g_healed.erase(it);
                continue;
            }
            result.push_back(actor);
            ++it;
        }
        return result;
    }

    std::vector<std::int32_t> TakeHealed(RE::Actor* actor)
    {
        std::vector<std::int32_t> codes;
        if (!actor) {
            return codes;
        }
        std::lock_guard lock(g_lock);
        const auto it = g_healed.find(actor->GetFormID());
        if (it == g_healed.end()) {
            return codes;
        }
        for (const auto& item : it->second) {
            if (item.stage == kStageSourceHealed) {
                codes.push_back(kHealSourceBase + item.source);
            } else if (item.stage == kStagePartHealed) {
                codes.push_back(kHealPartBase + item.part);
            } else {
                codes.push_back(item.source * 64 + item.part * 8 + item.stage);
            }
        }
        g_healed.erase(it);
        return codes;
    }

    std::vector<std::int32_t> TakeHits()
    {
        std::lock_guard lock(g_lock);
        std::vector<std::int32_t> codes;
        g_hitVictims.clear();
        g_hitAttackers.clear();
        for (const auto& e : g_hits) {
            auto* victim = RE::TESForm::LookupByID<RE::Actor>(e.victim);
            if (!victim) {
                continue;
            }
            codes.push_back(EncodeHit(e.fresh, e.source, e.mask, e.level));
            g_hitVictims.push_back(victim);
            g_hitAttackers.push_back(e.attacker.get().get());
        }
        g_hits.clear();
        return codes;
    }

    std::vector<RE::Actor*> HitVictims()
    {
        std::lock_guard lock(g_lock);
        return g_hitVictims;
    }

    std::vector<RE::Actor*> HitAttackers()
    {
        std::lock_guard lock(g_lock);
        return g_hitAttackers;
    }

    void NoteHealingSource(RE::Actor* target, RE::TESObjectREFR* caster, RE::MagicItem* item, RE::Effect* effect)
    {
        if (!target || !effect || !IsHealing(effect->baseEffect, effect->effectItem.magnitude)) {
            return;
        }
        std::lock_guard lock(g_lock);
        const auto it = g_actors.find(target->GetFormID());
        if (it == g_actors.end()) {
            return;
        }
        Seen(it->first, it->second, caster ? caster->As<RE::Actor>() : nullptr, item, NowSeconds());
    }

    std::vector<std::int32_t> SyncHealth()
    {
        std::lock_guard lock(g_lock);
        const double nowHours = NowHours();
        const double now = NowSeconds();
        std::vector<RE::FormID> ids;
        ids.reserve(g_actors.size());
        for (const auto& [id, st] : g_actors) {
            ids.push_back(id);
        }
        for (const auto id : ids) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) {
                Erase(id);
                continue;
            }
            if (!actor->Is3DLoaded()) {
                continue;
            }
            State* st = Current(actor, nowHours);
            if (!st) {
                continue;
            }
            ScanActiveEffects(actor, id, *st, now);
            const Healer* healer = ActiveHealer(id, now);
            ConvertHealth(actor, id, *st, nowHours, healer != nullptr);
            ClearWhipIfDone(id, *st, true);

            const bool pained = HasOpenDamage(*st);
            if (auto [it, inserted] = g_pained.try_emplace(id, pained); !inserted) {
                if (it->second && !pained) {
                    PushEvent(id, kPainGone, healer, healer ? Percent(*healer, TotalPoints(*st)) : 0);
                    SKSE::log::info("pain gone {:08X}", id);
                }
                it->second = pained;
            }
            if (!AnyInjury(*st)) {
                SKSE::log::info("forget {:08X} (healed)", id);
                PushEvent(id, kFullyHealed, healer, 100);
                Erase(id);
                continue;
            }
            if (const auto it = g_healers.find(id); it != g_healers.end() && !healer) {
                const int percent = Percent(it->second, TotalPoints(*st));
                SKSE::log::info("heal stopped {:08X} {}%", id, percent);
                PushEvent(id, kHealStopped, &it->second, percent);
                g_healers.erase(it);
            }
        }

        std::vector<std::int32_t> codes;
        g_syncActors.clear();
        g_syncCasters.clear();
        for (const auto& e : g_events) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(e.actor);
            if (!actor) {
                continue;
            }
            codes.push_back(e.kind * 10000 + e.source * 1000 + e.percent);
            g_syncActors.push_back(actor);
            g_syncCasters.push_back(e.caster.get().get());
        }
        g_events.clear();
        return codes;
    }

    std::vector<RE::Actor*> SyncActors()
    {
        std::lock_guard lock(g_lock);
        return g_syncActors;
    }

    std::vector<RE::Actor*> SyncCasters()
    {
        std::lock_guard lock(g_lock);
        return g_syncCasters;
    }

    int TrackedCount()
    {
        std::lock_guard lock(g_lock);
        return static_cast<int>(g_actors.size() + g_events.size() + g_hits.size());
    }

    std::string PartName(int part)
    {
        if (part < 0 || part >= kPartCount) {
            return {};
        }
        return GetBody().partName[part];
    }

    std::string SourceId(int source)
    {
        const auto* s = InjuryConfig::Find(source);
        return s ? s->id : std::string{};
    }

    int SourceCount()
    {
        return InjuryConfig::SourceCount();
    }

    std::string StateJson(RE::Actor* actor)
    {
        nlohmann::json out = { { "sources", nlohmann::json::array() } };
        if (!actor) {
            return out.dump();
        }
        std::lock_guard lock(g_lock);
        const State* st = Current(actor, NowHours());
        if (!st) {
            return out.dump();
        }
        const auto& body = GetBody();
        auto vars = ActorVars(actor);
        for (int s = 0; s < InjuryConfig::SourceCount(); ++s) {
            const auto* conf = InjuryConfig::Find(s);
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < kPartCount; ++i) {
                int stage = -1;
                if (conf->model == "whip") {
                    if (st->whip[i].hit) {
                        stage = StageIndex(st->whip[i]);
                    }
                } else if (st->real[s][i] > 0) {
                    stage = DamageStage(st->real[s][i]);
                }
                if (stage < 0) {
                    continue;
                }
                const std::string& region = body.regionName[body.partRegion[i]];
                vars["part"] = body.partName[i];
                const std::string key = region + "." + std::to_string(stage);
                parts.push_back({ { "part", body.partName[i] }, { "region", region }, { "stage", StageName(s, stage) },
                    { "look", InjuryConfig::Render(InjuryConfig::Str(s, "text.looks." + key), vars) },
                    { "feel", InjuryConfig::Render(InjuryConfig::Str(s, "text.feels." + key), vars) } });
            }
            if (parts.empty()) {
                continue;
            }
            out["sources"].push_back({ { "id", conf->id },
                { "looks_heading", InjuryConfig::Str(s, "text.looks_heading") },
                { "feels_intro", InjuryConfig::Render(InjuryConfig::Str(s, "text.feels_intro"), vars) },
                { "feels_outro", InjuryConfig::Str(s, "text.feels_outro") },
                { "parts", parts } });
        }
        return out.dump();
    }

    std::string OverlayJson()
    {
        const int whip = WhipIndex();
        const auto& body = GetBody();
        nlohmann::json out = { { "title", InjuryConfig::Str(whip, "overlay.title", "Whip Damage") },
            { "max", body.damageMax * kPartCount }, { "healed", body.healingMax },
            { "medium", body.medium }, { "severe", body.severe }, { "mostly_healed", body.mostlyHealed },
            { "colors", whip >= 0 ? InjuryConfig::Json(whip, "overlay.colors") : nlohmann::json::object() },
            { "actors", nlohmann::json::array() } };
        if (whip < 0) {
            return out.dump();
        }
        std::lock_guard lock(g_lock);
        for (const auto& [id, st] : g_actors) {
            if (!AnyWhipHit(st.whip)) {
                continue;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor || !actor->Is3DLoaded() || actor->IsDead()) {
                continue;
            }
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < kPartCount; ++i) {
                const Part& p = st.whip[body.overlayOrder[i]];
                parts.push_back({ p.hit ? p.damage : 0, p.hit ? p.healing : body.healingMax });
            }
            out["actors"].push_back({ { "id", id }, { "name", NameOf(actor) }, { "replaced", st.replaced }, { "parts", parts } });
        }
        return out.dump();
    }

    std::string HitText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int mask, int level, int count)
    {
        const int source = InjuryConfig::IndexOf(sourceId);
        if (source < 0 || !victim) {
            return {};
        }
        auto vars = ActorVars(victim);
        std::string by = InjuryConfig::Str(source, "narration.hit.by_none");
        if (attacker && attacker != victim) {
            by = InjuryConfig::Render(InjuryConfig::Str(source, "narration.hit.by_attacker"), { { "attacker", NameOf(attacker) } });
        }
        std::string times;
        if (count == 2) {
            times = InjuryConfig::Str(source, "narration.hit.times.two");
        } else if (count > 4) {
            times = InjuryConfig::Str(source, "narration.hit.times.many");
        } else if (count > 1) {
            times = InjuryConfig::Str(source, "narration.hit.times.several");
        }
        auto lead = vars;
        lead["parts"] = PartsText(source, mask);
        lead["verb"] = InjuryConfig::Str(source, std::popcount(static_cast<unsigned>(mask)) > 1 ? "narration.hit.verb_many" : "narration.hit.verb_one");
        lead["times"] = times;
        lead["by"] = by;
        const std::string text = InjuryConfig::Render(InjuryConfig::Str(source, "narration.hit.lead"), lead);
        const std::string levelText = InjuryConfig::Render(
            InjuryConfig::Str(source, "narration.hit.levels." + std::to_string(std::clamp(level, 0, 3))), vars);
        return text + levelText;
    }

    std::string HealText(RE::Actor* victim, const std::vector<std::int32_t>& codes)
    {
        if (!victim) {
            return {};
        }
        // Latest stage per (source, part), in first-seen order; source / part heals once each.
        struct Stage
        {
            int source, part, stage;
        };
        std::vector<Stage> stages;
        std::vector<int> sourcesHealed;
        std::vector<int> partsHealed;
        for (const auto code : codes) {
            if (code >= kHealPartBase) {
                if (std::ranges::find(partsHealed, code - kHealPartBase) == partsHealed.end()) {
                    partsHealed.push_back(code - kHealPartBase);
                }
            } else if (code >= kHealSourceBase) {
                if (std::ranges::find(sourcesHealed, code - kHealSourceBase) == sourcesHealed.end()) {
                    sourcesHealed.push_back(code - kHealSourceBase);
                }
            } else if (code >= 0) {
                const int source = code / 64, part = (code / 8) % 8, stage = code % 8;
                auto it = std::ranges::find_if(stages, [&](const Stage& s) { return s.source == source && s.part == part; });
                if (it != stages.end()) {
                    it->stage = stage;
                } else {
                    stages.push_back({ source, part, stage });
                }
            }
        }
        auto vars = ActorVars(victim);
        std::string text;
        const auto add = [&](const std::string& sentence) {
            if (!sentence.empty()) {
                text += (text.empty() ? "" : " ") + sentence;
            }
        };
        const auto& body = GetBody();
        for (const auto& s : stages) {
            if (s.part < 0 || s.part >= kPartCount) {
                continue;
            }
            vars["part"] = body.partName[s.part];
            add(InjuryConfig::Render(InjuryConfig::Str(s.source, std::string("narration.heal.stage.") + StageName(s.source, s.stage)), vars));
        }
        for (const int source : sourcesHealed) {
            add(InjuryConfig::Render(InjuryConfig::Str(source, "narration.heal.source_healed"), vars));
        }
        const int common = InjuryConfig::GenericIndex() >= 0 ? InjuryConfig::GenericIndex() : WhipIndex();
        for (const int part : partsHealed) {
            if (part < 0 || part >= kPartCount) {
                continue;
            }
            vars["part"] = body.partName[part];
            add(InjuryConfig::Render(InjuryConfig::Str(common, "narration.heal.part_healed"), vars));
        }
        return text;
    }

    std::string SourceHealText(int kind, int healingSource, int percent, RE::Actor* caster, RE::Actor* target)
    {
        if (!target) {
            return {};
        }
        int conf;
        {
            std::lock_guard lock(g_lock);
            const auto it = g_actors.find(target->GetFormID());
            conf = HealEventsSource(it != g_actors.end() ? &it->second : nullptr);
        }
        auto vars = ActorVars(target);
        const std::string name = vars["name"];
        const std::string casterName = caster ? NameOf(caster) : std::string{};
        const bool named = !casterName.empty() && (healingSource == kSourceSpell || healingSource == kSourceStaff);
        const bool pron = named && caster == target;
        const bool casterCasts = healingSource == kSourceSpell && !casterName.empty();
        const char* nounKey = healingSource == kSourceSpell ? "spell" : healingSource == kSourceStaff ? "staff" : healingSource == kSourcePotion ? "potion" : "magic";
        vars["caster"] = casterName;
        vars["noun"] = InjuryConfig::Str(conf, std::string("narration.heal_events.nouns.") + nounKey, nounKey);
        vars["who"] = pron ? vars["reflexive"] : name;
        vars["obj"] = pron ? vars["him"] : name;
        vars["own"] = pron ? vars["his"] : name + "'s";
        vars["owner"] = named ? casterName + "'s" : "The";
        vars["owner_lc"] = named ? casterName + "'s" : "the";
        vars["progress"] = InjuryConfig::Str(conf, percent < 34 ? "narration.heal_events.progress.low"
                                                  : percent < 67 ? "narration.heal_events.progress.mid"
                                                                 : "narration.heal_events.progress.high");
        const char* key = "stopped";
        switch (kind) {
        case kHealStart:
            key = casterCasts ? "start.caster_casts" : "start.other";
            break;
        case kPainGone:
            key = healingSource == kSourceNone ? "pain_gone.none" : "pain_gone.source";
            break;
        case kFullyHealed:
            key = healingSource == kSourceNone ? "full.none" : casterCasts ? "full.caster_casts" : "full.other";
            break;
        default:
            break;
        }
        return InjuryConfig::Render(InjuryConfig::Str(conf, std::string("narration.heal_events.") + key), vars);
    }

    std::string ActionText(std::string_view sourceId, std::string_view path, RE::Actor* speaker, RE::Actor* target, int count)
    {
        const int source = InjuryConfig::IndexOf(sourceId);
        if (source < 0) {
            return {};
        }
        const char* times = count == 1 ? "once" : count == 2 ? "twice" : count > 4 ? "many" : "several";
        return InjuryConfig::Render(InjuryConfig::Str(source, path),
            { { "speaker", NameOf(speaker) }, { "target", NameOf(target) },
                { "times", InjuryConfig::Str(source, std::string("narration.summary.times.") + times) } });
    }

    void SetHoursPerPoint(float hours)
    {
        std::lock_guard lock(g_lock);
        g_hoursPerPoint = std::max(0.05f, hours);
    }

    void SetRegenRate(float percent)
    {
        std::lock_guard lock(g_lock);
        g_regenRate = std::clamp(percent, 0.0f, 100.0f);
    }

    void Forget(RE::Actor* actor)
    {
        if (!actor) {
            return;
        }
        std::lock_guard lock(g_lock);
        Erase(actor->GetFormID());
    }

    void Save(SKSE::SerializationInterface* intfc)
    {
        std::lock_guard lock(g_lock);
        if (!intfc->OpenRecord(kRecord, kRecordVersion)) {
            SKSE::log::error("co-save open failed");
            return;
        }
        intfc->WriteRecordData(static_cast<std::uint32_t>(g_actors.size()));
        for (const auto& [id, st] : g_actors) {
            intfc->WriteRecordData(id);
            for (const auto& p : st.whip) {
                intfc->WriteRecordData(p.hit);
                intfc->WriteRecordData(p.damage);
                intfc->WriteRecordData(p.healing);
                intfc->WriteRecordData(p.lastHours);
            }
            intfc->WriteRecordData(st.replaced);
            std::uint32_t sources = 0;
            for (int s = 0; s < kMaxSources; ++s) {
                if (RealOnSource(st, s) > 0 && InjuryConfig::Find(s)) {
                    ++sources;
                }
            }
            intfc->WriteRecordData(sources);
            for (int s = 0; s < kMaxSources; ++s) {
                if (RealOnSource(st, s) <= 0 || !InjuryConfig::Find(s)) {
                    continue;
                }
                const std::string& sid = InjuryConfig::Find(s)->id;
                intfc->WriteRecordData(static_cast<std::uint16_t>(sid.size()));
                intfc->WriteRecordData(sid.data(), static_cast<std::uint32_t>(sid.size()));
                for (const int d : st.real[s]) {
                    intfc->WriteRecordData(static_cast<std::int32_t>(d));
                }
            }
        }
    }

    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t)
    {
        if (version != kRecordVersion) {
            SKSE::log::warn("co-save version {} unsupported, skipped (old whip wounds are dropped)", version);
            return;
        }
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_events.clear();
        g_hits.clear();
        std::uint32_t count = 0;
        if (!intfc->ReadRecordData(count)) {
            return;
        }
        for (std::uint32_t a = 0; a < count; ++a) {
            RE::FormID oldId = 0;
            State st;
            if (!intfc->ReadRecordData(oldId)) {
                SKSE::log::error("co-save truncated");
                return;
            }
            for (auto& p : st.whip) {
                if (!intfc->ReadRecordData(p.hit) || !intfc->ReadRecordData(p.damage) ||
                    !intfc->ReadRecordData(p.healing) || !intfc->ReadRecordData(p.lastHours)) {
                    SKSE::log::error("co-save truncated");
                    return;
                }
            }
            std::uint32_t sources = 0;
            if (!intfc->ReadRecordData(st.replaced) || !intfc->ReadRecordData(sources)) {
                SKSE::log::error("co-save truncated");
                return;
            }
            for (std::uint32_t s = 0; s < sources; ++s) {
                std::uint16_t length = 0;
                if (!intfc->ReadRecordData(length)) {
                    SKSE::log::error("co-save truncated");
                    return;
                }
                std::string sid(length, '\0');
                if (length > 0 && !intfc->ReadRecordData(sid.data(), length)) {
                    SKSE::log::error("co-save truncated");
                    return;
                }
                std::array<std::int32_t, kPartCount> damage{};
                for (auto& d : damage) {
                    if (!intfc->ReadRecordData(d)) {
                        SKSE::log::error("co-save truncated");
                        return;
                    }
                }
                if (const int index = InjuryConfig::IndexOf(sid); index >= 0) {
                    st.real[index] = damage;
                } else {
                    SKSE::log::warn("co-save: injury source \"{}\" is not loaded, its damage is dropped", sid);
                }
            }
            for (int s = 0; s < kMaxSources; ++s) {
                for (int p = 0; p < kPartCount; ++p) {
                    if (st.real[s][p] > 0) {
                        MarkTouched(st, s, p);
                    }
                }
            }
            if (const int whip = WhipIndex(); whip >= 0) {
                for (int p = 0; p < kPartCount; ++p) {
                    if (st.whip[p].hit) {
                        MarkTouched(st, whip, p);
                    }
                }
            }
            RE::FormID id = 0;
            if (intfc->ResolveFormID(oldId, id) && AnyInjury(st)) {
                g_actors[id] = st;
            }
        }
        SKSE::log::info("co-save loaded {} actors", g_actors.size());
    }

    void Revert()
    {
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_events.clear();
        g_hits.clear();
        g_syncActors.clear();
        g_syncCasters.clear();
        g_hitVictims.clear();
        g_hitAttackers.clear();
    }
}
