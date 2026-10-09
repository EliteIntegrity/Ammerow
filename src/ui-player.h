/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-player.h
 * \brief character info
 */

#ifndef UI_PLAYER_H
#define UI_PLAYER_H

enum ui_player_character_page {
	UI_PLAYER_CHARACTER_OVERVIEW = 0,
	UI_PLAYER_CHARACTER_DEFENCES,
	UI_PLAYER_CHARACTER_QUESTS,
	UI_PLAYER_CHARACTER_CAVE_SURVEY,
	UI_PLAYER_CHARACTER_PAGE_COUNT
};

void display_player_stat_info(void);
void display_player_xtra_info(void);
void display_player(int mode);
void write_character_dump(ang_file *fff);
bool dump_save(const char *path);
void do_cmd_change_name(void);
int ui_player_present_objectives(int *row_offset, int cursor, bool briefing,
		const char *briefing_subtitle);
int ui_player_character_page_step(int page, int direction);
void ui_player_present_fishing_quest_briefing(const char *subtitle, bool wait_for_ack);

#endif /* !UI_PLAYER_H */
