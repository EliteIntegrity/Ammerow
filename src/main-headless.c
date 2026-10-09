/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file main-headless.c
 * \brief Window-free development frontend for scenario-driven playtesting.
 */

#include "angband.h"

#ifdef USE_HEADLESS

#include "headless/runner.h"
#include "headless/scenario.h"
#include "main.h"

static struct headless_scenario scenario;
static char output_dir[HEADLESS_SCENARIO_PATH] = ".";

const char help_headless[] =
	"Deterministic development runner; subopts --scenario <file> "
	"[--output <existing-directory>] [--verbose]";

static bool run_headless(enum game_mode_type mode)
{
	char error[512];

	(void)mode;
	if (!headless_run_scenario(&scenario, output_dir, error, sizeof(error))) {
		plog(error);
		return false;
	}
	return true;
}

errr init_headless(int argc, char **argv)
{
	const char *scenario_path = NULL;
	char size_option[64];
	char zoom_option[32];
	char *sdl_argv[16];
	int sdl_argc = 1;
	bool verbose = false;
	char error[512];
	int i;

	for (i = 1; i < argc; i++) {
		if (streq(argv[i], "--scenario") && i + 1 < argc) {
			scenario_path = argv[++i];
		} else if (streq(argv[i], "--output") && i + 1 < argc) {
			if (strlen(argv[i + 1]) >= sizeof(output_dir)) {
				plog("Headless output path is too long");
				return 1;
			}
			my_strcpy(output_dir, argv[++i], sizeof(output_dir));
		} else if (streq(argv[i], "--verbose")) {
			verbose = true;
		} else {
			plog_fmt("Unknown headless option: %s", argv[i]);
			return 1;
		}
	}
	if (!scenario_path) {
		plog("Headless mode requires --scenario <file>");
		return 1;
	}
	if (!headless_scenario_load(scenario_path, &scenario, error,
			sizeof(error))) {
		plog(error);
		return 1;
	}
	if (!dir_exists(output_dir)) {
		plog_fmt("Headless output directory does not exist: %s", output_dir);
		return 1;
	}

	sdl_argv[0] = argv[0];
	if (verbose) sdl_argv[sdl_argc++] = "-v";
	if (scenario.interface_font[0]) {
		sdl_argv[sdl_argc++] = "-f";
		sdl_argv[sdl_argc++] = scenario.interface_font;
	}
	if (scenario.map_font[0]) {
		sdl_argv[sdl_argc++] = "-m";
		sdl_argv[sdl_argc++] = scenario.map_font;
	}
	if (scenario.theme[0]) {
		sdl_argv[sdl_argc++] = "-t";
		sdl_argv[sdl_argc++] = scenario.theme;
	}
	if (scenario.map_style[0]) {
		sdl_argv[sdl_argc++] = "-a";
		sdl_argv[sdl_argc++] = scenario.map_style;
	}
	if (scenario.map_presentation[0]) {
		sdl_argv[sdl_argc++] = "-g";
		sdl_argv[sdl_argc++] = scenario.map_presentation;
	}
	strnfmt(zoom_option, sizeof(zoom_option), "-q%d",
		scenario.map_zoom_percent);
	sdl_argv[sdl_argc++] = zoom_option;
	if (scenario.screen_width && scenario.screen_height) {
		strnfmt(size_option, sizeof(size_option), "-z%dx%d",
			scenario.screen_width, scenario.screen_height);
		sdl_argv[sdl_argc++] = size_option;
	}
	sdl_argv[sdl_argc] = NULL;

	/* Never inherit an interactive save selection in a development run. */
	savefile[0] = '\0';
	frontend_run_hook = run_headless;
	return init_sdl3_offscreen(sdl_argc, sdl_argv);
}

#endif /* USE_HEADLESS */
