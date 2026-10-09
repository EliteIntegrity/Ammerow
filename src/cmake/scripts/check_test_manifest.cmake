# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

# Keep portable unit-test registration aligned with CMake.  SDL3-only suites
# are intentionally excluded because the portable Makefile has no SDL3 target.

function(check_test_manifest source_root)
    file(READ "${source_root}/CMakeLists.txt" _cmake_text)
    string(REGEX MATCH
        "set\\(ANGBAND_TEST_CASE_SOURCES[^)]*\\)"
        _test_source_block "${_cmake_text}")
    if(NOT _test_source_block)
        message(FATAL_ERROR "Could not find ANGBAND_TEST_CASE_SOURCES")
    endif()

    string(REGEX MATCHALL "[A-Za-z0-9_-]+/[A-Za-z0-9_-]+\\.c"
        _cmake_source_files "${_test_source_block}")
    set(_cmake_tests)
    foreach(_source IN LISTS _cmake_source_files)
        if(_source MATCHES "^sdl3/")
            continue()
        endif()
        string(REGEX REPLACE "\\.c$" "" _test "${_source}")
        list(APPEND _cmake_tests "${_test}")
    endforeach()
    list(REMOVE_DUPLICATES _cmake_tests)
    list(SORT _cmake_tests)

    file(GLOB _suite_files "${source_root}/src/tests/*/suite.mk")
    set(_make_tests)
    foreach(_suite_file IN LISTS _suite_files)
        file(READ "${_suite_file}" _suite_text)
        string(REGEX MATCHALL "[A-Za-z0-9_-]+/[A-Za-z0-9_-]+"
            _suite_tests "${_suite_text}")
        list(APPEND _make_tests ${_suite_tests})
    endforeach()
    list(REMOVE_DUPLICATES _make_tests)
    list(SORT _make_tests)

    set(_missing_from_make)
    foreach(_test IN LISTS _cmake_tests)
        if(NOT _test IN_LIST _make_tests)
            list(APPEND _missing_from_make "${_test}")
        endif()
    endforeach()
    set(_missing_from_cmake)
    foreach(_test IN LISTS _make_tests)
        if(NOT _test IN_LIST _cmake_tests)
            list(APPEND _missing_from_cmake "${_test}")
        endif()
    endforeach()

    if(_missing_from_make OR _missing_from_cmake)
        set(_message "CMake and portable Makefile unit-test lists differ.")
        if(_missing_from_make)
            string(REPLACE ";" ", " _missing_text "${_missing_from_make}")
            string(APPEND _message
                "\nMissing from suite.mk files: ${_missing_text}")
        endif()
        if(_missing_from_cmake)
            string(REPLACE ";" ", " _missing_text "${_missing_from_cmake}")
            string(APPEND _message
                "\nMissing from CMakeLists.txt: ${_missing_text}")
        endif()
        message(FATAL_ERROR "${_message}")
    endif()
endfunction()
