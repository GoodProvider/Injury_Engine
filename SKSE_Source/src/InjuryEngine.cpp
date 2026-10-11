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
        constexpr int kStageHealed = 5;
        constexpr int kStageSourceHealed = 6;  // HealItem only: the source has healed on every part
        constexpr int kStagePartHealed = 7;    // HealItem only: the part has no injury left
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

        // Sparks / Flames / Frostbite (torture) only; vanilla elemental spells stay health damage.
        bool IsElementalTortureSpell(int source)
        {
            const auto* conf = InjuryConfig::Find(source);
            if (!conf || !conf->IsTorture()) {
                return false;
            }
            return InjuryConfig::IEquals(conf->id, "fire") || InjuryConfig::IEquals(conf->id, "frost") ||
                   InjuryConfig::IEquals(conf->id, "shock");
        }

        // Torture model: one deficit per (torture type, part) = damage + missing healing points, so
        // healingMax+1 .. healingMax+partMax is damage (light, medium, severe), 1 .. healingMax is healing
        // (recovering, mostly healed) and 0 on a hit cell is healed (scar). A part's torture is the maximum of
        // its types: that alone holds health down and shows on the overlay. Healing a part lowers every type on
        // it by one; real damage replacing torture lowers the maximum by one and clamps the other types to it.
        struct Cell
        {
            bool hit = false;
            std::int32_t deficit = 0;
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
            bool passive = true;  // queued without an active healing source (game time, natural regen)
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
        };

        struct HealthLogEntry
        {
            int part = 0;
            int countBefore = 0;
            int level = 0;
        };

        std::mutex g_lock;
        std::unordered_map<RE::FormID, State> g_actors;
        std::unordered_map<RE::FormID, std::vector<HealItem>> g_healed;  // not yet taken by Papyrus. Transient.
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
        float g_hoursPerPoint = 1.0f;
        float g_regenRate = 10.0f;
        thread_local int g_internal = 0;  // >0 while the engine itself changes health
        thread_local int g_sourceHealing = 0;  // >0 while a healing source (spell, potion, ...) heals points

        struct InternalScope
        {
            InternalScope() { ++g_internal; }
            ~InternalScope() { --g_internal; }
        };

        struct SourceHealingScope
        {
            explicit SourceHealingScope(bool active) : active(active) { g_sourceHealing += active; }
            ~SourceHealingScope() { g_sourceHealing -= active; }
            int active;
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

        // Stage of a torture deficit: 0..2 damage, 3 recovering, 4 mostly healed, 5 healed.
        int TortureStage(int deficit, int part)
        {
            const auto& b = GetBody();
            if (deficit > b.healingMax) {
                return DamageStage(deficit - b.healingMax, part);
            }
            const int healing = b.healingMax - std::max(0, deficit);
            if (healing >= b.healingMax) return kStageHealed;
            if (healing >= b.mostlyHealed) return 4;
            return 3;
        }

        // The part's torture: the largest deficit of its types (0 when none is open).
        int PartTorture(const State& st, int part)
        {
            int top = 0;
            for (const auto& source : st.torture) {
                top = std::max(top, source[part].deficit);
            }
            return top;
        }

        // Torture damage (the part's maximum above healingMax) on one part.
        int PartTortureDamage(const State& st, int part)
        {
            return std::max(0, PartTorture(st, part) - GetBody().healingMax);
        }

        bool PartTortureHit(const State& st, int part)
        {
            return std::ranges::any_of(st.torture, [part](const auto& source) { return source[part].hit; });
        }

        // Damage plus missing healing points: the sum over parts of each part's maximum.
        int Deficit(const State& st)
        {
            int total = 0;
            for (int i = 0; i < kPartCount; ++i) {
                total += PartTorture(st, i);
            }
            return total;
        }

        int TortureDamage(const State& st)
        {
            int total = 0;
            for (int i = 0; i < kPartCount; ++i) {
                total += PartTortureDamage(st, i);
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
            return PartTortureDamage(st, part) + RealOnPart(st, part);
        }

        bool HasTorture(const State& st)
        {
            return Deficit(st) > 0;
        }

        bool HasOpenDamage(const State& st)
        {
            return TortureDamage(st) > 0 || RealTotal(st) > 0;
        }

        int TotalPoints(const State& st)
        {
            return Deficit(st) + RealTotal(st);
        }

        bool AnyInjury(const State& st)
        {
            return AnyTortureHit(st) || RealTotal(st) > 0;
        }

        bool PartClear(const State& st, int part)
        {
            return PartTorture(st, part) == 0 && RealOnPart(st, part) == 0;
        }

        // Health per torture point / per real point.
        // Parts an actor can be wounded on (one genital part each).
        int BodyParts()
        {
            return ActiveParts() > kPartPenis ? ActiveParts() - 1 : ActiveParts();
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
            return b.woundShare * maxHealth / static_cast<float>(CapacityDamage() + BodyParts() * b.healingMax);
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

        void MarkTouched(State& st, int source, int part)
        {
            st.partSources[part] |= 1u << source;
            st.sourceParts[source] |= 1u << part;
        }

        // ---- heal queue --------------------------------------------------------------------------------

        // Latest stage per (source, part) wins; source / part heal items are kept once.
        // An item is passive only while no healing source has contributed to it.
        void QueueHealed(RE::FormID id, int source, int part, int stage)
        {
            const bool passive = g_sourceHealing == 0;
            SKSE::log::debug("queue healed {:08X} source:{} part:{} stage:{} passive:{}", id, SourceId(source), part, stage, passive);
            auto& items = g_healed[id];
            for (auto& item : items) {
                const bool same = stage == kStageSourceHealed ? item.stage == stage && item.source == source
                                  : stage == kStagePartHealed ? item.stage == stage && item.part == part
                                                              : item.stage < kStageSourceHealed && item.source == source && item.part == part;
                if (same) {
                    item.stage = stage;
                    item.passive = item.passive && passive;
                    return;
                }
            }
            items.push_back({ source, part, stage, passive });
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

        // ---- torture model -----------------------------------------------------------------------------

        // Lowers every open type on `part` by `points` (healing: game time, a healing source); queues the
        // stage changes. Returns true if anything changed.
        bool HealTortureCells(RE::FormID id, State& st, int part, int points)
        {
            bool changed = false;
            bool healed = false;
            for (int s = 0; s < kMaxSources; ++s) {
                Cell& c = st.torture[s][part];
                if (!c.hit || c.deficit <= 0) {
                    continue;
                }
                const int before = TortureStage(c.deficit, part);
                c.deficit = std::max(0, c.deficit - points);
                changed = true;
                if (const int after = TortureStage(c.deficit, part); after != before) {
                    QueueHealed(id, s, part, after);
                    healed = healed || after == kStageHealed;
                }
            }
            if (healed) {
                AfterHeal(id, st, part);
            }
            return changed;
        }

        // Spend elapsed heal points per part: every type on the part drops by the same amount.
        void Advance(RE::FormID id, State& st, double now)
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
                HealTortureCells(id, st, i, points);
            }
        }

        // One torture point healed on a random part that still needs it (every type there drops by one).
        // False when nothing is left. only >= 0: that part alone.
        bool HealTorturePoint(RE::FormID id, State& st, double now, int only = -1)
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
            HealTortureCells(id, st, index, 1);
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
            if (top > GetBody().healingMax) {
                ++st.replaced;
            }
            for (auto& source : st.torture) {
                source[part].deficit = std::min(source[part].deficit, top - 1);
            }
            return true;
        }

        // Clears each torture type that has healed (or been replaced) on every part it touched.
        // queue: narrate "source healed" (not for replacement).
        void ClearTortureIfDone(RE::FormID id, State& st, bool queue)
        {
            for (int s = 0; s < kMaxSources; ++s) {
                if (!SourceHit(st, s) || SourceDeficit(st, s) > 0) {
                    continue;
                }
                if (queue && std::popcount(st.sourceParts[s]) >= 2) {
                    QueueHealed(id, s, 0, kStageSourceHealed);
                }
                st.sourceParts[s] = 0;
                for (int i = 0; i < kPartCount; ++i) {
                    if (st.torture[s][i].hit) {
                        st.partSources[i] &= ~(1u << s);
                    }
                    st.torture[s][i] = Cell{};
                }
            }
            if (!AnyTortureHit(st)) {
                st.replaced = 0;
            }
        }

        // One torture hit of `source` on `part` (-1 or a part the actor can't take: weighted random by the
        // type's part_weights). Takes the health the part's new maximum holds down. Caller holds g_lock.
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
                if (TortureOpen(st, i) && (!PartFull(st, i) || PartTortureDamage(st, i) > 0)) {
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

        void AddRealPoint(State& st, int source, int part)
        {
            ++st.real[source][part];
            MarkTouched(st, source, part);
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
            const int before = DamageStage(st.real[source][part], part);
            --st.real[source][part];
            const int after = DamageStage(st.real[source][part], part);
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
            {
                SourceHealingScope scope(ActiveHealer(id, NowSeconds()) != nullptr);
                ClearTortureIfDone(id, st, true);
            }
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
                SKSE::log::debug("healing source on {:08X} ignored: no points left", id);
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
            // The engine's own walk: on VR GetActiveEffectList() is only a temporary snapshot.
            using Flag = RE::ActiveEffect::Flag;
            target->VisitActiveEffects([&](RE::ActiveEffect* ae) {
                if (ae && !ae->flags.any(Flag::kInactive, Flag::kDispelled) && IsHealing(ae->GetBaseObject(), ae->magnitude)) {
                    Seen(id, st, ae->caster.get().get(), ae->spell, now);
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
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
            SourceHealingScope sourceScope(sourceActive);
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
                if (pickTorture ? !HealTorturePoint(id, st, now) : !HealRealPoint(id, st)) {
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
                SKSE::ModCallbackEvent event{ "SkyrimNet_Whipped_Injury", "", 0.0f, nullptr };
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
            const auto* conf = InjuryConfig::Find(source);
            const auto& body = GetBody();
            const char* partName = partIndex >= 0 && partIndex < kPartCount ? body.partName[partIndex].c_str() : "?";
            const float maxHealth = victim->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth);
            LogEvent(kind, std::format("{} \xe2\x9e\xa4 {} | {} {} point:{}+{} level:{} amount:{:.1f} health:{}", LogActor(attacker),
                LogActor(victim), conf ? conf->id : "?", partName, pointBefore, pointAdded, LogLevelName(level), amount,
                FormatHealthPercent(healthAfter, maxHealth)));
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

        // Healing without a source (game time, natural regen): narration.heal.passive from the common file
        // wraps the text as ordinary healing. No key, or no text: unchanged.
        std::string PassiveText(std::map<std::string, std::string> vars, const std::string& text)
        {
            const int common = InjuryConfig::GenericIndex() >= 0 ? InjuryConfig::GenericIndex() : WhipIndex();
            const std::string wrapper = text.empty() ? std::string{} : InjuryConfig::Str(common, "narration.heal.passive");
            if (wrapper.empty()) {
                return text;
            }
            vars["text"] = text;
            return InjuryConfig::Render(wrapper, vars);
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
            const std::string what = InjuryConfig::SessionStr(std::string("hit.") + phase,
                again ? "hits {target} again and again with {form}" : "hits {target} with {form}");
            vars["what"] = InjuryConfig::Render(what, vars);
            const std::string sentence = InjuryConfig::Render(InjuryConfig::SessionStr("clause", "{attackers} {what}"), vars);
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
            for (int i = 0; i < ActiveParts(); ++i) {
                if (PartFits(female, i) && (*weight)[i] > 0.0f) {
                    candidates[count++] = i;
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

            const int healed = body.healingMax;
            const int top = PartTorture(st, index);
            // The part's state before the hit (any torture type): 0 undamaged .. 3 severe.
            int level = top <= 0 && !PartTortureHit(st, index) ? kUndamaged
                        : top <= healed                        ? kLight
                                                               : DamageStage(top - healed, index) + 1;
            Cell& c = st.torture[source][index];
            const int pointBefore = c.hit && c.deficit > 0 ? c.deficit : 0;
            DropHealed(id, source, index);
            if (top <= 0) {
                st.lastHours[index] = now;
            }
            if (!c.hit || c.deficit <= 0) {
                c.hit = true;
                c.deficit = healed + 1;
            } else if (c.deficit <= healed) {
                // A hit on a healing wound just adds one step to where it has healed down to.
                level = std::max(level, static_cast<int>(kLight));
                c.deficit = c.deficit + 1;
            } else {
                level = std::max(level, DamageStage(c.deficit - healed, index) + 1);
                c.deficit = std::min(healed + body.partMax[index], c.deficit + 1);
            }
            MarkTouched(st, source, index);
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

    void LogLine(std::string_view kind, std::string_view narrationType, std::string_view text)
    {
        if (text.empty()) {
            return;
        }
        if (auto logger = spdlog::get("event")) {
            logger->info("{}:{} {}", kind, narrationType, text);
        }
    }

    bool IsInternalDamage()
    {
        return g_internal > 0;
    }

    std::int32_t Hit(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId)
    {
        return HitPart(actor, attacker, sourceId, -1);
    }

    std::int32_t HitPart(RE::Actor* actor, RE::Actor* attacker, std::string_view sourceId, int part)
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
        return TortureHit(actor, attacker, st, source, part, now, index, 0.0f);
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
        // of it. A melee hit is one torture hit; spell damage is one hit per hook.hit_share of max health.
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
                if (const auto batchIt = g_tortureBatchLast.find(batchKey); batchIt != g_tortureBatchLast.end()) {
                    newBatch = nowReal - batchIt->second > kTortureBatchGap;
                }
                g_tortureBatchLast[batchKey] = nowReal;
                if (g_tortureBatchLast.size() > 512) {
                    g_tortureBatchLast.clear();
                    g_tortureBatchLast[batchKey] = nowReal;
                }

                int hits = 1;
                if (!physicalMelee) {
                    float& carry = g_tortureCarry[id][source];
                    const float share = conf.hitShare * maxHealth;
                    const float carryBefore = carry;
                    carry += amount;
                    if (IsElementalTortureSpell(source) && carryBefore <= 0.0f) {
                        carry = 0.0f;
                        hits = 1;
                    } else if (carryBefore <= 0.0f && carry < share) {
                        // First tick of a beam below hit_share still lands one wound so narration can start.
                        carry = 0.0f;
                        hits = 1;
                    } else {
                        hits = static_cast<int>(carry / share);
                        carry -= static_cast<float>(hits) * share;
                        hits = std::min(hits, 8);
                        if (hits == 0 && newBatch) {
                            hits = 1;
                        } else if (hits == 0) {
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
                const float pointAmount = physicalMelee ? amount : conf.hitShare * maxHealth;
                for (int i = 0; i < hits; ++i) {
                    int index = -1;
                    const std::int32_t code = TortureHit(victim, attacker, st, source, -1, now, index, pointAmount);
                    if (i == 0) {
                        fresh = code >= 100000000;
                    }
                    lastPart = index;
                    mask |= 1 << index;
                    level = std::max(level, static_cast<int>(code % 10));
                }
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
                    e.unarmed = false;
                    g_hits.push_back(e);
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
                    AddRealPoint(st, source, part);
                    healthLogs.push_back({ part, countBefore, pointLevel });
                    mask |= 1 << part;
                    ++points;
                    DropHealed(id, source, part);
                }
            }
            if (part < 0) {
                // every part is full: the rest is dropped
                st.carry = std::min(st.carry, ratioR);
            }
            st.carry = std::max(st.carry, 0.0f);
            absorbed = std::min(absorbed, amount);
            pass = amount - absorbed;
            ClearTortureIfDone(id, st, false);
            const char* firstName = firstPart >= 0 ? GetBody().partName[firstPart].c_str() : "none";

            g_pained[id] = true;
            if (mask == 0) {
                SKSE::log::debug("damage {:08X} {} part:{} amount:{:.1f} absorbed:{:.1f} torture_replaced:{}: no new point (carry {:.2f})", id,
                    conf->id, firstName, amount, absorbed, replacedNow, st.carry);
            }
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
                    e.unarmed = unarmedForm;
                    g_hits.push_back(e);
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
            SourceHealingScope scope(true);  // heals by a caller (spell, potion) are narrated, not passive
            const int points = std::max(1, static_cast<int>(std::floor(health / RatioTorture(maxHealth) + 1e-3f)));
            for (int i = 0; i < points; ++i) {
                if (!HealTorturePoint(id, *st, now, part) && !(part >= 0 && HealTorturePoint(id, *st, now, -1))) {
                    break;
                }
                ++healedPoints;
            }
            ClearTortureIfDone(id, *st, true);
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

    std::vector<RE::Actor*> TakeQueuedHealActors()
    {
        std::lock_guard lock(g_lock);
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
            std::int32_t code;
            if (item.stage == kStageSourceHealed) {
                code = kHealSourceBase + item.source;
            } else if (item.stage == kStagePartHealed) {
                code = kHealPartBase + item.part;
            } else {
                code = item.source * 128 + item.part * 8 + item.stage;
            }
            codes.push_back(item.passive ? code + kHealPassive : code);
        }
        g_healed.erase(it);
        SKSE::log::debug("take healed {:08X}: {} code(s)", actor->GetFormID(), codes.size());
        return codes;
    }

    std::vector<std::int32_t> TakeHits()
    {
        std::lock_guard lock(g_lock);
        std::vector<std::int32_t> codes;
        g_hitVictims.clear();
        g_hitAttackers.clear();
        g_hitUnarmed.clear();
        for (const auto& e : g_hits) {
            auto* victim = RE::TESForm::LookupByID<RE::Actor>(e.victim);
            if (!victim) {
                SKSE::log::debug("take hits: victim {:08X} gone, hit dropped", e.victim);
                continue;
            }
            SKSE::log::debug("take hits: {:08X} {} mask:{} level:{} fresh:{}", e.victim, SourceId(e.source), e.mask, e.level, e.fresh);
            codes.push_back(EncodeHit(e.fresh, e.source, e.mask, e.level));
            g_hitVictims.push_back(victim);
            g_hitAttackers.push_back(e.attacker.get().get());
            g_hitUnarmed.push_back(e.unarmed);
        }
        g_hits.clear();
        return codes;
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
        const auto it = g_actors.find(target->GetFormID());
        if (it == g_actors.end()) {
            SKSE::log::debug("healing effect on {:08X} ignored: not tracked", target->GetFormID());
            return;
        }
        SKSE::log::debug("healing effect on {:08X} item:{:08X} caster:{:08X} magnitude:{:.1f}", it->first, item ? item->GetFormID() : 0,
            caster ? caster->GetFormID() : 0, effect->effectItem.magnitude);
        Seen(it->first, it->second, caster ? caster->As<RE::Actor>() : nullptr, item, NowSeconds());
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
            ScanActiveEffects(actor, id, *st, now);
            const Healer* healer = ActiveHealer(id, now);
            ConvertHealth(actor, id, *st, nowHours, healer != nullptr);
            ClearTortureIfDone(id, *st, true);

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
        for (int s = 0; s < InjuryConfig::SourceCount(); ++s) {
            const auto* conf = InjuryConfig::Find(s);
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < ActiveParts(); ++i) {
                int stage = -1;
                if (conf->IsTorture()) {
                    if (const Cell& c = st->torture[s][i]; c.hit) {
                        stage = TortureStage(c.deficit, i);
                    }
                } else if (st->real[s][i] > 0) {
                    stage = DamageStage(st->real[s][i], i);
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
        const int active = ActiveParts();
        nlohmann::json partMax = nlohmann::json::array();
        for (int i = 0; i < active; ++i) {
            partMax.push_back(body.partMax[body.overlayOrder[i]]);
        }
        nlohmann::json out = { { "title", InjuryConfig::Str(whip, "overlay.title", "Torture Damage") },
            { "max", CapacityDamage() }, { "part_max", partMax }, { "healed", body.healingMax },
            { "medium", body.medium }, { "severe", body.severe }, { "mostly_healed", body.mostlyHealed },
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
            // Each part shows only its worst torture type: [damage, healing] from the part's maximum.
            nlohmann::json parts = nlohmann::json::array();
            for (int i = 0; i < active; ++i) {
                const int part = body.overlayOrder[i];
                const int top = PartTorture(st, part);
                parts.push_back({ PartTortureDamage(st, part), body.healingMax - std::clamp(top, 0, body.healingMax) });
            }
            out["actors"].push_back({ { "id", id }, { "name", NameOf(actor) }, { "replaced", st.replaced }, { "parts", parts } });
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
        bool passive = !codes.empty();
        for (auto code : codes) {
            if (code >= kHealPassive) {
                code %= kHealPassive;
            } else {
                passive = false;
            }
            if (code >= kHealPartBase) {
                if (std::ranges::find(partsHealed, code - kHealPartBase) == partsHealed.end()) {
                    partsHealed.push_back(code - kHealPartBase);
                }
            } else if (code >= kHealSourceBase) {
                if (std::ranges::find(sourcesHealed, code - kHealSourceBase) == sourcesHealed.end()) {
                    sourcesHealed.push_back(code - kHealSourceBase);
                }
            } else if (code >= 0) {
                const int source = code / 128, part = (code % 128) / 8, stage = code % 8;
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
        return passive ? PassiveText(vars, text) : text;
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
        const std::string text = InjuryConfig::Render(InjuryConfig::Str(conf, std::string("narration.heal_events.") + key), vars);
        return healingSource == kSourceNone ? PassiveText(vars, text) : text;
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
                        what = InjuryConfig::SessionStr(std::string("hit.") + ph + (many ? "_many" : ""));
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
                    sentences.push_back(InjuryConfig::Render(InjuryConfig::SessionStr("clause"), vars));
                }
                for (const auto* p : alone) {
                    auto vars = ActorVars(victim);
                    vars["target"] = target;
                    vars["form"] = NarrationForm(p->source, false);
                    sentences.push_back(target + " " + InjuryConfig::Render(InjuryConfig::SessionStr("no_attacker_hit." + ph), vars));
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

        // v2 / v3: whipping as {hit, damage, healing, lastHours} per part, then replaced.
        bool ReadOldWhip(SKSE::SerializationInterface* intfc, State& st, int parts)
        {
            const int whip = WhipIndex();
            const int healed = GetBody().healingMax;
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
                if (hit && whip >= 0) {
                    st.torture[whip][i] = { true, damage > 0 ? healed + damage : std::max(0, healed - healing) };
                }
            }
            return intfc->ReadRecordData(st.replaced);
        }

        // v4: lastHours per part, replaced, then torture per source id: count, {id, {hit, deficit} x parts}.
        bool ReadTorture(SKSE::SerializationInterface* intfc, State& st)
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
                for (auto& c : cells) {
                    if (!intfc->ReadRecordData(c.hit) || !intfc->ReadRecordData(c.deficit)) {
                        return false;
                    }
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
        if (version != kRecordVersion && version != 3 && version != 2) {
            SKSE::log::warn("co-save version {} unsupported, skipped (old wounds are dropped)", version);
            return;
        }
        const int parts = version == 2 ? kCoreParts : kPartCount;  // v2: 6 parts, sexual parts start empty
        std::lock_guard lock(g_lock);
        g_actors.clear();
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_tortureCarry.clear();
        g_tortureBatchLast.clear();
        g_events.clear();
        g_hits.clear();
        std::uint32_t count = 0;
        if (!intfc->ReadRecordData(count)) {
            return;
        }
        for (std::uint32_t a = 0; a < count; ++a) {
            RE::FormID oldId = 0;
            State st;
            const bool ok = intfc->ReadRecordData(oldId) &&
                            (version >= 4 ? ReadTorture(intfc, st) : ReadOldWhip(intfc, st, parts)) &&
                            ReadReal(intfc, st, parts);
            if (!ok) {
                SKSE::log::error("co-save truncated");
                return;
            }
            for (int s = 0; s < kMaxSources; ++s) {
                for (int p = 0; p < kPartCount; ++p) {
                    if (st.real[s][p] > 0 || st.torture[s][p].hit) {
                        MarkTouched(st, s, p);
                    }
                }
            }
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
        g_healed.clear();
        g_healers.clear();
        g_pained.clear();
        g_regenBank.clear();
        g_tortureCarry.clear();
        g_tortureBatchLast.clear();
        g_events.clear();
        g_hits.clear();
        g_syncActors.clear();
        g_syncCasters.clear();
        g_hitVictims.clear();
        g_hitAttackers.clear();
    }
}
