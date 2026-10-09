/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio.c
 * \brief Pure policy and settings helpers for SDL3 presentation audio.
 *
 *
 */

#include "sdl3/audio.h"

#include <stdint.h>
#include <string.h>

static bool path_has_category(const char *path, const char *category)
{
	const char *found;
	size_t length;

	if (!path || !category) return false;
	length = strlen(category);
	for (found = strstr(path, category); found;
			found = strstr(found + 1, category)) {
		bool starts_component = found == path || found[-1] == '/' ||
			found[-1] == '\\';
		bool ends_component = found[length] == '\0' || found[length] == '/' ||
			found[length] == '\\';

		if (starts_component && ends_component) return true;
	}
	return false;
}

void sdl3_audio_settings_defaults(struct sdl3_audio_settings *settings)
{
	if (!settings) return;
	settings->enabled = true;
	settings->movement_enabled = true;
	settings->music_enabled = true;
	settings->master_volume = 70;
	settings->interface_volume = 60;
	settings->gameplay_volume = 70;
	settings->creature_volume = 65;
	/* Authored ambience is deliberately subordinate to gameplay by default. */
	settings->ambient_volume = 40;
	settings->shop_ambient_percent = 25;
	/* The home theme should sit beneath navigation cues rather than master them. */
	settings->music_volume = 50;
}

int sdl3_audio_change_volume(int volume, int direction)
{
	if (volume < SDL3_AUDIO_VOLUME_MIN) volume = SDL3_AUDIO_VOLUME_MIN;
	if (volume > SDL3_AUDIO_VOLUME_MAX) volume = SDL3_AUDIO_VOLUME_MAX;
	if (direction > 0) volume += SDL3_AUDIO_VOLUME_STEP;
	if (direction < 0) volume -= SDL3_AUDIO_VOLUME_STEP;
	if (volume < SDL3_AUDIO_VOLUME_MIN) return SDL3_AUDIO_VOLUME_MIN;
	if (volume > SDL3_AUDIO_VOLUME_MAX) return SDL3_AUDIO_VOLUME_MAX;
	return volume;
}

static const char *cue_id_payload(const char *name)
{
	static const char retro_prefix[] = "retro/";
	static const char authored_prefix[] = "ammerow/";

	if (!name) return NULL;
	if (strncmp(name, retro_prefix, sizeof(retro_prefix) - 1) == 0) {
		return name + sizeof(retro_prefix) - 1;
	}
	if (strncmp(name, authored_prefix, sizeof(authored_prefix) - 1) == 0) {
		return name + sizeof(authored_prefix) - 1;
	}
	return NULL;
}

/** Validate a stable synthesized or Ammerow-authored cue identity. */
bool sdl3_audio_cue_id_is_valid(const char *name)
{
	const char *payload;
	const char *cursor;

	payload = cue_id_payload(name);
	if (!payload || !payload[0]) return false;
	cursor = payload;
	for (; *cursor; cursor++) {
		char c = *cursor;

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/')) {
			return false;
		}
		if (c == '/' && (cursor == payload || cursor[-1] == '/' ||
				cursor[1] == '\0')) {
			return false;
		}
	}
	return true;
}

bool sdl3_audio_cue_id_is_authored(const char *name)
{
	return name && strncmp(name, "ammerow/", 8) == 0 &&
		sdl3_audio_cue_id_is_valid(name);
}

enum sdl3_audio_category sdl3_audio_category_for_path(const char *path)
{
	if (path_has_category(path, "interface") ||
			path_has_category(path, "ui")) {
		return SDL3_AUDIO_INTERFACE;
	}
	if (path_has_category(path, "creatures") ||
			path_has_category(path, "monster") ||
			path_has_category(path, "breath") ||
			path_has_category(path, "summon")) {
		return SDL3_AUDIO_CREATURE;
	}
	if (path_has_category(path, "music")) return SDL3_AUDIO_MUSIC;
	if (path_has_category(path, "ambient")) return SDL3_AUDIO_AMBIENT;
	return SDL3_AUDIO_GAMEPLAY;
}

int sdl3_audio_category_volume(const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category)
{
	if (!settings) return 0;
	switch (category) {
	case SDL3_AUDIO_INTERFACE:
		return settings->interface_volume;
	case SDL3_AUDIO_CREATURE:
		return settings->creature_volume;
	case SDL3_AUDIO_AMBIENT:
		return settings->ambient_volume;
	case SDL3_AUDIO_MUSIC:
		return settings->music_volume;
	case SDL3_AUDIO_GAMEPLAY:
	default:
		return settings->gameplay_volume;
	}
}

bool sdl3_audio_category_is_background(enum sdl3_audio_category category)
{
	return category == SDL3_AUDIO_AMBIENT || category == SDL3_AUDIO_MUSIC;
}

bool sdl3_audio_category_is_audible(
		const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category)
{
	if (!settings || !settings->enabled || settings->master_volume <= 0) {
		return false;
	}
	if (category == SDL3_AUDIO_MUSIC && !settings->music_enabled) return false;
	return sdl3_audio_category_volume(settings, category) > 0;
}

static float volume_gain(int volume)
{
	if (volume <= 0) return 0.0f;
	if (volume >= 100) return 1.0f;
	return (float)volume / 100.0f;
}

float sdl3_audio_category_gain(const struct sdl3_audio_settings *settings,
		enum sdl3_audio_category category, bool in_store)
{
	float gain;

