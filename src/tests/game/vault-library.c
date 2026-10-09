/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Exercise every independently authored vault through the production builder. */

#include "unit-test.h"
#include "test-utils.h"

#include <string.h>

#include "cave.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-util.h"
#include "player.h"
#include "player-birth.h"
#include "z-file.h"
#include "z-rand.h"

#ifndef VAULT_SOURCE_ROOT
#define VAULT_SOURCE_ROOT "."
#endif

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	init_angband();
	return player_make_simple(NULL, NULL, "Vault Tester") ? 0 : 1;
}

int teardown_tests(void *state)
{
	(void)state;
	cleanup_angband();
	return 0;
}

static struct vault *vault_by_name(const char *name)
{
	struct vault *vault;

	for (vault = vaults; vault; vault = vault->next) {
		if (streq(vault->name, name)) return vault;
	}
	return NULL;
}

static int room_grid_count(struct chunk *chunk)
{
	int count = 0;
	int y;
	int x;

	for (y = 0; y < chunk->height; y++) {
		for (x = 0; x < chunk->width; x++) {
			if (square_isroom(chunk, loc(x, y))) count++;
		}
	}
	return count;
}

static int test_every_manifest_vault_builds(void *state)
{
	char path[1024];
	char line[2048];
	ang_file *manifest;
	struct dun_data scratch = { 0 };
	struct dun_data *saved_dun = dun;
	int records = 0;
	int built = 0;
	bool all_found = true;
	bool all_have_room_grids = true;
	(void)state;

	strnfmt(path, sizeof(path), "%s/docs/vault-originality-manifest.csv",
		VAULT_SOURCE_ROOT);
	manifest = file_open(path, MODE_READ, FTYPE_TEXT);
	require(manifest);
	require(file_getl(manifest, line, sizeof(line)));
	dun = &scratch;
	while (file_getl(manifest, line, sizeof(line))) {
		char *comma = strchr(line, ',');
		struct vault *vault;
		int sample;

		if (!comma) continue;
		*comma = '\0';
		vault = vault_by_name(line);
		records++;
		if (!vault) {
			all_found = false;
			continue;
		}
		for (sample = 0; sample < 4; sample++) {
			struct chunk *chunk = t_build_arena(100, 140);
			int low = vault->min_lev ? vault->min_lev : 1;
			int high = MIN(vault->max_lev, z_info->max_depth - 1);
			bool result;

			chunk->depth = low + (high - low) * sample / 3;
			Rand_state_init(UINT32_C(0xA6600000) +
				(uint32_t)(records * 4 + sample));
			result = build_vault(chunk, loc(70, 50), vault);
			if (result) built++;
			if (!result || !room_grid_count(chunk)) all_have_room_grids = false;
			wipe_mon_list(chunk, player);
			cave_free(chunk);
		}
	}
	dun = saved_dun;
	file_close(manifest);

	eq(records, 33);
	eq(built, 132);
	require(all_found);
	require(all_have_room_grids);
	ok;
}

const char *suite_name = "game/vault-library";
struct test tests[] = {
	{ "every manifest vault builds", test_every_manifest_vault_builds },
	{ NULL, NULL }
};
