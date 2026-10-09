/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file artifact-lore-data.h
 * \brief Validated authored language for generated artefact identities.
 */

#ifndef ARTIFACT_LORE_DATA_H
#define ARTIFACT_LORE_DATA_H

#include "h-basic.h"
#include "object.h"

struct file_parser;

/** Broad mechanical signatures used to select compatible story frames. */
enum artifact_lore_tag {
	ARTIFACT_LORE_ANY = 0,
	ARTIFACT_LORE_WEAPON,
	ARTIFACT_LORE_ARMOR,
	ARTIFACT_LORE_JEWELRY,
	ARTIFACT_LORE_RANGED,
	ARTIFACT_LORE_SPEED,
	ARTIFACT_LORE_STEALTH,
	ARTIFACT_LORE_WATCH,
	ARTIFACT_LORE_ELEMENT,
	ARTIFACT_LORE_LIGHT,
	ARTIFACT_LORE_DIGGING,
	ARTIFACT_LORE_CURSE,
	ARTIFACT_LORE_TAG_MAX
};

/** Parser lifecycle for lib/gamedata/artifact_lore.txt. */
extern struct file_parser artifact_lore_parser;

/** Whether a complete, release-reviewed registry is loaded. */
bool artifact_lore_data_is_loaded(void);

/** Introspection used by validation and unit tests. */
int artifact_lore_pool_count(void);
int artifact_lore_frame_count(void);
const char *artifact_lore_origin(void);
const char *artifact_lore_review(void);

/** Classify an artefact by the property which should lead its microstory. */
enum artifact_lore_tag artifact_lore_tag_for_artifact(
		const struct artifact *art);

/**
 * Create one quoted artefact name and one short description.  Both are
 * allocated for the caller.  Returns false when the reviewed data registry is
 * unavailable or no compatible frame can be expanded.
 */
bool artifact_lore_generate(const struct artifact *art,
		const char ***name_words, char **name, char **description);

#endif /* !ARTIFACT_LORE_DATA_H */