	if (!sdl3_audio_category_is_audible(settings, category)) return 0.0f;
	gain = volume_gain(settings->master_volume) *
		volume_gain(sdl3_audio_category_volume(settings, category));
	if (in_store && category == SDL3_AUDIO_AMBIENT) {
		gain *= volume_gain(settings->shop_ambient_percent);
	}
	return gain;
}

unsigned int sdl3_audio_rate_limit_ms(const char *path)
{
	if (path_has_category(path, "ambient") ||
			path_has_category(path, "music")) return 15000;
	if (path_has_category(path, "movement")) return 70;
	if (path_has_category(path, "status")) return 200;
	if (path_has_category(path, "interaction") ||
			path_has_category(path, "spelunk")) return 100;
	if (path_has_category(path, "fishing")) return 70;
	if (path_has_category(path, "creatures")) return 80;
	if (path_has_category(path, "interface") ||
			path_has_category(path, "ui")) {
		return 40;
	}
	return 35;
}

/**
 * Derive a restrained sine-wave cue from the stable sound-map path.  This is
 * deliberately deterministic: an asset-free build should keep a recognizable
 * audio vocabulary without storing or decoding any sample files.
 */
bool sdl3_audio_make_synth_profile(const char *path,
		struct sdl3_audio_synth_profile *profile)
{
	uint32_t hash = UINT32_C(2166136261);
	enum sdl3_audio_category category;
	const unsigned char *cursor;
	int frequency_base;
	int frequency_span;
	int duration_base;
	int duration_span;
	int amplitude_base;
	int amplitude_span;

	if (!path || !path[0] || !profile) return false;
	/* Movement uses a deliberately restrained C: one very quiet consonant
	 * pitch for steps and climbing, with the blocked cue displaced to the
	 * tritone so a collision reads instantly without becoming loud. */
	if (strstr(path, "movement/player_step") ||
			strstr(path, "movement/step_")) {
		profile->frequency_hz = 262;
		profile->end_frequency_hz = 262;
		profile->duration_ms = 24;
		profile->amplitude_percent = 2;
		return true;
	}
	if (strstr(path, "movement/climb")) {
		profile->frequency_hz = 262;
		profile->end_frequency_hz = 262;
		profile->duration_ms = 38;
		profile->amplitude_percent = 3;
		return true;
	}
	if (strstr(path, "movement/player_blocked") ||
			strstr(path, "movement/blocked_")) {
		profile->frequency_hz = 370;
		profile->end_frequency_hz = 349;
		profile->duration_ms = 72;
		profile->amplitude_percent = 5;
		return true;
	}
	if (strstr(path, "interaction/piton_hammer") ||
			strstr(path, "spelunk/piton_hammer")) {
		profile->frequency_hz = 1050;
		profile->end_frequency_hz = 720;
		profile->duration_ms = 58;
		profile->amplitude_percent = 10;
		return true;
	}
	if (strstr(path, "interaction/rope_drop") ||
			strstr(path, "spelunk/rope_deploy")) {
		profile->frequency_hz = 520;
		profile->end_frequency_hz = 105;
		profile->duration_ms = 320;
		profile->amplitude_percent = 7;
		return true;
	}
	if (strstr(path, "fishing/line_move")) {
		profile->frequency_hz = 330;
		profile->end_frequency_hz = 250;
		profile->duration_ms = 70;
		profile->amplitude_percent = 4;
		return true;
	}
	if (strstr(path, "fishing/reel")) {
		profile->frequency_hz = 220;
		profile->end_frequency_hz = 330;
		profile->duration_ms = 55;
		profile->amplitude_percent = 5;
		return true;
	}
	for (cursor = (const unsigned char *)path; *cursor; cursor++) {
		hash ^= *cursor;
		hash *= UINT32_C(16777619);
	}
	category = sdl3_audio_category_for_path(path);
	switch (category) {
	case SDL3_AUDIO_MUSIC:
		/* A missing long-form composition has no honest synthesized substitute.
		 * Keep asset-free packages quiet for this event. */
		frequency_base = 262;
		frequency_span = 1;
		duration_base = 10;
		duration_span = 1;
		amplitude_base = 0;
		amplitude_span = 1;
		break;
	case SDL3_AUDIO_INTERFACE:
		frequency_base = 620;
		frequency_span = 500;
		duration_base = 34;
		duration_span = 42;
		amplitude_base = 9;
		amplitude_span = 6;
		break;
	case SDL3_AUDIO_CREATURE:
		frequency_base = 105;
		frequency_span = 220;
		duration_base = 90;
		duration_span = 120;
		amplitude_base = 13;
		amplitude_span = 8;
		break;
	case SDL3_AUDIO_AMBIENT:
		frequency_base = 115;
		frequency_span = 150;
		duration_base = 220;
		duration_span = 220;
		amplitude_base = 6;
		amplitude_span = 6;
		break;
	case SDL3_AUDIO_GAMEPLAY:
	default:
		frequency_base = 250;
		frequency_span = 390;
		duration_base = 55;
		duration_span = 90;
		amplitude_base = 11;
		amplitude_span = 8;
		break;
	}
	profile->frequency_hz = frequency_base +
		(int)(hash % (uint32_t)frequency_span);
	profile->end_frequency_hz = profile->frequency_hz;
	profile->duration_ms = duration_base +
		(int)((hash >> 9) % (uint32_t)duration_span);
	profile->amplitude_percent = amplitude_base +
		(int)((hash >> 18) % (uint32_t)amplitude_span);
	return true;
}
