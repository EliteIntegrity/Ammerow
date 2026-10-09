/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/scenario.c
 * \brief Strict parser for deterministic development scenarios.
 */

#include "angband.h"
#include "headless/scenario.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

struct scenario_fields {
	bool name;
	bool seed;
	bool race;
	bool player_class;
	bool player_name;
	bool location;
	bool entry;
	bool enter_route;
	bool bot;
	bool steps;
	bool capture;
	bool capture_fps;
	bool capture_preroll_ms;
	bool capture_action_ms;
	bool capture_postroll_ms;
	bool expected_result;
	bool interface_font;
	bool map_font;
	bool theme;
	bool map_style;
	bool map_presentation;
	bool map_zoom_percent;
	bool screen_width;
	bool screen_height;
	bool start_x;
	bool start_y;
	bool start_anchor;
	bool encounter;
};

static char *trim(char *text)
{
	char *end;

	while (*text && isspace((unsigned char)*text)) text++;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1])) end--;
	*end = '\0';
	return text;
}

static bool set_error(char *error, size_t error_size, unsigned int line,
		const char *message, const char *value)
{
	if (error && error_size) {
		if (value) {
			strnfmt(error, error_size, "line %u: %s '%s'", line, message,
				value);
		} else {
			strnfmt(error, error_size, "line %u: %s", line, message);
		}
	}
	return false;
}

static bool set_once(char *destination, size_t size, bool *seen,
		const char *value, unsigned int line, char *error, size_t error_size)
{
	if (*seen) return set_error(error, error_size, line,
		"duplicate field", NULL);
	if (!value[0]) return set_error(error, error_size, line,
		"empty value", NULL);
	if (strlen(value) >= size) return set_error(error, error_size, line,
		"value is too long", value);
	my_strcpy(destination, value, size);
	*seen = true;
	return true;
}

static bool parse_unsigned(const char *value, unsigned long maximum,
		unsigned long *parsed)
{
	char *end;
	unsigned long result;

	errno = 0;
	result = strtoul(value, &end, 10);
	if (errno || end == value || *end || result > maximum) return false;
	*parsed = result;
	return true;
}

static bool parse_bot(const char *value, enum headless_bot_kind *bot)
{
	if (streq(value, "wait")) {
		*bot = HEADLESS_BOT_WAIT;
	} else if (streq(value, "random-walk")) {
		*bot = HEADLESS_BOT_RANDOM_WALK;
	} else if (streq(value, "scripted")) {
		*bot = HEADLESS_BOT_SCRIPTED;
	} else if (streq(value, "cave-larder-loop")) {
		*bot = HEADLESS_BOT_CAVE_LARDER_LOOP;
	} else if (streq(value, "cave-fatal-fall")) {
		*bot = HEADLESS_BOT_CAVE_FATAL_FALL;
	} else {
		return false;
	}
	return true;
}

static bool parse_capture(const char *value,
		enum headless_capture_kind *capture)
{
	if (streq(value, "none")) {
		*capture = HEADLESS_CAPTURE_NONE;
	} else if (streq(value, "initial")) {
		*capture = HEADLESS_CAPTURE_INITIAL;
	} else if (streq(value, "final")) {
		*capture = HEADLESS_CAPTURE_FINAL;
	} else if (streq(value, "both")) {
		*capture = HEADLESS_CAPTURE_BOTH;
	} else if (streq(value, "sequence")) {
		*capture = HEADLESS_CAPTURE_SEQUENCE;
	} else {
		return false;
	}
	return true;
}

static bool parse_expected_result(const char *value,
		enum headless_expected_result *expected)
{
	if (streq(value, "complete")) {
		*expected = HEADLESS_EXPECT_COMPLETE;
	} else if (streq(value, "dead")) {
		*expected = HEADLESS_EXPECT_DEAD;
	} else if (streq(value, "any")) {
		*expected = HEADLESS_EXPECT_ANY;
	} else {
		return false;
	}
	return true;
}

static bool valid_map_style(const char *value)
{
	return my_stricmp(value, "classic") == 0 ||
		my_stricmp(value, "natural") == 0;
}

static bool valid_map_presentation(const char *value)
{
	return my_stricmp(value, "ascii") == 0 ||
		my_stricmp(value, "tiles") == 0 ||
		my_stricmp(value, "hybrid") == 0 ||
		my_stricmp(value, "hybrid-32") == 0 ||
		my_stricmp(value, "hybrid-64") == 0;
}

