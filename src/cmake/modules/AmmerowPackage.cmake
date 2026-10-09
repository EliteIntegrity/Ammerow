# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

# Exact-manifest installation helpers for Ammerow release profiles.
#
# Manifest format:
#   data|path/in/source/tree|path/below/data/root
#   config|path/in/source/tree|path/below/config/root
#
# Empty lines and lines beginning with # are ignored.  Configure-time
# validation rejects missing sources, unsafe paths, malformed entries, and
# duplicate installed destinations.

function(ammerow_reset_package_destinations)
    set_property(GLOBAL PROPERTY AMMEROW_PACKAGE_DESTINATIONS "")
endfunction()

function(ammerow_install_manifest MANIFEST DATA_DEST CONFIG_DEST)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${MANIFEST}")
        message(FATAL_ERROR "Ammerow package manifest is missing: ${MANIFEST}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${MANIFEST}")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${MANIFEST}" _AMMEROW_LINES)
    foreach(_AMMEROW_RAW_LINE ${_AMMEROW_LINES})
        string(STRIP "${_AMMEROW_RAW_LINE}" _AMMEROW_LINE)
        if(_AMMEROW_LINE STREQUAL "")
            continue()
        endif()
        string(SUBSTRING "${_AMMEROW_LINE}" 0 1 _AMMEROW_FIRST)
        if(_AMMEROW_FIRST STREQUAL "#")
            continue()
        endif()

        string(REPLACE "|" ";" _AMMEROW_FIELDS "${_AMMEROW_LINE}")
        list(LENGTH _AMMEROW_FIELDS _AMMEROW_FIELD_COUNT)
        if(NOT _AMMEROW_FIELD_COUNT EQUAL 3)
            message(FATAL_ERROR
                "Malformed Ammerow package entry in ${MANIFEST}: ${_AMMEROW_LINE}")
        endif()
        list(GET _AMMEROW_FIELDS 0 _AMMEROW_COMPONENT)
        list(GET _AMMEROW_FIELDS 1 _AMMEROW_SOURCE)
        list(GET _AMMEROW_FIELDS 2 _AMMEROW_DESTINATION)
        string(STRIP "${_AMMEROW_COMPONENT}" _AMMEROW_COMPONENT)
        string(STRIP "${_AMMEROW_SOURCE}" _AMMEROW_SOURCE)
        string(STRIP "${_AMMEROW_DESTINATION}" _AMMEROW_DESTINATION)

        if(IS_ABSOLUTE "${_AMMEROW_SOURCE}" OR
                IS_ABSOLUTE "${_AMMEROW_DESTINATION}" OR
                _AMMEROW_SOURCE MATCHES "(^|/)\\.\\.(/|$)" OR
                _AMMEROW_DESTINATION MATCHES "(^|/)\\.\\.(/|$)" OR
                _AMMEROW_SOURCE MATCHES "\\\\" OR
                _AMMEROW_DESTINATION MATCHES "\\\\")
            message(FATAL_ERROR
                "Unsafe Ammerow package path in ${MANIFEST}: ${_AMMEROW_LINE}")
        endif()
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_AMMEROW_SOURCE}")
            message(FATAL_ERROR
                "Missing Ammerow package source in ${MANIFEST}: ${_AMMEROW_SOURCE}")
        endif()

        if(_AMMEROW_COMPONENT STREQUAL "data")
            set(_AMMEROW_ROOT "${DATA_DEST}")
        elseif(_AMMEROW_COMPONENT STREQUAL "config")
            set(_AMMEROW_ROOT "${CONFIG_DEST}")
        else()
            message(FATAL_ERROR
                "Unknown Ammerow package component in ${MANIFEST}: ${_AMMEROW_COMPONENT}")
        endif()

        set(_AMMEROW_DESTINATION_KEY
            "${_AMMEROW_COMPONENT}|${_AMMEROW_DESTINATION}")
        get_property(_AMMEROW_DESTINATIONS GLOBAL
            PROPERTY AMMEROW_PACKAGE_DESTINATIONS)
        list(FIND _AMMEROW_DESTINATIONS "${_AMMEROW_DESTINATION_KEY}"
            _AMMEROW_DUPLICATE_INDEX)
        if(NOT _AMMEROW_DUPLICATE_INDEX EQUAL -1)
            message(FATAL_ERROR
                "Duplicate Ammerow package destination: ${_AMMEROW_DESTINATION_KEY}")
        endif()
        set_property(GLOBAL APPEND PROPERTY AMMEROW_PACKAGE_DESTINATIONS
            "${_AMMEROW_DESTINATION_KEY}")

        get_filename_component(_AMMEROW_DESTINATION_DIR
            "${_AMMEROW_DESTINATION}" DIRECTORY)
        get_filename_component(_AMMEROW_DESTINATION_NAME
            "${_AMMEROW_DESTINATION}" NAME)
        if(_AMMEROW_DESTINATION_DIR STREQUAL "")
            set(_AMMEROW_INSTALL_DESTINATION "${_AMMEROW_ROOT}")
        else()
            set(_AMMEROW_INSTALL_DESTINATION
                "${_AMMEROW_ROOT}/${_AMMEROW_DESTINATION_DIR}")
        endif()
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/${_AMMEROW_SOURCE}"
            DESTINATION "${_AMMEROW_INSTALL_DESTINATION}"
            RENAME "${_AMMEROW_DESTINATION_NAME}"
            PERMISSIONS ${DATA_PERMISSIONS})
    endforeach()
endfunction()
