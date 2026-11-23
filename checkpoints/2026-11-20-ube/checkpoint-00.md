use says "
```
Using the UBE body which is a race of it's own, and the descriptions for my player character being wounded come out as if she were a draugr  https://i.gyazo.com/9613be8f3279e0f72d26ee5b5c095224.png 

Good Provider — Yesterday at 9:17 PM
will look, logs always welcome
Anna — Yesterday at 9:19 PM
I noticed this from the logs: [06:40:28] race dump: UBE_AllRace.esp 1B005735 00UBE_BretonRaceVampire -> humanoid_undead (keywords)
should be humanoid_vampire?
Anna — Yesterday at 9:44 PM
I added these to the races.json under "humanoid", I think that fixes it? pretty illiterate when it comes to this stuff.

      "00UBE_BretonRace",
      "00UBE_DarkElfRace",
      "00UBE_HighElfRace",
      "00UBE_ImperialRace",
      "00UBE_NordRace",
      "00UBE_OrcRace",
      "00UBE_RedguardRace",
      "00UBE_WoodElfRace",
      "00UBE_BretonRaceVampire",
      "00UBE_DarkElfRaceVampire",
      "00UBE_HighElfRaceVampire",
      "00UBE_ImperialRaceVampire",
      "00UBE_NordRaceVampire",
      "00UBE_OrcRaceVampire",
      "00UBE_RedguardRaceVampire",
      "00UBE_WoodElfRaceVampire",
```

Default for an unknown race should be humanoid. 
you can find UBE source in ../UBE 2.0
It would appear you can find the map between the UBE and it's group by removing 00UBE_ from the race.