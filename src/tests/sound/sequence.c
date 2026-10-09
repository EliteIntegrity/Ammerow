/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sound/sequence.c */
/* Exercise data-authored non-blocking sound sequence timing. */

#include "unit-test.h"

#include "sound.h"
#include "ui-prefs.h"

int setup_tests(void **state)
{
	(void)state;
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	return 0;
}

static int test_valid_sequences(void *state)
{
	struct sound_sequence sequence;

	(void)state;
	require(sound_sequence_parse("0 165 330", &sequence));
	eq(sequence.count, 3);
	eq(sequence.offsets_ms[0], 0);
	eq(sequence.offsets_ms[1], 165);
	eq(sequence.offsets_ms[2], 330);
	require(sound_sequence_parse("  0\t250  ", &sequence));
	eq(sequence.count, 2);
	eq(sequence.offsets_ms[1], 250);
	require(sound_sequence_parse("0", &sequence));
	eq(sequence.count, 1);
	ok;
}

static int test_invalid_sequences(void *state)
{
	struct sound_sequence sequence = { 7, { 23 } };

	(void)state;
	require(!sound_sequence_parse(NULL, &sequence));
	require(!sound_sequence_parse("0", NULL));
	require(!sound_sequence_parse("", &sequence));
	require(!sound_sequence_parse("20 40", &sequence));
	require(!sound_sequence_parse("0 0", &sequence));
	require(!sound_sequence_parse("0 200 100", &sequence));
	require(!sound_sequence_parse("0 hammer", &sequence));
	require(!sound_sequence_parse("0 -1", &sequence));
	require(!sound_sequence_parse("0 5001", &sequence));
	require(!sound_sequence_parse("0 1 2 3 4 5 6 7 8", &sequence));
	/* Failure is transactional: callers retain their prior definition. */
	eq(sequence.count, 7);
	eq(sequence.offsets_ms[0], 23);
	ok;
}

static int test_preference_directive(void *state)
{
	struct prefs_data prefs = { 0 };
	struct parser *parser = parser_new();

	(void)state;
	notnull(parser);
	parser_setpriv(parser, &prefs);
	eq(register_sound_pref_parser(parser), 0);
	eq(parser_parse(parser,
		"sound-sequence:PITON_HAMMER:0 165 330"), PARSE_ERROR_NONE);
	eq(parser_parse(parser,
		"sound-sequence:PITON_HAMMER:30 60"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(parser,
		"sound-sequence:NOT_A_MESSAGE:0 100"),
		PARSE_ERROR_INVALID_MESSAGE);
	parser_destroy(parser);
	ok;
}

const char *suite_name = "sound/sequence";
struct test tests[] = {
	{ "valid sequences", test_valid_sequences },
	{ "invalid sequences", test_invalid_sequences },
	{ "preference directive", test_preference_directive },
	{ NULL, NULL },
};
