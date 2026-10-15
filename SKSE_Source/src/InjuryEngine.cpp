#include "InjuryEngine.h"

#include "InjuryConfig.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
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
        constexpr int kOldHealingMax = 11;  // co-save v2..v4: a torture deficit was healing_max + damage
        constexpr double kSourceLinger = 1.0;  // real seconds a healing source stays active after last seen
        constexpr double kTortureBatchGap = 3.0;  // real seconds idle before the next tick is a new (attacker, victim, source) batch

        struct TortureBatchKey
        {
            RE::FormID victim = 0;
            RE::FormID attacker = 0;
            int source = 0;

            bool operator==(const TortureBatchKey&) const = default;
        };

        struct TortureBatchKeyHash
        {
            std::size_t operator()(const TortureBatchKey& key) const
            {
                return std::hash<RE::FormID>{}(key.victim) ^ (std::hash<RE::FormID>{}(key.attacker) << 1) ^
                       (static_cast<std::size_t>(key.source) << 2);
            }
        };

        double NowRealSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        // Torture model: torture points per (torture type, part), 0 .. part max. Damage raises them, healing
        // lowers them; the points give the stage (light / medium / severe) and, inverted, the heal level
        // (high / mid / low). A part's torture is the maximum of its types: that alone holds health down and
        // shows on the overlay. Healing a part lowers every type on it by one; real damage replacing torture
        // lowers the maximum by one and clamps the other types to it. A type at 0 on every part is cleared.
        struct Cell
        {
            bool hit = false;
            std::int32_t deficit = 0;  // torture points
        };
        using Torture = std::array<std::array<Cell, kPartCount>, kMaxSources>;       // [source][part]
        using Real = std::array<std::array<std::int32_t, kPartCount>, kMaxSources>;  // health model: [source][part] damage

        struct State
        {
            Torture torture{};
            std::array<double, kPartCount> lastHours{};  // game hours when time was last applied to the part
            Real real{};
            std::int32_t replaced = 0;  // torture damage points replaced by real damage (overlay)
            float carry = 0.0f;         // real damage (health) not yet a whole point
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

        struct HitEntry
        {
            RE::FormID victim = 0;
            RE::ActorHandle attacker;
            int source = 0;
            int mask = 0;
            int level = 0;
            bool fresh = false;
            bool unarmed = false;
            int count = 1;  // hits merged into this entry while it waited
        };

        // The last real (health) hit per victim: a death soon after it is that hit's killing blow.
        struct LastReal
        {
            RE::ActorHandle attacker;
            int source = 0;
            int part = -1;
            bool unarmed = false;
            double when = 0.0;  // real seconds
        };

        struct DeathEntry
        {
            RE::ActorHandle victim;
            RE::ActorHandle attacker;
            int source = 0;
            std::string text;
        };
        constexpr double kDeathWindow = 5.0;  // real seconds from the last real hit to the death (kill moves take a while)

        struct HealthLogEntry
        {
            int part = 0;
            int countBefore = 0;
            int level = 0;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, State> g_actors;
        std::unordered_map<RE::FormID, Healer> g_healers;
        std::unordered_map<RE::FormID, bool> g_pained;      // had damage at the last sync
        std::unordered_map<RE::FormID, float> g_regenBank;  // regen health not yet a whole point
        // Hooked torture damage (spell ticks) not yet a hit, per actor and source. Kept when the actor is
        // forgotten, so small ticks on an unwounded actor still add up.
        std::unordered_map<RE::FormID, std::array<float, kMaxSources>> g_tortureCarry;
        std::unordered_map<TortureBatchKey, double, TortureBatchKeyHash> g_tortureBatchLast;  // last tick real time per batch
        std::vector<Event> g_events;                        // not yet taken by SyncHealth
        std::vector<HitEntry> g_hits;                       // real hits not yet taken by Papyrus
        std::vector<RE::Actor*> g_syncActors;
        std::vector<RE::Actor*> g_syncCasters;
        std::vector<RE::Actor*> g_hitVictims;
        std::vector<RE::Actor*> g_hitAttackers;
        std::vector<bool> g_hitUnarmed;
        std::vector<int> g_hitCounts;
        std::unordered_map<RE::FormID, LastReal> g_lastReal;  // per victim, see LastReal
        std::vector<DeathEntry> g_deaths;                     // killing blows not yet taken by Papyrus
        std::vector<RE::Actor*> g_deathVictims;
        std::vector<RE::Actor*> g_deathAttackers;
        std::vector<std::string> g_deathSources;
        // Last release time per victim|attacker|source: TakeHits lets one hit per key through per kHitGap.
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_hitReleased;
        // Last VisitEffects scan per tracked actor (SyncHealth throttles to once per second).
        std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> g_lastScan;
        constexpr std::chrono::milliseconds kHitGap{ 1000 };

        // Queues a hit for narration. A queued hit of the same victim, attacker and source absorbs it (latest mask / level win).
        // Caller holds g_lock.
        void PushHit(const HitEntry& e)
        {
            for (auto& q : g_hits) {
                if (q.victim == e.victim && q.attacker == e.attacker && q.source == e.source) {
                    q.mask = e.mask;
                    q.level = e.level;
                    q.fresh = q.fresh || e.fresh;
                    q.unarmed = e.unarmed;
                    q.count += e.count;
                    return;
                }
            }
            if (g_hits.size() < 256) {
                g_hits.push_back(e);
            }
        }

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

        std::atomic<bool> g_sexualParts{ false };  // setting "include sexual parts"

        // Parts that new hits, prompts and the overlay use.
        int ActiveParts()
        {
            return g_sexualParts ? kPartCount : kCoreParts;
        }

        constexpr int kPartPussy = 7;
        constexpr int kPartPenis = 8;

        bool IsFemale(RE::Actor* actor)
        {
            const auto* base = actor ? actor->GetActorBase() : nullptr;
            return base && base->IsFemale();
        }

        // Only the genital part matching the actor's sex can be wounded.
        bool PartFits(bool female, int part)
        {
            return part != (female ? kPartPenis : kPartPussy);
        }

        // Random pick from candidate parts, weighted by `weight` (body.partWeight: small parts are hit less
        // often; a torture type's part_weights). Every weight 0: an even pick.
        int WeightedPart(const std::array<int, kPartCount>& candidates, int count, const std::array<float, kPartCount>& weight)
        {
            float total = 0.0f;
            for (int i = 0; i < count; ++i) {
                total += weight[candidates[i]];
            }
            if (total <= 0.0f) {
                return candidates[RandomIndex(count)];
            }
            float roll = std::uniform_real_distribution<float>(0.0f, total)(Rng());
            for (int i = 0; i < count; ++i) {
                roll -= weight[candidates[i]];
                if (roll < 0.0f && weight[candidates[i]] > 0.0f) {
                    return candidates[i];
                }
            }
            for (int i = count - 1; i >= 0; --i) {
                if (weight[candidates[i]] > 0.0f) {
                    return candidates[i];
                }
            }
            return candidates[count - 1];
        }

        // ---- stages and totals -------------------------------------------------------------------------

        int DamageStage(int damage, int part)
        {
            const auto& b = GetBody();
            if (damage >= b.partSevere[part]) return 2;
            if (damage >= b.partMedium[part]) return 1;
            if (damage >= 1) return 0;
            return -1;
        }

        // Heal level of a torture stage (0 light .. 2 severe): 0 high, 1 mid, 2 low.
        constexpr const char* kHealLevels[] = { "high", "mid", "low" };

        // The part's torture: the largest deficit of its types (0 when none is open).
        int PartTorture(const State& st, int part)
        {
            int top = 0;
            for (const auto& source : st.torture) {
                top = std::max(top, source[part].deficit);
            }
            return top;
        }

        // Torture points: the sum over parts of each part's maximum.
        int Deficit(const State& st)
        {
            int total = 0;
            for (int i = 0; i < kPartCount; ++i) {
                total += PartTorture(st, i);
            }
            return total;
        }

        int SourceDeficit(const State& st, int source)
        {
            int total = 0;
            for (const auto& c : st.torture[source]) {
                total += c.deficit;
            }
            return total;
        }

        bool SourceHit(const State& st, int source)
        {
            return std::ranges::any_of(st.torture[source], [](const Cell& c) { return c.hit; });
        }

        bool AnyTortureHit(const State& st)
        {
            for (int s = 0; s < kMaxSources; ++s) {
                if (SourceHit(st, s)) {
                    return true;
                }
            }
            return false;
        }

        // "whipping:14 slapping:3" for one part (debug log).
        std::string CellsText(const State& st, int part)
        {
            std::string text;
            for (int s = 0; s < kMaxSources; ++s) {
                if (const Cell& c = st.torture[s][part]; c.hit) {
                    const auto* conf = InjuryConfig::Find(s);
                    text += std::format("{}{}:{}", text.empty() ? "" : " ", conf ? conf->id : std::to_string(s), c.deficit);
                }
            }
            return text.empty() ? "none" : text;
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
            return PartTorture(st, part) + RealOnPart(st, part);
        }

        bool HasTorture(const State& st)
        {
            return Deficit(st) > 0;
        }

        bool HasOpenDamage(const State& st)
        {
            return Deficit(st) > 0 || RealTotal(st) > 0;
        }

        int TotalPoints(const State& st)
        {
            return Deficit(st) + RealTotal(st);
        }

        bool AnyInjury(const State& st)
        {
            return AnyTortureHit(st) || RealTotal(st) > 0;
        }

        // Damage points of every part an actor has (100% of max health); one genital part counted.
        int CapacityDamage()
        {
            const auto& b = GetBody();
            int total = 0;
            for (int i = 0; i < ActiveParts(); ++i) {
                total += b.partMax[i];
            }
            if (ActiveParts() > kPartPenis) {
                total -= std::max(b.partMax[kPartPussy], b.partMax[kPartPenis]);
            }
            return total;
        }

        float RatioTorture(float maxHealth)
        {
            const auto& b = GetBody();
            return b.woundShare * maxHealth / static_cast<float>(CapacityDamage());
        }

        float RatioReal(float maxHealth)
        {
            return maxHealth / static_cast<float>(CapacityDamage());
        }

        // Health the wounds hold health down to (torture never takes more than the floor allows).
        float WoundedLevel(const State& st, float maxHealth)
        {
            float level = maxHealth - Deficit(st) * RatioTorture(maxHealth) - RealTotal(st) * RatioReal(maxHealth);
            if (HasTorture(st)) {
                level = std::max(level, GetBody().floor * maxHealth);
            }
            return std::max(level, 0.0f);
        }

        // ---- torture model -----------------------------------------------------------------------------

        // Lowers every open type on `part` by `points` (healing: game time, a healing source).
        // Returns true if anything changed.
        bool HealTortureCells(State& st, int part, int points)
        {
            bool changed = false;
            for (auto& source : st.torture) {
                Cell& c = source[part];
                if (!c.hit || c.deficit <= 0) {
                    continue;
                }
                c.deficit = std::max(0, c.deficit - points);
                changed = true;
            }
            return changed;
        }

        // Spend elapsed heal points per part: every type on the part drops by the same amount.
        void Advance(State& st, double now)
        {
            const double step = g_hoursPerPoint;
            for (int i = 0; i < kPartCount; ++i) {
                double& last = st.lastHours[i];
                if (PartTorture(st, i) == 0 || now < last) {
                    last = now;
                    continue;
                }
                const int points = static_cast<int>(std::floor((now - last) / step));
                if (points <= 0) {
                    continue;
                }
                last += points * step;
                HealTortureCells(st, i, points);
            }
        }

        // One torture point healed on a random part that still needs it (every type there drops by one).
        // False when nothing is left. only >= 0: that part alone.
        bool HealTorturePoint(State& st, double now, int only = -1)
        {
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                if ((only < 0 || i == only) && PartTorture(st, i) > 0) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                return false;
            }
            const int index = open[RandomIndex(count)];
            HealTortureCells(st, index, 1);
            st.lastHours[index] = now;
            return true;
        }

        // The part still holds health down (some type has damage or missing healing points).
        bool TortureOpen(const State& st, int part)
        {
            return PartTorture(st, part) > 0;
        }

        // A real hit replaces one torture point on this part: the part's maximum drops by one and every type
        // above the new maximum is clamped to it. True if one was replaced.
        bool ReplaceTorturePoint(State& st, int part)
        {
            const int top = PartTorture(st, part);
            if (top <= 0) {
                return false;
            }
            ++st.replaced;
            for (auto& source : st.torture) {
                source[part].deficit = std::min(source[part].deficit, top - 1);
            }
            return true;
        }

        // Clears each torture type that has healed (or been replaced) on every part it touched.
        void ClearTortureIfDone(State& st)
        {
            for (int s = 0; s < kMaxSources; ++s) {
                if (!SourceHit(st, s) || SourceDeficit(st, s) > 0) {
                    continue;
                }
                st.torture[s].fill(Cell{});
            }
            if (!AnyTortureHit(st)) {
                st.replaced = 0;
            }
        }

        // One torture hit of `source` on `part` (-1 or a part the actor can't take: weighted random by the
        // type's part_weights, full parts skipped while another can take a point). Takes the health the part's new maximum holds down. Caller holds g_lock.
        // Returns the hit code; `index` gets the part.
        std::int32_t TortureHit(RE::Actor* actor, RE::Actor* attacker, State& st, int source, int part, double now, int& index, float amount);

        // ---- real (health model) -----------------------------------------------------------------------

        bool PartFull(const State& st, int part)
        {
            return PartDamage(st, part) >= GetBody().partMax[part];
        }

        // The part a real attack lands on: a weighted random tortured part that can still take a point
        // (converting torture damage frees room), else a weighted random part not at max damage; -1 when every
        // part is full.
        int PickPart(const State& st, bool female)
        {
            std::array<int, kPartCount> open{};
            int count = 0;
            for (int i = 0; i < kPartCount; ++i) {
                if (TortureOpen(st, i)) {
                    open[count++] = i;
                }
            }
            if (count == 0) {
                for (int i = 0; i < ActiveParts(); ++i) {
                    if (PartFits(female, i) && !PartFull(st, i)) {
                        open[count++] = i;
                    }
                }
            }
            return count > 0 ? WeightedPart(open, count, GetBody().partWeight) : -1;
        }

        // One real point healed on a random (source, part) that has damage. False when nothing is left.
        bool HealRealPoint(State& st)
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
            --st.real[source][part];
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
            g_healers.erase(id);
            g_pained.erase(id);
            g_regenBank.erase(id);
            g_lastScan.erase(id);
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
            Advance(st, now);
            if (actor->IsDead()) {
                SKSE::log::info("forget {:08X} (dead)", id);
                Erase(id);
                return nullptr;
            }
            ClearTortureIfDone(st);
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

        // A healing source touched an actor: refresh its session, or start one (queuing kHealStart). An unhurt actor
        // (st null or no points) still gets the start line: healing feels nice anyway.
        void Seen(RE::FormID id, const State* st, RE::Actor* caster, RE::MagicItem* item, double now)
        {
            if (const auto it = g_healers.find(id); it != g_healers.end()) {
                it->second.lastSeen = now;
                return;
            }
            const int points = st ? TotalPoints(*st) : 0;
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
            // The engine's own walk: on VR GetActiveEffectList() is only a temporary snapshot.
            // VisitEffects, not VisitActiveEffects: with ENABLE_SKYRIM_VR the latter calls the SE-only REL::ID(33756),
            // which is an unrelated function on AE and crashes.
            struct Visitor : RE::MagicTarget::ForEachActiveEffectVisitor
            {
                RE::FormID   id;
                const State& st;
                double       now;
                Visitor(RE::FormID a_id, const State& a_st, double a_now) : id(a_id), st(a_st), now(a_now) {}

                RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* ae) override
                {
                    using Flag = RE::ActiveEffect::Flag;
                    // Effects being torn down (short-lived torture / quiet spells) can hold stale pointers.
                    if (!ae || !ae->effect || !ae->spell || ae->flags.any(Flag::kInactive, Flag::kDispelled)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    const auto* mgef = ae->GetBaseObject();
                    if (mgef && IsHealing(mgef, ae->magnitude)) {
                        Seen(id, &st, ae->caster.get().get(), ae->spell, now);
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                }
            };
            SKSE::log::debug("scan effects {:08X}", id);
            Visitor visitor(id, st, now);
            target->VisitEffects(visitor);
        }

        // Health above the wounded level is healing: turn it into points on random parts (torture and real alike).
        // A healing source, or an actor without torture wounds, converts all of it; natural regen with torture wounds
        // only g_regenRate percent, the rest is taken back.
        void ConvertHealth(RE::Actor* actor, RE::FormID id, State& st, double now, bool sourceActive)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            if (maxHealth <= 0.0f) {
                return;
            }
            const float ratioT = RatioTorture(maxHealth);
            const float ratioR = RatioReal(maxHealth);
            const float excess = av->GetActorValue(RE::ActorValue::kHealth) - WoundedLevel(st, maxHealth);
            if (excess <= 0.0f) {
                return;
            }
            const bool fullSpeed = sourceActive || !HasTorture(st);
            float budget = excess;
            if (!fullSpeed) {
                float& bank = g_regenBank[id];
                bank += excess * g_regenRate / 100.0f;
                budget = bank;
            }
            float spent = 0.0f;
            while (true) {
                const bool torture = HasTorture(st);
                const bool real = RealTotal(st) > 0;
                if (!torture && !real) {
                    break;
                }
                const bool pickTorture = torture && (!real || RandomIndex(2) == 0);
                const float cost = pickTorture ? ratioT : ratioR;
                if (budget - spent < cost) {
                    break;
                }
                if (pickTorture ? !HealTorturePoint(st, now) : !HealRealPoint(st)) {
                    break;
                }
                spent += cost;
            }
            if (spent > 0.0f) {
                SKSE::log::debug("convert {:08X} excess:{:.2f} budget:{:.2f} spent:{:.2f} source:{} full_speed:{} points left:{}", id, excess,
                    budget, spent, sourceActive, fullSpeed, TotalPoints(st));
            }
            if (!fullSpeed) {
                g_regenBank[id] = std::max(0.0f, budget - spent);
                if (const float extra = excess - spent; extra > 0.0f) {
                    InternalScope scope;
                    av->DamageActorValue(RE::ActorValue::kHealth, extra);
                }
            }
        }

        // Health for torture points just added, never below the floor (torture can't kill).
        void TakeHealth(RE::Actor* actor, int points)
        {
            auto* av = actor->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            const float health = av->GetActorValue(RE::ActorValue::kHealth);
            const float floor = GetBody().floor * maxHealth;
            if (points <= 0 || maxHealth <= 0.0f || health <= floor) {
                SKSE::log::debug("take health {:08X} skipped: points:{} health:{:.1f} floor:{:.1f}", actor->GetFormID(), points, health, floor);
                return;
            }
            const float loss = std::min(points * RatioTorture(maxHealth), health - floor);
            SKSE::log::debug("take health {:08X} points:{} health:{:.1f} -> {:.1f} (max {:.1f}, floor {:.1f})", actor->GetFormID(), points,
                health, health - loss, maxHealth, floor);
            InternalScope scope;
            av->DamageActorValue(RE::ActorValue::kHealth, loss);
        }

        void NotifyPapyrus()
        {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent event{ "Injury_Engine_Injury", "", 0.0f, nullptr };
                source->SendEvent(&event);
            }
        }

        std::vector<std::int32_t> DrainPendingEvents()
        {
            std::vector<std::int32_t> codes;
            g_syncActors.clear();
            g_syncCasters.clear();
            for (const auto& e : g_events) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(e.actor);
                if (!actor) {
                    continue;
                }
                auto* caster = e.caster.get().get();
                SKSE::log::debug("sync event {:08X} kind:{} source:{} percent:{} caster:{:08X}", e.actor, e.kind, e.source, e.percent,
                    caster ? caster->GetFormID() : 0);
                codes.push_back(e.kind * 10000 + e.source * 1000 + e.percent);
                g_syncActors.push_back(actor);
                g_syncCasters.push_back(caster);
            }
            g_events.clear();
            return codes;
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

        std::string LogActor(RE::Actor* actor)
        {
            if (!actor) {
                return "Someone (00000000)";
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s (%08X)", NameOf(actor).c_str(), static_cast<std::uint32_t>(actor->GetFormID()));
            return buf;
        }

        const char* LogLevelName(int level)
        {
            switch (level) {
            case kLight:
                return "light";
            case kMedium:
                return "medium";
            case kSevere:
                return "severe";
            default:
                return "undamaged";
            }
        }

        std::string FormatHealthPercent(float health, float maxHealth)
        {
            if (maxHealth <= 0.0f) {
                return "0%";
            }
            const float pct = health / maxHealth * 100.0f;
            const float tenth = std::round(pct * 10.0f) / 10.0f;
            const float whole = std::round(tenth);
            if (std::fabs(tenth - whole) < 0.05f) {
                return std::format("{:.0f}%", whole);
            }
            return std::format("{:.1f}%", tenth);
        }

        void LogEvent(std::string_view category, const std::string& text)
        {
            if (text.empty()) {
                return;
            }
            if (auto logger = spdlog::get("event")) {
                logger->info("{}: {}", category, text);
            }
        }

        void LogInjuryPoint(std::string_view kind, RE::Actor* attacker, RE::Actor* victim, int source, int partIndex, int pointBefore,
            int pointAdded, int level, float amount, float healthAfter)
        {
            // One line per second per category + attacker + victim + source; the rest only count into skipped.
            int skipped = 0;
            {
                struct Throttle
                {
                    std::chrono::steady_clock::time_point last;
                    int skipped = 0;
                };
                static std::mutex throttleLock;
                static std::unordered_map<std::string, Throttle> throttles;
                const auto now = std::chrono::steady_clock::now();
                const std::string key = std::format("{}|{:08X}|{:08X}|{}", kind, attacker ? attacker->GetFormID() : 0u,
                    victim->GetFormID(), source);
                std::lock_guard lock(throttleLock);
                const auto it = throttles.find(key);
                if (it == throttles.end()) {
                    throttles[key] = { now, 0 };
                } else if (now - it->second.last < std::chrono::seconds(1)) {
                    ++it->second.skipped;
                    return;
                } else {
                    skipped = it->second.skipped;
                    it->second = { now, 0 };
                }
            }
            const auto* conf = InjuryConfig::Find(source);
            const auto& body = GetBody();
            const char* partName = partIndex >= 0 && partIndex < kPartCount ? body.partName[partIndex].c_str() : "?";
            auto* av = victim->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            // `health` is the engine's value for this point; `actual` is the victim's live game health right now.
            LogEvent(kind, std::format("{} \xe2\x9e\xa4 {} | {} {} point:{}+{} level:{} amount:{:.1f} health:{} actual:{:.1f}/{:.1f} skipped:{}",
                LogActor(attacker), LogActor(victim), conf ? conf->id : "?", partName, pointBefore, pointAdded, LogLevelName(level),
                amount, FormatHealthPercent(healthAfter, maxHealth), av->GetActorValue(RE::ActorValue::kHealth), maxHealth, skipped));
        }

        std::string NarrationForm(int source, bool unarmed)
        {
            if (unarmed) {
                const std::string alt = InjuryConfig::Str(source, "narration.form_unarmed");
                if (!alt.empty()) {
                    return alt;
                }
            }
            return InjuryConfig::Str(source, "narration.form");
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
            static const char* kFixed[] = { "light", "medium", "severe" };
            const auto* s = InjuryConfig::Find(source);
            if (s && stage >= 0 && stage < static_cast<int>(s->stages.size())) {
                return s->stages[stage].c_str();
            }
            return kFixed[std::clamp(stage, 0, 2)];
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

        // Papyrus interns "start" / "end" as "Start" / "End"; fold before session.json lookups.
        std::string FoldPhase(std::string_view phase)
        {
            std::string ph(phase);
            std::ranges::transform(ph, ph.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ph;
        }

        // narration.hit.levels often lead with a redundant clause (", the small flame..."); scene start keeps the rest.
        std::string LevelSensationTail(std::string_view levelText)
        {
            const std::string text(levelText);
            const auto dot = text.find(". ");
            return dot != std::string::npos ? text.substr(dot + 2) : text;
        }

        // session.json wording, overridden per source by its narration.session.<key> (falling: "{target} hits {form}").
        std::string SceneStr(int source, const std::string& key, std::string_view fallback = {})
        {
            const std::string own = InjuryConfig::Str(source, "narration.session." + key);
            return own.empty() ? InjuryConfig::SessionStr(key, fallback) : own;
        }

        // One attacker, one victim. The words come from session.json (hit.* / clause / inline.*) and the
        // source's narration.form and narration.hit.levels. Short events and the scene's start / again line share this.
        std::string AttackLine(int source, RE::Actor* attacker, RE::Actor* victim, int mask, int level, bool again, bool unarmed,
            bool sceneStart = false)
        {
            const std::string phase = again ? "again" : "start";
            auto vars = ActorVars(victim);
            vars["target"] = NameOf(victim);
            vars["attackers"] = NameOf(attacker);
            vars["form"] = NarrationForm(source, unarmed);
            vars["parts"] = PartsText(source, mask);
            const std::string what = SceneStr(source, std::string("hit.") + phase,
                again ? "hits {target} again and again with {form}" : "hits {target} with {form}");
            vars["what"] = InjuryConfig::Render(what, vars);
            const std::string sentence = InjuryConfig::Render(SceneStr(source, "clause", "{attackers} {what}"), vars);
            const std::string feeling = InjuryConfig::Render(
                InjuryConfig::Str(source, "narration.hit.levels." + std::to_string(std::clamp(level, 0, 3))), ActorVars(victim));
            if (sceneStart && !again) {
                return sentence + ", " + LevelSensationTail(feeling);
            }
            const std::string tail = InjuryConfig::Render(
                InjuryConfig::SessionStr(std::string("inline.") + phase,
                    again ? ", the last one hitting {target}'s {parts}" : " hitting {target}'s {parts}"),
                vars);
            return sentence + tail + feeling;
        }

        // The source whose heal_events wording fits the actor: torture-only wounds use the torture type with the
        // most left (whipping when they are all healed), anything else generic's.
        int HealEventsSource(const State* st)
        {
            const int whip = WhipIndex();
            const int generic = InjuryConfig::GenericIndex();
            if (st && RealTotal(*st) == 0 && AnyTortureHit(*st)) {
                int best = -1;
                int most = 0;
                for (int s = 0; s < kMaxSources; ++s) {
                    if (const int left = SourceDeficit(*st, s); SourceHit(*st, s) && InjuryConfig::Find(s) && (best < 0 || left > most)) {
                        best = s;
                        most = left;
                    }
                }
                if (best >= 0 && (most > 0 || whip < 0)) {
                    return best;
                }
                if (whip >= 0) {
                    return whip;
                }
            }
            return generic >= 0 ? generic : whip;
        }

        std::int32_t TortureHit(RE::Actor* actor, RE::Actor* attacker, State& st, int source, int part, double now, int& index, float amount)
        {
            const auto& body = GetBody();
            const RE::FormID id = actor->GetFormID();
            const bool fresh = !HasOpenDamage(st);
            const int deficitBefore = Deficit(st);

            // Candidates: the active parts the actor has that this type lands on (part_weights above 0);
            // a type that lands on none of them uses the body's weights.
            std::array<int, kPartCount> candidates{};
            int count = 0;
            const bool female = IsFemale(actor);
            const auto* conf = InjuryConfig::Find(source);
            const auto* weight = conf ? &conf->partWeight : &body.partWeight;
            // Full parts are skipped while any part can still take a point (a point there adds nothing).
            for (int i = 0; i < ActiveParts(); ++i) {
                if (PartFits(female, i) && (*weight)[i] > 0.0f && !PartFull(st, i)) {
                    candidates[count++] = i;
                }
            }
            if (count == 0) {
                for (int i = 0; i < ActiveParts(); ++i) {
                    if (PartFits(female, i) && (*weight)[i] > 0.0f) {
                        candidates[count++] = i;
                    }
                }
            }
            if (count == 0) {
                weight = &body.partWeight;
                for (int i = 0; i < ActiveParts(); ++i) {
                    if (PartFits(female, i)) {
                        candidates[count++] = i;
                    }
                }
            }
            // A requested part the actor can't take (sexual parts off, other sex) falls back to random.
            const bool wanted = part >= 0 && part < ActiveParts() && PartFits(female, part);
            index = wanted ? part : WeightedPart(candidates, count, *weight);

            const int top = PartTorture(st, index);
            // The part's state before the hit (any torture type): 0 undamaged .. 3 severe.
            const int level = DamageStage(top, index) + 1;
            Cell& c = st.torture[source][index];
            const int pointBefore = c.hit ? c.deficit : 0;
            if (top <= 0) {
                st.lastHours[index] = now;
            }
            c.hit = true;
            c.deficit = std::min(body.partMax[index], std::max(0, c.deficit) + 1);
            // A hit ends the victim's heal session (no event: Papyrus closes its side on the hit).
            g_healers.erase(id);
            g_pained[id] = true;
            TakeHealth(actor, Deficit(st) - deficitBefore);
            const int pointAdded = c.deficit - pointBefore;
            const float healthAfter = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            LogInjuryPoint("torture", attacker, actor, source, index, pointBefore, pointAdded, level, amount, healthAfter);
            SKSE::log::debug("hit {:08X} {} part:{} level:{} types:{} fresh:{} deficit:{} -> {}", id, conf ? conf->id : "?",
                body.partName[index], level, CellsText(st, index), fresh, deficitBefore, Deficit(st));
            return EncodeHit(fresh, source, 1 << index, level);
        }
    }

    void LogLine(std::string_view kind, std::string_view narrationType, std::string_view sessionType, std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        if (auto logger = spdlog::get("event")) {
            logger->info("{}:{}:{} {}", kind, narrationType, sessionType, text);
        }
    }

    bool IsInternalDamage()
    {
        return g_internal > 0;
    }

    void RestoreHealth(RE::Actor* actor, float amount)
    {
        if (!actor || amount <= 0.0f || actor->IsDead()) {
            return;
        }
        InternalScope scope;
        actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, amount);
    }

    namespace
    {
        std::int32_t HitPoints(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId, int part, bool configPoints)
        {
            const int source = InjuryConfig::IndexOf(sourceId);
            if (!actor || actor->IsDead() || !InjuryConfig::IsTorture(source)) {
                return -1;
            }
            std::lock_guard lock(g_lock);
            const double now = NowHours();
            State* existing = Current(actor, now);
            State& st = existing ? *existing : g_actors[actor->GetFormID()];
            int index = -1;
            const std::int32_t code = TortureHit(actor, attacker, st, source, part, now, index, 0.0f);
            // hook.points_per_hit: the rest of the points land on the part the first one picked
            const auto* conf = InjuryConfig::Find(source);
            for (int p = 1; configPoints && conf && p < conf->pointsPerHit; ++p) {
                TortureHit(actor, attacker, st, source, index, now, index, 0.0f);
            }
            return code;
        }
    }

    std::int32_t Hit(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId)
    {
        return HitPoints(actor, attacker, sourceId, -1, true);
    }

    std::int32_t HitPart(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId, int part)
    {
        return HitPoints(actor, attacker, sourceId, part, false);
    }

    std::int32_t RealHit(RE::Actor* actor, RE::Actor* attacker, std::string_view base, int points)
    {
        const int source = InjuryConfig::IndexOf(base, false);
        if (!actor || actor->IsDead() || source < 0 || points < 1) {
            return -1;
        }
        auto* av = actor->AsActorValueOwner();
        const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
        if (maxHealth <= 0.0f) {
            return -1;
        }
        // the same path as hooked damage: points worth of health, torture wounds absorb their share first
        const float amount = points * RatioReal(maxHealth);
        const float pass = OnDamage(actor, attacker, amount, source, false);
        SKSE::log::debug("real hit {:08X} {} points:{} amount:{:.1f} pass:{:.1f}", actor->GetFormID(), InjuryConfig::Find(source)->id, points,
            amount, pass);
        if (pass > 0.0f) {
            InternalScope scope;
            av->DamageActorValue(RE::ActorValue::kHealth, pass);
        }
        return source;
    }

    namespace
    {
        // Hooked torture damage (out-of-combat punches, "(torture)" spells): the game applies none
        // of it. A melee hit is hook.points_per_hit points on one part; spell damage lands hook.points_per_second
        // points per real second whatever the spell's own damage.
        float OnTortureDamage(RE::Actor* victim, RE::Actor* attacker, float amount, const InjuryConfig::Source& conf, int source,
            bool physicalMelee)
        {
            auto* av = victim->AsActorValueOwner();
            const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
            if (maxHealth <= 0.0f) {
                return amount;
            }
            {
                std::lock_guard lock(g_lock);
                const RE::FormID id = victim->GetFormID();
                const TortureBatchKey batchKey{ id, attacker ? attacker->GetFormID() : 0, source };
                const double nowReal = NowRealSeconds();
                bool newBatch = true;
                double elapsed = 0.0;
                if (const auto batchIt = g_tortureBatchLast.find(batchKey); batchIt != g_tortureBatchLast.end()) {
                    elapsed = nowReal - batchIt->second;
                    newBatch = elapsed > kTortureBatchGap;
                }
                g_tortureBatchLast[batchKey] = nowReal;
                if (g_tortureBatchLast.size() > 512) {
                    g_tortureBatchLast.clear();
                    g_tortureBatchLast[batchKey] = nowReal;
                }

                int hits = 1;
                if (!physicalMelee) {
                    float& carry = g_tortureCarry[id][source];
                    if (newBatch) {
                        // The first tick of a beam always lands one wound so narration can start.
                        carry = 0.0f;
                        hits = 1;
                    } else {
                        carry += static_cast<float>(std::min(elapsed, 0.5)) * conf.pointsPerSecond;
                        hits = std::min(static_cast<int>(carry), 8);
                        carry -= static_cast<float>(hits);
                        if (hits == 0) {
                            return 0.0f;
                        }
                    }
                }
                const double now = NowHours();
                State* existing = Current(victim, now);
                State& st = existing ? *existing : g_actors[id];
                int mask = 0;
                int level = 0;
                int lastPart = -1;
                bool fresh = false;
                for (int i = 0; i < hits; ++i) {
                    int index = -1;
                    std::int32_t code = TortureHit(victim, attacker, st, source, -1, now, index, amount);
                    if (i == 0) {
                        fresh = code >= 100000000;
                    }
                    // a melee hit is several points on the same part
                    for (int p = 1; physicalMelee && p < conf.pointsPerHit; ++p) {
                        const std::int32_t more = TortureHit(victim, attacker, st, source, index, now, index, amount);
                        level = std::max(level, static_cast<int>(more % 10));
                    }
                    lastPart = index;
                    mask |= 1 << index;
                    level = std::max(level, static_cast<int>(code % 10));
                }
                {
                    HitEntry e;
                    e.victim = id;
                    if (attacker) {
                        e.attacker = attacker->GetHandle();
                    }
                    e.source = source;
                    e.mask = mask;
                    e.level = level;
                    e.fresh = fresh;
                    e.unarmed = false;
                    e.count = hits;
                    PushHit(e);
                }
            }
            NotifyPapyrus();
            return 0.0f;
        }
    }

    float OnDamage(RE::Actor* victim, RE::Actor* attacker, float amount, int source, bool physicalMelee, bool unarmedForm)
    {
        const auto* conf = InjuryConfig::Find(source);
        if (!victim || amount <= 0.0f || !conf || victim->IsDead()) {
            return amount;
        }
        if (conf->IsTorture()) {
            return OnTortureDamage(victim, attacker, amount, *conf, source, physicalMelee);
        }
        if (conf->model != "health") {
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
            const bool female = IsFemale(victim);
            const float ratioT = RatioTorture(maxHealth);
            const float ratioR = RatioReal(maxHealth);

            // One attack lands on one part (tortured parts first). Torture points there hold health down: the
            // attack replaces them first (health is not lowered by that part; the part's maximum drops and the
            // other types are clamped to it) and their health becomes real points on the same part. The attack
            // stays on that part until it is full, then moves to another one.
            int part = PickPart(st, female);
            const int firstPart = part;
            int lastPart = firstPart;
            float left = amount;
            float absorbed = 0.0f;
            int replacedNow = 0;
            int points = 0;
            int mask = 0;
            int level = 0;
            std::vector<HealthLogEntry> healthLogs;
            while (left > 1e-4f && part >= 0) {
                if (TortureOpen(st, part) && left >= ratioT - 1e-3f) {
                    ReplaceTorturePoint(st, part);
                    left -= ratioT;
                    absorbed += ratioT;
                    st.carry += ratioT;
                    ++replacedNow;
                } else if (!PartFull(st, part)) {
                    const float take = std::min(left, std::max(0.0f, ratioR - st.carry));
                    left -= take;
                    st.carry += take;
                } else {
                    part = PickPart(st, female);
                    continue;
                }
                while (st.carry >= ratioR - 1e-4f) {
                    if (PartFull(st, part)) {
                        part = PickPart(st, female);
                        if (part < 0) {
                            break;
                        }
                    }
                    st.carry -= ratioR;
                    // the part's stage before the point lands: 0 undamaged .. 3 severe
                    const int pointLevel = DamageStage(PartDamage(st, part), part) + 1;
                    level = std::max(level, pointLevel);
                    const int countBefore = st.real[source][part];
                    ++st.real[source][part];
                    healthLogs.push_back({ part, countBefore, pointLevel });
                    lastPart = part;
                    mask |= 1 << part;
                    ++points;
                }
            }
            if (part < 0) {
                // every part is full: the rest is dropped
                st.carry = std::min(st.carry, ratioR);
            }
            st.carry = std::max(st.carry, 0.0f);
            absorbed = std::min(absorbed, amount);
            pass = amount - absorbed;
            ClearTortureIfDone(st);
            const char* firstName = firstPart >= 0 ? GetBody().partName[firstPart].c_str() : "none";

            g_pained[id] = true;
            g_healers.erase(id);  // real damage ends the heal session too (silently)
            if (g_lastReal.size() > 512) {
                g_lastReal.clear();
            }
            g_lastReal[id] = { attacker ? attacker->GetHandle() : RE::ActorHandle{}, source, lastPart, unarmedForm, NowSeconds() };
            if (mask == 0) {
                SKSE::log::debug("damage {:08X} {} part:{} amount:{:.1f} absorbed:{:.1f} torture_replaced:{}: no new point (carry {:.2f})", id,
                    conf->id, firstName, amount, absorbed, replacedNow, st.carry);
            }
            if (mask != 0) {
                {
                    HitEntry e;
                    e.victim = id;
                    if (attacker) {
                        e.attacker = attacker->GetHandle();
                    }
                    e.source = source;
                    e.mask = mask;
                    e.level = level;
                    e.fresh = fresh;
                    e.unarmed = unarmedForm;
                    PushHit(e);
                }
                const float currentHealth = av->GetActorValue(RE::ActorValue::kHealth);
                const float finalHealth = currentHealth - pass;
                for (std::size_t i = 0; i < healthLogs.size(); ++i) {
                    const auto& entry = healthLogs[i];
                    const float healthAfter = i + 1 == healthLogs.size() ? finalHealth : currentHealth - ratioR * static_cast<float>(i + 1);
                    LogInjuryPoint("health", attacker, victim, source, entry.part, entry.countBefore, 1, entry.level, ratioR, healthAfter);
                }
                if (replacedNow > 0 && firstPart >= 0) {
                    SKSE::log::debug("damage {:08X} {} types after replacement: {}", id, firstName, CellsText(st, firstPart));
                }
            }
            if (!AnyInjury(st)) {
                g_actors.erase(id);
            }
        }
        NotifyPapyrus();
        return pass;
    }

    std::int32_t HealPart(RE::Actor* actor, int part, float health)
    {
        if (!actor || actor->IsDead() || health <= 0.0f) {
            return -1;
        }
        auto* av = actor->AsActorValueOwner();
        const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
        if (maxHealth <= 0.0f) {
            return -1;
        }
        int healedPoints = 0;
        {
            std::lock_guard lock(g_lock);
            const double now = NowHours();
            const RE::FormID id = actor->GetFormID();
            State* st = Current(actor, now);
            if (!st) {
                return 0;
            }
            const int points = std::max(1, static_cast<int>(std::floor(health / RatioTorture(maxHealth) + 1e-3f)));
            for (int i = 0; i < points; ++i) {
                if (!HealTorturePoint(*st, now, part) && !(part >= 0 && HealTorturePoint(*st, now, -1))) {
                    break;
                }
                ++healedPoints;
            }
            ClearTortureIfDone(*st);
            if (healedPoints > 0) {
                // Raise health with the wounded level so SyncHealth sees no excess to convert again.
                InternalScope internal;
                av->RestoreActorValue(RE::ActorValue::kHealth, healedPoints * RatioTorture(maxHealth));
            }
            SKSE::log::info("heal part {:08X} part:{} health:{:.1f} points:{}", id, part, health, healedPoints);
        }
        if (healedPoints > 0) {
            NotifyPapyrus();
        }
        return healedPoints;
    }

    int PartIndex(std::string_view name)
    {
        if (name.empty()) {
            return -1;
        }
        const auto& b = GetBody();
        for (int i = 0; i < kPartCount; ++i) {
            if (InjuryConfig::IEquals(b.partName[i], name)) {
                return i;
            }
        }
        // Regions: the first part in that region ("head" -> face).
        for (int i = 0; i < kPartCount; ++i) {
            const int region = b.partRegion[i];
            if (region >= 0 && region < static_cast<int>(b.regionName.size()) && InjuryConfig::IEquals(b.regionName[region], name)) {
                return i;
            }
        }
        return -1;
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

    bool IsHealing(RE::Actor* actor)
    {
        if (!actor) {
            return false;
        }
        std::lock_guard lock(g_lock);
        return ActiveHealer(actor->GetFormID(), NowSeconds()) != nullptr;
    }

    std::vector<std::int32_t> TakeHits()
    {
        std::lock_guard lock(g_lock);
        std::vector<std::int32_t> codes;
        // an empty take must not wipe the arrays another Papyrus thread is still reading
        if (g_hits.empty()) {
            return codes;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto keyOf = [](const HitEntry& e) {
            return std::format("{:08X}|{:08X}|{}", e.victim, e.attacker.native_handle(), e.source);
        };
        // One hit per victim, attacker and source per kHitGap; the rest wait (merged) in g_hits.
        std::vector<bool> release(g_hits.size(), false);
        bool any = false;
        for (std::size_t i = 0; i < g_hits.size(); ++i) {
            const auto it = g_hitReleased.find(keyOf(g_hits[i]));
            if (it == g_hitReleased.end() || now - it->second >= kHitGap) {
                release[i] = true;
                any = true;
            }
        }
        if (!any) {
            return codes;
        }
        // Real damage clears the victim's torture scene: do not hold that victim's other hits behind it.
        for (std::size_t i = 0; i < g_hits.size(); ++i) {
            const auto* conf = InjuryConfig::Find(g_hits[i].source);
            if (release[i] && conf && !conf->IsTorture()) {
                for (std::size_t j = 0; j < g_hits.size(); ++j) {
                    if (g_hits[j].victim == g_hits[i].victim) {
                        release[j] = true;
                    }
                }
            }
        }
        g_hitVictims.clear();
        g_hitAttackers.clear();
        g_hitUnarmed.clear();
        g_hitCounts.clear();
        std::vector<HitEntry> held;
        for (std::size_t i = 0; i < g_hits.size(); ++i) {
            const auto& e = g_hits[i];
            if (!release[i]) {
                held.push_back(e);
                continue;
            }
            g_hitReleased[keyOf(e)] = now;
            auto* victim = RE::TESForm::LookupByID<RE::Actor>(e.victim);
            if (!victim) {
                SKSE::log::debug("take hits: victim {:08X} gone, hit dropped", e.victim);
                continue;
            }
            SKSE::log::debug("take hits: {:08X} {} mask:{} level:{} fresh:{} count:{}", e.victim, SourceId(e.source), e.mask, e.level,
                e.fresh, e.count);
            codes.push_back(EncodeHit(e.fresh, e.source, e.mask, e.level));
            g_hitVictims.push_back(victim);
            g_hitAttackers.push_back(e.attacker.get().get());
            g_hitUnarmed.push_back(e.unarmed);
            g_hitCounts.push_back(e.count);
        }
        g_hits = std::move(held);
        for (auto it = g_hitReleased.begin(); it != g_hitReleased.end();) {
            it = now - it->second > std::chrono::seconds(10) ? g_hitReleased.erase(it) : std::next(it);
        }
        return codes;
    }

    void QueueHit(RE::Actor* victim, RE::Actor* attacker, std::int32_t code)
    {
        if (!victim || code < 0) {
            return;
        }
        HitEntry e;
        e.victim = victim->GetFormID();
        if (attacker) {
            e.attacker = attacker->GetHandle();
        }
        e.level = code % 10;
        e.mask = (code / 10) % 100000;
        e.source = (code / 1000000) % 100;
        e.fresh = code >= 100000000;
        std::lock_guard lock(g_lock);
        PushHit(e);
    }

    int HitCount(std::size_t index)
    {
        std::lock_guard lock(g_lock);
        return index < g_hitCounts.size() ? g_hitCounts[index] : 1;
    }

    bool HitUnarmed(std::size_t index)
    {
        std::lock_guard lock(g_lock);
        return index < g_hitUnarmed.size() && g_hitUnarmed[index];
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
        if (target->IsDead()) {
            return;
        }
        const RE::FormID id = target->GetFormID();
        const auto it = g_actors.find(id);
        SKSE::log::debug("healing effect on {:08X} item:{:08X} caster:{:08X} magnitude:{:.1f} tracked:{}", id, item ? item->GetFormID() : 0,
            caster ? caster->GetFormID() : 0, effect->effectItem.magnitude, it != g_actors.end());
        Seen(id, it != g_actors.end() ? &it->second : nullptr, caster ? caster->As<RE::Actor>() : nullptr, item, NowSeconds());
        NotifyPapyrus();
    }

    std::vector<std::int32_t> TakeSyncEvents()
    {
        std::lock_guard lock(g_lock);
        return DrainPendingEvents();
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
            bool scanQueued = false;
            if (!actor->IsDead() && !actor->IsDisabled() && !actor->IsDeleted()) {
                const auto tick = std::chrono::steady_clock::now();
                auto& last = g_lastScan[id];
                if (tick - last >= std::chrono::seconds(1)) {
                    last = tick;
                    scanQueued = true;
                    SKSE::GetTaskInterface()->AddTask([id]() {
                        std::lock_guard taskLock(g_lock);
                        auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
                        const auto found = g_actors.find(id);
                        if (!a || found == g_actors.end() || a->IsDead() || a->IsDisabled() || a->IsDeleted() || !a->Is3DLoaded()) {
                            return;
                        }
                        ScanActiveEffects(a, id, found->second, NowSeconds());
                    });
                }
            }
            const Healer* healer = ActiveHealer(id, now);
            ConvertHealth(actor, id, *st, nowHours, healer != nullptr);
            ClearTortureIfDone(*st);

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
            // A scan queued this pass may refresh lastSeen on the game thread; do not stop the session yet.
            if (const auto it = g_healers.find(id); it != g_healers.end() && !healer && !scanQueued) {
                const int percent = Percent(it->second, TotalPoints(*st));
                SKSE::log::info("heal stopped {:08X} {}%", id, percent);
                PushEvent(id, kHealStopped, &it->second, percent);
                g_healers.erase(it);
            }
        }
        // Heal sessions on unhurt (untracked) actors end silently once the source is gone.
        for (auto it = g_healers.begin(); it != g_healers.end();) {
            it = !g_actors.contains(it->first) && now - it->second.lastSeen >= kSourceLinger ? g_healers.erase(it) : std::next(it);
        }

        return DrainPendingEvents();
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
        // Torture feelings: text.feels.torture.<region>.<stage>, or during a heal session text.feels.heal.<region>.<level>.
        const bool healing = ActiveHealer(actor->GetFormID(), NowSeconds()) != nullptr;
        for (int s = 0; s < InjuryConfig::SourceCount(); ++s) {
            const auto* conf = InjuryConfig::Find(s);
            const bool torture = conf->IsTorture();
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < ActiveParts(); ++i) {
                const int stage = DamageStage(torture ? st->torture[s][i].deficit : st->real[s][i], i);
                if (stage < 0) {
                    continue;
                }
                const std::string& region = body.regionName[body.partRegion[i]];
                vars["part"] = body.partName[i];
                const std::string key = region + "." + std::to_string(stage);
                const std::string feels = !torture ? "text.feels." : healing ? "text.feels.heal." : "text.feels.torture.";
                parts.push_back({ { "part", body.partName[i] }, { "region", region },
                    { "stage", torture && healing ? kHealLevels[stage] : StageName(s, stage) },
                    { "look", InjuryConfig::Render(InjuryConfig::Str(s, "text.looks." + key), vars) },
                    { "feel", InjuryConfig::Render(InjuryConfig::Str(s, feels + key), vars) } });
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
        const int active = ActiveParts();
        nlohmann::json partMax = nlohmann::json::array();
        for (int i = 0; i < active; ++i) {
            partMax.push_back(body.partMax[body.overlayOrder[i]]);
        }
        nlohmann::json out = { { "title", InjuryConfig::Str(whip, "overlay.title", "Torture Damage") },
            { "max", CapacityDamage() }, { "part_max", partMax }, { "medium", body.medium }, { "severe", body.severe },
            { "colors", whip >= 0 ? InjuryConfig::Json(whip, "overlay.colors") : nlohmann::json::object() },
            { "actors", nlohmann::json::array() } };
        std::lock_guard lock(g_lock);
        for (const auto& [id, st] : g_actors) {
            if (!AnyTortureHit(st)) {
                continue;
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor || !actor->Is3DLoaded() || actor->IsDead()) {
                continue;
            }
            // Each part shows only its worst torture type's points.
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < active; ++i) {
                parts.push_back(PartTorture(st, body.overlayOrder[i]));
            }
            const bool healing = ActiveHealer(id, NowSeconds()) != nullptr;
            out["actors"].push_back(
                { { "id", id }, { "name", NameOf(actor) }, { "replaced", st.replaced }, { "healing", healing }, { "parts", parts } });
        }
        return out.dump();
    }

    std::string HitText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int mask, int level, int count, bool noCause,
        bool unarmedForm)
    {
        const int source = InjuryConfig::IndexOf(sourceId);
        if (source < 0 || !victim) {
            return {};
        }
        const std::string levelText = LevelText(sourceId, victim, level);
        const std::string parts = PartsText(source, mask);
        if (!noCause && attacker && attacker != victim) {
            return AttackLine(source, attacker, victim, mask, level, count > 1, unarmedForm);
        }
        auto vars = ActorVars(victim);
        std::string by = noCause ? std::string() : InjuryConfig::Str(source, "narration.hit.by_none");
        if (!noCause && attacker && attacker != victim) {
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
        lead["parts"] = parts;
        lead["verb"] = InjuryConfig::Str(source, std::popcount(static_cast<unsigned>(mask)) > 1 ? "narration.hit.verb_many" : "narration.hit.verb_one");
        lead["times"] = times;
        lead["by"] = by;
        const std::string text = InjuryConfig::Render(InjuryConfig::Str(source, "narration.hit.lead"), lead);
        return text + levelText;
    }

    std::string LevelText(std::string_view sourceId, RE::Actor* victim, int level)
    {
        const int source = InjuryConfig::IndexOf(sourceId);
        if (source < 0 || !victim) {
            return {};
        }
        return InjuryConfig::Render(
            InjuryConfig::Str(source, "narration.hit.levels." + std::to_string(std::clamp(level, 0, 3))), ActorVars(victim));
    }

    std::string DeathText(int source, RE::Actor* attacker, RE::Actor* victim, int part, bool unarmedForm)
    {
        if (!victim || !InjuryConfig::Find(source)) {
            return {};
        }
        const auto& body = GetBody();
        if (part < 0 || part >= kPartCount) {
            part = RandomIndex(kCoreParts);
        }
        // death text exists for the main regions only: a sexual part dies as the nearest one (lips: head, else body)
        std::string region = "body";
        if (part < kCoreParts) {
            region = body.regionName[body.partRegion[part]];
        } else if (body.partName[part] == "lips") {
            region = "head";
        }
        const nlohmann::json lines = InjuryConfig::Json(source, "death.regions." + region);
        if (!lines.is_array() || lines.empty() || !lines[0].is_string()) {
            return {};
        }
        auto vars = ActorVars(victim);
        vars["part"] = body.partName[part];
        vars["form"] = NarrationForm(source, unarmedForm);
        const bool named = attacker && attacker != victim;
        vars["attacker"] = NameOf(attacker);
        std::string text = InjuryConfig::Render(InjuryConfig::Str(source, named ? "death.lead" : "death.lead_none"), vars);
        const auto& pick = lines[RandomIndex(static_cast<int>(lines.size()))];
        text += InjuryConfig::Render(pick.is_string() ? pick.get<std::string>() : std::string(), vars);
        const nlohmann::json soil = InjuryConfig::Json(source, "death.soil");
        const float chance = InjuryConfig::Number(source, "death.soil_chance", 0.0f);
        if (soil.is_array() && !soil.empty() && std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng()) < chance) {
            const auto& extra = soil[RandomIndex(static_cast<int>(soil.size()))];
            if (extra.is_string()) {
                text += " " + InjuryConfig::Render(extra.get<std::string>(), vars);
            }
        }
        return text;
    }

    std::string DeathText(std::string_view sourceId, RE::Actor* attacker, RE::Actor* victim, int part)
    {
        return DeathText(InjuryConfig::IndexOf(sourceId), attacker, victim, part, false);
    }

    namespace
    {
        // A death within kDeathWindow of the victim's last real hit: that hit was the killing blow. TESDeathEvent
        // comes twice (dying, then dead); the first one takes the record, so it is narrated once.
        class DeathSink : public RE::BSTEventSink<RE::TESDeathEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* event, RE::BSTEventSource<RE::TESDeathEvent>*) override
            {
                auto* victim = event && event->actorDying ? event->actorDying->As<RE::Actor>() : nullptr;
                if (!victim) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                LastReal rec;
                {
                    std::lock_guard lock(g_lock);
                    // torture must never kill: say so even without verbose logging
                    if (const auto st = g_actors.find(victim->GetFormID()); !event->dead && st != g_actors.end() && HasTorture(st->second)) {
                        const auto last = g_lastReal.find(victim->GetFormID());
                        auto* killer = event->actorKiller ? event->actorKiller->As<RE::Actor>() : nullptr;
                        SKSE::log::info("death {} under torture, killer {}, last real hit: {}", LogActor(victim), LogActor(killer),
                            last != g_lastReal.end() ? std::format("{} {:.1f}s ago", SourceId(last->second.source), NowSeconds() - last->second.when)
                                                     : std::string("none"));
                    }
                    const auto it = g_lastReal.find(victim->GetFormID());
                    if (it == g_lastReal.end()) {
                        return RE::BSEventNotifyControl::kContinue;
                    }
                    rec = it->second;
                    g_lastReal.erase(it);
                }
                const double age = NowSeconds() - rec.when;
                const auto* conf = InjuryConfig::Find(rec.source);
                if (age > kDeathWindow || !conf || conf->IsTorture()) {
                    SKSE::log::debug("death {:08X}: last real hit {:.1f}s old, no killing blow", victim->GetFormID(), age);
                    return RE::BSEventNotifyControl::kContinue;
                }
                RE::Actor* attacker = rec.attacker.get().get();
                if (!attacker && event->actorKiller) {
                    attacker = event->actorKiller->As<RE::Actor>();
                }
                std::string text = DeathText(rec.source, attacker, victim, rec.part, rec.unarmed);
                SKSE::log::info("death {} by {}: {} part:{} text:{}", LogActor(victim), LogActor(attacker), conf->id,
                    rec.part >= 0 ? GetBody().partName[rec.part] : "none", text.empty() ? "(none)" : "yes");
                if (text.empty()) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                {
                    std::lock_guard lock(g_lock);
                    if (g_deaths.size() < 64) {
                        g_deaths.push_back({ victim->GetHandle(), attacker ? attacker->GetHandle() : RE::ActorHandle{}, rec.source,
                            std::move(text) });
                    }
                }
                NotifyPapyrus();
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void InitDeaths()
    {
        static DeathSink sink;
        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESDeathEvent>(&sink);
            SKSE::log::info("InjuryEngine: death event sink registered");
        }
    }

    std::vector<std::string> TakeDeaths()
    {
        std::lock_guard lock(g_lock);
        std::vector<std::string> texts;
        // an empty take must not wipe the arrays another Papyrus thread is still reading
        if (g_deaths.empty()) {
            return texts;
        }
        g_deathVictims.clear();
        g_deathAttackers.clear();
        g_deathSources.clear();
        for (auto& d : g_deaths) {
            auto* victim = d.victim.get().get();
            if (!victim) {
                continue;
            }
            texts.push_back(d.text);
            g_deathVictims.push_back(victim);
            g_deathAttackers.push_back(d.attacker.get().get());
            g_deathSources.push_back(SourceId(d.source));
        }
        g_deaths.clear();
        return texts;
    }

    std::vector<RE::Actor*> DeathVictims()
    {
        std::lock_guard lock(g_lock);
        return g_deathVictims;
    }

    std::vector<RE::Actor*> DeathAttackers()
    {
        std::lock_guard lock(g_lock);
        return g_deathAttackers;
    }

    std::vector<std::string> DeathSources()
    {
        std::lock_guard lock(g_lock);
        return g_deathSources;
    }

    std::string HealMapText(RE::Actor* victim)
    {
        if (!victim) {
            return {};
        }
        // (source, level) -> parts at that heal level, in first-seen part order.
        struct Group
        {
            int source, level, mask;
        };
        std::vector<Group> groups;
        {
            std::lock_guard lock(g_lock);
            const auto it = g_actors.find(victim->GetFormID());
            if (it == g_actors.end()) {
                return {};
            }
            const State& st = it->second;
            for (int i = 0; i < ActiveParts(); ++i) {
                int top = -1;
                int points = 0;
                for (int s = 0; s < kMaxSources; ++s) {
                    if (const Cell& c = st.torture[s][i]; c.hit && c.deficit > points) {
                        top = s;
                        points = c.deficit;
                    }
                }
                if (top < 0) {
                    continue;
                }
                const int level = DamageStage(points, i);
                auto g = std::ranges::find_if(groups, [&](const Group& x) { return x.source == top && x.level == level; });
                if (g == groups.end()) {
                    groups.push_back({ top, level, 1 << i });
                } else {
                    g->mask |= 1 << i;
                }
            }
        }
        auto vars = ActorVars(victim);
        std::string text;
        for (const auto& g : groups) {
            vars["parts"] = PartsText(g.source, g.mask);
            const std::string sentence =
                InjuryConfig::Render(InjuryConfig::Str(g.source, std::string("narration.heal.level.") + kHealLevels[g.level]), vars);
            if (!sentence.empty()) {
                text += (text.empty() ? "" : " ") + sentence;
            }
        }
        return text;
    }

    std::string SourceHealText(int kind, int healingSource, int percent, RE::Actor* caster, RE::Actor* target)
    {
        if (!target) {
            return {};
        }
        int conf;
        bool unhurt;
        {
            std::lock_guard lock(g_lock);
            const auto it = g_actors.find(target->GetFormID());
            conf = HealEventsSource(it != g_actors.end() ? &it->second : nullptr);
            unhurt = it == g_actors.end() || TotalPoints(it->second) <= 0;
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
            if (unhurt) {
                const char* soft = casterCasts ? "start_unhurt.caster_casts" : "start_unhurt.other";
                if (const auto text = InjuryConfig::Str(conf, std::string("narration.heal_events.") + soft); !text.empty()) {
                    return InjuryConfig::Render(text, vars);
                }
            }
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

    std::string SceneText(std::string_view phase, const std::vector<RE::Actor*>& attackers, const std::vector<RE::Actor*>& victims,
        const std::vector<std::string>& sources, const std::vector<int>& masks, const std::vector<int>& levels)
    {
        const std::size_t n = std::min({ attackers.size(), victims.size(), sources.size(), masks.size(), levels.size() });
        struct Pair
        {
            RE::Actor* attacker;
            RE::Actor* victim;
            int source, mask, level;
        };
        std::vector<Pair> pairs;
        for (std::size_t i = 0; i < n; ++i) {
            const int source = InjuryConfig::IndexOf(sources[i]);
            if (source >= 0 && victims[i]) {
                pairs.push_back({ attackers[i], victims[i], source, masks[i], levels[i] });
            }
        }
        if (pairs.empty()) {
            return {};
        }
        const std::string ph = FoldPhase(phase);
        if (ph != "start" && ph != "again" && ph != "switch") {
            SKSE::log::warn("scene text: unknown phase \"{}\"", phase);
            return {};
        }
        if (pairs.size() == 1 && pairs[0].attacker && pairs[0].attacker != pairs[0].victim && (ph == "start" || ph == "again")) {
            const auto& p = pairs[0];
            std::string text = AttackLine(p.source, p.attacker, p.victim, p.mask, p.level, ph == "again", false, ph == "start");
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
            if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') {
                text += '.';
            }
            return text;
        }
        const auto sess = [](int source, const std::string& key) { return InjuryConfig::Str(source, "narration.session." + key); };
        const auto join = [](const std::vector<std::string>& names) {
            std::string out;
            for (std::size_t i = 0; i < names.size(); ++i) {
                out += i == 0 ? names[i]
                              : (i + 1 == names.size() ? InjuryConfig::SessionStr("and", " and ") : InjuryConfig::SessionStr("comma", ", ")) + names[i];
            }
            return out;
        };
        const auto finish = [](std::string text) {
            while (!text.empty() && text.back() == ' ') {
                text.pop_back();
            }
            if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') {
                text += '.';
            }
            return text;
        };
        const auto hasAttacker = [](const Pair& p) { return p.attacker && p.attacker != p.victim; };
        const auto feeling = [&](const Pair& p) {
            auto vars = ActorVars(p.victim);
            vars["target"] = NameOf(p.victim);
            vars["attacker"] = NameOf(p.attacker);
            vars["parts"] = PartsText(p.source, p.mask);
            vars["noun"] = sess(p.source, "hit_noun");
            return InjuryConfig::Render(InjuryConfig::SessionStr(hasAttacker(p) ? "feeling" : "feeling_none"), vars) +
                   LevelText(InjuryConfig::Find(p.source)->id, p.victim, p.level);
        };
        const auto inlineHit = [&](const Pair& p) {
            auto vars = ActorVars(p.victim);
            vars["target"] = NameOf(p.victim);
            vars["parts"] = PartsText(p.source, p.mask);
            return InjuryConfig::Render(InjuryConfig::SessionStr("inline." + ph), vars) +
                   LevelText(InjuryConfig::Find(p.source)->id, p.victim, p.level);
        };

        std::vector<std::string> sentences;
        if (ph == "switch") {
            // one clause per pair: "Bob switches to punching"
            for (const auto& p : pairs) {
                auto vars = ActorVars(p.victim);
                vars["target"] = NameOf(p.victim);
                sentences.push_back(NameOf(p.attacker) + " " + InjuryConfig::Render(sess(p.source, "switch"), vars));
            }
        } else {
            std::vector<RE::Actor*> order;  // victims, first seen first
            for (const auto& p : pairs) {
                if (std::ranges::find(order, p.victim) == order.end()) {
                    order.push_back(p.victim);
                }
            }
            for (auto* victim : order) {
                std::vector<const Pair*> group;
                std::vector<const Pair*> alone;  // no attacker: its own clause each
                for (const auto& p : pairs) {
                    if (p.victim == victim) {
                        (hasAttacker(p) ? group : alone).push_back(&p);
                    }
                }
                const std::string target = NameOf(victim);
                if (!group.empty()) {
                    std::vector<std::string> names;
                    bool same = true;
                    for (const auto* p : group) {
                        names.push_back(NameOf(p->attacker));
                        same = same && p->source == group.front()->source;
                    }
                    const bool many = group.size() > 1;
                    const int first = group.front()->source;
                    std::string what;
                    if (same) {
                        what = SceneStr(first, std::string("hit.") + ph + (many ? "_many" : ""));
                    } else {
                        what = InjuryConfig::SessionStr("mixed_hit." + ph);
                    }
                    auto vars = ActorVars(victim);
                    vars["target"] = target;
                    vars["attackers"] = join(names);
                    if (same) {
                        vars["form"] = NarrationForm(first, false);
                    }
                    vars["what"] = InjuryConfig::Render(what, vars);
                    sentences.push_back(InjuryConfig::Render(
                        same ? SceneStr(first, "clause") : InjuryConfig::SessionStr("clause"), vars));
                }
                for (const auto* p : alone) {
                    auto vars = ActorVars(victim);
                    vars["target"] = target;
                    vars["form"] = NarrationForm(p->source, false);
                    sentences.push_back(target + " " + InjuryConfig::Render(SceneStr(p->source, "no_attacker_hit." + ph), vars));
                }
            }
        }
        if (pairs.size() == 1 && sentences.size() == 1) {
            return finish(sentences.front() + inlineHit(pairs.front()));
        }
        std::string out;
        for (const auto& sentence : sentences) {
            out += finish(sentence) + " ";
        }
        for (const auto& p : pairs) {
            out += finish(inlineHit(p)) + " ";
        }
        return finish(out);
    }

    void SetIncludeSexualParts(bool enabled)
    {
        g_sexualParts = enabled;
        SKSE::log::debug("include sexual parts {}", enabled);
    }

    void SetHoursPerPoint(float hours)
    {
        std::lock_guard lock(g_lock);
        g_hoursPerPoint = std::max(0.05f, hours);
        SKSE::log::debug("hours per point {:.2f}", g_hoursPerPoint);
    }

    void SetRegenRate(float percent)
    {
        std::lock_guard lock(g_lock);
        g_regenRate = std::clamp(percent, 0.0f, 100.0f);
        SKSE::log::debug("regen rate {:.1f}%", g_regenRate);
    }

    void Forget(RE::Actor* actor)
    {
        if (!actor) {
            return;
        }
        std::lock_guard lock(g_lock);
        Erase(actor->GetFormID());
    }

    namespace
    {
        void WriteId(SKSE::SerializationInterface* intfc, const std::string& sid)
        {
            intfc->WriteRecordData(static_cast<std::uint16_t>(sid.size()));
            intfc->WriteRecordData(sid.data(), static_cast<std::uint32_t>(sid.size()));
        }

        bool ReadId(SKSE::SerializationInterface* intfc, std::string& sid)
        {
            std::uint16_t length = 0;
            if (!intfc->ReadRecordData(length)) {
                return false;
            }
            sid.assign(length, '\0');
            return length == 0 || intfc->ReadRecordData(sid.data(), length);
        }

        // Real damage per source id: count, then {id, damage x parts}.
        bool ReadReal(SKSE::SerializationInterface* intfc, State& st, int parts)
        {
            std::uint32_t sources = 0;
            if (!intfc->ReadRecordData(sources)) {
                return false;
            }
            for (std::uint32_t s = 0; s < sources; ++s) {
                std::string sid;
                if (!ReadId(intfc, sid)) {
                    return false;
                }
                std::array<std::int32_t, kPartCount> damage{};
                for (int i = 0; i < parts; ++i) {
                    if (!intfc->ReadRecordData(damage[i])) {
                        return false;
                    }
                }
                if (const int index = InjuryConfig::IndexOf(sid); index >= 0 && !InjuryConfig::IsTorture(index)) {
                    st.real[index] = damage;
                } else {
                    SKSE::log::warn("co-save: injury source \"{}\" is not loaded, its damage is dropped", sid);
                }
            }
            return true;
        }

        // v2 / v3: whipping as {hit, damage, healing, lastHours} per part, then replaced. Only the damage is kept.
        bool ReadOldWhip(SKSE::SerializationInterface* intfc, State& st, int parts)
        {
            const int whip = WhipIndex();
            for (int i = 0; i < parts; ++i) {
                bool hit = false;
                std::int32_t damage = 0;
                std::int32_t healing = 0;
                double lastHours = 0.0;
                if (!intfc->ReadRecordData(hit) || !intfc->ReadRecordData(damage) || !intfc->ReadRecordData(healing) ||
                    !intfc->ReadRecordData(lastHours)) {
                    return false;
                }
                st.lastHours[i] = lastHours;
                if (hit && whip >= 0 && damage > 0) {
                    st.torture[whip][i] = { true, std::min(damage, GetBody().partMax[i]) };
                }
            }
            return intfc->ReadRecordData(st.replaced);
        }

        // v4 / v5: lastHours per part, replaced, then torture per source id: count, {id, {hit, deficit} x parts}.
        // v4 deficits were healing_max + damage: only the damage is kept.
        bool ReadTorture(SKSE::SerializationInterface* intfc, State& st, std::uint32_t version)
        {
            for (int i = 0; i < kPartCount; ++i) {
                if (!intfc->ReadRecordData(st.lastHours[i])) {
                    return false;
                }
            }
            std::uint32_t sources = 0;
            if (!intfc->ReadRecordData(st.replaced) || !intfc->ReadRecordData(sources)) {
                return false;
            }
            for (std::uint32_t s = 0; s < sources; ++s) {
                std::string sid;
                if (!ReadId(intfc, sid)) {
                    return false;
                }
                std::array<Cell, kPartCount> cells{};
                for (int i = 0; i < kPartCount; ++i) {
                    Cell& c = cells[i];
                    if (!intfc->ReadRecordData(c.hit) || !intfc->ReadRecordData(c.deficit)) {
                        return false;
                    }
                    if (version < 5) {
                        c.deficit = std::max(0, c.deficit - kOldHealingMax);
                    }
                    c.deficit = std::min(c.deficit, GetBody().partMax[i]);
                    c.hit = c.hit && c.deficit > 0;
                }
                if (const int index = InjuryConfig::IndexOf(sid); index >= 0 && InjuryConfig::IsTorture(index)) {
                    st.torture[index] = cells;
                } else {
                    SKSE::log::warn("co-save: torture type \"{}\" is not loaded, its wounds are dropped", sid);
                }
            }
            return true;
        }
    }

    void Save(SKSE::SerializationInterface* intfc)
    {
        std::lock_guard lock(g_lock);
        if (!intfc->OpenRecord(kRecord, kRecordVersion)) {
            SKSE::log::error("co-save open failed");
            return;
        }
        intfc->WriteRecordData(static_cast<std::uint32_t>(g_actors.size()));
        SKSE::log::debug("co-save: saving {} actors", g_actors.size());
        for (const auto& [id, st] : g_actors) {
            SKSE::log::debug("co-save: {:08X} torture deficit:{} real points:{}", id, Deficit(st), RealTotal(st));
            intfc->WriteRecordData(id);
            for (const double last : st.lastHours) {
                intfc->WriteRecordData(last);
            }
            intfc->WriteRecordData(st.replaced);
            std::uint32_t torture = 0;
            for (int s = 0; s < kMaxSources; ++s) {
                if (SourceHit(st, s) && InjuryConfig::Find(s)) {
                    ++torture;
                }
            }
            intfc->WriteRecordData(torture);
            for (int s = 0; s < kMaxSources; ++s) {
                if (!SourceHit(st, s) || !InjuryConfig::Find(s)) {
                    continue;
                }
                WriteId(intfc, InjuryConfig::Find(s)->id);
                for (const Cell& c : st.torture[s]) {
                    intfc->WriteRecordData(c.hit);
                    intfc->WriteRecordData(c.deficit);
                }
            }
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
                WriteId(intfc, InjuryConfig::Find(s)->id);
                for (const int d : st.real[s]) {
                    intfc->WriteRecordData(static_cast<std::int32_t>(d));
                }
            }
        }
    }

    void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t)
    {
        if (version != kRecordVersion && version != 4 && version != 3 && version != 2) {
            SKSE::log::warn("co-save version {} unsupported, skipped (old wounds are dropped)", version);
            return;
        }
        const int parts = version == 2 ? kCoreParts : kPartCount;  // v2: 6 parts, sexual parts start empty
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_tortureCarry.clear();
        g_tortureBatchLast.clear();
        g_events.clear();
        g_hits.clear();
        g_lastReal.clear();
        g_deaths.clear();
        g_lastScan.clear();
        std::uint32_t count = 0;
        if (!intfc->ReadRecordData(count)) {
            return;
        }
        for (std::uint32_t a = 0; a < count; ++a) {
            RE::FormID oldId = 0;
            State st;
            const bool ok = intfc->ReadRecordData(oldId) &&
                            (version >= 4 ? ReadTorture(intfc, st, version) : ReadOldWhip(intfc, st, parts)) &&
                            ReadReal(intfc, st, parts);
            if (!ok) {
                SKSE::log::error("co-save truncated");
                return;
            }
            ClearTortureIfDone(st);
            RE::FormID id = 0;
            if (intfc->ResolveFormID(oldId, id) && AnyInjury(st)) {
                g_actors[id] = st;
            }
        }
        SKSE::log::info("co-save loaded {} actors (record version {})", g_actors.size(), version);
    }

    void Revert()
    {
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_tortureCarry.clear();
        g_tortureBatchLast.clear();
        g_events.clear();
        g_hits.clear();
        g_hitReleased.clear();
        g_lastScan.clear();
        g_syncActors.clear();
        g_syncCasters.clear();
        g_hitVictims.clear();
        g_hitAttackers.clear();
        g_lastReal.clear();
        g_deaths.clear();
        g_deathVictims.clear();
        g_deathAttackers.clear();
        g_deathSources.clear();
    }
}
