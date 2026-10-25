cmake_minimum_required(VERSION 3.21)

# Project name = SKSE .dll name.
project(Injury_Engine VERSION 0.1.5 LANGUAGES CXX)

# This repo is the MO2 mod folder, so the .dll is copied straight into ../SKSE/Plugins.
set(OUTPUT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

# CommonLibSSE: MinLL/CommonLibVR 4.39.5 (MIT) from the overlay port vcpkg-ports/commonlibsse-ng-fork
# (same pin as SkyrimNet_SexLab). Its config also brings add_commonlibsse_plugin().
find_package(CommonLibSSE CONFIG REQUIRED)
# CommonLibSSE's exported link interface names Microsoft::DirectXTK, so it has to be found here.
find_package(directxtk CONFIG REQUIRED)

file(GLOB SOURCES
    "src/*.cpp"
    "include/*.h"
    "version.rc"
)

add_commonlibsse_plugin(${PROJECT_NAME} SOURCES ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/PrismaUI
    # SkyrimNet public API (PublicAPI.h; the decision request needs API v12+)
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SkyrimNet devkit/CppAPI"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
)

# Workaround for MSBuild 17.14 bug: create props file to override ATL_KeyFile
if(MSVC)
    set(PROPS_FILE "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.props")
    file(WRITE "${PROPS_FILE}" "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n")
    file(APPEND "${PROPS_FILE}" "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n")
    file(APPEND "${PROPS_FILE}" "  <PropertyGroup>\n")
    file(APPEND "${PROPS_FILE}" "    <ATL_KeyFile></ATL_KeyFile>\n")
    file(APPEND "${PROPS_FILE}" "  </PropertyGroup>\n")
    file(APPEND "${PROPS_FILE}" "</Project>\n")
    set_target_properties(${PROJECT_NAME} PROPERTIES VS_USER_PROPS "${PROJECT_NAME}.props")

    set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_link_options(${PROJECT_NAME} PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF)
endif()

# Copy the .dll (and .pdb on Debug) into this mod's SKSE/Plugins folder.
set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")
add_custom_command(
    TARGET "${PROJECT_NAME}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM
)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )
endif()
