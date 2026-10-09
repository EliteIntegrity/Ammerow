/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/audio-wav.h
 * \brief Strict PCM WAV decoding for SDL3 presentation audio.
 *
 *
 */

#ifndef INCLUDED_SDL3_AUDIO_WAV_H
#define INCLUDED_SDL3_AUDIO_WAV_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Decode an Ammerow runtime WAV to interleaved 48 kHz float stereo.
 *
 * Effect and ambience assets use the 48 kHz delivery contract.  A caller may
 * explicitly admit a standard 44.1 kHz music master; SDL then resamples it
 * once into the same 48 kHz mixer format.  Only signed 16-bit little-endian
 * PCM with one or two channels is accepted.  The caller owns the returned
 * buffer and must release it with sdl3_audio_wav_free().
 */
bool sdl3_audio_wav_decode(const char *path, bool allow_44100, float **pcm,
		size_t *frame_count);
void sdl3_audio_wav_free(float *pcm);

#endif /* INCLUDED_SDL3_AUDIO_WAV_H */
