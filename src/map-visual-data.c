/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
/** Immutable semantic-to-art bindings for map-only presentations. */
#include "angband.h"
#include "datafile.h"
#include "map-visual-data.h"

struct binding_node {
	struct map_visual_binding value;
	struct binding_node *next;
};

static struct binding_node *bindings;

static void free_bindings(struct binding_node *node)
{
	while (node) {
		struct binding_node *next = node->next;
		string_free((char *)node->value.scope);
		string_free((char *)node->value.semantic);
		string_free((char *)node->value.domain);
		string_free((char *)node->value.art_id);
		mem_free(node);
		node = next;
	}
}

static bool semantic_valid(const char *id)
{
	const unsigned char *p = (const unsigned char *)id;
	if (!id || !id[0] || strlen(id) >= 128 || strstr(id, "..")) return false;
	for (; *p; p++) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
				*p == '-' || *p == '.')) return false;
	}
	return true;
}

static enum parser_error parse_binding(struct parser *p)
{
	struct binding_node *head = parser_priv(p), *node;
	const char *scope = parser_getsym(p, "scope");
	const char *semantic = parser_getsym(p, "semantic");
	const char *domain = parser_getsym(p, "domain");
	const char *id = parser_getsym(p, "id");

	if (!(streq(scope, "map") || streq(scope, "cave") ||
			streq(scope, "cave-material") || streq(scope, "cave-decoration") ||
			streq(scope, "cave-actor")) || !semantic_valid(semantic) ||
			!datafile_art_id_is_valid(id) ||
			!(streq(domain, "object") || streq(domain, "feature") ||
			  streq(domain, "actor")) ||
			strlen(domain) + strlen(id) + 2 > sizeof(node->value.key)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	/* Keep each consumer in its own namespace. */
	if ((streq(scope, "map") && !streq(domain, "object")) ||
			(streq(scope, "cave-actor") && !streq(domain, "actor")) ||
			(!streq(scope, "map") && !streq(scope, "cave-actor") &&
			 !streq(domain, "feature"))) return PARSE_ERROR_INVALID_VALUE;
	for (node = head; node; node = node->next) {
		if (streq(node->value.scope, scope) &&
				streq(node->value.semantic, semantic)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	node = mem_zalloc(sizeof(*node));
	node->value.scope = string_make(scope);
	node->value.semantic = string_make(semantic);
	node->value.domain = string_make(domain);
	node->value.art_id = string_make(id);
	strnfmt(node->value.key, sizeof(node->value.key), "%s:%s", domain, id);
	node->next = head;
	parser_setpriv(p, node);
	return PARSE_ERROR_NONE;
}

static struct parser *init_bindings(void)
{
	struct parser *p = parser_new();
	parser_reg(p, "tile sym scope sym semantic sym domain sym id", parse_binding);
	return p;
}

static errr run_bindings(struct parser *p)
{
	return parse_file_quit_not_found(p, "map_visuals");
}

static void cleanup_bindings(void)
{
	free_bindings(bindings);
	bindings = NULL;
}

static errr finish_bindings(struct parser *p)
{
	struct binding_node *head = parser_priv(p);
	parser_destroy(p);
	if (!head) return PARSE_ERROR_TOO_FEW_ENTRIES;
	cleanup_bindings();
	bindings = head;
	return PARSE_ERROR_NONE;
}

struct file_parser map_visual_parser = {
	"map visuals", init_bindings, run_bindings, finish_bindings, cleanup_bindings
};

const struct map_visual_binding *map_visual_for(
		const char *scope, const char *semantic)
{
	const struct binding_node *node;
	if (!scope || !semantic) return NULL;
	for (node = bindings; node; node = node->next) {
		if (streq(scope, node->value.scope) &&
				streq(semantic, node->value.semantic)) return &node->value;
	}
	return NULL; /* Unbound/new subjects deliberately retain their glyph. */
}
