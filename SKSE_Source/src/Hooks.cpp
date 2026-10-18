#include "Classifier.h"
#include "InjuryConfig.h"
#include "InjuryEngine.h"

#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>

// Vtable hooks on Character and PlayerCharacter:
//   MagicTarget::AddTarget          every effect applied to an actor, with the magic item and caster
//                                   (instant potions included), so healing sources can be named.
//   Actor::HandleHealthDamage       weapon and projectile hits including concentration beam ticks (SE / AE only;
//                                   the VR slot is not verified). It reports health already taken, so it gives back
//                                   what the engine did not keep (punches, real hits replacing torture, missed ticks)
//   ActorValueOwner::ModActorValue  other health damage when not nested inside HandleHealthDamage
// Vtable hooks on ValueModifierEffect and DualValueModifierEffect:
//   ModifyActorValue (0x20)         the per-tick change of a value-modifier effect; "(torture)" Health ticks are
//                                   blocked here before they land (the AV argument is kNone, the effect's own AV is
//                                   the member `actorValue`), and the dual Stamina / Magicka drain is applied by us
// Call hooks (SE / AE):
//   GetFactionFightReaction         in the victim's hit handling (the Simple Offence Suppression site): a Padded
//                                   Whip lash on a neutral victim reads as an ally hit, so it starts no combat
//   ProcessHitData                  in the melee hit handler, before health is taken: a torture melee hit (a punch)
//                                   becomes torture points and its damage is zeroed, so it can't kill; a Padded Whip
//                                   hit is zeroed too (its lash ENCH brings the torture)
// Damage is classified (Classifier) and handed to InjuryEngine::OnDamage, which may reduce it (torture wounds
// absorb their share; torture damage itself is taken whole). HandleHealthDamage's own nested ModActorValue calls are not counted twice.
namespace
{
    constexpr std::size_t kMagicTargetVtbl = 4;  // Actor bases: TESObjectREFR (0-3), MagicTarget (4)
    constexpr std::size_t kActorValueOwnerVtbl = 5;
    constexpr std::size_t kAddTargetIdx = 0x1;
    constexpr std::size_t kModActorValueIdx = 0x6;
    constexpr std::size_t kModBaseActorValueIdx = 0x5;  // drowning probe only
    constexpr std::size_t kSetActorValueIdx = 0x7;      // drowning probe only
    constexpr std::size_t kModifyEffectActorValueIdx = 0x20;  // ValueModifierEffect::ModifyActorValue (DualValueModifierEffect overrides it)
    constexpr std::size_t kHandleHealthDamageIdx = 0x104;  // Actor::HandleHealthDamage (SE / AE); CommonLib slot comments are hex

    thread_local int g_inHandle = 0;

    // The last torture melee hit ProcessHitData turned into points (victim, when): a HandleHealthDamage call for the
    // same hit that still carries damage only gives the health back.
    thread_local RE::FormID g_handledVictim = 0;
    thread_local std::chrono::steady_clock::time_point g_handledWhen{};

    bool HandledByProcessHit(RE::Actor* victim)
    {
        return victim && g_handledVictim == victim->GetFormID() && std::chrono::steady_clock::now() - g_handledWhen < std::chrono::milliseconds(100);
    }

    bool Verbose()
    {
        return spdlog::default_logger_raw()->should_log(spdlog::level::debug);
    }

    // Does a torture tick keep health? `before` / `after` around the hooked call (and our restore); `change` is
    // health after minus before (a blocked tick reads +0.000).
    void ProbeTorture(const char* hook, RE::Actor* victim, RE::Actor* attacker, float damage, float kept, float before, float after)
    {
        if (!Verbose()) {
            return;
        }
        SKSE::log::debug("[hook] {} torture victim:{:08X} attacker:{:08X} damage:{:.3f} kept:{:.3f} health:{:.2f} -> {:.2f} change:{:+.3f}", hook,
            victim->GetFormID(), attacker ? attacker->GetFormID() : 0, damage, kept, before, after, after - before);
    }

    bool FromThisPlugin(const RE::TESForm* form)
    {
        const auto* file = form ? form->GetFile(0) : nullptr;
        return file && file->GetFilename() == "Injury_Engine.esp";
    }

