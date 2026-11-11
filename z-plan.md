**2. In-game checks not done yet:**
- An older save with wounds, made before race groups, loads correctly.
ignore 
- Werewolf tail wounds heal.
verified 
- Part names sent from the scripts land on the right part of a non-human body.
- The overlay on Magelight UI: the Magelight checkpoint was still "in-game test pending". Its bars for 7-part bodies haven't been checked either.
verified 
- VR hasn't been tested on this machine.
i can not test, mark as untested in game 

**3. Known gaps you can ship with or fix first:**
- Most other creatures still use humanoid text, with a warning in the log: dragons, trolls, Dwarven constructs, atronachs, spirits, spriggans, birds, fish, mudcrabs, netch, the vampire lord form. They work; they just read as human wounds.
add remaining creatures 
- The sexual parts on insectoid, werewolf and undead still use the human text.
add individual sexual parts 
- Durnehviir has no entry of his own in `races.json`.
fix it 
- One script log helper (`Main.PartsText`) prints the 12 human part names for every body. It only affects the log.
fix it 