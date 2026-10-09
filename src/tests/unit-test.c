/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* unit-test.c
 *
 * Framework for unit/regression testing harness
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "unit-test.h"
#include "z-rand.h"
#include "z-util.h"

int verbose = 0;
int forcepath = 0;

int main(int argc, char *argv[]) {
	void *state;
	int i;
	int passed = 0;
	int total = 0;

#if defined(_MSC_VER)
	/* A failed assertion must terminate an unattended test instead of opening
	 * the Windows Error Reporting dialog and blocking the aggregate runner. */
	_set_abort_behavior(0, _CALL_REPORTFAULT);
#endif

	char *s = getenv("VERBOSE");
	if (s && s[0]) {
		verbose = 1;
	}
	s = getenv("FORCE_PATH");
	if (s && s[0]) {
		forcepath = 1;
	}
	for (i = 1; i < argc; ++i) {
		if (argv[i][0] == '-') {
			if (strchr(argv[i] + 1, 'v')) {
				verbose = 1;
			}
			if (strchr(argv[i] + 1, 'f')) {
				forcepath = 1;
			}
		}
	}

	/* A test suite must be reproducible rather than inheriting Rand_init()'s
	 * wall-clock and process-ID seed.  Individual tests which need another
	 * deterministic stream may reseed it explicitly. */
	rand_unfix();
	Rand_state_init(0x5eed1234U);

	if (verbose) {
		printf("%s: starting...\n", suite_name);
		fflush(stdout);
	}

	if (setup_tests(&state)) {
		printf("ERROR: %s setup failed\n", suite_name);
		return 1;
	}

	for (i = 0; tests[i].name; i++) {
		if (verbose) printf("  %-16s  ", tests[i].name);
		fflush(stdout);
		if (tests[i].func(state) == 0) passed++;
		total++;
		fflush(stdout);
	}

	if (teardown_tests(state)) {
		printf("ERROR: %s teardown failed\n", suite_name);
		return 1;
	}

	printf("%s finished: %d/%d passed\n", suite_name, passed, total);
	return passed == total ? 0 : 1;
}

int showpass(void) {
	if (verbose) printf("\033[01;32mPassed\033[00m\n");
	return 0;
}
int showfail(void) {
	if (verbose) printf("\033[01;31mFailed\033[00m\n");
	return 1;
}
