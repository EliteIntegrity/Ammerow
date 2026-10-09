/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-feature.c
 * \brief Curated data-driven feature art for the SDL3 Home screen.
 */

#include "angband.h"
#include "datafile.h"
#include "parser.h"
#include "sdl3/home-feature.h"

struct home_feature_data {
	char *assets[SDL3_HOME_FEATURE_MAX_ASSETS];
	unsigned int count;
};

static struct home_feature_data *home_features;

static void free_home_features(struct home_feature_data *data)
{
	unsigned int i;

	if (!data) return;
	for (i = 0; i < data->count; i++) string_free(data->assets[i]);
	mem_free(data);
}

static void cleanup_home_features(void)
{
	free_home_features(home_features);
	home_features = NULL;
}

static enum parser_error parse_home_feature(struct parser *p)
{
	struct home_feature_data *data = parser_priv(p);
	const char *asset = parser_getsym(p, "asset");
	unsigned int i;

	if (!asset || !asset[0]) return PARSE_ERROR_INVALID_VALUE;
	for (i = 0; i < data->count; i++) {
		if (streq(data->assets[i], asset)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (data->count >= SDL3_HOME_FEATURE_MAX_ASSETS) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	data->assets[data->count++] = string_make(asset);
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_home_features(void)
{
	struct parser *p = parser_new();
	struct home_feature_data *data = mem_zalloc(sizeof(*data));

	parser_setpriv(p, data);
	parser_reg(p, "hero sym asset", parse_home_feature);
	return p;
}

static errr run_parse_home_features(struct parser *p)
{
	return parse_file_quit_not_found(p, "home_feature");
}

static errr finish_parse_home_features(struct parser *p)
{
	struct home_feature_data *data = parser_priv(p);

	if (!data->count) {
		free_home_features(data);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	cleanup_home_features();
	home_features = data;
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser home_feature_parser = {
	"home feature",
	init_parse_home_features,
	run_parse_home_features,
	finish_parse_home_features,
	cleanup_home_features
};

unsigned int sdl3_home_feature_count(void)
{
	return home_features ? home_features->count : 0;
}

const char *sdl3_home_feature_at(unsigned int index)
{
	if (!home_features || index >= home_features->count) return NULL;
	return home_features->assets[index];
}

const char *sdl3_home_feature_for_seed(uint64_t seed)
{
	if (!home_features || !home_features->count) return NULL;
	return home_features->assets[seed % home_features->count];
}
