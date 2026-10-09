# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

# Keep the CMake core target and the portable Makefile source list synchronized.
# Frontend entry points and platform/backend helpers are deliberately Makefile-only.

function(check_core_manifest source_root)
    file(READ "${source_root}/CMakeLists.txt" _cmake_text)
    string(REGEX MATCH
        "add_library\\(OurCoreLib OBJECT[^)]*\\)"
        _core_target "${_cmake_text}")
    if(NOT _core_target)
        message(FATAL_ERROR "Could not find the OurCoreLib source list")
    endif()

    string(REGEX MATCHALL "src/[A-Za-z0-9_/-]+\\.c"
        _core_sources "${_core_target}")
    list(REMOVE_DUPLICATES _core_sources)

    file(READ "${source_root}/src/Makefile.src" _makefile_text)
    string(REGEX MATCHALL "[A-Za-z0-9_/-]+\\.o"
        _makefile_objects "${_makefile_text}")
    list(REMOVE_DUPLICATES _makefile_objects)

    set(_makefile_only_allowlist
        main-gcu.o
        main-sdl.o
        main-sdl2.o
        main-spoil.o
        main-stats.o
        main-test.o
        main-win.o
        main-x11.o
        main.o
        sdl2/pui-ctrl.o
        sdl2/pui-dlg.o
        sdl2/pui-misc.o
        snd-sdl.o
        stats/db.o
        win/readdib.o
        win/readpng.o
        win/scrnshot.o
        win/win-layout.o)

    set(_missing_from_makefile)
    foreach(_source IN LISTS _core_sources)
        string(REGEX REPLACE "^src/" "" _object "${_source}")
        string(REGEX REPLACE "\\.c$" ".o" _object "${_object}")
        if(NOT _object IN_LIST _makefile_objects)
            list(APPEND _missing_from_makefile "${_source}")
        endif()
    endforeach()

    set(_unexpected_makefile_objects)
    foreach(_object IN LISTS _makefile_objects)
        if(_object IN_LIST _makefile_only_allowlist)
            continue()
        endif()
        string(REGEX REPLACE "\\.o$" ".c" _source "${_object}")
        set(_source "src/${_source}")
        if(NOT _source IN_LIST _core_sources)
            list(APPEND _unexpected_makefile_objects "${_object}")
        endif()
    endforeach()

    if(_missing_from_makefile OR _unexpected_makefile_objects)
        set(_message "CMakeLists.txt and src/Makefile.src core sources differ.")
        if(_missing_from_makefile)
            string(REPLACE ";" ", " _missing_text
                "${_missing_from_makefile}")
            string(APPEND _message
                "\nMissing from src/Makefile.src: ${_missing_text}")
        endif()
        if(_unexpected_makefile_objects)
            string(REPLACE ";" ", " _unexpected_text
                "${_unexpected_makefile_objects}")
            string(APPEND _message
                "\nUnexpected Makefile core objects: ${_unexpected_text}")
        endif()
        message(FATAL_ERROR "${_message}")
    endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND DEFINED SOURCE_ROOT)
    check_core_manifest("${SOURCE_ROOT}")
    message(STATUS "CMake and portable Makefile core manifests are aligned")
endif()
