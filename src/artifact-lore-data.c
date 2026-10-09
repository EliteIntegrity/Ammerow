/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file artifact-lore-data.c
 * \brief Parser and generator for reviewed Ammerow artefact microstories.
 */

#include "angband.h"
#include "artifact-lore-data.h"
#include "datafile.h"
#include "init.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "randname.h"

#include <ctype.h>
#include <string.h>

#define LORE_TEXT_MAX 2048
#define LORE_TOKEN_MAX 32
#define LORE_MAKER_MIN 5
#define LORE_MAKER_MAX 9

struct artifact_lore_value {
	char *text;
	struct artifact_lore_value *next;
};

struct artifact_lore_pool {
	char *id;
	int count;
	struct artifact_lore_value *values;
	struct artifact_lore_value *last;
	struct artifact_lore_pool *next;
};

struct artifact_lore_frame {
	char *id;
	char *tone;
	int weight;
	enum artifact_lore_tag tag;
	char *name;
	char *text;
	uint32_t fields;
	struct artifact_lore_frame *next;
};

struct artifact_lore_registry {
	char *origin;
	char *review;
	int pool_count;
	int frame_count;
	struct artifact_lore_pool *pools;
	struct artifact_lore_pool *last_pool;
	struct artifact_lore_frame *frames;
	struct artifact_lore_frame *last_frame;
};

struct artifact_lore_parse_state {
	struct artifact_lore_registry registry;
	struct artifact_lore_pool *current_pool;
	struct artifact_lore_frame *current_frame;
};

struct artifact_lore_token {
	char id[LORE_TOKEN_MAX];
	char *value;
};

enum artifact_lore_frame_field {
	LORE_FRAME_TONE = 0x01,
	LORE_FRAME_WEIGHT = 0x02,
	LORE_FRAME_TAG = 0x04,
	LORE_FRAME_NAME = 0x08,
	LORE_FRAME_TEXT = 0x10,
	LORE_FRAME_ALL = 0x1f
};

static struct artifact_lore_registry lore_registry;
static bool lore_loaded;

static const char *lore_tag_names[ARTIFACT_LORE_TAG_MAX] = {
	"any", "weapon", "armor", "jewelry", "ranged", "speed",
	"stealth", "watch", "element", "light", "digging", "curse"
};

static bool valid_id(const char *id)
{
	const unsigned char *cursor = (const unsigned char *)id;

	if (!cursor || !*cursor) return false;
	while (*cursor) {
		if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '-')) {
			return false;
		}
		cursor++;
	}
	return true;
}

static bool valid_tone(const char *tone)
{
	static const char *tones[] = {
		"archaeological", "practical", "ominous", "dry", "peculiar"
	};
	int i;

	for (i = 0; i < (int)N_ELEMENTS(tones); i++) {
		if (streq(tone, tones[i])) return true;
	}
	return false;
}

static bool tag_from_name(const char *name, enum artifact_lore_tag *tag)
{
	int i;

	for (i = 0; i < ARTIFACT_LORE_TAG_MAX; i++) {
		if (streq(name, lore_tag_names[i])) {
			*tag = (enum artifact_lore_tag)i;
			return true;
		}
	}
	return false;
}

static void free_registry(struct artifact_lore_registry *registry)
{
	struct artifact_lore_pool *pool = registry->pools;
	struct artifact_lore_frame *frame = registry->frames;

	while (pool) {
		struct artifact_lore_pool *next_pool = pool->next;
		struct artifact_lore_value *value = pool->values;

		while (value) {
			struct artifact_lore_value *next_value = value->next;

			string_free(value->text);
			mem_free(value);
			value = next_value;
		}
		string_free(pool->id);
		mem_free(pool);
		pool = next_pool;
	}
	while (frame) {
		struct artifact_lore_frame *next_frame = frame->next;

		string_free(frame->id);
		string_free(frame->tone);
		string_free(frame->name);
		string_free(frame->text);
		mem_free(frame);
		frame = next_frame;
	}
	string_free(registry->origin);
	string_free(registry->review);
	memset(registry, 0, sizeof(*registry));
}

