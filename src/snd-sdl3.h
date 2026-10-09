/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file snd-sdl3.h
 * \brief Native SDL3 sound support for the ASCII SDL3 frontend.
 *
 */

#ifndef INCLUDED_SND_SDL3_H
#define INCLUDED_SND_SDL3_H

#include <stdbool.h>

#include "sdl3/audio.h"

struct sound_hooks;

errr init_sound_sdl3(struct sound_hooks *hooks, int argc, char **argv);
void sdl3_audio_set_settings(const struct sdl3_audio_settings *settings);
void sdl3_audio_set_active(bool active);
void sdl3_audio_set_in_store(bool in_store);
void sdl3_audio_stop_background(void);

#endif /* INCLUDED_SND_SDL3_H */
