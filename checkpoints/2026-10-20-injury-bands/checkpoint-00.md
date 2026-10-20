We are going to change how damage moves up the severity levels:

# manifest setting dashboard
Just below the narration section at the top we will now add 
a Injury Ranges section. It will have a set of slides
that control the maximum level of damage a body part can get.

| label | default | 
| ------|---------|
| Minium Damage for minor| minor | 25% | 
| Minium Damage for major| major | 75% | 
| Minium Damage for sever| sever | 90% | 

- damage below minor's minimum produces no narration
- damage between major's and sever's minimum produces major narration
- damage above or equal to sever's minimum produces sever narration

A body party can not take damage above the current level.

Take the body part's current points and change the ratio of points to each level to match the new division created by the minium.

example out of 15 points

| minimum | range | percent points | points | 
| 25      | 25-74 | 65%            | 10 | 
| 75      | 75-89 | 19%            | 3 | 
| 90      | 90    | 84%            | 2 | 