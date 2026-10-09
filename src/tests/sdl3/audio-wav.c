/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/audio-wav.c */
/* Exercise the strict authored PCM WAV decoder without proprietary fixtures. */

#include "unit-test.h"

#include "sdl3/audio-wav.h"

#include <SDL3/SDL_iostream.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MONO_TEST_PATH "test-audio-wav-mono.wav"
#define STEREO_TEST_PATH "test-audio-wav-stereo.wav"
#define WRONG_RATE_TEST_PATH "test-audio-wav-wrong-rate.wav"
#define MISSING_TEST_PATH "test-audio-wav-missing.wav"

static void put_u16le(unsigned char *destination, uint16_t value)
{
	destination[0] = (unsigned char)(value & 0xff);
	destination[1] = (unsigned char)(value >> 8);
}

static void put_u32le(unsigned char *destination, uint32_t value)
{
	destination[0] = (unsigned char)(value & 0xff);
	destination[1] = (unsigned char)((value >> 8) & 0xff);
	destination[2] = (unsigned char)((value >> 16) & 0xff);
	destination[3] = (unsigned char)(value >> 24);
}

static bool write_test_wav(const char *path, uint16_t channels,
		uint32_t frequency, const int16_t *samples, size_t sample_count)
{
	unsigned char header[44] = { 0 };
	SDL_IOStream *file;
	size_t i;
	uint32_t data_size;

	if (sample_count > (UINT32_MAX - 36) / 2) return false;
	data_size = (uint32_t)(sample_count * 2);
	memcpy(header, "RIFF", 4);
	put_u32le(header + 4, 36 + data_size);
	memcpy(header + 8, "WAVEfmt ", 8);
	put_u32le(header + 16, 16);
	put_u16le(header + 20, 1);
	put_u16le(header + 22, channels);
	put_u32le(header + 24, frequency);
	put_u32le(header + 28, frequency * channels * 2);
	put_u16le(header + 32, channels * 2);
	put_u16le(header + 34, 16);
	memcpy(header + 36, "data", 4);
	put_u32le(header + 40, data_size);

	file = SDL_IOFromFile(path, "wb");
	if (!file) return false;
	if (SDL_WriteIO(file, header, sizeof(header)) != sizeof(header)) {
		SDL_CloseIO(file);
		return false;
	}
	for (i = 0; i < sample_count; i++) {
		unsigned char encoded[2];

		put_u16le(encoded, (uint16_t)samples[i]);
		if (SDL_WriteIO(file, encoded, sizeof(encoded)) != sizeof(encoded)) {
			SDL_CloseIO(file);
			return false;
		}
	}
	return SDL_CloseIO(file);
}

int setup_tests(void **data)
{
	const int16_t mono[] = { 0, 32767, -32768, 0 };
	const int16_t stereo[] = { 32767, -32768, 1000, -1000, 0, 0 };

	(void)data;
	remove(MONO_TEST_PATH);
	remove(STEREO_TEST_PATH);
	remove(WRONG_RATE_TEST_PATH);
	remove(MISSING_TEST_PATH);
	if (!write_test_wav(MONO_TEST_PATH, 1, 48000, mono,
			sizeof(mono) / sizeof(mono[0]))) return 1;
	if (!write_test_wav(STEREO_TEST_PATH, 2, 48000, stereo,
			sizeof(stereo) / sizeof(stereo[0]))) return 1;
	if (!write_test_wav(WRONG_RATE_TEST_PATH, 1, 44100, mono,
			sizeof(mono) / sizeof(mono[0]))) return 1;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	remove(MONO_TEST_PATH);
	remove(STEREO_TEST_PATH);
	remove(WRONG_RATE_TEST_PATH);
	remove(MISSING_TEST_PATH);
	return 0;
}

static int test_decode_mono_runtime_format(void *state)
{
	float *pcm = NULL;
	size_t frames = 0;
	size_t i;
	bool has_signal = false;
	(void)state;

	require(sdl3_audio_wav_decode(MONO_TEST_PATH, false, &pcm, &frames));
	require(pcm != NULL);
	require(frames == 4);
	for (i = 0; i < frames; i++) {
		require(pcm[i * 2] == pcm[i * 2 + 1]);
		if (pcm[i * 2] != 0.0f) has_signal = true;
	}
	require(has_signal);
	sdl3_audio_wav_free(pcm);
	ok;
}

static int test_decode_stereo_runtime_format(void *state)
{
	float *pcm = NULL;
	size_t frames = 0;
	(void)state;

	require(sdl3_audio_wav_decode(STEREO_TEST_PATH, false, &pcm, &frames));
	require(pcm != NULL);
	require(frames == 3);
	require(pcm[0] != pcm[1]);
	sdl3_audio_wav_free(pcm);
	ok;
}

static int test_wrong_rate_fails_contract(void *state)
{
	float *pcm = (float *)1;
	size_t frames = 1;
	(void)state;

	require(!sdl3_audio_wav_decode(WRONG_RATE_TEST_PATH, false, &pcm, &frames));
	require(pcm == NULL);
	require(frames == 0);
	ok;
}

static int test_music_rate_converts_to_runtime_format(void *state)
{
	float *pcm = NULL;
	size_t frames = 0;
	(void)state;

	require(sdl3_audio_wav_decode(WRONG_RATE_TEST_PATH, true, &pcm, &frames));
	require(pcm != NULL);
	require(frames > 0);
	sdl3_audio_wav_free(pcm);
	ok;
}

static int test_missing_asset_fails_cleanly(void *state)
{
	float *pcm = (float *)1;
	size_t frames = 1;
	(void)state;

	require(!sdl3_audio_wav_decode(MISSING_TEST_PATH, false, &pcm, &frames));
	require(pcm == NULL);
	require(frames == 0);
	ok;
}

const char *suite_name = "sdl3/audio-wav";
struct test tests[] = {
	{ "decode mono runtime format", test_decode_mono_runtime_format },
	{ "decode stereo runtime format", test_decode_stereo_runtime_format },
	{ "wrong rate fails contract", test_wrong_rate_fails_contract },
	{ "music rate converts to runtime format",
		test_music_rate_converts_to_runtime_format },
	{ "missing asset fails cleanly", test_missing_asset_fails_cleanly },
	{ NULL, NULL },
};
