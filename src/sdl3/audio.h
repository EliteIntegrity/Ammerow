/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio.h
 * \brief Pure policy and settings helpers for SDL3 presentation audio.
 *
 *
 */

#ifndef INCLUDED_SDL3_AUDIO_H
#define INCLUDED_SDL3_AUDIO_H

#include <stdbool.h>

#define SDL3_AUDIO_VOLUME_MIN 0
#define SDL3_AUDIO_VOLUME_MAX 100
#define SDL3_AUDIO_VOLUME_STEP 10

enum sdl3_audio_category {
	SDL3_AUDIO_INTERFACE = 0,
	SDL3_AUDIO_GAMEPLAY,
	SDL3_AUDIO_CREATURE,
	SDL3_AUDIO_AMBIENT,
	SDL3_AUDIO_MUSIC,
	SDL3_AUDIO_CATEGORY_COUNT
};

struct sdl3_audio_settings {
	bool enabled;
	bool movement_enabled;
	bool music_enabled;
	int master_volume;
	int interface_volume;
	int gameplay_volume;
	int creature_volume;
	int ambient_volume;
	/* Fraction of outdoor ambience heard inside a shop; not a user-volume edit. */
	int shop_ambient_percent;
	int music_volume;
};

/** Parameters for the asset-free synthesized fallback for a sound cue. */
struct sdl3_audio_synth_profile {
	int frequency_hz;
	int end_frequency_hz;
	int duration_ms;
	int amplitude_percent;
};

void sdl3_audio_settings_defaults(struct sdl3_audio_settings *settings);
int sdl3_audio_change_volume(int volume, int direction);
bool sdl3_audio_cue_id_is_valid(const char *name);
bool sdl3_audio_cue_id_is_authored(const char *name);
enum sdl3_audio_category sdl3_audio_category_for_path(const char *path);
int sdl3_audio_category_volume(const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category);
bool sdl3_audio_category_is_background(enum sdl3_audio_category category);
bool sdl3_audio_category_is_audible(
		const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category);
float sdl3_audio_category_gain(const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category, bool in_store);
unsigned int sdl3_audio_rate_limit_ms(const char *path);
bool sdl3_audio_make_synth_profile(const char *path,
		struct sdl3_audio_synth_profile *profile);

#endif /* INCLUDED_SDL3_AUDIO_H */