void headless_scenario_defaults(struct headless_scenario *scenario)
{
	memset(scenario, 0, sizeof(*scenario));
	scenario->seed = 1U;
	my_strcpy(scenario->race, "Nearlander", sizeof(scenario->race));
	my_strcpy(scenario->player_class, "Warrior",
		sizeof(scenario->player_class));
	my_strcpy(scenario->player_name, "Headless",
		sizeof(scenario->player_name));
	my_strcpy(scenario->location, "core.hub", sizeof(scenario->location));
	my_strcpy(scenario->entry, "default", sizeof(scenario->entry));
	scenario->bot = HEADLESS_BOT_WAIT;
	scenario->steps = 20U;
	scenario->capture = HEADLESS_CAPTURE_FINAL;
	scenario->capture_fps = 30U;
	scenario->capture_preroll_ms = 500U;
	scenario->capture_action_ms = 250U;
	scenario->capture_postroll_ms = 500U;
	scenario->expected_result = HEADLESS_EXPECT_COMPLETE;
	my_strcpy(scenario->map_style, "natural", sizeof(scenario->map_style));
	my_strcpy(scenario->map_presentation, "ascii",
		sizeof(scenario->map_presentation));
	scenario->map_zoom_percent = 100;
}

static bool parse_field(struct headless_scenario *scenario,
		struct scenario_fields *fields, const char *key, const char *value,
		unsigned int line, char *error, size_t error_size)
{
	unsigned long number;

	if (streq(key, "name")) {
		return set_once(scenario->name, sizeof(scenario->name), &fields->name,
			value, line, error, error_size);
	}
	if (streq(key, "seed")) {
		if (fields->seed) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, UINT32_MAX, &number)) {
			return set_error(error, error_size, line, "invalid seed", value);
		}
		scenario->seed = (uint32_t)number;
		fields->seed = true;
		return true;
	}
	if (streq(key, "race")) {
		return set_once(scenario->race, sizeof(scenario->race), &fields->race,
			value, line, error, error_size);
	}
	if (streq(key, "class")) {
		return set_once(scenario->player_class,
			sizeof(scenario->player_class), &fields->player_class, value,
			line, error, error_size);
	}
	if (streq(key, "player")) {
		return set_once(scenario->player_name, sizeof(scenario->player_name),
			&fields->player_name, value, line, error, error_size);
	}
	if (streq(key, "location")) {
		return set_once(scenario->location, sizeof(scenario->location),
			&fields->location, value, line, error, error_size);
	}
	if (streq(key, "entry")) {
		return set_once(scenario->entry, sizeof(scenario->entry), &fields->entry,
			value, line, error, error_size);
	}
	if (streq(key, "enter-route")) {
		return set_once(scenario->enter_route, sizeof(scenario->enter_route),
			&fields->enter_route, value, line, error, error_size);
	}
	if (streq(key, "bot")) {
		if (fields->bot) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_bot(value, &scenario->bot)) {
			return set_error(error, error_size, line, "invalid bot", value);
		}
		fields->bot = true;
		return true;
	}
	if (streq(key, "steps")) {
		if (fields->steps) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, HEADLESS_SCENARIO_MAX_STEPS, &number)) {
			return set_error(error, error_size, line, "invalid step count",
				value);
		}
		scenario->steps = (unsigned int)number;
		fields->steps = true;
		return true;
	}
	if (streq(key, "capture")) {
		if (fields->capture) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_capture(value, &scenario->capture)) {
			return set_error(error, error_size, line, "invalid capture mode",
				value);
		}
		fields->capture = true;
		return true;
	}
	if (streq(key, "capture-fps")) {
		if (fields->capture_fps) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, HEADLESS_CAPTURE_MAX_FPS, &number) ||
				!number) {
			return set_error(error, error_size, line,
				"invalid capture frame rate", value);
		}
		scenario->capture_fps = (unsigned int)number;
		fields->capture_fps = true;
		return true;
	}
	if (streq(key, "capture-preroll-ms") ||
			streq(key, "capture-action-ms") ||
			streq(key, "capture-postroll-ms")) {
		bool *seen = streq(key, "capture-preroll-ms") ?
			&fields->capture_preroll_ms :
			(streq(key, "capture-action-ms") ?
				&fields->capture_action_ms : &fields->capture_postroll_ms);

		if (*seen) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, HEADLESS_CAPTURE_MAX_DURATION_MS,
				&number)) {
			return set_error(error, error_size, line,
				"invalid capture duration", value);
		}
		if (streq(key, "capture-preroll-ms")) {
			scenario->capture_preroll_ms = (unsigned int)number;
		} else if (streq(key, "capture-action-ms")) {
			scenario->capture_action_ms = (unsigned int)number;
		} else {
			scenario->capture_postroll_ms = (unsigned int)number;
		}
		*seen = true;
		return true;
	}
	if (streq(key, "expect")) {
		if (fields->expected_result) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_expected_result(value, &scenario->expected_result)) {
			return set_error(error, error_size, line,
				"invalid expected result", value);
		}
		fields->expected_result = true;
		return true;
	}
	if (streq(key, "interface-font")) {
		return set_once(scenario->interface_font,
			sizeof(scenario->interface_font), &fields->interface_font, value,
			line, error, error_size);
	}
	if (streq(key, "map-font")) {
		return set_once(scenario->map_font, sizeof(scenario->map_font),
			&fields->map_font, value, line, error, error_size);
	}
	if (streq(key, "theme")) {
		return set_once(scenario->theme, sizeof(scenario->theme), &fields->theme,
			value, line, error, error_size);
	}
	if (streq(key, "map-style")) {
		if (!valid_map_style(value)) {
			return set_error(error, error_size, line, "invalid map style", value);
		}
		return set_once(scenario->map_style, sizeof(scenario->map_style),
			&fields->map_style, value, line, error, error_size);
	}
	if (streq(key, "map-presentation")) {
		if (!valid_map_presentation(value)) {
			return set_error(error, error_size, line,
				"invalid map presentation", value);
		}
		return set_once(scenario->map_presentation,
			sizeof(scenario->map_presentation), &fields->map_presentation,
			value, line, error, error_size);
	}
	if (streq(key, "map-zoom")) {
		if (fields->map_zoom_percent) {
			return set_error(error, error_size, line, "duplicate field", key);
		}
		if (!parse_unsigned(value, 800, &number) || number < 50) {
			return set_error(error, error_size, line, "invalid map zoom",
				value);
		}
		scenario->map_zoom_percent = (int)number;
		fields->map_zoom_percent = true;
		return true;
	}
	if (streq(key, "screen-width") || streq(key, "screen-height")) {
		bool *seen = streq(key, "screen-width") ? &fields->screen_width :
			&fields->screen_height;

		if (*seen) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, INT_MAX, &number) || number == 0) {
			return set_error(error, error_size, line, "invalid screen size",
				value);
		}
		if (streq(key, "screen-width")) {
			scenario->screen_width = (int)number;
		} else {
			scenario->screen_height = (int)number;
		}
		*seen = true;
		return true;
	}
	if (streq(key, "start-x") || streq(key, "start-y")) {
		bool *seen = streq(key, "start-x") ? &fields->start_x :
			&fields->start_y;

		if (*seen) return set_error(error, error_size, line,
			"duplicate field", key);
		if (!parse_unsigned(value, INT_MAX, &number)) {
			return set_error(error, error_size, line,
				"invalid start coordinate", value);
		}
		if (streq(key, "start-x")) {
			scenario->start_x = (int)number;
		} else {
			scenario->start_y = (int)number;
		}
		*seen = true;
		return true;
	}
	if (streq(key, "start-anchor")) {
		if (!streq(value, "fishing-stance") && !streq(value, "open-floor") &&
				!streq(value, "first-rope-anchor") &&
				!streq(value, "downward-peek-right")) {
			return set_error(error, error_size, line,
				"invalid start anchor", value);
		}
		return set_once(scenario->start_anchor,
			sizeof(scenario->start_anchor), &fields->start_anchor, value,
			line, error, error_size);
	}
	if (streq(key, "encounter")) {
		return set_once(scenario->encounter, sizeof(scenario->encounter),
			&fields->encounter, value, line, error, error_size);
	}
	if (streq(key, "inventory")) {
		const char *definition = value;
		const char *separator = strchr(value, ':');
		const char *second = separator ? strchr(separator + 1, ':') : NULL;
		unsigned long quantity = 1;

		if (second) {
			char amount[16];
			size_t length = (size_t)(separator - value);

			if (!length || length >= sizeof(amount)) {
				return set_error(error, error_size, line,
					"invalid inventory quantity", value);
			}
			memcpy(amount, value, length);
			amount[length] = '\0';
			if (!parse_unsigned(amount, UINT8_MAX, &quantity) || !quantity) {
				return set_error(error, error_size, line,
					"invalid inventory quantity", amount);
			}
			definition = separator + 1;
			separator = second;
		}
		if (!separator || separator == definition || !separator[1] ||
				strlen(definition) >= HEADLESS_SCENARIO_TEXT) {
			return set_error(error, error_size, line,
				"inventory must be [quantity:]tval:item name", value);
		}
		if (scenario->inventory_count >= HEADLESS_SCENARIO_MAX_INVENTORY) {
			return set_error(error, error_size, line,
				"too many inventory items", NULL);
		}
		my_strcpy(scenario->inventory[scenario->inventory_count], definition,
			HEADLESS_SCENARIO_TEXT);
		scenario->inventory_quantity[scenario->inventory_count++] =
			(uint8_t)quantity;
		return true;
	}
	if (streq(key, "equipment")) {
		const char *separator = strchr(value, ':');

		if (!separator || separator == value || !separator[1] ||
				strlen(value) >= HEADLESS_SCENARIO_TEXT) {
			return set_error(error, error_size, line,
				"equipment must be tval:item name", value);
		}
		if (scenario->equipment_count >= HEADLESS_SCENARIO_MAX_INVENTORY) {
			return set_error(error, error_size, line,
				"too many equipment items", NULL);
		}
		my_strcpy(scenario->equipment[scenario->equipment_count++], value,
			HEADLESS_SCENARIO_TEXT);
		return true;
	}
	if (streq(key, "action")) {
		if (!value[0] || strlen(value) >= HEADLESS_SCENARIO_ACTION) {
			return set_error(error, error_size, line, "invalid action", value);
		}
		if (scenario->action_count >= HEADLESS_SCENARIO_MAX_ACTIONS) {
			return set_error(error, error_size, line, "too many actions", NULL);
		}
		my_strcpy(scenario->actions[scenario->action_count++], value,
			HEADLESS_SCENARIO_ACTION);
		return true;
	}
	return set_error(error, error_size, line, "unknown field", key);
}

