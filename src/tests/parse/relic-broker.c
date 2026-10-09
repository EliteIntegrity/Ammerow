/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "unit-test.h"
#include "relic-broker-data.h"
NOSETUP
NOTEARDOWN

static int test_bands(void *unused)
{
	struct parser *p = relic_broker_parser.init();
	(void)unused;
	eq(parser_parse(p, "band:1:modest:1:100:50000:3:Modest"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "band:2:great:100:500:500000:2:Great"), PARSE_ERROR_NONE);
	eq(relic_broker_parser.finish(p), PARSE_ERROR_NONE);
	eq(relic_broker_band_count(), 2);
	eq(relic_broker_band(1)->price, 50000);
	eq(relic_broker_band(2)->quantity, 2U);
	null(relic_broker_band(0));
	null(relic_broker_band(3));
	relic_broker_parser.cleanup();
	ok;
}

static int test_invalid(void *unused)
{
	struct parser *p = relic_broker_parser.init();
	(void)unused;
	eq(parser_parse(p, "band:0:no:1:100:50000:3:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:1:no:1:100:0:3:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:1:no:1:100:50000:0:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:1:no:100:1:50000:3:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:1:no:1:100:4294967295:3:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:1:modest:1:100:50000:32:Modest"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "band:1:other:100:200:60000:1:Other"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "band:2:modest:100:200:60000:1:Other"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "band:2:other:99:200:60000:1:Other"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "band:2:other:100:200:60000:1:Other"), PARSE_ERROR_TOO_MANY_ENTRIES);
	eq(relic_broker_parser.finish(p), PARSE_ERROR_NONE);
	relic_broker_parser.cleanup();
	p = relic_broker_parser.init();
	eq(parser_parse(p, "band:2:gap:100:200:60000:1:Gap"), PARSE_ERROR_NONE);
	eq(relic_broker_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	eq(relic_broker_band_count(), 0);
	p = relic_broker_parser.init();
	eq(parser_parse(p, "band:1:first:1:100:50000:1:First"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "band:2:second:101:200:60000:1:Second"), PARSE_ERROR_NONE);
	eq(relic_broker_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	ok;
}
const char *suite_name = "parse/relic-broker";
struct test tests[] = {
	{ "bands", test_bands }, { "invalid input", test_invalid }, { NULL, NULL }
};
