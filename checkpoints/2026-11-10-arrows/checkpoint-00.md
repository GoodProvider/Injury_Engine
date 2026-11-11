The user wants better arrow descriptions:
```
Description: There was an arrow fight!! 

(In all seriousness: all my hits that were not fire were arrow hits.)

Expected behavior: Arrow hits should be described as arrow hits, not as melee stabbing attacks.

Actual behavior: Injury Engine correctly registers the damage as piercing, but generates descriptions such as:

"Dwimm hits Draugr with a sharp stabbing point hitting Draugr's right arm into flesh that is already split."

The log confirms that the hits are registered, but the generated descriptions don't distinguish arrows from stabbing weapons.

I've attached the verbose log.
```

can you identify arrow damage?
Create for arrow-piercing damage for all creature types. 