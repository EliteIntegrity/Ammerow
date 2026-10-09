/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio-wav.c
 * \brief Strict PCM WAV decoding for SDL3 presentation audio.
 *
 *
 */

#include "sdl3/audio-wav.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

#include <limits.h>

#define SDL3_AUDIO_WAV_FREQUENCY 48000
#define SDL3_AUDIO_WAV_CHANNELS 2

bool sdl3_audio_wav_decode(const char *path, bool allow_44100, float **pcm,
		size_t *frame_count)
{
	SDL_AudioSpec source = { 0 };
	SDL_AudioSpec target = { 0 };
	Uint8 *source_data = NULL;
	Uint8 *target_data = NULL;
	Uint32 source_length = 0;
	int target_length = 0;
	const int frame_bytes = sizeof(float) * SDL3_AUDIO_WAV_CHANNELS;

	if (!path || !path[0]) return SDL_InvalidParamError("path");
	if (!pcm) return SDL_InvalidParamError("pcm");
	if (!frame_count) return SDL_InvalidParamError("frame_count");
	*pcm = NULL;
	*frame_count = 0;

	if (!SDL_LoadWAV(path, &source, &source_data, &source_length)) {
		return false;
	}
	if ((source.freq != SDL3_AUDIO_WAV_FREQUENCY &&
			(!allow_44100 || source.freq != 44100)) ||
			source.format != SDL_AUDIO_S16LE ||
			(source.channels != 1 && source.channels != 2)) {
		SDL_free(source_data);
		return SDL_SetError("Authored WAV has an unsupported PCM format");
	}
	if (source_length > INT_MAX) {
		SDL_free(source_data);
		return SDL_SetError("Authored WAV is too large to decode");
	}

	target.format = SDL_AUDIO_F32;
	target.channels = SDL3_AUDIO_WAV_CHANNELS;
	target.freq = SDL3_AUDIO_WAV_FREQUENCY;
	if (!SDL_ConvertAudioSamples(&source, source_data, (int)source_length,
			&target, &target_data, &target_length)) {
		SDL_free(source_data);
		return false;
	}
	SDL_free(source_data);
	if (target_length <= 0 || target_length % frame_bytes != 0) {
		SDL_free(target_data);
		return SDL_SetError("Authored WAV decoded to an invalid frame count");
	}

	*pcm = (float *)target_data;
	*frame_count = (size_t)(target_length / frame_bytes);
	return true;
}

void sdl3_audio_wav_free(float *pcm)
{
	SDL_free(pcm);
}
