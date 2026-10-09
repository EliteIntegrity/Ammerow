/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* White-box mixer regression: compile the production callback into this test
 * so its reserved voice and PCM output can be checked without a speaker,
 * private WAV fixtures, sleeps or a production-only diagnostic API. Rename
 * the two symbols shared with the generic unit-test runner's sound stub. */
#ifndef SOUND_SDL3
#define SOUND_SDL3
#endif
#define verbose mixer_verbose
#define init_sound_sdl3 init_test_mixer
#include "../../snd-sdl3.c"
#undef init_sound_sdl3
#undef verbose

#include "unit-test.h"

static struct sound_data test_sounds[2];

static void reset_mixer(void)
{
	int i;
	close_audio_sdl3();
	for (i = 0; i < 2; i++) unload_sound_sdl3(&test_sounds[i]);
	sdl3_audio_settings_defaults(&audio_settings);
	application_active = true;
	inside_store = false;
}

static bool prepare_mixer(void)
{
	SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 48000 };
	const char *names[] = {
		"synth:retro/ambient/dungeon", "synth:retro/ambient/surface"
	};
	int i, frame;
	reset_mixer();
	open_audio_sdl3();
	/* An unbound stream uses SDL's real queue/conversion, but no device can
	 * run the callback asynchronously or make this test audible. */
	audio_stream = SDL_CreateAudioStream(&spec, &spec);
	if (!audio_stream) return false;
	for (i = 0; i < 2; i++) {
		struct sdl3_sound_sample *sample;
		if (!load_sound_sdl3(names[i], SDL3_AUDIO_FILE_SYNTH,
				&test_sounds[i])) return false;
		sample = test_sounds[i].plat_data;
		sample->pcm = SDL_malloc(16 * sizeof(float));
		if (!sample->pcm) return false;
		sample->frame_count = 8;
		sample->loop = true;
		for (frame = 0; frame < 16; frame++) sample->pcm[frame] = 0.5f;
		loaded_sample_count++;
	}
	return true;
}

static float mix_one_frame(void)
{
	float result[2];
	fill_audio_stream(NULL, audio_stream, sizeof(result), 0);
	if (SDL_GetAudioStreamData(audio_stream, result, sizeof(result)) !=
			sizeof(result)) return -1.0f;
	return result[0];
}

int setup_tests(void **state)
{
	(void)state;
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	reset_mixer();
	return 0;
}

static int test_shop_gain_and_cursor(void *state)
{
	float outside, inside;
	size_t cursor;
	(void)state;
	require(prepare_mixer());
	require(play_sound_sdl3(&test_sounds[0]));
	outside = mix_one_frame();
	require(outside > 0.139f && outside < 0.141f);
	cursor = voices[0].frame_cursor;
	sdl3_audio_set_in_store(true);
	eq(voices[0].frame_cursor, cursor);
	inside = mix_one_frame();
	require(inside > 0.034f && inside < 0.036f);
	sdl3_audio_set_in_store(false);
	eq(mix_one_frame(), outside);
	eq(audio_settings.ambient_volume, 40);
	ok;
}

static int test_rapid_background_replacement(void *state)
{
	size_t cursor;
	(void)state;
	require(prepare_mixer());
	require(play_sound_sdl3(&test_sounds[0]));
	require(play_sound_sdl3(&test_sounds[1]));
	require(voices[0].sample == test_sounds[1].plat_data);
	require(mix_one_frame() > 0.0f);
	cursor = voices[0].frame_cursor;
	require(play_sound_sdl3(&test_sounds[1]));
	eq(voices[0].frame_cursor, cursor);
	require(play_sound_sdl3(&test_sounds[0]));
	require(voices[0].sample == test_sounds[0].plat_data);
	ok;
}

static int test_failed_replacement_stops_old_loop(void *state)
{
	struct sdl3_sound_sample *surface;
	(void)state;
	require(prepare_mixer());
	require(play_sound_sdl3(&test_sounds[0]));
	surface = test_sounds[1].plat_data;
	surface->runtime_load_failed = true;
	require(!play_sound_sdl3(&test_sounds[1]));
	require(!voices[0].active);
	eq(mix_one_frame(), 0.0f);
	require(current_background == surface);
	surface->runtime_load_failed = false;
	require(play_sound_sdl3(&test_sounds[1]));
	require(voices[0].sample == surface);
	ok;
}

static int test_background_unmute_ignores_cooldown(void *state)
{
	struct sdl3_audio_settings settings;
	struct sdl3_sound_sample *sample;
	(void)state;
	require(prepare_mixer());
	require(play_sound_sdl3(&test_sounds[0]));
	sample = test_sounds[0].plat_data;
	settings = audio_settings;
	settings.ambient_volume = 0;
	sdl3_audio_set_settings(&settings);
	require(!voices[0].active);
	/* Force the old cooldown branch regardless of timer initialization. */
	sample->last_play_ms = SDL_GetTicks();
	if (!sample->last_play_ms) sample->last_play_ms = UINT64_MAX;
	settings.ambient_volume = 40;
	sdl3_audio_set_settings(&settings);
	require(voices[0].active);
	require(mix_one_frame() > 0.0f);
	ok;
}

const char *suite_name = "sdl3/audio-mixer";
struct test tests[] = {
	{ "shop gain and uninterrupted cursor", test_shop_gain_and_cursor },
	{ "rapid background replacement", test_rapid_background_replacement },
	{ "failed replacement stops old loop", test_failed_replacement_stops_old_loop },
	{ "background unmute ignores cooldown", test_background_unmute_ignores_cooldown },
	{ NULL, NULL }
};
