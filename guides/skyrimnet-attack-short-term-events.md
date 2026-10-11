# SkyrimNet attack short-term events

How `SkyrimNet.log` records a concentration spell attack as short-lived events. Measured 2026-10-11 from `Documents\my games\Skyrim Special Edition\SKSE\SkyrimNet.log` (OneDrive-redirected on this machine).

A cast produces two event types. Neither is one event per damage tick.

| Type | When | Id | TTL |
|------|------|----|-----|
| `spell_cast` | Once, when the cast starts | `spell_cast_<actorUuid>_{json}` | 60 s |
| `hit` | Once per `TES` hit while the beam connects | `combat_hit_<attackerUuid>_to_<targetUuid>` | 15 s |

The same id is overwritten and registered again. A repeat refreshes the TTL. It does not stack a second event.

Actors in these sessions: Bob `4581553741539567915`, Nia The Nerd `5883796160205289627`, Nina `3344083181343742200`.

## spell_cast

`SpellCastEventHandler.cpp:54` logs the cast. `SceneContextBuilder.cpp:335` registers the short-lived event.

```
Spell cast event: 'Bob' cast spell 'Flames'
Registered short-lived event 'spell_cast_4581553741539567915_{"actor":"Bob","spell":"Flames","spell_editor_id":"Flames","spell_id":77773}' of type 'spell_cast' with TTL 60000ms
```

`spell_id` `77773` is `0x00012FCD` (vanilla Flames). The id is the caster plus that spell. A later cast of the same spell by the same actor logs `already exists, overwriting` and registers the same id again.

Ongoing damage does not emit another `spell_cast`. The second Flames cast, 4.0 s after the first attack's last hit, was the only overwrite.

## hit

`HitEventHandler.cpp:216` logs the hit. `SceneContextBuilder` registers type `hit`.

```
Hit event: Bob hit Nia The Nerd | Weapon: 0x00012FCD | Projectile: 0x00012FCF | Flags: None
Registered short-lived event 'combat_hit_4581553741539567915_to_5883796160205289627' of type 'hit' with TTL 15000ms
```

`Weapon` is the spell form, not a weapon. `Projectile` is the beam (`0x00012FCF` for vanilla Flames, `0x00034190` for Sparks).

The id is the attacker–target pair. Further hits on that pair overwrite it. A different target is a new id: Bob → Nina was `combat_hit_4581553741539567915_to_3344083181343742200`.

The first hit lands about 33 ms after the spell-cast line. Later hits, while the beam stays on that target, are about **1.10 s** apart. Each one overwrites the pair's `hit` event and sets the TTL back to 15 s.

## Flames, 08:25

Vanilla Flames. Two attacks.

**Bob → Nia.** Cast `08:25:46.601`. Seven hits, then the beam stopped. No second `spell_cast` during the hits.

| Hit | Gap from previous |
|-----|-------------------|
| `08:25:46.634` | 33 ms after the cast line (first hit; new id) |
| `08:25:47.734` | 1.100 s |
| `08:25:48.852` | 1.118 s |
| `08:25:49.968` | 1.116 s |
| `08:25:51.068` | 1.100 s |
| `08:25:52.168` | 1.100 s |
| `08:25:53.268` | 1.100 s |

**Bob → Nina.** Cast `08:25:57.302` (4.034 s after Nia's last hit). One hit, `08:25:57.335`. The Flames `spell_cast` id was overwritten. The `hit` id was new because the target changed. No further Flames hit after that.

## Sparks (torture), 07:53

`SkyrimNet_Whipped_SparksTorture`, weapon `0xFE050818`, projectile `0x00034190`. Cast `07:53:44.235`. Short-lived `spell_cast` at `07:53:44.261` (`spell_id` `4261742616`).

Two `hit` events on Bob → Nia, same id, second one an overwrite:

| Hit | Gap |
|-----|-----|
| `07:53:44.252` | 17 ms after the cast line |
| `07:53:45.350` | 1.098 s |

No further `Hit event:` lines in that log. Shock damage kept landing: `SkyrimNet_Whipped.log` records 31 shock applications on Nia across `07:53:44`–`07:53:46` (9, then 1, then 21). Those ticks did not become SkyrimNet `hit` events.

## Damage ticks are a different stream

SkyrimNet's `hit` short-lived event follows `TES` hit events. Concentration Flames / Sparks / Frostbite also apply health damage many times a second through `Actor::HandleHealthDamage`. Those ticks do not each create a `spell_cast` or `hit` event.

`torture_hit` lines in the same log (`Papyrus::RegisterShortLivedEvent`, type `torture_hit`, TTL 30 s) are this mod calling into SkyrimNet. They are not the attack events above. In the Sparks session they started at `07:53:51.767`, about 6.4 s after the last native hit, and then ran about every 430–530 ms.
