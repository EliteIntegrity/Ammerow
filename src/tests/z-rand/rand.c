/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* z-rand/rand.c */
/* Exercise the random-stream contract declared in z-rand.h. */

#include "unit-test.h"
#include "z-rand.h"

NOSETUP
NOTEARDOWN

static int test_known_sequence(void *state)
{
	static const uint32_t expected[] = {
		UINT32_C(1104872190), UINT32_C(151173159),
		UINT32_C(1865107298), UINT32_C(1826205625),
		UINT32_C(4116508021), UINT32_C(58057697),
		UINT32_C(3568922191), UINT32_C(2979204372)
	};
	static const uint32_t expected_state[RAND_STATE_WORDS] = {
		UINT32_C(2245681930), UINT32_C(1406367928),
		UINT32_C(3737606946), UINT32_C(3192696637)
	};
	struct randomizer_state snapshot;
	size_t i;

	Rand_state_init(UINT32_C(0x12345678));
	for (i = 0; i < N_ELEMENTS(expected); i++) {
		require(Rand_div(UINT32_MAX) == expected[i]);
	}
	Rand_state_export(&snapshot);
	require(snapshot.algorithm == RAND_ALGORITHM_XOSHIRO128SS);
	for (i = 0; i < RAND_STATE_WORDS; i++) {
		require(snapshot.words[i] == expected_state[i]);
	}

	/* Reusing the seed must restart the sequence, not continue it. */
	Rand_state_init(UINT32_C(0x12345678));
	for (i = 0; i < N_ELEMENTS(expected); i++) {
		require(Rand_div(UINT32_MAX) == expected[i]);
	}

	ok;
}

static int test_snapshot_resume(void *state)
{
	struct randomizer_state snapshot;
	uint32_t expected[8];
	size_t i;

	Rand_state_init(UINT32_C(0x0badf00d));
	for (i = 0; i < 5; i++) (void)Rand_div(37);
	Rand_state_export(&snapshot);
	for (i = 0; i < N_ELEMENTS(expected); i++) {
		expected[i] = Rand_div(UINT32_C(1000003));
	}

	require(Rand_state_import(&snapshot));
	for (i = 0; i < N_ELEMENTS(expected); i++) {
		require(Rand_div(UINT32_C(1000003)) == expected[i]);
	}

	ok;
}

static int test_snapshot_validation(void *state)
{
	struct randomizer_state original;
	struct randomizer_state invalid;
	struct randomizer_state after;
	size_t i;

	Rand_state_init(UINT32_C(0x13579bdf));
	(void)Rand_div(19);
	Rand_state_export(&original);

	invalid = original;
	invalid.algorithm++;
	require(!Rand_state_import(&invalid));
	Rand_state_export(&after);
	require(after.algorithm == original.algorithm);
	for (i = 0; i < RAND_STATE_WORDS; i++) {
		require(after.words[i] == original.words[i]);
	}

	invalid = original;
	for (i = 0; i < RAND_STATE_WORDS; i++) invalid.words[i] = 0;
	require(!Rand_state_import(&invalid));
	Rand_state_export(&after);
	for (i = 0; i < RAND_STATE_WORDS; i++) {
		require(after.words[i] == original.words[i]);
	}

	ok;
}

static int test_stream_isolation(void *state)
{
	uint32_t expected_gameplay[2];
	uint32_t actual_gameplay[2];
	uint32_t first_generation[3];
	uint32_t repeated_generation[3];
	size_t i;

	Rand_state_init(UINT32_C(0x2468ace0));
	expected_gameplay[0] = Rand_div(UINT32_MAX);
	expected_gameplay[1] = Rand_div(UINT32_MAX);

	Rand_state_init(UINT32_C(0x2468ace0));
	actual_gameplay[0] = Rand_div(UINT32_MAX);
	Rand_begin_deterministic(UINT32_C(0x11223344));
	for (i = 0; i < N_ELEMENTS(first_generation); i++) {
		first_generation[i] = Rand_div(UINT32_MAX);
	}
	Rand_end_deterministic();
	actual_gameplay[1] = Rand_div(UINT32_MAX);

	Rand_begin_deterministic(UINT32_C(0x11223344));
	for (i = 0; i < N_ELEMENTS(repeated_generation); i++) {
		repeated_generation[i] = Rand_div(UINT32_MAX);
	}
	Rand_end_deterministic();

	for (i = 0; i < N_ELEMENTS(expected_gameplay); i++) {
		require(actual_gameplay[i] == expected_gameplay[i]);
	}
	for (i = 0; i < N_ELEMENTS(first_generation); i++) {
		require(repeated_generation[i] == first_generation[i]);
	}

	ok;
}

static int test_range_and_fixed_output(void *state)
{
	uint32_t fixed_low, fixed_middle, fixed_high;
	int i;

	Rand_state_init(UINT32_C(0xdecafbad));
	require(Rand_div(0) == 0);
	require(Rand_div(1) == 0);
	for (i = 0; i < 10000; i++) require(Rand_div(7) < 7);

	rand_fix(0);
	fixed_low = Rand_div(101);
	rand_fix(50);
	fixed_middle = Rand_div(101);
	rand_fix(100);
	fixed_high = Rand_div(101);
	rand_unfix();

	require(fixed_low == 0);
	require(fixed_middle == 50);
	require(fixed_high == 100);

	ok;
}

const char *suite_name = "z-rand/rand";
struct test tests[] = {
	{ "known xoshiro sequence", test_known_sequence },
	{ "snapshot resumes sequence", test_snapshot_resume },
	{ "snapshot validation", test_snapshot_validation },
	{ "stream isolation", test_stream_isolation },
	{ "range and fixed output", test_range_and_fixed_output },
	{ NULL, NULL }
};