    // Always-on drowning diagnosis: one info line per second per hook and actor while the actor shows any sign of
    // water, so the hook that carries vanilla drowning damage is visible without verbose logging.
    void UnderwaterProbe(const char* hook, RE::Actor* actor, float value, std::string_view note)
    {
        if (!actor) {
            return;
        }
        const auto signals = Classifier::ReadDrownSignals(actor);
        if (!signals.Wet()) {
            return;
        }
        {
            static std::mutex lock;
            static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last;
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard guard(lock);
            if (last.size() > 256) {
                last.clear();
            }
            auto& when = last[std::format("{}|{:08X}", hook, actor->GetFormID())];
            if (when != std::chrono::steady_clock::time_point{} && now - when < std::chrono::seconds(1)) {
                return;
            }
            when = now;
        }
        auto* av = actor->AsActorValueOwner();
        SKSE::log::info("[probe] {} {:08X} value:{:.2f} health:{:.1f}/{:.1f} inHandle:{} internal:{} {} | {}", hook, actor->GetFormID(), value,
            av->GetActorValue(RE::ActorValue::kHealth), av->GetPermanentActorValue(RE::ActorValue::kHealth), g_inHandle,
            InjuryEngine::IsInternalDamage(), note, signals.Text());
    }

    struct HandleScope
    {
        HandleScope() { ++g_inHandle; }
        ~HandleScope() { --g_inHandle; }
    };

