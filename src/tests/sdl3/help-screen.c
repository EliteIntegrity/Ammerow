/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "unit-test.h"
#include "test-utils.h"
#include "init.h"
#include "ui-help.h"
#include "ui-term.h"
#include "z-color.h"
#include "sdl3/screen-model.h"

static struct sdl3_screen screen;
static term terminal;
static term *previous;
static const uint32_t *keys;
static int key_count, key_index;
static bool saw_commands, saw_search_prompt, saw_highlight, saw_scroll;

static void capture(const struct ui_screen *source)
{
	sdl3_screen_capture(&screen, source);
	if (!source) return;
	if (strstr(screen.title, "Command Summary")) saw_commands = true;
	if (screen.document_text && wcsstr(screen.document_text, L"Aim a wand")) saw_scroll = true;
	for (size_t i = 0; i < screen.document_length; i++)
		if (screen.document_attrs[i] == COLOUR_YELLOW) saw_highlight = true;
}

static errr input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		if (key_index >= key_count) quit("Help test exhausted input");
		if (!screen.active) saw_search_prompt = true;
		Term_keypress(keys[key_index++], 0);
	}
	return 0;
}

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	previous = Term;
	term_init(&terminal, 120, 36, 128);
	terminal.screen_hook = capture;
	terminal.xtra_hook = input;
	term_screen = angband_term[0] = &terminal;
	Term_activate(&terminal);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	Term_activate(previous);
	term_screen = angband_term[0] = NULL;
	term_nuke(&terminal);
	return 0;
}

static int test_layout_and_long_pages(void *state)
{
	const char *files[] = { "index.txt", "commands.txt", "interface.txt",
		"fishing.txt", "spelunking.txt", "world.txt", "symbols.txt", "r_index.txt", "r_comm.txt" };
	(void)state;
	for (int width = 80; width <= 160; width += 40) {
		Term_resize(width, 24);
		for (size_t file = 0; file < N_ELEMENTS(files); file++) {
			require(ui_help_preview(files[file], 0));
			eq(screen.kind, UI_SCREEN_HELP);
			require(screen.active);
			require(!wcsstr(screen.document_text, L".. menu::"));
			for (int i = 0; i < screen.document_line_count; i++) {
				require(screen.document_line_lengths[i] <= (size_t)screen.content_cols);
				require(screen.content_row + i < terminal.hgt - 2);
			}
			require(ui_help_preview(files[file], 10000));
			require(screen.document_length > 0);
		}
	}
	ok;
}

static int test_links_search_paging_and_back(void *state)
{
	const uint32_t script[] = { 'a', '/', 'w', 'a', 'n', 'd', KC_ENTER,
		KC_END, KC_HOME, KC_PGDOWN, KC_PGUP, '=', '+', '_', ESCAPE, ESCAPE };
	(void)state;
	Term_resize(120, 36);
	keys = script;
	key_count = N_ELEMENTS(script);
	key_index = 0;
	saw_commands = saw_search_prompt = saw_highlight = saw_scroll = false;
	require(show_file("index.txt", NULL, 0, 0));
	eq(key_index, key_count);
	require(saw_commands);
	require(saw_search_prompt);
	require(saw_highlight);
	require(saw_scroll);
	require(!screen.active);
	{
		const uint32_t close[] = { 'f', '?' };
		keys = close; key_count = N_ELEMENTS(close); key_index = 0;
		require(!show_file("index.txt", NULL, 0, 0));
		eq(key_index, key_count);
	}
	ok;
}

const char *suite_name = "sdl3/help-screen";
struct test tests[] = {
	{ "layout and long pages", test_layout_and_long_pages },
	{ "links search paging and back", test_links_search_paging_and_back },
	{ NULL, NULL }
};
