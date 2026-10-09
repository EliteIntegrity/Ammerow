# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

macro(configure_sdl3_frontend _NAME_TARGET)

    find_package(SDL3 3.2.0 REQUIRED CONFIG)
    find_package(SDL3_ttf 3.2.0 REQUIRED CONFIG)
	find_package(SDL3_image REQUIRED CONFIG)

	target_link_libraries(${_NAME_TARGET} PRIVATE
		SDL3::SDL3
		SDL3_ttf::SDL3_ttf
		SDL3_image::SDL3_image
    )
    target_compile_definitions(${_NAME_TARGET} PRIVATE
        USE_SDL3 SOUND_SDL3 SOUND)

    if(WIN32)
        add_custom_command(TARGET ${_NAME_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared>
                $<TARGET_FILE_DIR:${_NAME_TARGET}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3_ttf::SDL3_ttf-shared>
                $<TARGET_FILE_DIR:${_NAME_TARGET}>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:SDL3_image::SDL3_image-shared>
				$<TARGET_FILE_DIR:${_NAME_TARGET}>
		COMMENT "Copying SDL3 runtime libraries"
        )
    endif()

    message(STATUS "Support for SDL3 front end - Ready")

endmacro()
