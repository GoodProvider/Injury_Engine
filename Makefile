VERSION=0.1.5
NAME=Injury Engine

RELEASE_FILE=versions/Injury_Engine ${VERSION}.7z

# Build Injury_Engine.dll (SKSE_Source/, needs VCPKG_ROOT); copied into SKSE/Plugins/.
dll:
	cd SKSE_Source && cmake --preset release && cmake --build --preset build-release

# Rebuild the ESP from Spriggit/ (source of truth).
esp:
	if not exist "SpriggitCLI\Spriggit.CLI.exe" call updateSpriggit.bat
	SpriggitCLI\Spriggit.CLI.exe convert-to-plugin -i "Spriggit\Injury_Engine" -o "Injury_Engine.esp"

# Write ESP edits made in xEdit/CK back to Spriggit/.
serialize:
	if not exist "SpriggitCLI\Spriggit.CLI.exe" call updateSpriggit.bat
	SpriggitCLI\Spriggit.CLI.exe convert-from-plugin -i "Injury_Engine.esp" -o "Spriggit\Injury_Engine" -p Spriggit.Json.Skyrim -v 0.38.6 -g SkyrimSE

release:
	python3 ./python_scripts/check-version.py -v ${VERSION}
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force 'SKSE/Plugins/Injury_Engine' | Out-Null"
	python3 ./python_scripts/info.py -v ${VERSION} -n '${NAME}' -o SKSE/Plugins/Injury_Engine/info.json
	if not exist FOMOD mkdir FOMOD
	python3 ./python_scripts/fomod-update-name-version.py -v ${VERSION} -n '${NAME}' -o FOMOD/info.xml FOMOD_source/info.xml
	python3 ./python_scripts/fomod-update-name-version.py -v ${VERSION} -n '${NAME}' -o FOMOD/ModuleConfig.xml FOMOD_source/ModuleConfig.xml
	$(MAKE) dll
	$(MAKE) esp
	if not exist versions mkdir versions
	if exist "$(subst /,\,${RELEASE_FILE})" del /q "$(subst /,\,${RELEASE_FILE})"
	if exist "$(subst /,\,core)" rmdir /s /q "$(subst /,\,core)"
	if exist "$(subst /,\,fight_actions)" rmdir /s /q "$(subst /,\,fight_actions)"
	if exist "$(subst /,\,overlay)" rmdir /s /q "$(subst /,\,overlay)"
	mkdir core
	powershell -NoProfile -Command "Copy-Item -Path 'Scripts','SKSE','meshes','textures','Sound','PrismaUI','Seq','Injury_Engine.esp','CHANGELOG-player.md' -Destination 'core/.' -Recurse -Force"
# FOMOD option "Add fight/stop/yield actions": move the fight actions and their decision prompts out of core.
	powershell -NoProfile -Command "foreach ($$f in 'actions/Start_Fighting.yaml','actions/Stop_Fighting.yaml','actions/Stop_Fighting_by_Yielding.yaml','prompts/decisions/injury_engine/accept_truce.prompt','prompts/decisions/injury_engine/accept_surrender.prompt') { $$s = Join-Path 'core/SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine' $$f; $$d = Split-Path (Join-Path 'fight_actions/SKSE/Plugins/SkyrimNet/external/goodprovider.injuryengine' $$f); New-Item -ItemType Directory -Force $$d | Out-Null; Move-Item $$s $$d -Force -ErrorAction Stop }"
# FOMOD option "Show torture overlay": core defaults the overlay off, the option restores the originals (default on).
	powershell -NoProfile -Command "foreach ($$f in 'SkyrimNet/config/plugins/Injury_Engine/manifest.yaml','Injury_Engine/injuries/whipping.json') { $$d = Split-Path (Join-Path 'overlay/SKSE/Plugins' $$f); New-Item -ItemType Directory -Force $$d | Out-Null; Copy-Item (Join-Path 'SKSE/Plugins' $$f) $$d -Force -ErrorAction Stop }"
	python3 ./python_scripts/fomod-overlay-off.py core/SKSE/Plugins/SkyrimNet/config/plugins/Injury_Engine/manifest.yaml core/SKSE/Plugins/Injury_Engine/injuries/whipping.json
	7z -bb1 a '${RELEASE_FILE}' -aoa FOMOD core images fight_actions overlay
	if exist "$(subst /,\,core)" rmdir /s /q "$(subst /,\,core)"
	if exist "$(subst /,\,fight_actions)" rmdir /s /q "$(subst /,\,fight_actions)"
	if exist "$(subst /,\,overlay)" rmdir /s /q "$(subst /,\,overlay)"
