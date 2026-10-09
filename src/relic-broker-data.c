/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Validated, immutable appraisal and pricing authority. */
#include "angband.h"
#include "relic-broker-data.h"
#include "world-location.h"

struct broker_data {
	int count;
	struct relic_broker_band bands[RELIC_BROKER_MAX_BANDS];
};
static struct broker_data registry;

static void free_data(struct broker_data *data)
{
	int i;
	for (i = 0; i < RELIC_BROKER_MAX_BANDS; i++) {
		string_free(data->bands[i].id);
		string_free(data->bands[i].name);
	}
	memset(data, 0, sizeof(*data));
}

static enum parser_error parse_band(struct parser *p)
{
	struct broker_data *data = parser_priv(p);
	struct relic_broker_band *band;
	unsigned int index = parser_getuint(p, "index");
	unsigned int min = parser_getuint(p, "min");
	unsigned int max = parser_getuint(p, "max");
	unsigned int price = parser_getuint(p, "price");
	unsigned int quantity = parser_getuint(p, "quantity");
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	unsigned int total = quantity;
	int i;
	if (!index || index > RELIC_BROKER_MAX_BANDS || !min || min >= max ||
		max > INT32_MAX || !price || price > INT32_MAX || !quantity ||
		quantity > RELIC_BROKER_MAX_LOTS || !world_id_is_valid(id) || !name[0])
		return PARSE_ERROR_INVALID_VALUE;
	for (i = 0; i < RELIC_BROKER_MAX_BANDS; i++) {
		const struct relic_broker_band *other = &data->bands[i];
		if (!other->index) continue;
		if (other->index == index || streq(other->id, id))
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		if (min < (unsigned)other->maximum && max > (unsigned)other->minimum)
			return PARSE_ERROR_INVALID_VALUE;
		total += other->quantity;
	}
	if (total > RELIC_BROKER_MAX_LOTS) return PARSE_ERROR_TOO_MANY_ENTRIES;
	band = &data->bands[index - 1];
	band->index = (uint8_t)index;
	band->id = string_make(id);
	band->name = string_make(name);
	band->minimum = (int32_t)min;
	band->maximum = (int32_t)max;
	band->price = (int32_t)price;
	band->quantity = quantity;
	data->count++;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_broker(void)
{
	struct parser *p = parser_new();
	parser_setpriv(p, mem_zalloc(sizeof(struct broker_data)));
	parser_reg(p, "band uint index sym id uint min uint max uint price uint quantity str name", parse_band);
	return p;
}

static errr run_parse_broker(struct parser *p)
{
	return parse_file_quit_not_found(p, "relic_broker");
}

static errr finish_parse_broker(struct parser *p)
{
	struct broker_data *data = parser_priv(p);
	errr r = 0;
	int i;
	if (!data->count) r = PARSE_ERROR_TOO_FEW_ENTRIES;
	for (i = 0; i < data->count && !r; i++) {
		const struct relic_broker_band *band = &data->bands[i];
		if (!band->index || (i && (band->minimum != data->bands[i-1].maximum ||
			band->price <= data->bands[i-1].price))) r = PARSE_ERROR_INVALID_VALUE;
	}
	if (!r) {
		free_data(&registry);
		registry = *data;
	} else free_data(data);
	mem_free(data);
	parser_destroy(p);
	return r;
}

static void cleanup_broker(void) { free_data(&registry); }
struct file_parser relic_broker_parser = {
	"relic_broker", init_parse_broker, run_parse_broker,
	finish_parse_broker, cleanup_broker
};
int relic_broker_band_count(void) { return registry.count; }
const struct relic_broker_band *relic_broker_band(int index)
{
	return index > 0 && index <= registry.count ? &registry.bands[index-1] : NULL;
}
