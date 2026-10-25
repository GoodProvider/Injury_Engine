The game froze on the user 
- bug-logs/freeze/Injury_Engine-freez.log is this log 
- bug-logs/freeze/injury-engine-pickpart.patch is a different agent's recommanded patch

Review the log and the patch, for the best solution to the bug.

user comments: 
no crash log
the game froze
it's what I described above, the plugin caused a thread to hang in deadlock
very difficult to find and requires complex steps to trace down
The bigger news: the upscaler didn’t cause the freeze. Injury Engine did.

One stuck thread. Of the 580 threads in the game, only one was still running. It’s one of the game’s worker threads that update actors, and it was stuck inside Injury_Engine and never returned. The main thread waits for that worker to finish its job, so the game froze.
How it got there. Your Fireball’s burning damage on a horker went through the damage hooks of ChocolatePoise, ActorValueExtension and Battle Summaries into Injury Engine’s own damage hook, which never returned.
Both freezes match. In both runs, Injury Engine’s last log line is your Fireball burning a horker, followed by a “torture … punching” line for the same hit. The game stopped drawing frames within a second of that line.
The upscaler isn’t in any thread’s stack. Turning it off most likely changed the timing enough that this test didn’t hit the hang.
Battle Summaries only passes through. I checked its code: its hook calls the next one with the same values. Our fix isn’t involved.
What the report shows

The cause: Injury Engine’s damage loop can run forever. It happens when every body part holding torture points is already at its maximum and what’s left of a hit is smaller than one torture point. That loop runs on one of the game’s worker threads while holding the plugin’s lock, and the main thread waits for that worker, so the game freezes without a crash log.
How it hit you: the horkers were badly hurt from fighting wolves, then got a “punching” torture point on an already-full part. The next small Fireball burn tick started the endless loop. Both of your freezes follow this pattern.
Proof: the DLL you sent is byte-identical to the one in the repository at a known commit. That let me match all three Injury Engine addresses on the stuck thread to exact lines in its source.
A test program, included in the report, copies the loop. As shipped, your exact case never returns; with the fix, it returns at once. Over 20,000 random cases the shipped loop hung 4,562 times and the fixed one never did.
The fix: a small change to how the plugin picks a part, plus an optional limit on the loop so a future slip costs a log line instead of a frozen game. I couldn’t build the plugin here, so the patch is only tested in that program, and the report says so.
Three lower-priority notes:
why a Fireball got counted as “punching”;
real damage can land on body parts that are switched off;
a possible problem with the hook’s return value, raised as a question only.
the report should tell all the details