# Ammerow modifications Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.7...3.26 FATAL_ERROR)
project(Ammerow LANGUAGES C)
string(TOLOWER ${PROJECT_NAME} OUR_EXECUTABLE_NAME)

# The single public version authority is deliberately a plain file so the
# executable, documentation, and packaging tools can all consume it without
# maintaining competing constants.
set(AMMEROW_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/version")
if(NOT EXISTS "${AMMEROW_VERSION_FILE}")
    message(FATAL_ERROR "Missing Ammerow version file: ${AMMEROW_VERSION_FILE}")
endif()
file(STRINGS "${AMMEROW_VERSION_FILE}" AMMEROW_VERSION LIMIT_COUNT 1)
string(STRIP "${AMMEROW_VERSION}" AMMEROW_VERSION)
if(NOT AMMEROW_VERSION MATCHES
        "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9A-Za-z.-]+))?$")
    message(FATAL_ERROR
        "Ammerow version must be SemVer-like MAJOR.MINOR.PATCH[-PRERELEASE]: ${AMMEROW_VERSION}")
endif()
set(AMMEROW_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(AMMEROW_VERSION_MINOR "${CMAKE_MATCH_2}")
set(AMMEROW_VERSION_PATCH "${CMAKE_MATCH_3}")
set(AMMEROW_VERSION_PRERELEASE "${CMAKE_MATCH_5}")
set(AMMEROW_VERSION_BUILD 0)
set(AMMEROW_VERSION_FILE_FLAGS "0x0L")
if(AMMEROW_VERSION_PRERELEASE)
    set(AMMEROW_VERSION_FILE_FLAGS "VS_FF_PRERELEASE")
    if(AMMEROW_VERSION_PRERELEASE MATCHES "(^|\\.)([0-9]+)$")
        set(AMMEROW_VERSION_BUILD "${CMAKE_MATCH_2}")
    endif()
endif()
message(STATUS "Ammerow version: ${AMMEROW_VERSION}")

# Append the custom modules to the search path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/modules")

# SDL3 is the only release-supported Ammerow frontend.  These inherited
# frontends remain best-effort developer/ancestry builds and are not product
# compatibility promises.
option(SUPPORT_GCU_FRONTEND "Inherited best-effort GCU developer frontend (not release-supported)." OFF)
option(SUPPORT_NCURSES_FRONTEND "Inherited synonym for the best-effort GCU developer frontend." OFF)
option(SUPPORT_SDL_FRONTEND "Inherited best-effort SDL1 developer frontend (not release-supported)." OFF)
option(SUPPORT_SDL2_FRONTEND "Inherited best-effort SDL2 developer frontend (not release-supported)." OFF)
option(SUPPORT_SDL3_FRONTEND "Support for ASCII-first SDL3 front end." OFF)
option(SUPPORT_HEADLESS_FRONTEND
    "Support for deterministic off-screen development scenarios; requires SDL3."
    OFF)
option(SUPPORT_X11_FRONTEND "Inherited best-effort X11 developer frontend (not release-supported)." OFF)
option(SUPPORT_SDL_SOUND "Inherited SDL1 sound support (not release-supported)." OFF)
option(SUPPORT_SDL2_SOUND "Inherited SDL2 sound support (not release-supported)." OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SPOIL_DEFAULT OFF)
else()
    set(SPOIL_DEFAULT ON)
endif()
option(SUPPORT_SPOIL_FRONTEND "Support for spoiler front end." ${SPOIL_DEFAULT})
option(SUPPORT_STATS_FRONTEND "Support for statistics front end; requires sqlite3 development library." OFF)
option(SUPPORT_TEST_FRONTEND "Support for test front end." OFF)
option(SUPPORT_WINDOWS_FRONTEND "Inherited best-effort native Windows frontend (not release-supported)." OFF)
option(SUPPORT_BUNDLED_PNG "Use bundled Windows PNG+Zlib (32-bit x86 only)" OFF)
option(SUPPORT_STATIC_LINKING "Enable static linking where possible" OFF)
option(SUPPORT_STATS_BACKEND "Enable backend support for statistics and related debugging commands.  Implied by SUPPORT_STATS_FRONTEND." OFF)

# By default, generate a self-contained build left where the build was run.
# If not using the Windows front end, the executable will have hardwired
# relative paths to the game's data, and when run, the working directory should
# be the location of executable.  The other options are to build for a shared
# installation with a setgid executable that manages the save and high score
# files or to build for a read-only installation where all the variable
# persistent state is stored in the user's directories.  Those two other
# options use paths from GNUInstallDirs as the basis for where stuff is
# installed.  Because paths are configured into the executable, it is not
# possible to set the destination dir when running make (i.e. with DESTDIR).
# One can set the paths when running cmake.  To set the path prefix, set
# CMAKE_INSTALL_PREFIX.  To customize where the pieces go beneath that prefix,
# set CMAKE_INSTALL_BINDIR, CMAKE_INSTALL_DATAROOTDIR, CMAKE_INSTALL_SYSCONFDIR,
# or CMAKE_INSTALL_SHAREDSTATEDIR.  Use the INSTALL_GROUP_ID cache variable
# (a string either holding the name or number of the group) as the source for
# the group ID to use for the setgid executable.  Note that with SHARED_INSTALL,
# running "make install" will always require superuser privileges because
# the owner of some of the installed files and directories is set to
# root:${INSTALL_GROUP_ID}.
option(SC_INSTALL "Generate a self-contained build that could be left as is or moved elsewhere.  Conflicts with SHARED_INSTALL and READONLY_INSTALL." OFF)
option(SHARED_INSTALL "Install for shared use with a setgid executable.  Conflicts with SC_INSTALL, READONLY_INSTALL, and SUPPORT_WINOOWS_FRONTEND." OFF)
set(INSTALL_GROUP_ID "games" CACHE STRING "Group ID (either name or number) to use if SHARED_INSTALL is on.")
option(READONLY_INSTALL "Install for shared use with variable persistent data stored in the user's directories.  Conflicts with SC_INSTALL, SHARED_INSTALL, and SUPPORT_WINDOWS_FRONTEND." OFF)
include(GNUInstallDirs)

set(AMMEROW_PACKAGE_PROFILE "development" CACHE STRING
	"Install profile: development or full-content release (audio-playtest is a legacy alias).")
set_property(CACHE AMMEROW_PACKAGE_PROFILE PROPERTY STRINGS
	development release audio-playtest)
string(TOLOWER "${AMMEROW_PACKAGE_PROFILE}" AMMEROW_PACKAGE_PROFILE_NORMALIZED)
if(AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "audio-playtest")
    message(STATUS "audio-playtest now aliases the full-content release profile")
    set(AMMEROW_PACKAGE_PROFILE_NORMALIZED "release")
endif()
if(NOT AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "development" AND
		NOT AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "release" AND
		NOT AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "audio-playtest")
    message(FATAL_ERROR
        "Unknown AMMEROW_PACKAGE_PROFILE: ${AMMEROW_PACKAGE_PROFILE}")
endif()
if(NOT SUPPORT_SDL3_FRONTEND AND
        NOT AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "development")
    message(FATAL_ERROR
        "Ammerow release package profiles require SUPPORT_SDL3_FRONTEND.")
endif()
include(AmmerowPackage)
message(STATUS "Ammerow install profile: ${AMMEROW_PACKAGE_PROFILE_NORMALIZED}")

# Allow building the documentation if sphinx-build is available.
include(CMakeDependentOption)
find_package(Sphinx)
cmake_dependent_option(BUILD_DOC "Build the documentation" OFF "Sphinx_FOUND" OFF)
set(DOC_HTML_THEME "" CACHE STRING "If set and not an empty string, it is the builtin Sphinx HTML theme to use in place of the default theme in conf.py (currently the better theme).")

# Allow the user to enable code coverage if the system supports it.
include(src/cmake/modules/CheckCoverage.cmake)
check_coverage(Coverage_FOUND)
cmake_dependent_option(SUPPORT_COVERAGE "Compile for reporting code coverage" OFF "Coverage_FOUND" OFF)

if((SUPPORT_NCURSES_FRONTEND) AND (NOT SUPPORT_GCU_FRONTEND))
    set(SUPPORT_GCU_FRONTEND ON)
endif()
if(SUPPORT_GCU_FRONTEND OR SUPPORT_SDL_FRONTEND OR SUPPORT_SDL2_FRONTEND OR
        SUPPORT_X11_FRONTEND OR SUPPORT_WINDOWS_FRONTEND)
    message(STATUS
        "Configuring an inherited best-effort frontend; Ammerow releases support SDL3 only.")
endif()
if((SUPPORT_STATS_FRONTEND) AND (NOT SUPPORT_STATS_BACKEND))
    set(SUPPORT_STATS_BACKEND ON)
endif()
# If none of the graphical front ends will be configured, configure the one for
# Windows if that's the target plaform or the X11 one for anything else.
if((NOT SUPPORT_GCU_FRONTEND) AND (NOT SUPPORT_SDL_FRONTEND) AND (NOT SUPPORT_SDL2_FRONTEND) AND (NOT SUPPORT_SDL3_FRONTEND) AND (NOT SUPPORT_HEADLESS_FRONTEND) AND (NOT SUPPORT_WINDOWS_FRONTEND) AND (NOT SUPPORT_X11_FRONTEND) AND (NOT SUPPORT_TEST_FRONTEND) AND (NOT SUPPORT_STATS_FRONTEND) AND (NOT SUPPORT_SPOIL_FRONTEND))
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(SUPPORT_WINDOWS_FRONTEND ON)
    else()
        set(SUPPORT_X11_FRONTEND ON)
    endif()
endif()
# Can not have support for SDL and SDL2 at the same time.
if(SUPPORT_SDL2_FRONTEND OR SUPPORT_SDL2_SOUND)
    if(SUPPORT_SDL_FRONTEND OR SUPPORT_SDL_SOUND)
        message(FATAL_ERROR "Can not configure with support for both SDL and SDL2")
    endif()
endif()
if(SUPPORT_HEADLESS_FRONTEND AND NOT SUPPORT_SDL3_FRONTEND)
    message(FATAL_ERROR
        "SUPPORT_HEADLESS_FRONTEND requires SUPPORT_SDL3_FRONTEND so captures use the production presenter.")
endif()
# SDL_ttf 2 and SDL3_ttf export overlapping symbols.  Keep the SDL3 frontend
# in a separate build from the older SDL frontends and sound backends.
if(SUPPORT_SDL3_FRONTEND)
    if(SUPPORT_SDL_FRONTEND OR SUPPORT_SDL2_FRONTEND OR SUPPORT_SDL_SOUND OR SUPPORT_SDL2_SOUND)
        message(FATAL_ERROR "Can not configure the SDL3 front end with SDL or SDL2 support")
    endif()
endif()
if(SUPPORT_WINDOWS_FRONTEND)
    # The Windows front end bypasses main.c so can't use any of the other front
    # ends when it is used.  For somewhat similar reasons, disable SDL or SDL2
    # sound with the Windows front end.
    if(SUPPORT_HEADLESS_FRONTEND)
        message(WARNING "Disabling headless front end because Windows front end is enabled")
        set(SUPPORT_HEADLESS_FRONTEND OFF)
    endif()
    if(SUPPORT_GCU_FRONTEND)
        message(WARNING "Disabling GCU front end because Windows front end is enabled")
        set(SUPPORT_GCU_FRONTEND OFF)
    endif()
    if(SUPPORT_SDL_FRONTEND)
        message(WARNING "Disabling SDL front end because Windows front end is enabled")
        set(SUPPORT_SDL_FRONTEND OFF)
    endif()
    if(SUPPORT_SDL2_FRONTEND)
        message(WARNING "Disabling SDL2 front end because Windows front end is enabled")
        set(SUPPORT_SDL2_FRONTEND OFF)
    endif()
    if(SUPPORT_SDL3_FRONTEND)
        message(WARNING "Disabling SDL3 front end because Windows front end is enabled")
        set(SUPPORT_SDL3_FRONTEND OFF)
    endif()
    if(SUPPORT_SPOIL_FRONTEND)
        message(WARNING "Disabling spoiler front end because Windows front end is enabled")
        set(SUPPORT_SPOIL_FRONTEND OFF)
    endif()
    if(SUPPORT_STATS_FRONTEND)
        message(WARNING "Disabling statistics front end because Windows front end is enabled")
        set(SUPPORT_STATS_FRONTEND OFF)
    endif()
    if(SUPPORT_TEST_FRONTEND)
        message(WARNING "Disabling test front end because Windows front end is enabled")
        set(SUPPORT_TEST_FRONTEND OFF)
    endif()
    if(SUPPORT_X11_FRONTEND)
        message(WARNING "Disabling X11 front end because Windows front end is enabled")
        set(SUPPORT_X11_FRONTEND OFF)
    endif()
    if(SUPPORT_SDL_SOUND)
        message(WARNING "Disabling SDL sound because Windows front end is enabled")
        set(SUPPORT_SDL_SOUND OFF)
    endif()
    if(SUPPORT_SDL2_SOUND)
        message(WARNING "Disabling SDL2 sound because Windows front end is enabled")
        set(SUPPORT_SDL2_SOUND OFF)
    endif()
    # Set up the path for the source of the resource file.
    string(CONCAT OUR_WINDOWS_RC src/win/ ${OUR_EXECUTABLE_NAME} .rc)
    # Enable compilation of resource files.
    enable_language(RC)
endif()

# The supported SDL3 executable has its own small Windows resource. The icon
# is an adjacent, separately licensed product asset and is intentionally absent
# from public GPL source exports; those exports still compile with version
# metadata and the platform's default executable icon.
set(AMMEROW_SDL3_WINDOWS_RESOURCE "")
if(WIN32 AND SUPPORT_SDL3_FRONTEND)
    enable_language(RC)
    set(AMMEROW_ICON_RESOURCE "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ammerow.ico")
        file(TO_CMAKE_PATH
            "${CMAKE_CURRENT_SOURCE_DIR}/src/win/ammerow.ico"
            AMMEROW_ICON_PATH)
        set(AMMEROW_ICON_RESOURCE
            "AMMEROW_ICON ICON \"${AMMEROW_ICON_PATH}\"")
    endif()
    configure_file(src/win/ammerow.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/ammerow.rc" @ONLY)
    set(AMMEROW_SDL3_WINDOWS_RESOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/ammerow.rc")
endif()

# If none of SC_INSTALL, SHARED_INSTALL, and READONLY_INSTALL are set, use
# SC_INSTALL.
if((NOT SC_INSTALL) AND (NOT SHARED_INSTALL) AND (NOT READONLY_INSTALL))
    set(SC_INSTALL ON)
endif()
if(SHARED_INSTALL)
    if(SC_INSTALL)
        message(FATAL_ERROR "Conflicting options, SHARED_INSTALL and SC_INSTALL, enabled")
    endif()
    if(READONLY_INSTALL)
        message(FATAL_ERROR "Conflicting options, SHARED_INSTALL and READONLY_INSTALL, enabled")
    endif()
    if(SUPPORT_WINDOWS_FRONTEND)
        message(FATAL_ERROR "Conflicting options, SHARED_INSTALL and SUPPORT_WINDOWS_FRONTEND, enabled")
    endif()
endif()
if(READONLY_INSTALL)
    if(SC_INSTALL)
        message(FATAL_ERROR "Conflicting options, READONLY_INSTALL and SC_INSTALL, enabled")
    endif()
    if(SUPPORT_WINDOWS_FRONTEND)
        message(FATAL_ERROR "Conflicting options, READONLY_INSTALL and SUPPORT_WINDOWS_FRONTEND, enabled")
    endif()
endif()

# Put all executables and lib resources (and DLLs on Windows) into build/game
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")

# Global compiler settings
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused-parameter)
endif()

add_library(OurCoreLib OBJECT
		src/artifact-balance-data.c
		src/artifact-lore-data.c
        src/buildid.c
        src/cave-map.c
        src/cave-square.c
        src/cave-view.c
        src/cave.c
        src/cmd-cave.c
        src/cmd-core.c
		src/cmd-fishing.c
		src/cmd-larder.c
		src/cmd-mode-policy.c
        src/cmd-misc.c
        src/cmd-obj.c
        src/cmd-pickup.c
        src/cmd-spelunking.c
        src/cmd-spoil.c
        src/cmd-wizard.c
        src/cmd-world.c
        src/datafile.c
        src/debug.c
        src/effect-handler-attack.c
        src/effect-handler-general.c
        src/effects.c
        src/effects-info.c
        src/fishing-data.c
        src/terrain-visual-data.c
        src/map-visual-data.c
        src/game-event.c
        src/game-input.c
        src/game-world.c
		src/world-data.c
        src/world-destination.c
        src/world-entry.c
		src/world-fishing.c
		src/world-fishing-discovery.c
		src/world-fishing-site.c
		src/world-generation.c
		src/world-larder.c
		src/world-larder-data.c
		src/relic-broker-data.c
		src/relic-broker.c
		src/relic-broker-save.c
		src/world-objective-data.c
		src/world-story-data.c
		src/world-location.c
		src/world-map.c
		src/world-object-transfer.c
		src/world-overworld.c
        src/world-spelunking.c
        src/world-spelunking-adapter.c
        src/world-spelunking-actor-data.c
        src/world-spelunking-breath.c
		src/world-spelunking-combat.c
		src/world-spelunking-codec.c
        src/world-spelunking-data-util.c
        src/world-spelunking-effects.c
        src/world-spelunking-inventory.c
        src/world-spelunking-layout.c
        src/world-spelunking-layout-data.c
        src/world-spelunking-local.c
		src/world-spelunking-population-data.c
        src/world-spelunking-generation.c
		src/world-spelunking-generation-endpoint.c
        src/world-spelunking-generation-geology.c
		src/world-spelunking-generation-population.c
        src/world-spelunking-generation-route.c
        src/world-spelunking-generation-system.c
        src/world-spelunking-generation-validation.c
        src/world-spelunking-generation-witness.c
        src/world-spelunking-geology-data.c
		src/world-spelunking-publication.c
		src/world-spelunking-passage.c
        src/world-spelunking-recipe-data.c
		src/world-spelunking-traversal-data.c
		src/world-spelunking-traversal-proof.c
        src/world-spelunking-water-data.c
        src/world-spelunking-runtime.c
        src/world-spelunking-system.c
        src/world-spelunking-system-data.c
        src/world-spelunking-transition.c
        src/world-spelunking-view.c
        src/world-spelunking-view-terrain.c
        src/world-spelunking-visibility.c
        src/world-travel-action.c
        src/world-travel-resources.c
        src/world-transition.c
        src/world-turn.c
        src/gen-cave.c
        src/gen-chunk.c
        src/gen-monster.c
        src/gen-room.c
        src/gen-util.c
        src/generate.c
        src/grafmode.c
        src/guid.c
        src/init.c
        src/load.c
        src/message.c
        src/mon-attack.c
        src/mon-blows.c
        src/mon-desc.c
        src/mon-group.c
        src/mon-init.c
        src/mon-list.c
        src/mon-lore.c
        src/mon-make.c
        src/mon-move.c
        src/mon-msg.c
        src/mon-predicate.c
        src/mon-spell.c
        src/mon-summon.c
        src/mon-timed.c
        src/mon-util.c
        src/obj-chest.c
        src/obj-curse.c
        src/obj-desc.c
        src/obj-gear.c
        src/obj-ignore.c
        src/obj-info.c
        src/obj-init.c
        src/obj-knowledge.c
        src/obj-list.c
        src/obj-make.c
        src/obj-pile.c
        src/obj-power.c
        src/obj-properties.c
        src/obj-randart.c
        src/obj-slays.c
        src/obj-tval.c
        src/obj-util.c
        src/option.c
        src/parser.c
        src/player-attack.c
        src/player-birth.c
        src/player-calcs.c
        src/player-class.c
        src/player-history.c
        src/player-path.c
        src/player-properties.c
		src/player-resource.c
        src/player-quest.c
        src/player-race.c
        src/player-spell.c
        src/player-timed.c
        src/player-util.c
        src/player.c
        src/project-feat.c
        src/project-mon.c
        src/project-obj.c
        src/project-player.c
        src/project.c
        src/randname.c
        src/save.c
        src/savefile.c
        src/score.c
        src/score-util.c
        src/sound-core.c
        src/source.c
        src/store.c
        src/target.c
        src/trap.c
        src/ui-birth.c
        src/ui-birth-screen.c
        src/ui-command.c
        src/ui-context.c
        src/ui-curse.c
        src/ui-death.c
        src/ui-display.c
        src/ui-effect.c
		src/ui-fishing.c
		src/ui-fishing-input.c
        src/ui-entry-combiner.c
        src/ui-entry-renderers.c
        src/ui-entry.c
        src/ui-equip-cmp.c
        src/ui-event.c
        src/ui-game.c
        src/ui-help.c
        src/ui-world-intro.c
        src/ui-history.c
        src/ui-history-screen.c
        src/ui-init.c
        src/ui-input.c
        src/ui-keymap.c
        src/ui-knowledge.c
        src/ui-knowledge-screen.c
        src/ui-map.c
        src/ui-menu.c
        src/ui-mode-input.c
        src/ui-nearby-screen.c
        src/ui-mon-list.c
        src/ui-mon-lore.c
        src/ui-obj-list.c
        src/ui-object.c
        src/ui-object-screen.c
        src/ui-options.c
        src/ui-output.c
        src/ui-player-properties.c
        src/ui-player.c
        src/ui-prefs.c
        src/ui-score.c
        src/ui-signals.c
        src/ui-spell.c
        src/ui-spoil.c
        src/ui-spelunking-appearance.c
        src/ui-spelunking.c
        src/ui-spelunking-input.c
        src/ui-spelunking-survey.c
        src/ui-store.c
        src/ui-store-screen.c
        src/ui-relic-broker.c
		src/ui-story.c
        src/ui-target.c
        src/ui-term.c
        src/ui-travel.c
        src/ui-world.c
        src/ui-visuals.c
        src/ui-wizard.c
        src/wiz-debug.c
        src/wiz-spoil.c
        src/wiz-stats.c
        src/z-bitflag.c
        src/z-color.c
        src/z-dice.c
        src/z-expression.c
        src/z-file.c
        src/z-form.c
        src/z-quark.c
        src/z-queue.c
        src/z-rand.c
        src/z-textblock.c
        src/z-type.c
        src/z-util.c
        src/z-virt.c
)

include("${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/check_core_manifest.cmake")
check_core_manifest("${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/check_test_manifest.cmake")
check_test_manifest("${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(OurCoreLib PROPERTIES C_STANDARD 99)
set(ANGBAND_CORE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(ANGBAND_CORE_LINK_LIBRARIES "")

if(SUPPORT_SDL3_FRONTEND)
    target_compile_definitions(OurCoreLib PRIVATE SOUND_SDL3 SOUND)
endif()

if(SUPPORT_COVERAGE)
    configure_target_for_coverage(OurCoreLib)
endif()

if(SUPPORT_SDL_SOUND OR SUPPORT_SDL2_SOUND)
    add_library(OurSoundSupportLib OBJECT
            src/snd-sdl.c
    )
    set_target_properties(OurSoundSupportLib PROPERTIES C_STANDARD 99)
    set(SOUND_SUPPORT_LIB OurSoundSupportLib)
    if(SUPPORT_COVERAGE)
        configure_target_for_coverage(OurSoundSupportLib)
    endif()
else()
    set(SOUND_SUPPORT_LIB "")
endif()

add_executable(OurExecutable
        $<TARGET_OBJECTS:OurCoreLib>
        ${AMMEROW_SDL3_WINDOWS_RESOURCE}
        $<$<BOOL:${SOUND_SUPPORT_LIB}>:$<TARGET_OBJECTS:${SOUND_SUPPORT_LIB}>>
        $<$<BOOL:${SUPPORT_GCU_FRONTEND}>:src/main-gcu.c>
        $<$<BOOL:${SUPPORT_SDL_FRONTEND}>:src/main-sdl.c>
        $<$<BOOL:${SUPPORT_SDL2_FRONTEND}>:src/main-sdl2.c>
        $<$<BOOL:${SUPPORT_SDL2_FRONTEND}>:src/sdl2/pui-ctrl.c>
        $<$<BOOL:${SUPPORT_SDL2_FRONTEND}>:src/sdl2/pui-dlg.c>
        $<$<BOOL:${SUPPORT_SDL2_FRONTEND}>:src/sdl2/pui-misc.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/main-sdl3.c>
        $<$<BOOL:${SUPPORT_HEADLESS_FRONTEND}>:src/main-headless.c>
        $<$<BOOL:${SUPPORT_HEADLESS_FRONTEND}>:src/headless/fall-bot.c>
        $<$<BOOL:${SUPPORT_HEADLESS_FRONTEND}>:src/headless/loop-bot.c>
        $<$<BOOL:${SUPPORT_HEADLESS_FRONTEND}>:src/headless/runner.c>
        $<$<BOOL:${SUPPORT_HEADLESS_FRONTEND}>:src/headless/scenario.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/snd-sdl3.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/audio.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/audio-wav.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/config.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/credits.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/dungeon-map-screen.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/effects.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/fishing.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/font.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/frontend-events.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/frontend-lifecycle.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/frontend-settings.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/home.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/world-intro.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/home-feature.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/home-layout.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/home-model.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/input.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/layout.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/legal.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/map-presenter.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/ascii-art.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/monster-art.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/inspect-card.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/inspect-card-presenter.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/monster-card-data.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/monster-card-layout.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/monster-card-presenter.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/monster-lore-screen.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/pause-menu.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/pause-menu-model.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/presentation.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/presenter.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/render.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/run-summary-screen.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/screen.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/screen-model.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/settings.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/settings-model.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/spelunking-presenter.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/store-art.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/terrain.c>
		$<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/tiles.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/term.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/theme.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/ui-draw.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/weather.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/world-map-screen.c>
        $<$<BOOL:${SUPPORT_SDL3_FRONTEND}>:src/sdl3/zoom.c>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:${OUR_WINDOWS_RC}>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:src/main-win.c>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:src/win/readdib.c>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:src/win/readpng.c>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:src/win/scrnshot.c>
        $<$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>:src/win/win-layout.c>
        $<$<BOOL:${SUPPORT_X11_FRONTEND}>:src/main-x11.c>
        $<$<BOOL:${SUPPORT_SPOIL_FRONTEND}>:src/main-spoil.c>
        $<$<BOOL:${SUPPORT_STATS_FRONTEND}>:src/main-stats.c>
        $<$<BOOL:${SUPPORT_STATS_FRONTEND}>:src/stats/db.c>
        $<$<BOOL:${SUPPORT_TEST_FRONTEND}>:src/main-test.c>
        $<$<NOT:$<BOOL:${SUPPORT_WINDOWS_FRONTEND}>>:src/main.c>
)

set_target_properties(OurExecutable PROPERTIES
    C_STANDARD 99
    OUTPUT_NAME ${OUR_EXECUTABLE_NAME}
)

# A Windows release is a copyable application, not a developer-machine build.
# Keep the MSVC runtime inside the executable and suppress the console window
# when the SDL3 front end owns all player interaction.  Development builds
# deliberately retain the normal dynamic CRT and console diagnostics.
if(MSVC AND NOT AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "development")
    set_property(TARGET OurCoreLib OurExecutable PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET OurExecutable PROPERTY WIN32_EXECUTABLE ON)
    target_link_options(OurExecutable PRIVATE "/ENTRY:mainCRTStartup")
endif()
target_include_directories(OurExecutable PRIVATE
    ${ANGBAND_CORE_INCLUDE_DIRS}
)
target_link_libraries(OurExecutable PRIVATE ${ANGBAND_CORE_LINK_LIBRARIES})

if(SUPPORT_HEADLESS_FRONTEND)
    target_compile_definitions(OurExecutable PRIVATE -D USE_HEADLESS)
endif()

# Allow for some introspection:  have rules to print out names that something
# downstream could use.  Have the output start with "our-<something>=" so it
# can very likely be separated from whatever other junk the build system prints
# out.
add_custom_target(print-project-name
    COMMAND "${CMAKE_COMMAND}" -E echo "our-project-name=${PROJECT_NAME}")
add_custom_target(print-executable-name
    COMMAND "${CMAKE_COMMAND}" -E echo "our-executable-name=$<TARGET_FILE_NAME:OurExecutable>")

if(SUPPORT_X11_FRONTEND)
    include(src/cmake/macros/X11_Frontend.cmake)
    configure_x11_frontend(OurExecutable)
endif()
if(SUPPORT_GCU_FRONTEND)
    include(src/cmake/macros/GCU_Frontend.cmake)
    configure_gcu_frontend(OurExecutable)
endif()
if(SUPPORT_SDL_FRONTEND)
    include(src/cmake/macros/SDL_Frontend.cmake)
    configure_sdl_frontend(OurExecutable)
elseif(SUPPORT_SDL2_FRONTEND)
    include(src/cmake/macros/SDL2_Frontend.cmake)
    configure_sdl2_frontend(OurExecutable)
elseif(SUPPORT_SDL3_FRONTEND)
    include(src/cmake/macros/SDL3_Frontend.cmake)
    configure_sdl3_frontend(OurExecutable)
endif()
if(SUPPORT_WINDOWS_FRONTEND)
    include(src/cmake/modules/PNG_Detection.cmake)
    determine_png(PNG_TARGET PNG_DLLS ${SUPPORT_BUNDLED_PNG})

    include(src/cmake/macros/WINDOWS_Frontend.cmake)
    configure_windows_frontend(OurExecutable ${PNG_TARGET})
    configure_windows_frontend(OurCoreLib "")
endif()

if(SUPPORT_SDL_SOUND)
    include(src/cmake/macros/SDL_Sound.cmake)
    configure_sdl_sound(OurExecutable NO)
    configure_sdl_sound(OurCoreLib YES)
    configure_sdl_sound(${SOUND_SUPPORT_LIB} NO)
endif()

if(SUPPORT_SDL2_SOUND)
    include(src/cmake/macros/SDL2_Sound.cmake)
    configure_sdl2_sound(OurExecutable NO)
    configure_sdl2_sound(OurCoreLib YES)
    configure_sdl2_sound(${SOUND_SUPPORT_LIB} NO)
endif()

if(SUPPORT_SPOIL_FRONTEND)
    include(src/cmake/macros/SPOIL_Frontend.cmake)
    configure_spoil_frontend(OurExecutable)
endif()

if(SUPPORT_STATS_FRONTEND)
    include(src/cmake/macros/STATS_Frontend.cmake)
    configure_stats_frontend(OurExecutable)
endif()

if(SUPPORT_STATS_BACKEND)
    include(src/cmake/macros/STATS_Backend.cmake)
    if(SUPPORT_STATS_FRONTEND)
        configure_stats_backend(OurExecutable NO)
    else()
        configure_stats_backend(OurExecutable YES)
    endif()
    configure_stats_backend(OurCoreLib NO)
endif()

if(SUPPORT_TEST_FRONTEND)
    include(src/cmake/macros/TEST_Frontend.cmake)
    configure_test_frontend(OurExecutable)
endif()

if(SUPPORT_COVERAGE)
    configure_target_for_coverage(OurExecutable)
endif()

# Retain the inherited variable name only at interfaces which still consume
# it. Its value now comes solely from Ammerow's tracked version authority.
set(ANGBAND_BUILD_ID "${AMMEROW_VERSION}")
set(ANGBAND_BUILD_ID_OPTION "BUILD_ID=${AMMEROW_VERSION}")
target_compile_definitions(OurExecutable PRIVATE "${ANGBAND_BUILD_ID_OPTION}")
target_compile_definitions(OurCoreLib PRIVATE "${ANGBAND_BUILD_ID_OPTION}")

# Set up to generate documentation if requested.
if(BUILD_DOC)
    include(src/cmake/modules/SphinxDocument.cmake)

    # Use a custom target to rewrite some of the configuration information
    # for sphinx-build.  Need to bring over the rest of the configuration
    # information from the source directories because of that.
    set(_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/manual-conf")
    add_custom_target(
        OurManualConf ALL
        "${CMAKE_COMMAND}" "-E" "make_directory" "${_OUTDIR}"
        COMMAND "${CMAKE_COMMAND}" "-E" "copy_directory"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/_static"
        "${_OUTDIR}/_static"
        COMMAND "${CMAKE_COMMAND}" "-E" "copy_directory"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/_templates"
        "${_OUTDIR}/_templates"
        VERBATIM
    )
    set_target_properties(OurManualConf PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_OUTDIR}")
    set(_SCRIPT "src/cmake/scripts/rewrite.cmake")
    set(_IN "docs/conf.py")
    set(_OUT "conf.py")
    add_custom_command(
        TARGET OurManualConf POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
        "-P" "${CMAKE_CURRENT_SOURCE_DIR}/${_SCRIPT}" --
        "${_OUTDIR}/${_OUT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${_IN}"
        "DOC_VERSION" "${ANGBAND_BUILD_ID}"
        "DOC_HTML_THEME" "${DOC_HTML_THEME}"
        BYPRODUCTS "${_OUTDIR}/${_OUT}"
        VERBATIM
    )

    # Add the target to generate the documentation.
    add_sphinx_document(
        OurManual
        SOURCE_DIR docs
        CONF_TARGET OurManualConf
        OUTPUT_FORMATS html
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/manual-output"
        ALL
        BUILD_FAIL_NO_SPHINX
    )

    if(SC_INSTALL)
        # Add another target to transfer the built documentation to the
        # self-contained installation.  Don't do that as an install target
        # since everything else will be put in place by the build process
        # without an install step.
        sphinx_document_get_output_directory(_DOC OurManual)
        sphinx_document_get_output_formats(_FORMATS OurManual)
        if(_DOC STREQUAL "_DOC-NOTFOUND")
            message(FATAL_ERROR "logic error:  Sphinx output directory not set for OurManual target")
        endif()
        if(_FORMATS STREQUAL "_FORMATS-NOTFOUND")
            message(FATAL_ERROR "logic error:  Sphinx output formats not set for OurManual target")
        endif()
        if(NOT (_DOC MATCHES "/$"))
            set(_DOC "${_DOC}/")
        endif()
        unset(_COMMANDS)
        foreach(_X ${_FORMATS})
            # Strip out the format-specific subdirectory when transferring
            # so there's less nesting.  Works okay with only HTML output;
            # will there be name conflicts if multiple formats are used?
            # Escape the generator expression to extract the executable's
            # directory so it will not be expanded here but will be expanded
            # in the add_custom_target() call below.
            list(
                APPEND
                _COMMANDS
                "COMMAND" "${CMAKE_COMMAND}" "-E" "copy_directory"
                "${_DOC}${_X}/"
                "\$<TARGET_FILE_DIR:OurExecutable>/docs"
            )
        endforeach()
        add_custom_target(
            OurManualTransfer ALL
            "${CMAKE_COMMAND}" "-E" "make_directory" "$<TARGET_FILE_DIR:OurExecutable>/docs"
            ${_COMMANDS}
            VERBATIM
        )
        add_dependencies(OurManualTransfer OurManual)
    endif()
endif()

set(EXECUTABLE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(DIR_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(VARDATADIR_PERMISSIONS ${DIR_PERMISIONS})
set(DATA_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

if(SHARED_INSTALL)
    include(src/cmake/macros/GID_Handling.cmake)
    configure_gid_handling(OurExecutable)
    configure_gid_handling(OurCoreLib)
    target_compile_definitions(OurExecutable PRIVATE -D SETGID)
    target_compile_definitions(OurCoreLib PRIVATE -D SETGID)
    set(EXECUTABLE_PERMISSIONS ${EXECUTABLE_PERMISSIONS} SETGID)
    # Override so the world has no access (read access for directory listing
    # would be okay but leads to confusing messages, for instance from ls,
    # when search access is not allowed; so disallow read access as well) and
    # group can do anything.  Means the world has to use the elevated
    # privileges of the setgid executatble to do stuff.
    set(
        VARDATADIR_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_WRITE GROUP_EXECUTE
    )
endif()

if(READONLY_INSTALL)
    target_compile_definitions(OurExecutable PRIVATE -D USE_PRIVATE_PATHS)
    target_compile_definitions(OurCoreLib PRIVATE -D USE_PRIVATE_PATHS)
endif()

# This is the executable to be used for the end-to-end tests; those will
# be run with the current working direcctory set to CMAKE_CURRENT_BINARY_DIR.
# Builds using an installation step will override this.
set(TEST_EXECUTABLE ./${OUR_EXECUTABLE_NAME})

if((SHARED_INSTALL) OR (READONLY_INSTALL))
    # When hardwiring paths, assume lib/gamedata from the source directories
    # is in DEFAULT_LIB_PATH rather than DEFAULT_CONFIG_PATH.  If not set,
    # have to change the installation of lib/gamedata so it's parent is the
    # same as lib/customize.
    target_compile_definitions(OurExecutable PRIVATE -D GAMEDATA_IN_LIB)
    target_compile_definitions(OurCoreLib PRIVATE -D GAMEDATA_IN_LIB)
    # The paths set here should end in a path separator and must match up with
    # the paths passed to INSTALL().
    set(ANGBAND_CONFIG_PATH ${CMAKE_INSTALL_FULL_SYSCONFDIR}/${PROJECT_NAME}/)
    set(ANGBAND_LIB_PATH ${CMAKE_INSTALL_FULL_DATAROOTDIR}/${PROJECT_NAME}/)
    set(ANGBAND_DATA_PATH ${CMAKE_INSTALL_FULL_SHAREDSTATEDIR}/${PROJECT_NAME}/)
    target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_CONFIG_PATH="${ANGBAND_CONFIG_PATH}")
    target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_LIB_PATH="${ANGBAND_LIB_PATH}")
    # This is always passed to init_file_paths() but is ignored internally when
    # the USE_PRIVATE_PATHS preprocessor macro is set (i.e. for READONLY_INSTALL
    # builds).
    target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_DATA_PATH="${ANGBAND_DATA_PATH}")

    # Set up for installation.
    install(TARGETS OurExecutable RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS ${EXECUTABLE_PERMISSIONS})
    install(FILES COPYING NOTICE.md ASSET-LICENSE.md README.md
        docs/copying.rst docs/thanks.rst
        DESTINATION ${CMAKE_INSTALL_DOCDIR} PERMISSIONS ${DATA_PERMISSIONS})
    set(INSTALLED_EXECUTABLE ${CMAKE_INSTALL_FULL_BINDIR}/${OUR_EXECUTABLE_NAME})
    set(TEST_EXECUTABLE ${INSTALLED_EXECUTABLE})
    set(ANGBAND_CHANGE_OWNER ${INSTALLED_EXECUTABLE})
    if(AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "development")
        set(ANGBAND_DIRS gamedata help screens)
        if((SUPPORT_SDL_FRONTEND) OR (SUPPORT_SDL2_FRONTEND) OR
                (SUPPORT_WINDOWS_FRONTEND))
            set(ANGBAND_DIRS ${ANGBAND_DIRS} fonts icons tiles)
        elseif(SUPPORT_SDL3_FRONTEND)
            set(ANGBAND_DIRS ${ANGBAND_DIRS} fonts icons)
        endif()
        if((SUPPORT_WINDOWS_FRONTEND) OR (SUPPORT_SDL_SOUND) OR
                (SUPPORT_SDL2_SOUND) OR (SUPPORT_SDL3_FRONTEND))
            set(ANGBAND_DIRS ${ANGBAND_DIRS} sounds)
        endif()
        foreach(ANGBAND_DIR ${ANGBAND_DIRS})
            set(ANGBAND_DEST ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME})
            install(DIRECTORY lib/${ANGBAND_DIR} DESTINATION ${ANGBAND_DEST}
                FILE_PERMISSIONS ${DATA_PERMISSIONS}
                DIRECTORY_PERMISSIONS ${DIR_PERMISSIONS}
                PATTERN Makefile EXCLUDE PATTERN .deps EXCLUDE)
        endforeach()
        if(SUPPORT_SDL3_FRONTEND AND
                EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/font")
            set(ANGBAND_DEST ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME})
            install(DIRECTORY assets/font/ DESTINATION ${ANGBAND_DEST}/fonts
                FILE_PERMISSIONS ${DATA_PERMISSIONS}
                DIRECTORY_PERMISSIONS ${DIR_PERMISSIONS})
        endif()
        set(ANGBAND_DEST ${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME})
        install(DIRECTORY lib/customize DESTINATION ${ANGBAND_DEST}
            FILE_PERMISSIONS ${DATA_PERMISSIONS}
            DIRECTORY_PERMISSIONS ${DIR_PERMISSIONS}
            PATTERN Makefile EXCLUDE PATTERN .deps EXCLUDE)
    else()
        ammerow_reset_package_destinations()
        set(ANGBAND_DATA_DEST
            ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME})
        set(ANGBAND_CONFIG_DEST
            ${CMAKE_INSTALL_SYSCONFDIR}/${PROJECT_NAME})
		ammerow_install_manifest(packaging/runtime.txt
			"${ANGBAND_DATA_DEST}" "${ANGBAND_CONFIG_DEST}")
		ammerow_install_manifest(packaging/runtime-art.txt
			"${ANGBAND_DATA_DEST}" "${ANGBAND_CONFIG_DEST}")
    endif()
    # Ammerow publishes desktop integration only for its supported SDL3
    # product. Inherited SDL, SDL2, and X11 front ends remain useful source
    # build options, but are not Ammerow packaging targets.
    if(SUPPORT_SDL3_FRONTEND)
        set(ANGBAND_DESKTOP_SRC_DIR lib/icons)
        set(ANGBAND_DESKTOP_DEST_DIR ${CMAKE_INSTALL_DATAROOTDIR})
        install(FILES ${ANGBAND_DESKTOP_SRC_DIR}/ammerow-sdl3.desktop
            DESTINATION ${ANGBAND_DESKTOP_DEST_DIR}/applications
            PERMISSIONS ${DATA_PERMISSIONS} RENAME ammerow.desktop)
        install(FILES ${ANGBAND_DESKTOP_SRC_DIR}/ammerow.metainfo.xml
            DESTINATION ${ANGBAND_DESKTOP_DEST_DIR}/metainfo
            PERMISSIONS ${DATA_PERMISSIONS})
    endif()
    if(SHARED_INSTALL)
        foreach(ANGBAND_DIR panic save scores)
            set(ANGBAND_DEST ${CMAKE_INSTALL_SHAREDSTATEDIR}/${PROJECT_NAME})
            install(DIRECTORY lib/user/${ANGBAND_DIR} DESTINATION ${ANGBAND_DEST} FILE_PERMISSIONS ${DATA_PERMISSIONS} DIRECTORY_PERMISSIONS ${VARDATADIR_PERMISSIONS} PATTERN Makefile EXCLUDE PATTERN .deps EXCLUDE)
            set(ANGBAND_CHANGE_OWNER ${ANGBAND_CHANGE_OWNER} ${CMAKE_INSTALL_FULL_SHAREDSTATEDIR}/${PROJECT_NAME}/${ANGBAND_DIR})
        endforeach()
        install(CODE "execute_process(COMMAND chown root:${INSTALL_GROUP_ID} ${ANGBAND_CHANGE_OWNER} RESULT_VARIABLE ANGBAND_RESULT)\nif(NOT ANGBAND_RESULT EQUAL 0)\nmessage(FATAL_ERROR \"Changing ownership to root:${INSTALL_GROUP_ID} on ${ANGBAND_CHANGE_OWNER} failed\")\nendif()")
        # Since chown can reset the setgid bit that CMake set when installing,
        # turn that bit back on.
        install(CODE "execute_process(COMMAND chmod g+s ${INSTALLED_EXECUTABLE} RESULT_VARIABLE ANGBAND_RESULT)\nif(NOT ANGBAND_RESULT EQUAL 0)\nmessage(FATAL ERROR \"Resetting setgid bit on ${INSTALLED_EXECUTABLE} failed\")\nendif()")
    endif()

    if(BUILD_DOC)
        # Install the built documentation.
        sphinx_document_get_output_directory(_DOC OurManual)
        sphinx_document_get_output_formats(_FORMATS OurManual)
        if(_DOC STREQUAL "_DOC-NOTFOUND")
            message(FATAL_ERROR "logic error:  Sphinx output directory not set for OurManual target")
        endif()
        if(_FORMATS STREQUAL "_FORMATS-NOTFOUND")
            message(FATAL_ERROR "logic error:  Sphinx output formats not set for OurManual target")
        endif()
        if(NOT (_DOC MATCHES "/$"))
            set(_DOC "${_DOC}/")
        endif()
        foreach(_X ${_FORMATS})
            # Remove the format subdirectory when installing for one less
            # level of nesting.  Works okay with just HTML output; may have
            # conflicting names with multiple output types.
            install(
                DIRECTORY "${_DOC}${_X}/"
                DESTINATION "${CMAKE_INSTALL_DOCDIR}"
                FILE_PERMISSIONS ${DATA_PERMISSIONS}
                DIRECTORY_PERMISSIONS ${DIR_PERMISSIONS}
                PATTERN .buildinfo  EXCLUDE
            )
        endforeach()
    endif()
endif()

if(SC_INSTALL)
    set(ANGBAND_CONFIG_PATH ./lib/)
    set(ANGBAND_LIB_PATH ./lib/)
    # Would use lib/user here to match up with the preexisting structure from
    # the source directory, but previous CMake builds used this (because of
    # the default setting in config.h).  So, to be compatible if people
    # rebuild on an existing copy, use this.  When there's backward
    # incompatible changes to the save files, could change this since
    # compatiblity with something already on disk will be less of a concern.
    set(ANGBAND_DATA_PATH ./lib/)
    # The Windows front end doesn't access these so don't set them in that case.
    if(NOT SUPPORT_WINDOWS_FRONTEND)
        target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_CONFIG_PATH="${ANGBAND_CONFIG_PATH}")
        target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_LIB_PATH="${ANGBAND_LIB_PATH}")
        target_compile_definitions(OurExecutable PRIVATE -D DEFAULT_DATA_PATH="${ANGBAND_DATA_PATH}")
        if(SUPPORT_SDL3_FRONTEND)
            # A self-contained SDL3 build is portable by definition.  Resolve
            # its read/write tree from the executable, never from the process
            # working directory selected by a shortcut, shell, or launcher.
            target_compile_definitions(OurExecutable PRIVATE
                -D USE_EXECUTABLE_RELATIVE_PATHS)
        endif()
    endif()
    # Provide an install rule which copies the self-contained form to another
    # location.
    install(TARGETS OurExecutable DESTINATION . PERMISSIONS ${EXECUTABLE_PERMISSIONS})
    if(WIN32 AND SUPPORT_SDL3_FRONTEND)
        # The SDL3 package targets point at the exact runtime libraries linked
        # by this build.  Installing those targets keeps the self-contained
        # package runnable without copying unrelated files from the SDKs.
		install(FILES
			$<TARGET_FILE:SDL3::SDL3-shared>
			$<TARGET_FILE:SDL3_ttf::SDL3_ttf-shared>
			$<TARGET_FILE:SDL3_image::SDL3_image-shared>
			DESTINATION .
            PERMISSIONS ${EXECUTABLE_PERMISSIONS})
    endif()
    install(FILES COPYING NOTICE.md ASSET-LICENSE.md THIRD_PARTY_NOTICES.md
        README.md SOURCE-CODE.txt DESTINATION .
        PERMISSIONS ${DATA_PERMISSIONS})
    install(FILES
        third_party/licenses/Cozette-MIT.txt
        third_party/licenses/Fonts-OFL-1.1.txt
        third_party/licenses/FreeType-FTL.txt
        third_party/licenses/HarfBuzz-Microsoft-USE.txt
        third_party/licenses/HarfBuzz.txt
        third_party/licenses/PlutoSVG-MIT.txt
        third_party/licenses/PlutoVG-MIT.txt
        third_party/licenses/SDL-family-zlib.txt
        third_party/licenses/STB-dual-license.txt
        third_party/licenses/Xoshiro-SplitMix-public-domain.txt
        DESTINATION third_party/licenses
        PERMISSIONS ${DATA_PERMISSIONS})
    install(FILES docs/copying.rst docs/thanks.rst DESTINATION docs
        PERMISSIONS ${DATA_PERMISSIONS})
    if(SUPPORT_WINDOWS_FRONTEND AND PNG_DLLS)
        # DATA_PERMISSIONS matches the permissions those files have in the
        # source tree, but should the installed permissions be
        # EXECUTABLE_PERMISSIONS?
        install(FILES ${PNG_DLLS} DESTINATION . PERMISSIONS ${DATA_PERMISSIONS})
    endif()
    if(AMMEROW_PACKAGE_PROFILE_NORMALIZED STREQUAL "development")
        install(DIRECTORY lib/ DESTINATION lib)
        if(SUPPORT_SDL3_FRONTEND AND
                EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/font")
            install(DIRECTORY assets/font/ DESTINATION lib/fonts
                FILE_PERMISSIONS ${DATA_PERMISSIONS}
                DIRECTORY_PERMISSIONS ${DIR_PERMISSIONS})
        endif()
    else()
        ammerow_reset_package_destinations()
		ammerow_install_manifest(packaging/runtime.txt lib lib)
		ammerow_install_manifest(packaging/runtime-art.txt lib lib)
		ammerow_install_manifest(packaging/runtime-audio.txt lib lib)
    endif()
    if(BUILD_DOC)
        install(DIRECTORY docs/ DESTINATION docs)
    endif()

    # Set up a target to transfer the files in lib/ at build time.
    add_custom_target(TransferLib ALL)
    add_custom_command(
        TARGET TransferLib
        PRE_BUILD
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:OurExecutable>/lib/monsters"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:OurExecutable>/lib/shopkeepers"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory
            "$<TARGET_FILE_DIR:OurExecutable>/lib/sounds/retro"
		COMMAND "${CMAKE_COMMAND}" -E remove_directory
			"$<TARGET_FILE_DIR:OurExecutable>/lib/tiles"
        COMMAND "${CMAKE_COMMAND}" "-P"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/copy_with_exclude.cmake"
        "--"
        "$<TARGET_FILE_DIR:OurExecutable>"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        "Makefile"
        ".deps"
        ".gitignore"
        COMMENT "Removing retired media and copying needed files"
        VERBATIM
    )
    if(SUPPORT_SDL3_FRONTEND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/font")
        add_custom_command(
            TARGET TransferLib
            PRE_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/font"
                "$<TARGET_FILE_DIR:OurExecutable>/lib/fonts"
            COMMENT "Copying SDL3 fonts"
            VERBATIM
        )
    endif()

    # A targeted executable build must also produce a complete, current
    # self-contained runtime tree.  TransferLib is always out of date by
    # design, so this also removes files retired from the runtime manifests.
    add_dependencies(OurExecutable TransferLib)

    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.15.0")
        set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "$<TARGET_FILE_DIR:OurExecutable>/lib")
    endif()

    if(SUPPORT_WINDOWS_FRONTEND AND PNG_DLLS)
        # Transfer the packaged DLLs so there in the same directory as the
        # executable.
        foreach(dll ${PNG_DLLS})
            add_custom_command(
                TARGET OurExecutable
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}"
                    "$<TARGET_FILE_DIR:OurExecutable>"
                COMMENT "Copying ${dll} to executable directory"
            )
            if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.15.0")
                set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "$<TARGET_FILE_DIR:OurExecutable>/${dll}")
            endif()
        endforeach()
    endif()
endif()

# This is the list of the unit test case source files relative to src/tests in
# the source tree, one per test case executable.  List them alphabetically to
# make maintenance easier though, when running them, it would be preferable to
# run the lower level ones first.
set(ANGBAND_TEST_CASE_SOURCES
    artifact/name.c
    cave/find.c
    cave/scatter.c
    command/lookup.c
    effects/chain.c
    effects/destruction.c
    effects/earthquake.c
    effects/info.c
    game/basic.c
	game/fishing.c
	game/larder.c
    game/mage.c
	game/provisions.c
	game/relic-broker.c
	game/spelunking-combat.c
	game/spelunking-items.c
	game/spelunking-persistence.c
	game/spelunking-transitions.c
	game/spelunking-traversal-proof.c
	game/spelunking-codec.c
	game/spelunking-movement.c
	game/spelunking-visibility.c
	game/vault-library.c
	game/world-travel.c
    message/message.c
    monster/attack.c
    monster/desc.c
    monster/monster.c
    object/alloc.c
    object/attack.c
    object/info.c
    object/pile.c
    object/slays.c
    object/util.c
    parse/a-info.c
	parse/artifact-balance.c
	parse/artifact-lore.c
    parse/blowe.c
    parse/blowm.c
    parse/body.c
    parse/brand.c
    parse/c-info.c
    parse/curse.c
    parse/e-info.c
    parse/f-info.c
	parse/fishing.c
	parse/larder.c
	parse/relic-broker.c
	parse/spelunking-actor.c
	parse/spelunking-layout.c
	parse/spelunking-population.c
	parse/spelunking-geology.c
	parse/spelunking-recipe.c
	parse/spelunking-system.c
	parse/spelunking-traversal.c
	parse/story.c
	parse/terrain-visual.c
	parse/map-visual.c
	parse/flavor.c
    parse/graphics.c
    parse/h-info.c
    parse/hints.c
    parse/k-info.c
    parse/lore.c
    parse/mbase.c
    parse/mspell.c
    parse/names.c
    parse/objact.c
    parse/objbase.c
    parse/objprop.c
    parse/p-info.c
    parse/pain.c
    parse/parse.c
    parse/partrap.c
    parse/pit.c
    parse/pprop.c
    parse/proj.c
    parse/ptimed.c
    parse/r-info.c
    parse/readstore.c
    parse/realm.c
    parse/shape.c
    parse/slay.c
    parse/ui_knowledge.c
    parse/v-info.c
    parse/world.c
    parse/z-info.c
    player/birth.c
    player/calc-inventory.c
    player/combine-pack.c
    player/digging.c
    player/history.c
    player/inven-carry-num.c
    player/inven-wield.c
    player/pathfind.c
    player/playerstat.c
    player/resource.c
    player/pscore.c
    player/timed.c
    player/util.c
    sound/sequence.c
    ui/death.c
    ui/sidebar.c
    ui/spell-menu.c
    ui/target.c
    ui/world-intro.c
      sdl3/audio.c
      sdl3/audio-wav.c
      sdl3/audio-mixer.c
      sdl3/birth-screen.c
      sdl3/help-screen.c
      sdl3/command-screen.c
      sdl3/config.c
    sdl3/effects.c
	sdl3/font.c
	sdl3/ascii-art.c
    sdl3/home-model.c
    sdl3/home-feature.c
    sdl3/home-layout.c
    sdl3/headless-scenario.c
    sdl3/history-screen.c
      sdl3/input.c
      sdl3/inspect-card.c
      sdl3/item-screen.c
      sdl3/layout.c
    sdl3/monster-art.c
    sdl3/monster-card-data.c
    sdl3/monster-card-layout.c
    sdl3/nearby-screen.c
    sdl3/pause-menu-model.c
    sdl3/presentation.c
	sdl3/player-screen.c
    sdl3/screen-model.c
    sdl3/settings-model.c
    sdl3/spelunking-presenter.c
    sdl3/store-art.c
    sdl3/store-screen.c
    sdl3/terrain.c
    sdl3/theme.c
	sdl3/tiles.c
    sdl3/weather.c
    sdl3/zoom.c
    trivial/trivial.c
    z-dice/dice.c
    z-expression/expression.c
    z-file/filename-index.c
    z-file/path-normalize.c
    z-quark/quark.c
    z-queue/qp.c
    z-rand/rand.c
    z-textblock/textblock.c
    z-util/guard.c
    z-util/meanvar.c
    z-util/rational.c
    z-util/util.c
    z-virt/mem.c
    z-virt/string.c
)

# First copy some scripts and, as necessary, test case data from the source
# tree.
if(SUPPORT_TEST_FRONTEND)
    add_custom_command(TARGET OurExecutable POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "$<TARGET_FILE_DIR:OurExecutable>/tests"
    )
    add_custom_command(TARGET OurExecutable POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/run-tests"
            "$<TARGET_FILE_DIR:OurExecutable>/run-tests"
    )
    add_custom_command(TARGET OurExecutable POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-test"
            "$<TARGET_FILE_DIR:OurExecutable>/tests/run-test"
    )
    if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.15.0")
        set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES "$<TARGET_FILE_DIR:OurExecutable>/run-tests;$<TARGET_FILE_DIR:OurExecutable>/tests")
    endif()