bool headless_scenario_load(const char *path,
		struct headless_scenario *scenario, char *error, size_t error_size)
{
	struct scenario_fields fields = { 0 };
	ang_file *file;
	char buffer[2048];
	unsigned int line = 0;
	bool okay = true;

	if (!path || !path[0] || !scenario) return false;
	headless_scenario_defaults(scenario);
	file = file_open(path, MODE_READ, -1);
	if (!file) {
		if (error && error_size) {
			strnfmt(error, error_size, "cannot open scenario '%s'", path);
		}
		return false;
	}
	while (file_getl(file, buffer, sizeof(buffer))) {
		char *key;
		char *value;
		char *separator;

		line++;
		key = trim(buffer);
		if (!key[0] || key[0] == '#') continue;
		separator = strchr(key, ':');
		if (!separator) {
			okay = set_error(error, error_size, line,
				"expected field:value", NULL);
			break;
		}
		*separator = '\0';
		value = trim(separator + 1);
		key = trim(key);
		if (!key[0] || !parse_field(scenario, &fields, key, value, line,
				error, error_size)) {
			okay = false;
			break;
		}
	}
	file_close(file);
	if (!okay) return false;
	if (!fields.name) return set_error(error, error_size, line,
		"missing required name field", NULL);
	if (fields.screen_width != fields.screen_height) {
		return set_error(error, error_size, line,
			"screen-width and screen-height must be specified together", NULL);
	}
	if (fields.start_x != fields.start_y) {
		return set_error(error, error_size, line,
			"start-x and start-y must be specified together", NULL);
	}
	if ((fields.capture_fps || fields.capture_preroll_ms ||
			fields.capture_action_ms || fields.capture_postroll_ms) &&
			scenario->capture != HEADLESS_CAPTURE_SEQUENCE) {
		return set_error(error, error_size, line,
			"capture timing fields require capture:sequence", NULL);
	}
	if (fields.start_anchor && fields.start_x) {
		return set_error(error, error_size, line,
			"start-anchor cannot be combined with start coordinates", NULL);
	}
	scenario->has_start_position = fields.start_x && fields.start_y;
	if (scenario->bot == HEADLESS_BOT_SCRIPTED) {
		if (!scenario->action_count) return set_error(error, error_size, line,
			"scripted bot requires at least one action", NULL);
		if (!fields.steps) scenario->steps = (unsigned int)scenario->action_count;
		if (scenario->steps != scenario->action_count) {
			return set_error(error, error_size, line,
				"scripted steps must equal the action count", NULL);
		}
	} else if (scenario->action_count) {
		return set_error(error, error_size, line,
			"actions require bot:scripted", NULL);
	}
	if (scenario->capture == HEADLESS_CAPTURE_SEQUENCE &&
			(uint64_t)scenario->capture_preroll_ms +
			(uint64_t)scenario->steps * scenario->capture_action_ms +
			(uint64_t)scenario->capture_postroll_ms >
			HEADLESS_CAPTURE_MAX_DURATION_MS) {
		return set_error(error, error_size, line,
			"capture sequence exceeds the ten-minute limit", NULL);
	}
	return true;
}
