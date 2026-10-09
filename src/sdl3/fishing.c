/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/fishing.c
 * \brief Full-screen transparent SDL3 presentation for fishing.
 *
 * The composition follows the authorised NetHook prototype (see AUTHORS.md):
 * a translucent live-world overlay, fine independent
 * water grid, right-bank angler, curved ASCII rod, bubbles, phase feedback,
 * and a bottom-right control legend.  Gameplay remains in world-fishing.c.
 */

#include "angband.h"

#include <math.h>
#include <string.h>

#include "sdl3/fishing.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"
#include "world-fishing.h"

struct fishing_point {
	float x;
	float y;
};

static int clamp_int(int value, int minimum, int maximum)
{
	if (value < minimum) return minimum;
	if (value > maximum) return maximum;
	return value;
}

static int text_width(const struct sdl3_visual *visual, const char *text)
{
	return text ? (int)strlen(text) * visual->cell_width : 0;
}

static void draw_text_pixels(struct sdl3_visual *visual, const char *text,
		float x, float y, int maximum, SDL_Color color)
{
	int i;

	if (!visual || !visual->font_fits || !text || maximum <= 0) return;
	for (i = 0; text[i] && i < maximum; i++) {
		sdl3_font_draw(&visual->font, (uint8_t)text[i], color,
			x + i * visual->cell_width, y, (float)visual->cell_width,
			(float)visual->cell_height);
	}
}

static void draw_glyph_pixels(struct sdl3_visual *visual, char glyph,
		float x, float y, SDL_Color color)
{
	char text[2] = { glyph, '\0' };

	draw_text_pixels(visual, text, x, y, 1, color);
}

static void draw_text_right_pixels(struct sdl3_visual *visual,
		const char *text, float left, float right, float y, SDL_Color color)
{
	int maximum;
	int length;

	if (!visual || !text || right <= left) return;
	maximum = MAX(0, (int)(right - left) / visual->cell_width);
	length = MIN((int)strlen(text), maximum);
	draw_text_pixels(visual, text, right - length * visual->cell_width,
		y, length, color);
}

static struct fishing_point bezier_point(struct fishing_point start,
		struct fishing_point control_a, struct fishing_point control_b,
		struct fishing_point end, double t)
{
	double one_minus_t = 1.0 - t;
	struct fishing_point point;

	point.x = (float)(one_minus_t * one_minus_t * one_minus_t * start.x +
		3.0 * one_minus_t * one_minus_t * t * control_a.x +
		3.0 * one_minus_t * t * t * control_b.x + t * t * t * end.x);
	point.y = (float)(one_minus_t * one_minus_t * one_minus_t * start.y +
		3.0 * one_minus_t * one_minus_t * t * control_a.y +
		3.0 * one_minus_t * t * t * control_b.y + t * t * t * end.y);
	return point;
}

static SDL_Color fish_color(const struct world_fishing_fish *fish,
		const struct sdl3_theme *theme)
{
	return sdl3_theme_color(theme, fish ? fish->attr : COLOUR_WHITE);
}

static float simulation_x(const struct world_fishing_runtime *runtime,
		int col, float left, float right)
{
	int columns = MAX(2, runtime->columns);

	return left + clamp_int(col, 0, columns - 1) * (right - left) /
		(float)(columns - 1);
}

static float depth_y(const struct world_fishing_runtime *runtime, int row,
		float water_top, float row_height, int text_height)
{
	int clamped = clamp_int(row, 1, runtime->depth_rows);

	return water_top + (clamped - 1) * row_height +
		(row_height - text_height) * 0.5f;
}

bool sdl3_fishing_is_active(const struct world_fishing_runtime *runtime)
{
	return runtime && runtime->active;
}

