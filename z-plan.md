Read guides/skyrimnet-attack-short-term-events.md, we want our short-term-events to have the same pattern. A single short term event of the same type (so the new one replaces the old one) for each pair of torturer and tortured. 

Improved Log:

[<hour>:<min>:second>] [<spdlog level>] <type>: <torturer.name> (<torturer.id>) <tortured.name> (<tortured.id>) <source> <body_part>:<body_part.torture_total_before_event>+<torture_points_added> level:<level> amount:<amount> health:<tortured.health_after_adding_in_new_torture>

I just ran the game and did a torture frost attack. no short or narration lines in the log.

Example of correct form below: 
```
[07:53:41] [info] torture: Bob (00000014) ➤ Nia The Nerd (22005902) | shock face point:0+1 level:1 amount:0.1 health:89.5%
[07:53:46] [info] narration: Bob casts sparks on Nia The Nerd, hitting Nia The Nerd's right arm over skin that is already prickling and raw. The sparks bite into the fresh burns, and the twitching muscle knots into a hard, aching cramp that drives the breath from her
[07:53:42] [info] torture: Bob (00000014) ➤ Nia The Nerd (22005902) | shock face point:1+1 level:1 amount:0.1 health:88%
[07:53:42] [info] short: Bob continues to cast sparks on Nia The Nerd, electric arcs hit Nia The Nerd's right arm over skin that is already prickling and raw. The sparks bite into the fresh burns, and the twitching muscle knots into a hard, aching cramp that drives the breath from her
```
.... repeates until the narration point is reached again 
[07:53:42] [info] torture: Bob (00000014) ➤ Nia The Nerd (22005902) | shock face point:7+1 level:2 amount:0.1 health:88%
[07:53:47] [info] torture: Bob (00000014) Nia The Nerd (22005902) shock face point:1 amount:0.1 hit:1
[07:53:47] [info] narration: Bob casts sparks on Nia The Nerd again and again, the last one hitting Nia The Nerd's right arm over skin that is already prickling and raw. The sparks bite into the fresh burns, and the twitching muscle knots into a hard, aching cramp that drives the breath from her
```

Remember that the tortured.health is set to health_max*90*(current_total_torture_points_summed_all_body_parts/max_torture_points_summed_all_body_parts)

first: '<torture.name> hits <tortured.name> with <source.form> {feeling}`
after first: '<torture.name> hits <tortured.name> again and again with <source.form>, the last {feeling}`

Add the following form for use in the narration to their JSON
| name | torture form |
|------|------|
| whipping | a biting leather lash |
| pinching | sharp pinching fingers |
| slapping | a stinging open hand |
| pulling | hard wrenching pulls |
| punching | heavy clenched fists |
| flames | burning hot flames |
| frostbites | freezing cold |
| sparks | shocking electric arcs |
| falling | the rough ground |

| name | health form | source id |
|------|------|-----------|
| slashing | a keen cutting blade | slashing |
| piercing | a sharp stabbing point | piercing |
| bludgeoning | a heavy blunt weapon | bludgeoning |
| punching | heavy clenched fists | bludgeoning (in combat only) |
| burning | searing fire | burning |
| freezing | biting frost | freezing |
| shocking | crackling lightning | shocking |
| mauling | teeth and claws | mauling |
| generic | a hard blow | generic |
| falling | a hard landing | falling_real |

