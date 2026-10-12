VERSION=0.1.0
NAME=SkyrimNet Whipped

RELEASE_FILE=versions/SkyrimNet_Whipped ${VERSION}.7z

# Build SkyrimNet_Whipped.dll (SKSE_Source/, needs VCPKG_ROOT); copied into SKSE/Plugins/.
dll:
	cd SKSE_Source && cmake --preset release && cmake --build --preset build-release

# Rebuild the ESP from Spriggit/ (source of truth).
esp:
	if not exist "SpriggitCLI\Spriggit.CLI.exe" call updateSpriggit.bat
	SpriggitCLI\Spriggit.CLI.exe convert-to-plugin -i "Spriggit\SkyrimNet_Whipped" -o "SkyrimNet_Whipped.esp"

# Write ESP edits made in xEdit/CK back to Spriggit/.
serialize:
	if not exist "SpriggitCLI\Spriggit.CLI.exe" call updateSpriggit.bat
	SpriggitCLI\Spriggit.CLI.exe convert-from-plugin -i "SkyrimNet_Whipped.esp" -o "Spriggit\SkyrimNet_Whipped" -p Spriggit.Json.Skyrim -v 0.38.6 -g SkyrimSE

release:
	python3 ./python_scripts/check-version.py -v ${VERSION}
	powershell -NoProfile -Command "New-Item -ItemType Directory -Force 'SKSE/Plugins/SkyrimNet_Whipped' | Out-Null"
	python3 ./python_scripts/info.py -v ${VERSION} -n '${NAME}' -o SKSE/Plugins/SkyrimNet_Whipped/info.json
	if not exist FOMOD mkdir FOMOD
	python3 ./python_scripts/fomod-update-name-version.py -v ${VERSION} -n '${NAME}' -o FOMOD/info.xml FOMOD_source/info.xml
	python3 ./python_scripts/fomod-update-name-version.py -v ${VERSION} -n '${NAME}' -o FOMOD/ModuleConfig.xml FOMOD_source/ModuleConfig.xml
	$(MAKE) dll
	$(MAKE) esp
	if not exist versions mkdir versions
	if exist "${RELEASE_FILE}" del /q "${RELEASE_FILE}"
	if exist "$(subst /,\,core)" rmdir /s /q "$(subst /,\,core)"
	mkdir core
	powershell -NoProfile -Command "Copy-Item -Path 'Scripts','SKSE','meshes','textures','Sound','PrismaUI','Seq','SkyrimNet_Whipped.esp','CHANGELOG-player.md' -Destination 'core/.' -Recurse -Force"
	7z -bb1 a '${RELEASE_FILE}' -aoa FOMOD core
	if exist "$(subst /,\,core)" rmdir /s /q "$(subst /,\,core)"