void sdl3_fishing_draw(const struct world_fishing_runtime *runtime,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int reserved_bottom_rows)
{
	static const struct {
		int numerator;
		int period;
		int offset;
	} bubbles[] = {
		{ 5, 920, 0 }, { 13, 710, 190 }, { 23, 1080, 410 },
		{ 34, 830, 620 }, { 43, 990, 100 }
	};
	const char *status_text = "FISHING RIG";
	const char *control_a = "LEFT/RIGHT REACH   UP/DOWN DEPTH   SPACE WAIT";
	const char *control_b = "ENTER STRIKE   ESC STOP";
	SDL_Color status_color;
	SDL_FRect rect;
	SDL_FRect control_panel;
	char rig[224];
	char phase_detail[96];
	Uint64 now;
	int width, height, margin, text_height, header_height;
	int control_padding, control_width, control_height;
	int desired_water_top, minimum_water_height, latest_water_top;
	float water_top, water_bottom, water_left, water_right, row_height;
	float player_x, hook_x;
	int hook_depth;
	int i;

	if (!runtime || !runtime->active || !renderer || !visual || !theme ||
			!visual->font_fits || visual->output_width < 320 ||
			visual->output_height < 240) {
		return;
	}
	width = visual->output_width;
	height = reserved_bottom_rows > 0 ? visual->origin_y +
		(visual->rows - reserved_bottom_rows) * visual->cell_height :
		visual->output_height;
	text_height = visual->cell_height;
	margin = MAX(12, MIN(width, height) / 45);
	header_height = MAX(text_height * 4, height / 9);
	now = SDL_GetTicks();
	status_color = theme->accent;
	if (runtime->phase == WORLD_FISHING_BITE) {
		status_text = "BITE - STRIKE NOW";
		status_color = (SDL_Color){ 255, 220, 60, 255 };
		control_a = "ENTER STRIKE   SPACE WAIT";
		control_b = "ESC STOP";
	} else if (runtime->phase == WORLD_FISHING_WINDING) {
		status_text = "REELING IN";
		status_color = (SDL_Color){ 100, 230, 145, 255 };
		control_a = "UP / KP8 REEL";
		control_b = "ESC STOP";
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	sdl3_ui_fill_output(renderer, visual, (SDL_Color){ 4, 12, 22, 158 });
	sdl3_ui_set_color(renderer, (SDL_Color){ 5, 13, 22, 188 });
	rect = (SDL_FRect){ 0.0f, 0.0f, (float)width, (float)header_height };
	SDL_RenderFillRect(renderer, &rect);
	draw_text_pixels(visual, status_text,
		(float)(width - text_width(visual, status_text)) * 0.5f,
		(float)margin, (int)strlen(status_text), status_color);
	strnfmt(rig, sizeof(rig), "Hook: %s   Depth: %d/%d   Reach: %d/%d   Water: %s   Bait: none   Tackle: %s",
		runtime->depth ? "wet" : "dry", runtime->depth,
		runtime->maximum_depth, runtime->rod_reach, runtime->maximum_reach,
		world_fishing_habitat_name(runtime->habitat), runtime->rig_name);
	draw_text_pixels(visual, rig, (float)margin,
		(float)(margin + text_height + 5),
		MAX(1, (width - margin * 2) / visual->cell_width), theme->text);

	phase_detail[0] = '\0';
	if (runtime->phase == WORLD_FISHING_BITE) {
		int timeout = runtime->hooked_index >= 0 &&
			runtime->hooked_index < runtime->fish_count ?
			runtime->fish[runtime->hooked_index].bite_timeout : 4;
		strnfmt(phase_detail, sizeof(phase_detail), "Strike window: %d",
			MAX(0, timeout - runtime->bite_counter));
	} else if (runtime->phase == WORLD_FISHING_WINDING) {
		char bar[32];
		int filled = runtime->wind_needed > 0 ? clamp_int(
			runtime->wind_turns * 20 / runtime->wind_needed, 0, 20) : 0;
		int cursor = 0;

		bar[cursor++] = '[';
		for (i = 0; i < 20; i++) bar[cursor++] = i < filled ? '=' : '.';
		bar[cursor++] = ']';
		bar[cursor] = '\0';
		strnfmt(phase_detail, sizeof(phase_detail), "Reel %s", bar);
	}
	if (phase_detail[0]) {
		draw_text_pixels(visual, phase_detail, (float)margin,
			(float)(margin + text_height * 2 + 8),
			(int)strlen(phase_detail), runtime->phase == WORLD_FISHING_BITE ?
			(SDL_Color){ 255, 165, 70, 255 } :
			(SDL_Color){ 100, 230, 145, 255 });
	}

	control_padding = MAX(8, margin / 2);
	control_width = MAX(text_width(visual, control_a),
		text_width(visual, control_b)) + control_padding * 2;
	control_width = MIN(width - margin * 2, control_width);
	control_height = control_padding * 2 + text_height * 2 + 3;
	control_panel = (SDL_FRect) {
		(float)(width - margin - control_width),
		(float)(height - margin - control_height),
		(float)control_width, (float)control_height
	};
	desired_water_top = MAX(text_height * 6, height * 28 / 100);
	minimum_water_height = runtime->depth_rows * MAX(5, text_height - 3);
	latest_water_top = (int)control_panel.y - margin - minimum_water_height;
	water_top = (float)MAX(text_height * 5,
		MIN(desired_water_top, latest_water_top));
	water_bottom = MAX(water_top + runtime->depth_rows,
		control_panel.y - margin);
	row_height = MAX(1.0f,
		(water_bottom - water_top) / runtime->depth_rows);
	water_left = (float)(margin + visual->cell_width * 3);
	player_x = (float)(width - margin - visual->cell_width * 3);
	water_right = MAX(water_left + 1.0f,
		player_x - visual->cell_width * 2.0f);

	for (i = 1; i <= runtime->depth_rows; i++) {
		Uint8 alpha = (Uint8)(35 + i * 65 / MAX(1, runtime->depth_rows));

		rect = (SDL_FRect){ water_left, water_top + (i - 1) * row_height,
			water_right - water_left, row_height };
		sdl3_ui_set_color(renderer, (SDL_Color){ 4, 34, 65, alpha });
		SDL_RenderFillRect(renderer, &rect);
		strnfmt(phase_detail, sizeof(phase_detail), "%d", i);
		draw_text_pixels(visual, phase_detail, (float)margin,
			water_top + (i - 1) * row_height +
			MAX(0.0f, (row_height - text_height) * 0.5f),
			3, (SDL_Color){ theme->text.r, theme->text.g,
				theme->text.b, 190 });
	}
	{
		int wave_step = MAX(8, visual->cell_width);
		int wave_time = (int)(now / 280);
		int x;

		for (x = (int)water_left; x < (int)water_right; x += wave_step) {
			int wave = (x - (int)water_left) / wave_step;
			draw_glyph_pixels(visual,
				(wave + wave_time) % 5 < 2 ? '~' : '-', (float)x,
				water_top - text_height,
				(SDL_Color){ 75, 175, 225, 235 });
		}
	}

	rect = (SDL_FRect){ water_right, water_top - 2.0f,
		(float)width - water_right, water_bottom - water_top + 2.0f };
	sdl3_ui_set_color(renderer, (SDL_Color){ 38, 46, 48, 105 });
	SDL_RenderFillRect(renderer, &rect);
	draw_glyph_pixels(visual, '@', player_x,
		water_top - text_height * 2.0f, theme->title);

	hook_x = simulation_x(runtime, world_fishing_hook_col(runtime),
		water_left, water_right);
	{
		double stress = runtime->phase == WORLD_FISHING_BITE ? 1.0 :
			runtime->phase == WORLD_FISHING_WINDING ? 0.85 : 0.42;
		struct fishing_point rod_base = {
			player_x + 2.0f, water_top - text_height * 0.5f
		};
		float rod_span = MAX(1.0f, rod_base.x - hook_x);
		int maximum_lift = MAX(28,
			(int)water_top - MAX(header_height, 88) - text_height);
		int rod_lift = clamp_int(26 + (int)rod_span / 7 +
			(int)(stress * 12.0), 28, maximum_lift);
		struct fishing_point rod_tip = {
			hook_x, water_top - text_height - 6.0f + (float)(stress * 5.0)
		};
		struct fishing_point control_a_point = {
			rod_base.x - rod_span / 5.0f, rod_base.y - rod_lift
		};
		struct fishing_point control_b_point = {
			rod_tip.x + rod_span * 2.0f / 5.0f,
			rod_tip.y - rod_lift * 3.0f / 5.0f + (float)(stress * 10.0)
		};
		int segment_step = MAX(7, visual->cell_width - 1);
		int segments = MAX(4, (int)rod_span / segment_step);
		int segment;

		for (segment = 0; segment <= segments; segment++) {
			double t = (double)segment / segments;
			double next_t = MIN(1.0,
				t + 1.0 / (double)segments);
			struct fishing_point point = bezier_point(rod_base,
				control_a_point, control_b_point, rod_tip, t);
			struct fishing_point next = bezier_point(rod_base,
				control_a_point, control_b_point, rod_tip, next_t);
			float tangent_x = next.x - point.x;
			float tangent_y = next.y - point.y;
			char glyph = '-';

			if (fabsf(tangent_y) > fabsf(tangent_x) * 2.0f) {
				glyph = '|';
			} else if (fabsf(tangent_y) * 3.0f > fabsf(tangent_x)) {
				glyph = tangent_x * tangent_y >= 0.0f ? '\\' : '/';
			}
			draw_glyph_pixels(visual, glyph, point.x, point.y,
				(SDL_Color){ 190, 220, 225, 255 });
		}
		draw_glyph_pixels(visual, 'O', rod_base.x - visual->cell_width,
			rod_base.y + text_height * 0.5f,
			(SDL_Color){ 115, 225, 240, 255 });

		hook_depth = runtime->depth;
		if (runtime->phase == WORLD_FISHING_WINDING &&
				runtime->wind_needed > 0) {
			hook_depth = MAX(0, runtime->depth - runtime->wind_turns *
				runtime->depth / runtime->wind_needed);
		}
		if (hook_depth > 0) {
			float y;
			float hook_y = depth_y(runtime, hook_depth, water_top,
				row_height, text_height);

			for (y = rod_tip.y + text_height; y < hook_y;
					y += text_height) {
				draw_glyph_pixels(visual, '|', hook_x, y,
					(SDL_Color){ 215, 225, 235, 220 });
			}
		}
		for (i = 0; i < (int)N_ELEMENTS(bubbles); i++) {
			int phase = (int)((now + bubbles[i].offset) /
				bubbles[i].period) % (runtime->depth_rows + 3);
			int bubble_row;
			int bubble_col;

			if (phase >= runtime->depth_rows) continue;
			bubble_row = runtime->depth_rows - phase;
			bubble_col = bubbles[i].numerator *
				(runtime->columns - 1) / 48;
			draw_glyph_pixels(visual, phase % 2 == 0 ? 'o' : '.',
				simulation_x(runtime, bubble_col, water_left, water_right),
				depth_y(runtime, bubble_row, water_top, row_height,
					text_height), (SDL_Color){ 130, 205, 235, 135 });
		}

		for (i = 0; i < runtime->fish_count; i++) {
			const struct world_fishing_fish *fish = &runtime->fish[i];
			SDL_Color color;

			if (runtime->phase == WORLD_FISHING_WINDING &&
					i == runtime->hooked_index) {
				continue;
			}
			color = fish_color(fish, theme);
			if (fish->biting && runtime->phase == WORLD_FISHING_BITE) {
				color = (now / 160) % 2 == 0 ?
					(SDL_Color){ 255, 240, 80, 255 } :
					(SDL_Color){ 255, 140, 40, 255 };
			}
			draw_glyph_pixels(visual, fish->glyph ? fish->glyph :
				world_fishing_kind_glyph(fish->kind),
				simulation_x(runtime, fish->col, water_left, water_right),
				depth_y(runtime, fish->row, water_top, row_height,
					text_height), color);
		}

		{
			char hook_glyph = 'o';
			SDL_Color hook_color = { 245, 220, 70, 255 };
			float hook_y = rod_tip.y;

			if (hook_depth > 0) {
				hook_y = depth_y(runtime, hook_depth, water_top,
					row_height, text_height);
			}
			if (runtime->phase == WORLD_FISHING_BITE) {
				hook_glyph = '*';
				hook_color = (SDL_Color){ 255, 80, 80, 255 };
			} else if (runtime->phase == WORLD_FISHING_WINDING) {
				hook_glyph = '0';
				hook_color = (now / 150) % 2 == 0 ?
					(SDL_Color){ 255, 240, 80, 255 } :
					(SDL_Color){ 255, 255, 255, 255 };
			}
			draw_glyph_pixels(visual, hook_glyph, hook_x, hook_y,
				hook_color);
			if (runtime->phase == WORLD_FISHING_BITE &&
					(now / 180) % 2 == 0) {
				draw_glyph_pixels(visual, '*', hook_x - visual->cell_width,
					water_top - text_height,
					(SDL_Color){ 220, 245, 255, 230 });
				draw_glyph_pixels(visual, '*', hook_x + visual->cell_width,
					water_top - text_height,
					(SDL_Color){ 220, 245, 255, 230 });
			}
		}
	}

	sdl3_ui_set_color(renderer, (SDL_Color){ 5, 13, 22, 220 });
	SDL_RenderFillRect(renderer, &control_panel);
	rect = control_panel;
	rect.h = 2.0f;
	sdl3_ui_set_color(renderer, theme->accent);
	SDL_RenderFillRect(renderer, &rect);
	draw_text_right_pixels(visual, control_a,
		control_panel.x + control_padding,
		control_panel.x + control_panel.w - control_padding,
		control_panel.y + control_padding, theme->text);
	draw_text_right_pixels(visual, control_b,
		control_panel.x + control_padding,
		control_panel.x + control_panel.w - control_padding,
		control_panel.y + control_padding + text_height + 3, theme->muted);
}