endif()

# Set up target for running all tests.  Those (from tests in the source tree)
# using the test front end will be run directly.  The unit tests will be
# handled through a dependency.  In the SHARED_INSTALL case, some unit tests
# will always fail because the test case executables are not setgid.
if((NOT CMAKE_CROSSCOMPILING) AND SUPPORT_TEST_FRONTEND)
    if(WIN32)
        find_program(TEST_SH_EXECUTABLE NAMES sh)
        set(FRONTEND_TEST_OUTPUT
            "$<TARGET_FILE_DIR:OurExecutable>/frontend-test-output")
        add_custom_target(alltests
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${FRONTEND_TEST_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}"
                "-DGAME_EXECUTABLE=$<TARGET_FILE:OurExecutable>"
                "-DGAME_WORKING_DIRECTORY=$<TARGET_FILE_DIR:OurExecutable>"
                "-DTEST_ROOT=$<TARGET_FILE_DIR:OurExecutable>/tests"
                "-DOUTPUT_ROOT=${FRONTEND_TEST_OUTPUT}"
                "-DSH_EXECUTABLE=${TEST_SH_EXECUTABLE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/run_frontend_tests.cmake"
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>"
            VERBATIM)
    else()
        add_custom_target(alltests
            COMMAND "$<TARGET_FILE_DIR:OurExecutable>/run-tests" ${TEST_EXECUTABLE}
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>")
    endif()
    add_dependencies(alltests OurExecutable)
else()
    add_custom_target(alltests)
endif()
if(SUPPORT_COVERAGE)
    add_coverage_targets(resetcoverage reportcoverage coverage
        COMBINED_SUBTARGETS alltests)
endif()

# For unit tests, use a working directory so the data files can be found.
# This complements how DEFAULT_CONFIG_PATH, DEFAULT_LIB_PATH, DEFAULT_DATA_PATH,
# and TEST_OVERRIDE_PATHS are set below.
if((READONLY_INSTALL) OR (SHARED_INSTALL))
    set(TEST_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    set(TEST_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>")
endif()

# Set up an object library with the shared code for unit tests.
add_library(OurUnitTestLib OBJECT EXCLUDE_FROM_ALL
        src/tests/test-utils.c
        src/tests/unit-test.c
)
set_target_properties(OurUnitTestLib PROPERTIES C_STANDARD 99)
set(ANGBAND_UNIT_TEST_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src/tests")
target_include_directories(OurUnitTestLib PRIVATE
    ${ANGBAND_CORE_INCLUDE_DIRS}
)
target_compile_definitions(OurUnitTestLib PRIVATE "${ANGBAND_BUILD_ID_OPTION}")
if(SUPPORT_SDL3_FRONTEND)
    target_compile_definitions(OurUnitTestLib PRIVATE SOUND_SDL3 SOUND)
endif()

if((NOT CMAKE_CROSSCOMPILING) AND SUPPORT_HEADLESS_FRONTEND)
    set(HEADLESS_SMOKE_OUTPUT
        "$<TARGET_FILE_DIR:OurExecutable>/headless-smoke")
    add_custom_target(headless-smoke
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${HEADLESS_SMOKE_OUTPUT}"
            "${HEADLESS_SMOKE_OUTPUT}/user"
            "${HEADLESS_SMOKE_OUTPUT}/save"
        COMMAND "$<TARGET_FILE:OurExecutable>"
            "-duser=${HEADLESS_SMOKE_OUTPUT}/user"
            "-dsave=${HEADLESS_SMOKE_OUTPUT}/save"
            -mheadless -- --scenario
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/scenarios/smoke.scn"
            --output "${HEADLESS_SMOKE_OUTPUT}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>"
        VERBATIM)
    add_dependencies(headless-smoke OurExecutable)

    set(HEADLESS_LOOP_OUTPUT
        "$<TARGET_FILE_DIR:OurExecutable>/headless-cave-larder-loop")
    add_custom_target(headless-loop-acceptance
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${HEADLESS_LOOP_OUTPUT}"
            "${HEADLESS_LOOP_OUTPUT}/user"
            "${HEADLESS_LOOP_OUTPUT}/save"
        COMMAND "$<TARGET_FILE:OurExecutable>"
            "-duser=${HEADLESS_LOOP_OUTPUT}/user"
            "-dsave=${HEADLESS_LOOP_OUTPUT}/save"
            -mheadless -- --scenario
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/scenarios/cave-larder-loop.scn"
            --output "${HEADLESS_LOOP_OUTPUT}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>"
        VERBATIM)
    add_dependencies(headless-loop-acceptance OurExecutable)
else()
    add_custom_target(headless-smoke)
    add_custom_target(headless-loop-acceptance)
endif()

# Use the same definitions for the hardwired paths as were used (or would
# be used if it wasn't the Windows front end) for the executable.
target_compile_definitions(OurUnitTestLib PRIVATE -D DEFAULT_CONFIG_PATH="${ANGBAND_CONFIG_PATH}")
target_compile_definitions(OurUnitTestLib PRIVATE -D DEFAULT_LIB_PATH="${ANGBAND_LIB_PATH}")
target_compile_definitions(OurUnitTestLib PRIVATE -D DEFAULT_DATA_PATH="${ANGBAND_DATA_PATH}")
# If building for installation elsewhere allows those paths to be overridden
# so the test cases can be run before installation.  To run the cases after
# installation with the installed data files, set the environment variable
# FORCE_PATH to be non-empty prior to running the tests.
if((READONLY_INSTALL) OR (SHARED_INSTALL))
    target_compile_definitions(OurUnitTestLib PRIVATE -D TEST_OVERRIDE_PATHS)
endif()
if(SUPPORT_WINDOWS_FRONTEND)
    configure_windows_frontend(OurUnitTestLib "")
endif()

# Set up targets for exercising the unit tests (from src/tests in the source
# tree).  Create the directories needed to hold the test case executables.
# The private checkout also exercises its evidence-backed identity policy.
# Exported source has no private policy; public gameplay tests are unchanged.
set(AMMEROW_PRIVATE_AUDITS_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/identity/terms.c")
    set(AMMEROW_PRIVATE_AUDITS_DEFAULT ON)
endif()
option(AMMEROW_PRIVATE_AUDITS "Run private identity-policy regression tests."
    ${AMMEROW_PRIVATE_AUDITS_DEFAULT})
if(AMMEROW_PRIVATE_AUDITS)
    foreach(required src/tests/identity/terms.c docs/identity-audit-policy.txt
            tests/identity/retired-terms.txt tests/identity/retired-monster-names.txt)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${required}")
            message(FATAL_ERROR "Private audit input missing: ${required}")
        endif()
    endforeach()
    list(APPEND ANGBAND_TEST_CASE_SOURCES identity/terms.c)
endif()

set(ANGBAND_TEST_CASE_PATHS "")
set(ANGBAND_TEST_CASE_TARGETS "")
foreach(ANGBAND_TEST_CASE_SOURCE ${ANGBAND_TEST_CASE_SOURCES})
    string(REGEX REPLACE "/[^/]*$" "" ANGBAND_TEST_CASE_DIR ${ANGBAND_TEST_CASE_SOURCE})
    string(REGEX REPLACE "\.[^\./]*$" "" ANGBAND_TEST_CASE_PATH ${ANGBAND_TEST_CASE_SOURCE})
    string(REGEX REPLACE "^${ANGBAND_TEST_CASE_DIR}/" "" ANGBAND_TEST_CASE_FILE ${ANGBAND_TEST_CASE_PATH})
    if(ANGBAND_TEST_CASE_FILE STREQUAL "")
        message(FATAL_ERROR "Misconfigured test case source file, ${ANGBAND_TEST_CASE_SOURCE}: no file name")
    endif()
    string(REGEX REPLACE "/" "-" ANGBAND_TEST_CASE_NAME
        unittest- ${ANGBAND_TEST_CASE_PATH})
    set(ANGBAND_TEST_CASE_PATH "unittests/${ANGBAND_TEST_CASE_PATH}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/unittests/${ANGBAND_TEST_CASE_DIR}")
    set(ANGBAND_TEST_CASE_EXTRA_SOURCES "")
      if(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/birth-screen.c" OR
          ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/help-screen.c" OR
          ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/command-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/inspect-card.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/inspect-card-presenter.c
              src/sdl3/monster-card-presenter.c
              src/sdl3/monster-card-data.c
              src/sdl3/ascii-art.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/item-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/player-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/history-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/nearby-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/store-screen.c")
          list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
              src/sdl3/screen-model.c
          )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/audio.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/audio.c
        )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/audio-mixer.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/audio.c
            src/sdl3/audio-wav.c
        )
      elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/audio-wav.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/audio-wav.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/config.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/audio.c
            src/sdl3/config.c
            src/sdl3/zoom.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/effects.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/effects.c
        )
	elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/font.c")
		list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
			src/sdl3/font.c
			src/sdl3/zoom.c
		)
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/home-model.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/home-model.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/home-feature.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/home-feature.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/home-layout.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/home-layout.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/headless-scenario.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/headless/scenario.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/input.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/input.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/layout.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/layout.c
            src/sdl3/term.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/monster-art.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/font.c
            src/sdl3/monster-art.c
            src/sdl3/zoom.c
        )
	elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/ascii-art.c")
		list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
			src/sdl3/ascii-art.c
		)
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/monster-card-data.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/monster-card-data.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/monster-card-layout.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/monster-card-layout.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/pause-menu-model.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/pause-menu-model.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/presentation.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/presentation.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/screen-model.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/screen-model.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/settings-model.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/settings-model.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/spelunking-presenter.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/spelunking-presenter.c
            src/sdl3/tiles.c
            src/sdl3/terrain.c
            src/sdl3/term.c
            src/sdl3/zoom.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/store-art.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/store-art.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/terrain.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/terrain.c
        )
	elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/tiles.c")
		list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
			src/sdl3/tiles.c
		)
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/theme.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/theme.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/weather.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/weather.c
        )
    elseif(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/zoom.c")
        list(APPEND ANGBAND_TEST_CASE_EXTRA_SOURCES
            src/sdl3/zoom.c
        )
    endif()
    add_executable(${ANGBAND_TEST_CASE_NAME} EXCLUDE_FROM_ALL
            "src/tests/${ANGBAND_TEST_CASE_SOURCE}"
            ${ANGBAND_TEST_CASE_EXTRA_SOURCES}
            $<TARGET_OBJECTS:OurUnitTestLib>
            $<TARGET_OBJECTS:OurCoreLib>
            $<$<BOOL:${SOUND_SUPPORT_LIB}>:$<TARGET_OBJECTS:${SOUND_SUPPORT_LIB}>>
    )
    set_target_properties(${ANGBAND_TEST_CASE_NAME} PROPERTIES
        C_STANDARD 99
        OUTPUT_NAME "${ANGBAND_TEST_CASE_FILE}"
        RUNTIME_OUTPUT_DIRECTORY "${ANGBAND_TEST_CASE_PATH}")
    target_include_directories(${ANGBAND_TEST_CASE_NAME} PRIVATE
        ${ANGBAND_CORE_INCLUDE_DIRS}
        ${ANGBAND_UNIT_TEST_INCLUDE_DIRS}
    )
    target_compile_definitions(${ANGBAND_TEST_CASE_NAME} PRIVATE "${ANGBAND_BUILD_ID_OPTION}")
    if(ANGBAND_TEST_CASE_SOURCE STREQUAL "identity/terms.c")
        target_compile_definitions(${ANGBAND_TEST_CASE_NAME} PRIVATE
            IDENTITY_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(ANGBAND_TEST_CASE_SOURCE STREQUAL "game/vault-library.c")
        target_compile_definitions(${ANGBAND_TEST_CASE_NAME} PRIVATE
            VAULT_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    target_link_libraries(${ANGBAND_TEST_CASE_NAME} PRIVATE
        ${ANGBAND_CORE_LINK_LIBRARIES}
    )
    if(ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/audio-wav.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/audio-mixer.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/config.c" OR
			ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/font.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/input.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/inspect-card.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/monster-art.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/spelunking-presenter.c" OR
			ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/tiles.c" OR
            ANGBAND_TEST_CASE_SOURCE STREQUAL "sdl3/theme.c")
        configure_sdl3_frontend(${ANGBAND_TEST_CASE_NAME})
    endif()
    if(SUPPORT_STATS_BACKEND)
        configure_stats_backend(${ANGBAND_TEST_CASE_NAME} NO)
    endif()
    if(SUPPORT_SDL_SOUND)
        configure_sdl_sound(${ANGBAND_TEST_CASE_NAME} NO)
    endif()
    if(SUPPORT_SDL2_SOUND)
        configure_sdl2_sound(${ANGBAND_TEST_CASE_NAME} NO)
    endif()
    if(SUPPORT_WINDOWS_FRONTEND)
        configure_windows_frontend(${ANGBAND_TEST_CASE_NAME} "")
        # Guarantee that it is not marked as having a WinMain entry point
        # regardless of the setting of CMAKE_WIN32_EXECUTABLE.
        set_target_properties(${ANGBAND_TEST_CASE_NAME} PROPERTIES
            WIN32_EXECUTABLE OFF)
        if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            # Despite the guarantee above, the Visual Studio generator still
            # needs this to avoid a linker error about missing WinMain.
            target_link_options(${ANGBAND_TEST_CASE_NAME} PRIVATE "/SUBSYSTEM:CONSOLE")
        endif()
    endif()
    if(SUPPORT_COVERAGE)
        configure_target_for_coverage(${ANGBAND_TEST_CASE_NAME} LINKONLY)
    endif()
    # Very few of the test cases use the math library, but it is easier to
    # have all use it.
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(${ANGBAND_TEST_CASE_NAME} PRIVATE ${MATH_LIBRARY})
    endif()
    # Add a shortcut for running (and compiling if necessary) this test case.
    add_custom_target(run-${ANGBAND_TEST_CASE_NAME}
        COMMAND ${ANGBAND_TEST_CASE_NAME}
        WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    add_dependencies(run-${ANGBAND_TEST_CASE_NAME} ${ANGBAND_TEST_CASE_NAME})
    if(SC_INSTALL)
        add_dependencies(run-${ANGBAND_TEST_CASE_NAME} TransferLib)
    endif()
    # Add it to the list that will be run by the target for all unit tests.
    list(APPEND ANGBAND_TEST_CASE_PATHS "$<TARGET_FILE:${ANGBAND_TEST_CASE_NAME}>")
    list(APPEND ANGBAND_TEST_CASE_TARGETS "${ANGBAND_TEST_CASE_NAME}")
endforeach()

# Set up a target to run all the test cases.
if((NOT CMAKE_CROSSCOMPILING) OR (DEFINED CMAKE_CROSSCOMPILING_EMULATOR))
    if(DEFINED CMAKE_CROSSCOMPILING_EMULATOR)
        add_custom_target(allunittests
            COMMAND "${CMAKE_COMMAND}"
                -DTOOL="${CMAKE_CROSSCOMPILING_EMULATOR}" -P
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/run_tests.cmake"
                -- ${ANGBAND_TEST_CASE_PATHS}
            WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    else()
        add_custom_target(allunittests
            COMMAND "${CMAKE_COMMAND}" -P
                "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/scripts/run_tests.cmake"
                -- ${ANGBAND_TEST_CASE_PATHS}
            WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}")
    endif()
    if(SC_INSTALL)
        add_dependencies(allunittests TransferLib ${ANGBAND_TEST_CASE_TARGETS})
    endif()
else()
    add_custom_target(allunittests)
endif()
add_dependencies(alltests allunittests)

# The cave lifecycle gate deliberately composes the production
# long-loop scenario with the focused authorities that cannot be observed by
# an autonomous movement policy (Look presentation, save/load continuity and
# the run-summary UI).  Keep the commands sequential: several inherited test
# suites use stable save names and must not share a working directory in
# parallel.
if((NOT CMAKE_CROSSCOMPILING) AND SUPPORT_HEADLESS_FRONTEND)
    set(CAVE_DEATH_OUTPUT
        "$<TARGET_FILE_DIR:OurExecutable>/headless-cave-fatal-fall")
    add_custom_target(cave-lifecycle-acceptance
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-codec>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-movement>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-visibility>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-persistence>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-items>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-transitions>"
        COMMAND "$<TARGET_FILE:unittest-game-spelunking-combat>"
        COMMAND "$<TARGET_FILE:unittest-sdl3-spelunking-presenter>"
        COMMAND "$<TARGET_FILE:unittest-ui-death>"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CAVE_DEATH_OUTPUT}"
            "${CAVE_DEATH_OUTPUT}/user"
            "${CAVE_DEATH_OUTPUT}/save"
        COMMAND "$<TARGET_FILE:OurExecutable>"
            "-duser=${CAVE_DEATH_OUTPUT}/user"
            "-dsave=${CAVE_DEATH_OUTPUT}/save"
            -mheadless -- --scenario
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/scenarios/cave-fatal-fall.scn"
            --output "${CAVE_DEATH_OUTPUT}"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:OurExecutable>"
        VERBATIM)
    add_dependencies(cave-lifecycle-acceptance
        headless-loop-acceptance
        unittest-game-spelunking-codec
        unittest-game-spelunking-movement
        unittest-game-spelunking-visibility
        unittest-game-spelunking-persistence
        unittest-game-spelunking-items
        unittest-game-spelunking-transitions
        unittest-game-spelunking-combat
        unittest-sdl3-spelunking-presenter
        unittest-ui-death)
else()
    add_custom_target(cave-lifecycle-acceptance)
endif()
