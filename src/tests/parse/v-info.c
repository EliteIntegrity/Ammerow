/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/v-info */

#include "unit-test.h"

#include "init.h"
#include "cave.h"
#include "generate.h"


int setup_tests(void **state) {
	*state = init_parse_vault();
	return !*state;
}

int teardown_tests(void *state) {
	struct vault *v = parser_priv(state);
	while (v) {
		struct vault *next = v->next;
		string_free(v->name);
		string_free(v->text);
		string_free(v->typ);
		mem_free(v);
		v = next;
	}
	parser_destroy(state);
	return 0;
}

static enum parser_error parse_test_header(struct parser *p, int rows,
		int columns, int min_depth, int max_depth) {
	enum parser_error r;

	r = parser_parse(p, "name:test vault");
	if (r) return r;
	r = parser_parse(p, "type:Lesser vault");
	if (r) return r;
	r = parser_parse(p, "rating:5");
	if (r) return r;
	r = parser_parse(p, format("rows:%d", rows));
	if (r) return r;
	r = parser_parse(p, format("columns:%d", columns));
	if (r) return r;
	r = parser_parse(p, format("min-depth:%d", min_depth));
	if (r) return r;
	return parser_parse(p, format("max-depth:%d", max_depth));
}

static void destroy_test_parser(struct parser *p) {
	struct vault *v = parser_priv(p);

	while (v) {
		struct vault *next = v->next;
		string_free(v->name);
		string_free(v->text);
		string_free(v->typ);
		mem_free(v);
		v = next;
	}
	parser_destroy(p);
}

static int test_name0(void *state) {
	enum parser_error r = parser_parse(state, "name:round");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	require(v);
	require(streq(v->name, "round"));
	ok;
}

static int test_typ0(void *state) {
	enum parser_error r = parser_parse(state, "type:Lesser vault");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	require(v);
	require(streq(v->typ, "Lesser vault"));
	ok;
}

static int test_rat0(void *state) {
	enum parser_error r = parser_parse(state, "rating:5");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	eq(v->rat, 5);
	require(v);
	ok;
}

static int test_hgt0(void *state) {
	enum parser_error r = parser_parse(state, "rows:12");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	eq(v->hgt, 12);
	require(v);
	ok;
}

static int test_wid0(void *state) {
	enum parser_error r = parser_parse(state, "columns:6");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	eq(v->wid, 6);
	require(v);
	ok;
}

static int test_min_lev0(void *state) {
	enum parser_error r = parser_parse(state, "min-depth:15");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	eq(v->min_lev, 15);
	require(v);
	ok;
}

static int test_max_lev0(void *state) {
	enum parser_error r = parser_parse(state, "max-depth:25");
	struct vault *v;

	eq(r, PARSE_ERROR_NONE);
	v = parser_priv(state);
	eq(v->max_lev, 25);
	require(v);
	ok;
}

static int test_d0(void *state) {
	enum parser_error r0 = parser_parse(state, "D:  %%  ");
	enum parser_error r1 = parser_parse(state, "D: %  % ");
	struct vault *v;

	eq(r0, PARSE_ERROR_NONE);
	eq(r1, PARSE_ERROR_NONE);
	v = parser_priv(state);
	require(v);
	require(streq(v->text, "  %%   %  % "));
	ok;
}

static int test_valid_record0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 5, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%...%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:next"), PARSE_ERROR_NONE);
	destroy_test_parser(p);
	ok;
}

static int test_wrong_height0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 5, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:next"), PARSE_ERROR_VAULT_DESC_WRONG_HEIGHT);
	destroy_test_parser(p);
	ok;
}

static int test_too_many_rows0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 1, 5, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_VAULT_DESC_WRONG_HEIGHT);
	destroy_test_parser(p);
	ok;
}

static int test_invalid_symbol0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 4, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%(.%"), PARSE_ERROR_VAULT_DESC_INVALID_SYMBOL);
	destroy_test_parser(p);
	ok;
}

static int test_open_boundary0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 5, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:....."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%...%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:next"), PARSE_ERROR_VAULT_DESC_OPEN_BOUNDARY);
	destroy_test_parser(p);
	ok;
}

static int test_invalid_depth0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 5, 20, 10), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%...%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:%%%%%"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:next"), PARSE_ERROR_VAULT_INVALID_DEPTH_RANGE);
	destroy_test_parser(p);
	ok;
}

static int test_missing_entrance0(void *state) {
	struct parser *p = init_parse_vault();
	(void)state;

	eq(parse_test_header(p, 3, 5, 0, 100), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:@@@@@"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:@...@"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "D:@@@@@"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:next"), PARSE_ERROR_VAULT_NO_ENTRANCE);
	destroy_test_parser(p);
	ok;
}

const char *suite_name = "parse/v-info";
struct test tests[] = {
	{ "name0", test_name0 },
	{ "typ0", test_typ0 },
	{ "rat0", test_rat0 },
	{ "hgt0", test_hgt0 },
	{ "wid0", test_wid0 },
	{ "min_lev0", test_min_lev0 },
	{ "max_lev0", test_max_lev0 },
	{ "d0", test_d0 },
	{ "valid_record0", test_valid_record0 },
	{ "wrong_height0", test_wrong_height0 },
	{ "too_many_rows0", test_too_many_rows0 },
	{ "invalid_symbol0", test_invalid_symbol0 },
	{ "open_boundary0", test_open_boundary0 },
	{ "invalid_depth0", test_invalid_depth0 },
	{ "missing_entrance0", test_missing_entrance0 },
	{ NULL, NULL }
};
