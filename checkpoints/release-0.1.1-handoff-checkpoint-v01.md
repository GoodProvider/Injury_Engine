# Release 0.1.1 test package — handoff v01 (2026-10-17)

- `check-version.py -v 0.1.1` passes.
- Papyrus recompiled (5 scripts) → `Scripts/*.pex`.
- `make SHELL=cmd.exe release` → `versions/Injury_Engine 0.1.1.7z` (64 files: FOMOD, core, images, fight_actions, overlay). DLL was up to date (13:40 build); ESP rebuilt from Spriggit.
- Makefile fix: the old-archive `del` line now converts `/` to `\` (`$(subst /,\,${RELEASE_FILE})`); before, cmd failed with "The system cannot find the path specified" whenever the 7z already existed.
- Not done (test package): no commit, tag or GitHub Release.

Next: install the 7z in MO2, test in game; if it's good, commit the Makefile fix and tag.