    template <class T>
    struct AddTarget
    {
        static bool thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
        {
            const bool added = func(a_this, a_data);
            if (Verbose()) {
                auto* target = skyrim_cast<RE::Actor*>(a_this);
                const auto* mgef = a_data.effect ? a_data.effect->baseEffect : nullptr;
                SKSE::log::debug("[hook] AddTarget victim:{:08X} effect:{:08X} item:{:08X} caster:{:08X} added:{} torture source:{}",
                    target ? target->GetFormID() : 0, mgef ? mgef->GetFormID() : 0, a_data.magicItem ? a_data.magicItem->GetFormID() : 0,
                    a_data.caster ? a_data.caster->GetFormID() : 0, added, mgef ? Classifier::TortureSourceOfEffect(mgef) : -1);
            }
            if (added) {
                auto* actor = skyrim_cast<RE::Actor*>(a_this);
                InjuryEngine::NoteHealingSource(actor, a_data.caster, a_data.magicItem, a_data.effect);
                Classifier::NoteEffect(actor, a_data.effect, a_data.caster);
            }
            return added;
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    template <class T>
    struct HandleHealthDamage
    {
        static void thunk(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage)
        {
            if (a_damage != 0.0f && !a_attacker) {
                UnderwaterProbe("HandleHealthDamage", a_this, a_damage, "");
            }
            if (InjuryEngine::IsInternalDamage() || a_damage == 0.0f) {
                if (Verbose() && a_damage != 0.0f) {
                    SKSE::log::debug("[hook] HandleHealthDamage victim:{:08X} damage:{:.2f} internal, passed", a_this->GetFormID(), a_damage);
                }
                return func(a_this, a_attacker, a_damage);
            }
            const float amount = std::fabs(a_damage);
            bool physicalMelee = false;
            bool unarmedForm = false;
            bool uncaused = false;
            const int source = Classifier::Classify(a_this, a_attacker, &physicalMelee, &unarmedForm, &uncaused);
            SKSE::log::debug("[hook] HandleHealthDamage victim:{:08X} attacker:{:08X} damage:{:.2f} source:{} physical:{} unarmed:{}",
                a_this->GetFormID(), a_attacker ? a_attacker->GetFormID() : 0, a_damage, InjuryEngine::SourceId(source), physicalMelee, unarmedForm);
            if (uncaused) {
                // scripted damage: the game keeps it; it becomes a wound only if it sticks (InjuryEngine::NoteUncaused)
                {
                    HandleScope scope;
                    func(a_this, a_attacker, a_damage);
                }
                InjuryEngine::NoteUncaused(a_this, amount, source, a_this->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth));
                return;
            }
            if (source >= 0 && InjuryConfig::IsTorture(source)) {
                Classifier::MarkQuiet(a_this);  // no vanilla hit bark over the torture narration
                Classifier::NoteCalm(a_this, a_attacker);  // and no fight back
            }
            // torture melee hits and Padded Whip hits zeroed by ProcessHitData: only give back what still landed
            const bool handled = ((source >= 0 && InjuryConfig::IsTorture(source)) || (source == -1 && physicalMelee)) && HandledByProcessHit(a_this);
            const float kept = handled ? 0.0f : source >= 0 ? InjuryEngine::OnDamage(a_this, a_attacker, amount, source, physicalMelee, unarmedForm) : amount;
            auto* av = a_this->AsActorValueOwner();
            const float before = av->GetActorValue(RE::ActorValue::kHealth);
            {
                HandleScope scope;
                func(a_this, a_attacker, std::copysign(kept, a_damage));
            }
            // The game took `amount` before calling this; give back what the engine did not keep (torture ticks: all
            // of it, TakeHealth applies the floor-limited loss; real hits: the share torture wounds absorbed).
            if (kept < amount) {
                InjuryEngine::RestoreHealth(a_this, amount - kept);
            }
            if (source >= 0 && InjuryConfig::IsTorture(source)) {
                ProbeTorture("HandleHealthDamage", a_this, a_attacker, a_damage, kept, before, av->GetActorValue(RE::ActorValue::kHealth));
            }
            if (Verbose()) {
                SKSE::log::debug("[hook] HandleHealthDamage victim:{:08X} kept:{:.2f} health:{:.2f} -> {:.2f}", a_this->GetFormID(), kept,
                    before, av->GetActorValue(RE::ActorValue::kHealth));
            }
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    template <class T>
    struct ModActorValue
    {
        static void thunk(RE::ActorValueOwner* a_this, RE::ACTOR_VALUE_MODIFIER a_modifier, RE::ActorValue a_av, float a_value)
        {
            if (a_av == RE::ActorValue::kHealth && a_value < 0.0f) {
                UnderwaterProbe("ModActorValue", skyrim_cast<RE::Actor*>(a_this), a_value,
                    a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage ? "damage" : a_modifier == RE::ACTOR_VALUE_MODIFIER::kTemporary ? "temporary" : "permanent");
            }
            if (a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && a_av == RE::ActorValue::kHealth && a_value < 0.0f &&
                g_inHandle == 0 && !InjuryEngine::IsInternalDamage()) {
                if (auto* actor = skyrim_cast<RE::Actor*>(a_this)) {
                    bool uncaused = false;
                    const int source = Classifier::Classify(actor, nullptr, nullptr, nullptr, &uncaused);
                    SKSE::log::debug("[hook] ModActorValue victim:{:08X} damage:{:.2f} source:{}", actor->GetFormID(), a_value,
                        InjuryEngine::SourceId(source));
                    if (source >= 0 && InjuryConfig::IsTorture(source)) {
                        Classifier::MarkQuiet(actor);
                    }
                    if (uncaused) {
                        func(a_this, a_modifier, a_av, a_value);
                        InjuryEngine::NoteUncaused(actor, -a_value, source, a_this->GetActorValue(RE::ActorValue::kHealth));
                        return;
                    }
                    if (source >= 0) {
                        const float damage = a_value;
                        a_value = -InjuryEngine::OnDamage(actor, nullptr, -a_value, source, false);
                        if (InjuryConfig::IsTorture(source)) {
                            auto* av = actor->AsActorValueOwner();
                            const float before = av->GetActorValue(RE::ActorValue::kHealth);
                            func(a_this, a_modifier, a_av, a_value);
                            ProbeTorture("ModActorValue", actor, nullptr, damage, -a_value, before, av->GetActorValue(RE::ActorValue::kHealth));
                            return;
                        }
                    }
                }
            } else if (a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && a_av == RE::ActorValue::kHealth && a_value < 0.0f && Verbose()) {
                auto* actor = skyrim_cast<RE::Actor*>(a_this);
                SKSE::log::debug("[hook] ModActorValue victim:{:08X} damage:{:.2f} skipped (in HandleHealthDamage:{} internal:{})",
                    actor ? actor->GetFormID() : 0, a_value, g_inHandle > 0, InjuryEngine::IsInternalDamage());
            }
            func(a_this, a_modifier, a_av, a_value);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // Diagnosis only (pass-through): ActorValueOwner 0x05 ModBaseActorValue and 0x07 SetActorValue on Health, in
    // case vanilla drowning lowers health through one of them instead of the damage modifier.
    template <class T>
    struct ModBaseActorValue
    {
        static void thunk(RE::ActorValueOwner* a_this, RE::ActorValue a_av, float a_value)
        {
            if (a_av == RE::ActorValue::kHealth && a_value < 0.0f) {
                UnderwaterProbe("ModBaseActorValue", skyrim_cast<RE::Actor*>(a_this), a_value, "");
            }
            func(a_this, a_av, a_value);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    template <class T>
    struct SetActorValue
    {
        static void thunk(RE::ActorValueOwner* a_this, RE::ActorValue a_av, float a_value)
        {
            if (a_av == RE::ActorValue::kHealth) {
                if (auto* actor = skyrim_cast<RE::Actor*>(a_this); actor && a_value < a_this->GetActorValue(RE::ActorValue::kHealth)) {
                    UnderwaterProbe("SetActorValue", actor, a_value, "");
                }
            }
            func(a_this, a_av, a_value);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // The per-tick call that actually changes the actor value of a value-modifier effect (HandleHealthDamage only
    // reports a change already made). A "(torture)" effect, or any Health effect on a "(torture)" spell, never
    // touches Health: the tick becomes torture points (OnDamage, whose TakeHealth keeps the floor). The dual
    // variant (frost / shock) still drains its second value (Stamina / Magicka) here.
    template <bool Dual>
    struct ModifyEffectActorValue
    {
        static void thunk(RE::ValueModifierEffect* a_this, RE::Actor* a_actor, float a_value, RE::ActorValue a_av)
        {
            const auto* mgef = a_this && a_this->effect ? a_this->effect->baseEffect : nullptr;
            // The engine passes kNone here; the effect's own value is the member set by SetActorValue.
            const auto effectAv = a_av != RE::ActorValue::kNone ? a_av : (a_this ? a_this->actorValue : RE::ActorValue::kNone);
            if (Verbose() && FromThisPlugin(mgef)) {
                SKSE::log::debug("[hook] effect{} victim:{:08X} effect:{:08X} spell:{:08X} av:{} member:{} value:{:.3f} torture source:{}",
                    Dual ? " (dual)" : "", a_actor ? a_actor->GetFormID() : 0, mgef->GetFormID(), a_this->spell ? a_this->spell->GetFormID() : 0,
                    static_cast<int>(a_av), static_cast<int>(a_this->actorValue), a_value, Classifier::TortureSourceOfEffect(mgef));
            }
            if (effectAv != RE::ActorValue::kHealth || a_value >= 0.0f || !a_actor || !mgef) {
                return func(a_this, a_actor, a_value, a_av);
            }
            int source = Classifier::TortureSourceOfEffect(mgef);
            const bool tortureEffect = source >= 0;
            if (!tortureEffect) {
                source = Classifier::TortureSourceOfSpell(a_this->spell);
            }
            if (source < 0) {
                return func(a_this, a_actor, a_value, a_av);
            }
            const auto caster = a_this->GetCasterActor();
            auto* attacker = caster.get();
            auto* av = a_actor->AsActorValueOwner();
            const float before = av->GetActorValue(RE::ActorValue::kHealth);
            Classifier::MarkQuiet(a_actor);
            Classifier::NoteCalm(a_actor, attacker);
            if (tortureEffect) {
                InjuryEngine::OnDamage(a_actor, attacker, std::fabs(a_value), source, false);
            }
            if constexpr (Dual) {
                if (mgef->data.secondaryAV != RE::ActorValue::kNone && mgef->data.secondaryAV != RE::ActorValue::kHealth) {
                    av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, mgef->data.secondaryAV, -std::fabs(a_value) * mgef->data.secondAVWeight);
                }
            }
            SKSE::log::debug("[hook] ModifyActorValue{} victim:{:08X} caster:{:08X} effect:{:08X} value:{:.3f} source:{} blocked", Dual ? " (dual)" : "",
                a_actor->GetFormID(), attacker ? attacker->GetFormID() : 0, mgef->GetFormID(), a_value, InjuryEngine::SourceId(source));
            ProbeTorture(Dual ? "DualValueModifierEffect" : "ValueModifierEffect", a_actor, attacker, a_value, 0.0f, before,
                av->GetActorValue(RE::ActorValue::kHealth));
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // The hit handler asks how the victim feels about its attacker; neutral means the hit is an attack (combat,
    // assault). A whip lash or torture punch is torture, not aggression: report an ally and ignore the friendly hit as well.
    struct FightReaction
    {
        static RE::FIGHT_REACTION thunk(RE::Actor* a_subject, RE::Actor* a_attacker)
        {
            const auto reaction = func(a_subject, a_attacker);
            if (reaction == RE::FIGHT_REACTION::kNeutral && a_subject && a_attacker && Classifier::IsTortureAttacker(a_attacker) &&
                !a_subject->IsHostileToActor(a_attacker)) {
                Classifier::SuppressFriendHits(a_subject);
                SKSE::log::debug("[hook] fight reaction {:08X} -> {:08X}: torture hit, ally", a_subject->GetFormID(), a_attacker->GetFormID());
                return RE::FIGHT_REACTION::kAlly;
            }
            return reaction;
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    // The melee hit handler hands its HitData to the victim before any health is taken. A torture melee hit
    // (a punch) becomes torture points here and its damage is zeroed, so a punch can't kill: HandleHealthDamage only
    // reports health already taken, too late for a hit that is lethal by itself.
    struct ProcessHitData
    {
        static void thunk(RE::Actor* a_victim, RE::HitData& a_hit)
        {
            if (a_victim && !a_victim->IsDead() && a_hit.totalDamage > 0.0f && !InjuryEngine::IsInternalDamage()) {
                auto attackerPtr = a_hit.aggressor.get();
                auto* attacker = attackerPtr.get();
                bool physicalMelee = false;
                bool unarmedForm = false;
                const int source = Classifier::Classify(a_victim, attacker, &physicalMelee, &unarmedForm);
                if (source >= 0 && physicalMelee && InjuryConfig::IsTorture(source)) {
                    const float damage = a_hit.totalDamage;
                    Classifier::MarkQuiet(a_victim);
                    Classifier::NoteCalm(a_victim, attacker);
                    InjuryEngine::OnDamage(a_victim, attacker, damage, source, true, unarmedForm);
                    a_hit.totalDamage = 0.0f;
                    a_hit.physicalDamage = 0.0f;
                    a_hit.bonusHealthDamageMult = 0.0f;
                    g_handledVictim = a_victim->GetFormID();
                    g_handledWhen = std::chrono::steady_clock::now();
                    SKSE::log::debug("[hook] ProcessHitData torture victim:{:08X} attacker:{:08X} source:{} damage:{:.2f} -> 0",
                        a_victim->GetFormID(), attacker ? attacker->GetFormID() : 0, InjuryEngine::SourceId(source), damage);
                } else if (source == -1 && physicalMelee) {
                    // The Padded Whip: its lash ENCH brings the torture (OnLash), the weapon hit itself must not take health.
                    const float damage = a_hit.totalDamage;
                    a_hit.totalDamage = 0.0f;
                    a_hit.physicalDamage = 0.0f;
                    a_hit.bonusHealthDamageMult = 0.0f;
                    g_handledVictim = a_victim->GetFormID();
                    g_handledWhen = std::chrono::steady_clock::now();
                    SKSE::log::debug("[hook] ProcessHitData padded whip victim:{:08X} attacker:{:08X} damage:{:.2f} -> 0", a_victim->GetFormID(),
                        attacker ? attacker->GetFormID() : 0, damage);
                }
            }
            func(a_victim, a_hit);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    void InstallProcessHitData()
    {
        if (REL::Module::IsVR()) {
            SKSE::log::warn("ProcessHitData hook skipped on VR (call site not verified); punches are restored after the hit");
            return;
        }
        REL::Relocation<std::uintptr_t> target{ REL::RelocationID(37673, 38627), REL::VariantOffset(0x3C0, 0x4A8, 0) };
        if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
            SKSE::log::warn("ProcessHitData hook skipped: no call at {:X}; punches are restored after the hit", target.offset());
            return;
        }
        SKSE::AllocTrampoline(14);
        ProcessHitData::func = SKSE::GetTrampoline().write_call<5>(target.address(), ProcessHitData::thunk);
        SKSE::log::info("ProcessHitData hook installed");
    }

    void InstallFightReaction()
    {
        if (REL::Module::IsVR()) {
            SKSE::log::warn("GetFactionFightReaction hook skipped on VR (call site not verified); whipped NPCs are calmed after combat starts");
            return;
        }
        REL::Relocation<std::uintptr_t> target{ REL::RelocationID(37672, 38626), REL::VariantOffset(0x187, 0x182, 0) };
        if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
            SKSE::log::warn("GetFactionFightReaction hook skipped: no call at {:X}; whipped NPCs are calmed after combat starts",
                target.offset());
            return;
        }
        SKSE::AllocTrampoline(14);
        FightReaction::func = SKSE::GetTrampoline().write_call<5>(target.address(), FightReaction::thunk);
        SKSE::log::info("GetFactionFightReaction hook installed");
    }
}

void Install_Hooks()
{
    REL::Relocation<std::uintptr_t> character{ RE::VTABLE_Character[kMagicTargetVtbl] };
    AddTarget<RE::Character>::func = character.write_vfunc(kAddTargetIdx, AddTarget<RE::Character>::thunk);

    REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[kMagicTargetVtbl] };
    AddTarget<RE::PlayerCharacter>::func = player.write_vfunc(kAddTargetIdx, AddTarget<RE::PlayerCharacter>::thunk);
    SKSE::log::info("MagicTarget::AddTarget hooks installed");

    REL::Relocation<std::uintptr_t> characterAv{ RE::VTABLE_Character[kActorValueOwnerVtbl] };
    ModActorValue<RE::Character>::func = characterAv.write_vfunc(kModActorValueIdx, ModActorValue<RE::Character>::thunk);
    REL::Relocation<std::uintptr_t> playerAv{ RE::VTABLE_PlayerCharacter[kActorValueOwnerVtbl] };
    ModActorValue<RE::PlayerCharacter>::func = playerAv.write_vfunc(kModActorValueIdx, ModActorValue<RE::PlayerCharacter>::thunk);
    SKSE::log::info("ActorValueOwner::ModActorValue hooks installed");
    ModBaseActorValue<RE::Character>::func = characterAv.write_vfunc(kModBaseActorValueIdx, ModBaseActorValue<RE::Character>::thunk);
    ModBaseActorValue<RE::PlayerCharacter>::func = playerAv.write_vfunc(kModBaseActorValueIdx, ModBaseActorValue<RE::PlayerCharacter>::thunk);
    SetActorValue<RE::Character>::func = characterAv.write_vfunc(kSetActorValueIdx, SetActorValue<RE::Character>::thunk);
    SetActorValue<RE::PlayerCharacter>::func = playerAv.write_vfunc(kSetActorValueIdx, SetActorValue<RE::PlayerCharacter>::thunk);
    SKSE::log::info("ActorValueOwner::ModBaseActorValue / SetActorValue probe hooks installed (drowning diagnosis)");

    REL::Relocation<std::uintptr_t> valueModifier{ RE::VTABLE_ValueModifierEffect[0] };
    ModifyEffectActorValue<false>::func = valueModifier.write_vfunc(kModifyEffectActorValueIdx, ModifyEffectActorValue<false>::thunk);
    REL::Relocation<std::uintptr_t> dualValueModifier{ RE::VTABLE_DualValueModifierEffect[0] };
    ModifyEffectActorValue<true>::func = dualValueModifier.write_vfunc(kModifyEffectActorValueIdx, ModifyEffectActorValue<true>::thunk);
    SKSE::log::info("ValueModifierEffect::ModifyActorValue hooks installed (plain and dual{})", REL::Module::IsVR() ? ", VR: unverified" : "");

    InstallFightReaction();
    InstallProcessHitData();

    if (REL::Module::IsVR()) {
        SKSE::log::warn("Actor::HandleHealthDamage hook skipped on VR (vtable slot not verified); melee damage is seen through ModActorValue only");
        return;
    }
    REL::Relocation<std::uintptr_t> characterActor{ RE::VTABLE_Character[0] };
    HandleHealthDamage<RE::Character>::func = characterActor.write_vfunc(kHandleHealthDamageIdx, HandleHealthDamage<RE::Character>::thunk);
    REL::Relocation<std::uintptr_t> playerActor{ RE::VTABLE_PlayerCharacter[0] };
    HandleHealthDamage<RE::PlayerCharacter>::func = playerActor.write_vfunc(kHandleHealthDamageIdx, HandleHealthDamage<RE::PlayerCharacter>::thunk);
    SKSE::log::info("Actor::HandleHealthDamage hooks installed");
}
