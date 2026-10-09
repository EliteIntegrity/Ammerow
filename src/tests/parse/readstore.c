/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/readstore */

#include "unit-test.h"
#include "cave.h"
#include "object.h"
#include "store.h"
#include "init.h"

int setup_tests(void **state) {
	z_info = mem_zalloc(sizeof(struct angband_constants));
	z_info->store_max = 6;
	z_info->store_inven_max = 24;
	/* Do the bare miminum so terrain lookup and shop assignment works. */
	f_info = mem_zalloc(FEAT_MAX * sizeof(*f_info));
	flag_on(f_info[FEAT_STORE_ARMOR].flags, TF_SIZE, TF_SHOP);
	f_info[FEAT_STORE_ARMOR].shopnum = 3;
	/* Do the bare minimum so sval and kind lookups work. */
	z_info->k_max = 2;
	z_info->ordinary_kind_max = 2;
	k_info = mem_zalloc(z_info->k_max * sizeof(*k_info));
	k_info[1].tval = 3;
	k_info[1].sval = 5;
	*state = init_parse_stores();
	return !*state;
}

int teardown_tests(void *state) {
	struct store *s = parser_priv(state);
	struct owner *o = s->owners, *o_next;
	while (o) {
		o_next = o->next;
		string_free(o->name);
		string_free(o->art_id);
		mem_free(o);
		o = o_next;
	}
	mem_free(s->normal_table);
	mem_free(s->limited_table);
	string_free(s->welcome);
	string_free(s->objective_briefing);
	string_free(s->fishing_rig_hint);
	string_free(s->art_id);
	mem_free(stores);
	parser_destroy(state);
	mem_free(k_info);
	mem_free(z_info);
	mem_free(f_info);
	return 0;
}

static int test_store_bad0(void *state) {
	struct parser *p = (struct parser*) state;
	/* Using an unknown terrain code should fail. */
	enum parser_error r = parser_parse(p, "store:XYZZY");

	eq(r, PARSE_ERROR_INVALID_VALUE);
	/* Using known terrain that does not have the SHOP flag should fail. */
	r = parser_parse(p, "store:FLOOR");
	eq(r, PARSE_ERROR_INVALID_VALUE);
	ok;
}

static int test_store0(void *state) {
	struct parser *p = (struct parser*) state;
	enum parser_error r = parser_parse(p, "store:STORE_ARMOR");
	struct store *s;

	eq(r, PARSE_ERROR_NONE);
	s = (struct store*) parser_priv(p);
	ptreq(s, stores + (f_info[FEAT_STORE_ARMOR].shopnum - 1));
	eq(s->feat, FEAT_STORE_ARMOR);
	eq(s->stock_size, z_info->store_inven_max);
	null(s->owners);
	null(s->owner);
	eq(s->stock_num, 0);
	eq(s->always_size, 0);
	eq(s->always_num, 0);
	null(s->always_table);
	eq(s->normal_size, 0);
	eq(s->normal_num, 0);
	null(s->normal_table);
	null(s->buy);
	eq(s->turnover, 0);
	eq(s->normal_stock_min, 0);
	eq(s->normal_stock_max, 0);
	ok;
}

static int test_slots0(void *state) {
	enum parser_error r = parser_parse(state, "slots:2:33");
	struct store *s;

	eq(r, PARSE_ERROR_NONE);
	s = parser_priv(state);
	require(s);
	eq(s->normal_stock_min, 2);
	eq(s->normal_stock_max, 33);
	ok;
}

static int test_owner0(void *state) {
	enum parser_error r = parser_parse(state, "owner:5000:Foo");
	struct store *s;

	eq(r, PARSE_ERROR_NONE);
	s = parser_priv(state);
	eq(s->owners->max_cost, 5000);
	require(streq(s->owners->name, "Foo"));
	ok;
}

static int test_art_ids(void *state) {
	struct store *s;
	enum parser_error r = parser_parse(state, "art:armoury");

	eq(r, PARSE_ERROR_NONE);
	r = parser_parse(state, "owner-art:foo-the-smith");
	eq(r, PARSE_ERROR_NONE);
	s = parser_priv(state);
	require(streq(s->art_id, "armoury"));
	require(streq(s->owners->art_id, "foo-the-smith"));
	eq(parser_parse(state, "art:-armoury"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(state, "owner-art:Foo"), PARSE_ERROR_INVALID_VALUE);
	require(streq(s->art_id, "armoury"));
	require(streq(s->owners->art_id, "foo-the-smith"));
	ok;
}

static int test_welcome0(void *state) {
	enum parser_error r = parser_parse(state,
		"welcome:Take rope and test every anchor.");
	struct store *s = parser_priv(state);

	eq(r, PARSE_ERROR_NONE);
	require(streq(s->welcome, "Take rope and test every anchor."));
	ok;
}

static int test_activity_copy(void *state)
{
	struct store *s = parser_priv(state);

	eq(parser_parse(state, "objective-briefing:Prepare carefully."),
		PARSE_ERROR_NONE);
	eq(parser_parse(state, "fishing-rig-hint:Ask the outfitter for a rod."),
		PARSE_ERROR_NONE);
	require(streq(s->objective_briefing, "Prepare carefully."));
	require(streq(s->fishing_rig_hint,
		"Ask the outfitter for a rod."));
	require(streq(store_fishing_rig_hint(),
		"Ask the outfitter for a rod."));
	eq(parser_parse(state, "objective-briefing:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(state, "fishing-rig-hint:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	ok;
}

static int test_i0(void *state) {
	enum parser_error r = parser_parse(state, "normal:3:5");
	struct store *s;

	eq(r, PARSE_ERROR_NONE);
	s = parser_priv(state);
	require(s);
	require(s->normal_table[0] && s->normal_table[0]->tval == 3
		&& s->normal_table[0]->sval == 5);
	ok;
}

static int test_limited0(void *state)
{
	struct store *s;
	enum parser_error r = parser_parse(state, "limited:7:3:5");

	eq(r, PARSE_ERROR_NONE);
	s = parser_priv(state);
	eq(s->limited_num, 1);
	ptreq(s->limited_table[0].kind, &k_info[1]);
	eq(s->limited_table[0].quantity, 7);
	eq(parser_parse(state, "limited:7:3:5"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	ok;
}

const char *suite_name = "parse/store";
/* test_store_bad0() has to be before test_store0(). */
struct test tests[] = {
	{ "store_bad0", test_store_bad0 },
	{ "store0", test_store0 },
	{ "slots0", test_slots0 },
	{ "owner0", test_owner0 },
	{ "art ids", test_art_ids },
	{ "welcome0", test_welcome0 },
	{ "activity copy", test_activity_copy },
	{ "i0", test_i0 },
	{ "limited0", test_limited0 },
	{ NULL, NULL }
};
