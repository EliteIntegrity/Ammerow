# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

# Run the small stdin/stdout end-to-end test corpus without assuming Perl,
# POSIX find, executable permission bits, or shell redirection.  This is the
# portable counterpart to the inherited run-tests and tests/run-test scripts.

cmake_policy(VERSION 3.5...3.31)

foreach(_required GAME_EXECUTABLE GAME_WORKING_DIRECTORY TEST_ROOT OUTPUT_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(GLOB_RECURSE _inputs LIST_DIRECTORIES false "${TEST_ROOT}/*/input")
list(SORT _inputs)
if(NOT _inputs)
    message(FATAL_ERROR "No frontend tests found below ${TEST_ROOT}")
endif()

set(_passed 0)
set(_total 0)
set(_failed "")
foreach(_input IN LISTS _inputs)
    get_filename_component(_case_dir "${_input}" DIRECTORY)
    file(RELATIVE_PATH _case_name "${TEST_ROOT}" "${_case_dir}")
    set(_expected "${_case_dir}/output")
    set(_matcher "${_case_dir}/matcher")
    set(_actual "${_case_dir}/run.out")
    if(NOT EXISTS "${_expected}" AND NOT EXISTS "${_matcher}")
        message(FATAL_ERROR
            "Frontend test ${_case_name} has neither output nor matcher")
    endif()

    math(EXPR _total "${_total} + 1")
    file(REMOVE_RECURSE "${OUTPUT_ROOT}/user" "${OUTPUT_ROOT}/save")
    file(MAKE_DIRECTORY "${OUTPUT_ROOT}/user" "${OUTPUT_ROOT}/save")
    execute_process(
        COMMAND "${GAME_EXECUTABLE}"
            "-duser=${OUTPUT_ROOT}/user"
            "-dsave=${OUTPUT_ROOT}/save"
            -mtest
        WORKING_DIRECTORY "${GAME_WORKING_DIRECTORY}"
        INPUT_FILE "${_input}"
        OUTPUT_FILE "${_actual}"
        ERROR_VARIABLE _game_error
        RESULT_VARIABLE _game_result
        TIMEOUT 60)

    set(_case_result ${_game_result})
    if(_case_result EQUAL 0 AND EXISTS "${_matcher}")
        if(WIN32)
            if(NOT DEFINED SH_EXECUTABLE OR
                    "${SH_EXECUTABLE}" STREQUAL "" OR
                    NOT EXISTS "${SH_EXECUTABLE}")
                message(FATAL_ERROR
                    "Frontend test ${_case_name} requires a matcher, but no sh executable is available")
            endif()
            execute_process(
                COMMAND "${SH_EXECUTABLE}" "${_matcher}" "${_case_dir}"
                RESULT_VARIABLE _case_result
                TIMEOUT 60)
        else()
            execute_process(
                COMMAND "${_matcher}" "${_case_dir}"
                RESULT_VARIABLE _case_result
                TIMEOUT 60)
        endif()
    elseif(_case_result EQUAL 0)
        # Text-mode stdout uses the host newline convention.  Compare the
        # transcript as text so a checkout's autocrlf policy is not part of
        # the gameplay contract.
        file(READ "${_actual}" _actual_text)
        file(READ "${_expected}" _expected_text)
        string(REPLACE "\r\n" "\n" _actual_text "${_actual_text}")
        string(REPLACE "\r" "\n" _actual_text "${_actual_text}")
        string(REPLACE "\r\n" "\n" _expected_text "${_expected_text}")
        string(REPLACE "\r" "\n" _expected_text "${_expected_text}")
        if(NOT _actual_text STREQUAL _expected_text)
            set(_case_result 1)
        endif()
    endif()

    if(_case_result EQUAL 0)
        math(EXPR _passed "${_passed} + 1")
        message("    ${_case_name} passed")
    else()
        list(APPEND _failed "${_case_name}")
        if(NOT "${_game_error}" STREQUAL "")
            string(STRIP "${_game_error}" _game_error)
            message("    ${_case_name} failed: ${_game_error}")
        else()
            message("    ${_case_name} failed")
        endif()
    endif()
endforeach()

message("Frontend total: ${_passed}/${_total} passed")
if(_failed)
    list(JOIN _failed ", " _failed_text)
    message(FATAL_ERROR "Frontend tests failed: ${_failed_text}")
endif()