static struct artifact_lore_pool *find_pool(
		const struct artifact_lore_registry *registry, const char *id)
{
	struct artifact_lore_pool *pool;

	for (pool = registry->pools; pool; pool = pool->next) {
		if (streq(pool->id, id)) return pool;
	}
	return NULL;
}

static enum parser_error parse_lore_origin(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *origin = parser_getsym(p, "origin");

	if (state->registry.origin) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!streq(origin, "ammerow-original")) return PARSE_ERROR_INVALID_VALUE;
	state->registry.origin = string_make(origin);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_review(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *review = parser_getsym(p, "review");

	if (state->registry.review) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!streq(review, "release")) return PARSE_ERROR_INVALID_VALUE;
	state->registry.review = string_make(review);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_pool(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	struct artifact_lore_pool *pool;

	if (!state->registry.origin || !state->registry.review) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!valid_id(id) || find_pool(&state->registry, id)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	pool = mem_zalloc(sizeof(*pool));
	pool->id = string_make(id);
	if (state->registry.last_pool) {
		state->registry.last_pool->next = pool;
	} else {
		state->registry.pools = pool;
	}
	state->registry.last_pool = pool;
	state->registry.pool_count++;
	state->current_pool = pool;
	state->current_frame = NULL;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_value(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");
	struct artifact_lore_value *value;

	if (!state->current_pool) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	value = mem_zalloc(sizeof(*value));
	value->text = string_make(text);
	if (state->current_pool->last) {
		state->current_pool->last->next = value;
	} else {
		state->current_pool->values = value;
	}
	state->current_pool->last = value;
	state->current_pool->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_frame(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	struct artifact_lore_frame *cursor;
	struct artifact_lore_frame *frame;

	if (!state->registry.origin || !state->registry.review) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!valid_id(id)) return PARSE_ERROR_INVALID_VALUE;
	for (cursor = state->registry.frames; cursor; cursor = cursor->next) {
		if (streq(cursor->id, id)) return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	frame = mem_zalloc(sizeof(*frame));
	frame->id = string_make(id);
	if (state->registry.last_frame) {
		state->registry.last_frame->next = frame;
	} else {
		state->registry.frames = frame;
	}
	state->registry.last_frame = frame;
	state->registry.frame_count++;
	state->current_frame = frame;
	state->current_pool = NULL;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_tone(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *tone = parser_getsym(p, "tone");

	if (!state->current_frame) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if ((state->current_frame->fields & LORE_FRAME_TONE) ||
			!valid_tone(tone)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current_frame->tone = string_make(tone);
	state->current_frame->fields |= LORE_FRAME_TONE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_weight(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	unsigned int weight = parser_getuint(p, "weight");

	if (!state->current_frame) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if ((state->current_frame->fields & LORE_FRAME_WEIGHT) ||
			weight < 1 || weight > 100) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current_frame->weight = (int)weight;
	state->current_frame->fields |= LORE_FRAME_WEIGHT;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_tag(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	enum artifact_lore_tag tag;

	if (!state->current_frame) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if ((state->current_frame->fields & LORE_FRAME_TAG) ||
			!tag_from_name(parser_getsym(p, "tag"), &tag)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current_frame->tag = tag;
	state->current_frame->fields |= LORE_FRAME_TAG;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_name(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *name = parser_getstr(p, "name");

	if (!state->current_frame) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if ((state->current_frame->fields & LORE_FRAME_NAME) || !name[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current_frame->name = string_make(name);
	state->current_frame->fields |= LORE_FRAME_NAME;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_lore_text(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state->current_frame) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if ((state->current_frame->fields & LORE_FRAME_TEXT) || !text[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current_frame->text = string_make(text);
	state->current_frame->fields |= LORE_FRAME_TEXT;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_artifact_lore(void)
{
	struct parser *p = parser_new();
	struct artifact_lore_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "origin sym origin", parse_lore_origin);
	parser_reg(p, "review sym review", parse_lore_review);
	parser_reg(p, "pool sym id", parse_lore_pool);
	parser_reg(p, "value str text", parse_lore_value);
	parser_reg(p, "frame sym id", parse_lore_frame);
	parser_reg(p, "tone sym tone", parse_lore_tone);
	parser_reg(p, "weight uint weight", parse_lore_weight);
	parser_reg(p, "tag sym tag", parse_lore_tag);
	parser_reg(p, "name str name", parse_lore_name);
	parser_reg(p, "text str text", parse_lore_text);
	return p;
}

static errr run_parse_artifact_lore(struct parser *p)
{
	return parse_file_quit_not_found(p, "artifact_lore");
}

static bool token_is_valid(const struct artifact_lore_registry *registry,
		const char *token)
{
	return streq(token, "kind") || streq(token, "maker") ||
		find_pool(registry, token) != NULL;
}

static bool template_is_valid(const struct artifact_lore_registry *registry,
		const char *text)
{
	const char *cursor = text;

	while (*cursor) {
		if (*cursor == '}') return false;
		if (*cursor == '{') {
			const char *end = strchr(cursor + 1, '}');
			char token[LORE_TOKEN_MAX];
			size_t length;

			if (!end || end == cursor + 1) return false;
			length = (size_t)(end - cursor - 1);
			if (length >= sizeof(token)) return false;
			memcpy(token, cursor + 1, length);
			token[length] = '\0';
			if (!valid_id(token) || !token_is_valid(registry, token)) {
				return false;
			}
			cursor = end;
		}
		cursor++;
	}
	return true;
}

static errr validate_registry(const struct artifact_lore_registry *registry)
{
	bool tags[ARTIFACT_LORE_TAG_MAX] = { false };
	struct artifact_lore_pool *pool;
	struct artifact_lore_frame *frame;
	int i;

	if (!registry->origin || !registry->review ||
			registry->pool_count < 1 || registry->frame_count < 1) {
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (pool = registry->pools; pool; pool = pool->next) {
		if (pool->count < 3) return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (frame = registry->frames; frame; frame = frame->next) {
		if (frame->fields != LORE_FRAME_ALL) {
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
		if (!template_is_valid(registry, frame->name) ||
				!template_is_valid(registry, frame->text)) {
			return PARSE_ERROR_INVALID_VALUE;
		}
		tags[frame->tag] = true;
	}
	for (i = 0; i < ARTIFACT_LORE_TAG_MAX; i++) {
		if (!tags[i]) return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_artifact_lore(void)
{
	free_registry(&lore_registry);
	lore_loaded = false;
}

static errr finish_parse_artifact_lore(struct parser *p)
{
	struct artifact_lore_parse_state *state = parser_priv(p);
	errr result = validate_registry(&state->registry);

	if (result != PARSE_ERROR_NONE) {
		free_registry(&state->registry);
		mem_free(state);
		parser_destroy(p);
		return result;
	}
	cleanup_artifact_lore();
	lore_registry = state->registry;
	memset(&state->registry, 0, sizeof(state->registry));
	lore_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser artifact_lore_parser = {
	"artifact_lore",
	init_parse_artifact_lore,
	run_parse_artifact_lore,
	finish_parse_artifact_lore,
	cleanup_artifact_lore
};

bool artifact_lore_data_is_loaded(void)
{
	return lore_loaded;
}

int artifact_lore_pool_count(void)
{
	return lore_loaded ? lore_registry.pool_count : 0;
}

int artifact_lore_frame_count(void)
{
	return lore_loaded ? lore_registry.frame_count : 0;
}

const char *artifact_lore_origin(void)
{
	return lore_loaded ? lore_registry.origin : NULL;
}

const char *artifact_lore_review(void)
{
	return lore_loaded ? lore_registry.review : NULL;
}

static bool artifact_has_curse(const struct artifact *art)
{
	int i;

	if (!art->curses || !z_info) return false;
	for (i = 1; i < z_info->curse_max; i++) {
		if (art->curses[i]) return true;
	}
	return false;
}

static bool artifact_has_element(const struct artifact *art)
{
	int i;

	for (i = 0; i < ELEM_MAX; i++) {
		if (ABS(art->el_info[i].res_level) >= 2) return true;
	}
	if (art->brands && z_info) {
		for (i = 1; i < z_info->brand_max; i++) {
			if (art->brands[i]) return true;
		}
	}
	return false;
}

enum artifact_lore_tag artifact_lore_tag_for_artifact(
		const struct artifact *art)
{
	if (!art) return ARTIFACT_LORE_ANY;
	if (artifact_has_curse(art)) return ARTIFACT_LORE_CURSE;
	if (art->modifiers[OBJ_MOD_SPEED]) return ARTIFACT_LORE_SPEED;
	if (art->tval == TV_DIGGING || art->modifiers[OBJ_MOD_TUNNEL]) {
		return ARTIFACT_LORE_DIGGING;
	}
	if (art->modifiers[OBJ_MOD_STEALTH]) return ARTIFACT_LORE_STEALTH;
	if (art->tval == TV_LIGHT || art->modifiers[OBJ_MOD_LIGHT] ||
			of_has(art->flags, OF_LIGHT_2) ||
			of_has(art->flags, OF_LIGHT_3)) {
		return ARTIFACT_LORE_LIGHT;
	}
	if (of_has(art->flags, OF_SEE_INVIS) ||
			of_has(art->flags, OF_TELEPATHY)) {
		return ARTIFACT_LORE_WATCH;
	}
	if (artifact_has_element(art)) return ARTIFACT_LORE_ELEMENT;
	if (art->tval == TV_BOW) return ARTIFACT_LORE_RANGED;
	if (art->tval == TV_HAFTED || art->tval == TV_POLEARM ||
			art->tval == TV_SWORD) {
		return ARTIFACT_LORE_WEAPON;
	}
	if (art->tval == TV_BOOTS || art->tval == TV_GLOVES ||
			art->tval == TV_HELM || art->tval == TV_CROWN ||
			art->tval == TV_SHIELD || art->tval == TV_CLOAK ||
			art->tval == TV_SOFT_ARMOR || art->tval == TV_HARD_ARMOR ||
			art->tval == TV_DRAG_ARMOR) {
		return ARTIFACT_LORE_ARMOR;
	}
	if (art->tval == TV_RING || art->tval == TV_AMULET) {
		return ARTIFACT_LORE_JEWELRY;
	}
	return ARTIFACT_LORE_ANY;
}

static const struct artifact_lore_frame *choose_frame(
		enum artifact_lore_tag tag)
{
	const struct artifact_lore_frame *frame;
	int total = 0;
	int choice;

	for (frame = lore_registry.frames; frame; frame = frame->next) {
		if (frame->tag == tag) total += frame->weight;
	}
	if (!total && tag != ARTIFACT_LORE_ANY) {
		return choose_frame(ARTIFACT_LORE_ANY);
	}
	if (!total) return NULL;
	choice = randint0(total);
	for (frame = lore_registry.frames; frame; frame = frame->next) {
		if (frame->tag != tag) continue;
		if (choice < frame->weight) return frame;
		choice -= frame->weight;
	}
	return NULL;
}

static const char *choose_pool_value(const struct artifact_lore_pool *pool)
{
	const struct artifact_lore_value *value = pool->values;
	int choice = randint0(pool->count);

	while (choice-- > 0 && value) value = value->next;
	return value ? value->text : NULL;
}

static void artifact_kind_name(const struct artifact *art, char *buffer,
		size_t size)
{
	struct object_kind *kind = NULL;

	if (z_info && k_info) kind = lookup_kind(art->tval, art->sval);
	if (kind) {
		char *cursor;

		object_short_name(buffer, size, kind->name);
		for (cursor = buffer; *cursor; cursor++) {
			*cursor = (char)tolower((unsigned char)*cursor);
		}
		return;
	}
	switch (art->tval) {
	case TV_BOOTS:
		my_strcpy(buffer, "pair of boots", size);
		break;
	case TV_GLOVES:
		my_strcpy(buffer, "pair of gloves", size);
		break;
	case TV_SOFT_ARMOR:
		my_strcpy(buffer, "suit of light armour", size);
		break;
	case TV_HARD_ARMOR:
		my_strcpy(buffer, "suit of heavy armour", size);
		break;
	case TV_DRAG_ARMOR:
		my_strcpy(buffer, "suit of scale armour", size);
		break;
	case TV_HAFTED:
		my_strcpy(buffer, "hafted weapon", size);
		break;
	case TV_DIGGING:
		my_strcpy(buffer, "digging tool", size);
		break;
	default:
		my_strcpy(buffer, tval_find_name(art->tval), size);
		break;
	}
}

static const char *cached_token(const char *id,
		const struct artifact *art, const char ***name_words,
		struct artifact_lore_token cache[], int *cache_count)
{
	const struct artifact_lore_pool *pool;
	const char *value = NULL;
	char generated[LORE_MAKER_MAX + 1];
	char kind_name[120];
	int i;

	for (i = 0; i < *cache_count; i++) {
		if (streq(cache[i].id, id)) return cache[i].value;
	}
	if (*cache_count >= LORE_TOKEN_MAX) return NULL;
	if (streq(id, "kind")) {
		artifact_kind_name(art, kind_name, sizeof(kind_name));
		value = kind_name;
	} else if (streq(id, "maker")) {
		if (!name_words || !name_words[RANDNAME_AMMEROW]) return NULL;
		randname_make(RANDNAME_AMMEROW, LORE_MAKER_MIN, LORE_MAKER_MAX,
			generated, sizeof(generated), name_words);
		my_strcap(generated);
		value = generated;
	} else {
		pool = find_pool(&lore_registry, id);
		if (pool) value = choose_pool_value(pool);
	}
	if (!value) return NULL;
	my_strcpy(cache[*cache_count].id, id,
		sizeof(cache[*cache_count].id));
	cache[*cache_count].value = string_make(value);
	(*cache_count)++;
	return cache[*cache_count - 1].value;
}

static bool expand_template(const char *input, const struct artifact *art,
		const char ***name_words, struct artifact_lore_token cache[],
		int *cache_count, char output[LORE_TEXT_MAX])
{
	const char *cursor = input;

	output[0] = '\0';
	while (*cursor) {
		if (*cursor == '{') {
			const char *end = strchr(cursor + 1, '}');
			char token[LORE_TOKEN_MAX];
			const char *value;
			size_t length;

			if (!end) return false;
			length = (size_t)(end - cursor - 1);
			if (length >= sizeof(token)) return false;
			memcpy(token, cursor + 1, length);
			token[length] = '\0';
			value = cached_token(token, art, name_words, cache,
				cache_count);
			if (!value) return false;
			my_strcat(output, value, LORE_TEXT_MAX);
			cursor = end + 1;
		} else {
			char one[2] = { *cursor, '\0' };

			my_strcat(output, one, LORE_TEXT_MAX);
			cursor++;
		}
	}
	return true;
}

bool artifact_lore_generate(const struct artifact *art,
		const char ***name_words, char **name, char **description)
{
	const struct artifact_lore_frame *frame;
	struct artifact_lore_token cache[LORE_TOKEN_MAX] = { 0 };
	char title[LORE_TEXT_MAX];
	char quoted[LORE_TEXT_MAX];
	char text[LORE_TEXT_MAX];
	int cache_count = 0;
	int i;
	bool result = false;

	if (name) *name = NULL;
	if (description) *description = NULL;
	if (!lore_loaded || !art || !name || !description) return false;
	frame = choose_frame(artifact_lore_tag_for_artifact(art));
	if (!frame) return false;
	if (!expand_template(frame->name, art, name_words, cache, &cache_count,
			title)) {
		goto cleanup;
	}
	if (!expand_template(frame->text, art, name_words, cache, &cache_count,
			text)) {
		goto cleanup;
	}
	strnfmt(quoted, sizeof(quoted), "'%s'", title);
	*name = string_make(quoted);
	*description = string_make(text);
	result = true;

cleanup:
	for (i = 0; i < cache_count; i++) string_free(cache[i].value);
	return result;
}
