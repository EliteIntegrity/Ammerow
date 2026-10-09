/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* ui/death.c */
/* Exercise the deterministic end-of-run controller and message excerpt. */

#include "unit-test.h"

#include "message.h"
#include "ui-death.h"

int setup_tests(void **state)
{
	(void)state;
	messages_init();
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	messages_free();
	return 0;
}

static int test_primary_results_are_terminal(void *state)
{
	(void)state;
	eq(death_screen_primary_result(DEATH_SCREEN_HOME, true),
		DEATH_SCREEN_RETURN_HOME);
	eq(death_screen_primary_result(DEATH_SCREEN_HOME, false),
		DEATH_SCREEN_STAY);
	eq(death_screen_primary_result(DEATH_SCREEN_NEW_RUN, true),
		DEATH_SCREEN_START_NEW_RUN);
	eq(death_screen_primary_result(DEATH_SCREEN_QUIT, true),
		DEATH_SCREEN_EXIT_GAME);
	eq(death_screen_primary_result(DEATH_SCREEN_PRIMARY_COUNT, true),
		DEATH_SCREEN_STAY);
	ok;
}

static int test_recent_messages_are_chronological_and_bounded(void *state)
{
	char excerpt[512];
	char *second;
	char *sixth;
	(void)state;

	message_add("first, too old", MSG_GENERIC);
	message_add("second", MSG_GENERIC);
	message_add("third", MSG_GENERIC);
	message_add("fourth", MSG_GENERIC);
	message_add("fifth", MSG_GENERIC);
	message_add("sixth", MSG_HIT);
	message_add("sixth", MSG_HIT);
	death_screen_recent_messages(excerpt, sizeof(excerpt), 5);
	require(strstr(excerpt, "first, too old") == NULL);
	second = strstr(excerpt, "second");
	sixth = strstr(excerpt, "sixth");
	require(second != NULL);
	require(sixth != NULL);
	require(second < sixth);
	require(strstr(excerpt, "<2x>") != NULL);
	ok;
}

const char *suite_name = "ui/death";
struct test tests[] = {
	{ "primary results are terminal", test_primary_results_are_terminal },
	{ "recent messages are chronological and bounded",
		test_recent_messages_are_chronological_and_bounded },
	{ NULL, NULL }
};
