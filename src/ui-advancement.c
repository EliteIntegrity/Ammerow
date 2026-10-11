/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "angband.h"
#include "init.h"
#include "player-advancement.h"
#include "player-util.h"
#include "ui-advancement.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-screen.h"

static bool has_reward(int level)
{
	return player_advancement_offer(level) &&
		(player->advancement.rewards & (1U << (level / 5 - 1)));
}

void ui_advancement_present(int level, int cursor, const char *notice)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[4] = { 0 };
	char labels[4][100], title[80], subtitle[160];
	bool reward;
	int count, top, height;
	const char *help = "Arrows choose   Enter confirms   Escape chooses later";
	if (!Term || !player || level < 2 || level > PY_MAX_LEVEL) return;
	reward = has_reward(level);
	count = reward ? 4 : 1;
	height = count + 14;
	top = MAX(1, (Term->hgt - height) / 2);
	strnfmt(title, sizeof(title), "LEVEL %d%s", level, reward ? " - MILESTONE REWARD" : " - WELL EARNED");
	strnfmt(subtitle, sizeof(subtitle), "Current max HP: %d   Focus: %d   Rank: %s",
		player->mhp, player->msp, player->class->title[(player->lev - 1) / PY_TITLE_LEVELS]);
	if (reward) {
		const struct advancement_offer *o = player_advancement_offer(level);
		strnfmt(labels[0], sizeof(labels[0]), "%d Potion%s of %s", o->potions,
			o->potions == 1 ? "" : "s", o->potion);
		strnfmt(labels[1], sizeof(labels[1]), "%d Scroll%s of %s", o->scrolls,
			o->scrolls == 1 ? "" : "s", o->scroll);
		strnfmt(labels[2], sizeof(labels[2]), "%d gold", o->gold);
		my_strcpy(labels[3], "Choose later (reward stays available)", sizeof(labels[3]));
	} else {
		my_strcpy(labels[0], "Continue the expedition", sizeof(labels[0]));
		help = "Enter / Space / Escape or click Continue";
	}
	screen.kind = UI_SCREEN_ADVANCEMENT;
	screen.modal = true;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.context = notice && notice[0] ? notice : reward ?
		"Choose one. Deferred rewards: Enter > Information > Level rewards." :
		"A supply choice awaits at each new five-level milestone.";
	screen.help = help;
	screen.content_cols = MIN(76, MAX(40, Term->wid - 12));
	screen.content_col = MAX(4, (Term->wid - screen.content_cols) / 2);
	screen.content_row = top + 8;
	screen.content_rows = count;
	screen.cursor = cursor;
	screen.rows = rows;
	screen.row_count = count;
	for (int i = 0; i < count; i++) {
		rows[i].label = labels[i];
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
	}
	if (Term->screen_hook) {
		Term->screen_hook(&screen);
	} else {
		/* Do not paint terminal text behind the semantic modal. */
		Term_clear();
		Term_putstr(screen.content_col, top + 2, screen.content_cols, COLOUR_YELLOW, title);
		Term_putstr(screen.content_col, top + 4, screen.content_cols, COLOUR_WHITE, subtitle);
		Term_putstr(screen.content_col, top + 6, screen.content_cols, COLOUR_L_BLUE, screen.context);
		for (int i = 0; i < count; i++)
			Term_putstr(screen.content_col + 2, screen.content_row + i, screen.content_cols - 2,
				i == cursor ? COLOUR_L_BLUE : COLOUR_WHITE, labels[i]);
		Term_putstr(screen.content_col, top + height - 2, screen.content_cols, COLOUR_SLATE, help);
	}
}

/* Returns false on disconnect; no pending state is consumed by opening. */
static bool show_card(int level)
{
	bool reward = has_reward(level), acknowledged = false;
	int count = reward ? 4 : 1, cursor = count - 1;
	const char *notice = NULL;
	disturb(player);
	screen_save();
	if (player->advancement.cards[level]) sound(MSG_LEVEL);
	while (!acknowledged) {
		ui_event e;
		ui_advancement_present(level, cursor, notice);
		e = inkey_m();
		if (e.type == EVT_DISCONNECT) break;
		if (e.type == EVT_ESCAPE || (e.type == EVT_KBRD && e.key.code == ESCAPE)) {
			player_advancement_dismiss(player, level);
			acknowledged = true;
		} else if (e.type == EVT_KBRD) {
			if (e.key.code == ARROW_UP || e.key.code == '8') {
				cursor = (cursor + count - 1) % count;
				sound(MSG_UI_NAVIGATE);
			} else if (e.key.code == ARROW_DOWN || e.key.code == '2') {
				cursor = (cursor + 1) % count;
				sound(MSG_UI_NAVIGATE);
			} else if (e.key.code == KC_ENTER || (!reward && e.key.code == ' ')) {
				if (!reward || cursor == 3) {
					player_advancement_dismiss(player, level);
					acknowledged = true;
				} else {
					enum advancement_result result = player_advancement_claim(player,
						level, (enum advancement_choice)cursor);
					if (result == ADVANCEMENT_OK) {
						sound(MSG_UI_ACCEPT);
						acknowledged = true;
					} else {
						notice = result == ADVANCEMENT_NO_ROOM ?
							"No room for the full reward. Choose gold or come back later." :
							"That reward is unavailable; no reward has been spent.";
					}
				}
			}
		} else if (e.type == EVT_MOUSE && !Term->screen_hook && e.mouse.button == 1) {
			int width = MIN(76, MAX(40, Term->wid - 12));
			int left = MAX(4, (Term->wid - width) / 2);
			int row = MAX(1, (Term->hgt - count - 14) / 2) + 8;
			if (e.mouse.x >= left && e.mouse.x < left + width &&
					e.mouse.y >= row && e.mouse.y < row + count) {
				cursor = e.mouse.y - row;
				Term_keypress(KC_ENTER, 0);
			}
		}
	}
	if (Term->screen_hook) Term->screen_hook(NULL);
	screen_load();
	return acknowledged;
}

void ui_advancement_event(game_event_type type, game_event_data *data, void *user)
{
	int level;
	(void)type; (void)data; (void)user;
	if (!Term || !player || player->is_dead || streq(ANGBAND_SYS, "headless")) return;
	while ((level = player_advancement_next_card(player)) != 0)
		if (!show_card(level)) break;
}

void do_cmd_level_rewards(void)
{
	int level;
	if (!player || player->is_dead) return;
	level = player_advancement_next_reward(player);
	if (!level) { msg("You have no unclaimed level rewards."); return; }
	if (!Term || streq(ANGBAND_SYS, "headless")) return;
	(void)show_card(level);
}
