#include "Classifier.h"
#include "InjuryEngine.h"

#include <cmath>

// Vtable hooks on Character and PlayerCharacter:
//   MagicTarget::AddTarget          every effect applied to an actor, with the magic item and caster
//                                   (instant potions included), so healing sources can be named.
//   Actor::HandleHealthDamage       weapon and projectile hits including concentration beam ticks (SE / AE only;
//                                   the VR slot is not verified)
//   ActorValueOwner::ModActorValue  other health damage when not nested inside HandleHealthDamage
// Damage is classified (Classifier) and handed to InjuryEngine::OnDamage, which may reduce it (torture wounds
// absorb their share; torture damage itself is taken whole). HandleHealthDamage's own nested ModActorValue calls are not counted twice.
namespace
{
    constexpr std::size_t kMagicTargetVtbl = 4;  // Actor bases: TESObjectREFR (0-3), MagicTarget (4)
    constexpr std::size_t kActorValueOwnerVtbl = 5;
    constexpr std::size_t kAddTargetIdx = 0x1;
    constexpr std::size_t kModActorValueIdx = 0x6;
    constexpr std::size_t kHandleHealthDamageIdx = 0x104;  // Actor::HandleHealthDamage (SE / AE); CommonLib slot comments are hex

    thread_local int g_inHandle = 0;

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
            if (added) {
                auto* actor = skyrim_cast<RE::Actor*>(a_this);
                InjuryEngine::NoteHealingSource(actor, a_data.caster, a_data.magicItem, a_data.effect);
                Classifier::NoteEffect(actor, a_data.effect);
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
            if (InjuryEngine::IsInternalDamage() || a_damage == 0.0f) {
                return func(a_this, a_attacker, a_damage);
            }
            const float amount = std::fabs(a_damage);
            bool physicalMelee = false;
            bool unarmedForm = false;
            const int source = Classifier::Classify(a_this, a_attacker, &physicalMelee, &unarmedForm);
            SKSE::log::debug("[hook] HandleHealthDamage victim:{:08X} attacker:{:08X} damage:{:.2f} source:{} physical:{} unarmed:{}",
                a_this->GetFormID(), a_attacker ? a_attacker->GetFormID() : 0, a_damage, InjuryEngine::SourceId(source), physicalMelee, unarmedForm);
            const float kept = source >= 0 ? InjuryEngine::OnDamage(a_this, a_attacker, amount, source, physicalMelee, unarmedForm) : amount;
            HandleScope scope;
            func(a_this, a_attacker, std::copysign(kept, a_damage));
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    template <class T>
    struct ModActorValue
    {
        static void thunk(RE::ActorValueOwner* a_this, RE::ACTOR_VALUE_MODIFIER a_modifier, RE::ActorValue a_av, float a_value)
        {
            if (a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && a_av == RE::ActorValue::kHealth && a_value < 0.0f &&
                g_inHandle == 0 && !InjuryEngine::IsInternalDamage()) {
                if (auto* actor = skyrim_cast<RE::Actor*>(a_this)) {
                    const int source = Classifier::Classify(actor, nullptr);
                    SKSE::log::debug("[hook] ModActorValue victim:{:08X} damage:{:.2f} source:{}", actor->GetFormID(), a_value,
                        InjuryEngine::SourceId(source));
                    if (source >= 0) {
                        a_value = -InjuryEngine::OnDamage(actor, nullptr, -a_value, source, false);
                    }
                }
            }
            func(a_this, a_modifier, a_av, a_value);
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
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
