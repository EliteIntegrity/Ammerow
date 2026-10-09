/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file snd-sdl3.c
 * \brief Bounded native SDL3 sound support for the ASCII SDL3 frontend.
 *
 *
 */

#include "angband.h"

#ifdef SOUND_SDL3

#include <SDL3/SDL.h>

#include <math.h>

#include "init.h"
#include "snd-sdl3.h"
#include "sdl3/audio-wav.h"
#include "sound.h"

#define SDL3_AUDIO_CACHE_CAPACITY 32
#define SDL3_AUDIO_VOICE_CAPACITY 12
#define SDL3_AUDIO_MIX_FRAMES 1024
#define SDL3_AUDIO_FREQUENCY 48000
#define SDL3_AUDIO_CHANNELS 2

enum {
	SDL3_AUDIO_FILE_NONE = 0,
	SDL3_AUDIO_FILE_WAV,
	SDL3_AUDIO_FILE_SYNTH
};

struct sdl3_sound_sample {
	char *filename;
	char *cue_id;
	float *pcm;
	size_t frame_count;
	enum sdl3_audio_category category;
	int file_type;
	Uint64 use_serial;
	Uint64 last_play_ms;
	bool loop;
	bool runtime_load_failed;
	struct sdl3_sound_sample *next;
};

struct sdl3_sound_voice {
	struct sdl3_sound_sample *sample;
	size_t frame_cursor;
	size_t delay_frames;
	enum sdl3_audio_category category;
	Uint64 use_serial;
	bool active;
};

static const struct sound_file_type supported_sound_files[] = {
	{ ".wav", SDL3_AUDIO_FILE_WAV },
	{ ".synth", SDL3_AUDIO_FILE_SYNTH },
	{ "", SDL3_AUDIO_FILE_NONE }
};

static struct sdl3_audio_settings audio_settings = {
	.enabled = true,
	.movement_enabled = true,
	.music_enabled = true,
	.master_volume = 70,
	.interface_volume = 60,
	.gameplay_volume = 70,
	.creature_volume = 65,
	.ambient_volume = 40,
	.shop_ambient_percent = 25,
	.music_volume = 50
};
static struct sdl3_sound_sample *sample_list;
static struct sdl3_sound_voice voices[SDL3_AUDIO_VOICE_CAPACITY];
static SDL_AudioStream *audio_stream;
static float mix_buffer[SDL3_AUDIO_MIX_FRAMES * SDL3_AUDIO_CHANNELS];
static Uint64 use_serial;
static int loaded_sample_count;
static bool module_open;
static bool application_active = true;
static bool inside_store;
static bool audio_subsystem_owned;
static bool verbose;
static struct sdl3_sound_sample *current_background;

static bool play_sample_sdl3(struct sdl3_sound_sample *sample);
static void stop_sample_voices(struct sdl3_sound_sample *sample);

static bool audio_should_run(void)
{
	int category;

	if (!module_open || !application_active) return false;
	for (category = 0; category < SDL3_AUDIO_CATEGORY_COUNT; category++) {
		if (sdl3_audio_category_is_audible(&audio_settings,
				(enum sdl3_audio_category)category)) return true;
	}
	return false;
}

static void clear_voices(void)
{
	int i;

	for (i = 0; i < SDL3_AUDIO_VOICE_CAPACITY; i++) {
		voices[i].sample = NULL;
		voices[i].frame_cursor = 0;
		voices[i].delay_frames = 0;
		voices[i].category = SDL3_AUDIO_GAMEPLAY;
		voices[i].use_serial = 0;
		voices[i].active = false;
	}
}

static void release_decoded_samples(void)
{
	struct sdl3_sound_sample *sample;

	for (sample = sample_list; sample; sample = sample->next) {
		sdl3_audio_wav_free(sample->pcm);
		sample->pcm = NULL;
		sample->frame_count = 0;
		sample->last_play_ms = 0;
		sample->runtime_load_failed = false;
	}
	loaded_sample_count = 0;
}

static void close_audio_runtime(void)
{
	if (audio_stream) {
		/* Destroying a device stream stops its callback before returning, so
		 * cached PCM can be released immediately afterwards. */
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
	}
	clear_voices();
	release_decoded_samples();
	if (audio_subsystem_owned) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		audio_subsystem_owned = false;
	}
}

static void SDLCALL fill_audio_stream(void *userdata, SDL_AudioStream *stream,
		int additional_amount, int total_amount)
{
	const int frame_bytes = (int)(sizeof(float) * SDL3_AUDIO_CHANNELS);
	int remaining = additional_amount;
	int i;

	(void)userdata;
	(void)total_amount;
	while (remaining > 0) {
		int frame_count = (remaining + frame_bytes - 1) / frame_bytes;
		int byte_count;
		int frame;

		if (frame_count > SDL3_AUDIO_MIX_FRAMES) {
			frame_count = SDL3_AUDIO_MIX_FRAMES;
		}
		byte_count = frame_count * frame_bytes;
		SDL_memset(mix_buffer, 0, (size_t)byte_count);
		for (i = 0; i < SDL3_AUDIO_VOICE_CAPACITY; i++) {
			struct sdl3_sound_voice *voice = &voices[i];
			float gain;
			int start_frame = 0;

			if (!voice->active || !voice->sample ||
					!voice->sample->pcm) {
				continue;
			}
			if (voice->delay_frames >= (size_t)frame_count) {
				voice->delay_frames -= (size_t)frame_count;
				continue;
			}
			if (voice->delay_frames) {
				start_frame = (int)voice->delay_frames;
				voice->delay_frames = 0;
			}
			gain = sdl3_audio_category_gain(&audio_settings,
				voice->category, inside_store);
			for (frame = start_frame; frame < frame_count; frame++) {
				size_t source;
				int channel;

				if (voice->frame_cursor >= voice->sample->frame_count) {
					if (voice->sample->loop) {
						voice->frame_cursor = 0;
					} else {
						voice->active = false;
						voice->sample = NULL;
						break;
					}
				}
				source = voice->frame_cursor * SDL3_AUDIO_CHANNELS;
				for (channel = 0; channel < SDL3_AUDIO_CHANNELS; channel++) {
					mix_buffer[frame * SDL3_AUDIO_CHANNELS + channel] +=
						voice->sample->pcm[source + channel] * gain;
				}
				voice->frame_cursor++;
			}
		}
		for (i = 0; i < frame_count * SDL3_AUDIO_CHANNELS; i++) {
			if (mix_buffer[i] > 1.0f) mix_buffer[i] = 1.0f;
			else if (mix_buffer[i] < -1.0f) mix_buffer[i] = -1.0f;
		}
		if (!SDL_PutAudioStreamData(stream, mix_buffer, byte_count)) return;
		remaining -= byte_count;
	}
}

static bool ensure_audio_runtime(void)
{
	SDL_AudioSpec specification;

	if (!audio_should_run()) return false;
	if (audio_stream) return true;
	if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
				"Could not initialize SDL3 audio: %s", SDL_GetError());
			return false;
		}
		audio_subsystem_owned = true;
	}
	specification.format = SDL_AUDIO_F32;
	specification.channels = SDL3_AUDIO_CHANNELS;
	specification.freq = SDL3_AUDIO_FREQUENCY;
	audio_stream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specification,
		fill_audio_stream, NULL);
	if (!audio_stream) {
		SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
			"Could not open the default SDL3 audio device: %s",
			SDL_GetError());
		close_audio_runtime();
		return false;
	}
	clear_voices();
	if (!SDL_ResumeAudioStreamDevice(audio_stream)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
			"Could not start the default SDL3 audio device: %s",
			SDL_GetError());
		close_audio_runtime();
		return false;
	}
	if (verbose) {
		SDL_Log("SDL3 native audio: %d Hz float stereo; %d voices; "
			"%d-cue decoded cache", SDL3_AUDIO_FREQUENCY,
			SDL3_AUDIO_VOICE_CAPACITY, SDL3_AUDIO_CACHE_CAPACITY);
	}
	return true;
}

void sdl3_audio_set_settings(const struct sdl3_audio_settings *settings)
{
	if (!settings) return;
	if (audio_stream && SDL_LockAudioStream(audio_stream)) {
		audio_settings = *settings;
		SDL_UnlockAudioStream(audio_stream);
	} else if (audio_stream) {
		/* A failed lock is exceptional.  Closing the stream establishes a safe
		 * boundary and it can be lazily reopened for the next sound. */
		close_audio_runtime();
		audio_settings = *settings;
	} else {
		audio_settings = *settings;
	}
	if (!audio_should_run()) close_audio_runtime();
	else if (current_background && sdl3_audio_category_is_audible(
			&audio_settings, current_background->category)) {
		(void)play_sample_sdl3(current_background);
	} else if (current_background && audio_stream &&
			SDL_LockAudioStream(audio_stream)) {
		stop_sample_voices(current_background);
		SDL_UnlockAudioStream(audio_stream);
	}
}

void sdl3_audio_set_active(bool active)
{
	application_active = active;
	if (!active) close_audio_runtime();
	else if (current_background && sdl3_audio_category_is_audible(
			&audio_settings, current_background->category)) {
		(void)play_sample_sdl3(current_background);
	}
}

void sdl3_audio_set_in_store(bool in_store)
{
	/* This is transient presentation state, not a saved volume preference.
	 * Keep the current loop/cursor alive so crossing the door is seamless. */
	if (audio_stream && SDL_LockAudioStream(audio_stream)) {
		inside_store = in_store;
		SDL_UnlockAudioStream(audio_stream);
	} else {
		if (audio_stream) close_audio_runtime();
		inside_store = in_store;
	}
}

void sdl3_audio_stop_background(void)
{
	struct sdl3_sound_sample *sample = current_background;
	float *pcm = NULL;

	current_background = NULL;
	if (!audio_stream) return;
	if (SDL_LockAudioStream(audio_stream)) {
		/* Voice zero belongs exclusively to backgrounds. Clear even a stale
		 * voice left by an earlier failed replacement, not just the pointer
		 * naming the most recently requested background. */
		SDL_zero(voices[0]);
		/* Long-form music is much larger than an effect.  Do not retain its
		 * decoded float buffer throughout gameplay merely to satisfy the
		 * count-bounded one-shot cache. */
		if (sample && sample->category == SDL3_AUDIO_MUSIC && sample->pcm) {
			pcm = sample->pcm;
			sample->pcm = NULL;
			sample->frame_count = 0;
			sample->last_play_ms = 0;
			sample->runtime_load_failed = false;
			loaded_sample_count--;
		}
		SDL_UnlockAudioStream(audio_stream);
		sdl3_audio_wav_free(pcm);
	} else {
		/* A failed lock leaves ownership uncertain; close the device so its
		 * callback cannot retain a background voice. */
		close_audio_runtime();
	}
}

static bool resolve_sound_sdl3(const char *name, const char *extension,
		char *path, size_t path_capacity)
{
	if (!name || !extension || !path || !path_capacity) return false;
	if (!sdl3_audio_cue_id_is_valid(name)) {
		path[0] = '\0';
		return false;
	}
	if (streq(extension, ".wav") &&
			sdl3_audio_cue_id_is_authored(name)) {
		strnfmt(path, path_capacity, "wav:%s", name);
		return true;
	}
	if (streq(extension, ".synth")) {
		strnfmt(path, path_capacity, "synth:%s", name);
		return true;
	}
	path[0] = '\0';
	return false;
}

static bool open_audio_sdl3(void)
{
	/* Opening is deliberately lazy.  A muted game never starts an audio
	 * device or background mixing thread. */
	module_open = true;
	return true;
}

static bool load_sound_sdl3(const char *filename, int file_type,
		struct sound_data *data)
{
	char resolved_path[2048];
	const char *cue_id;
	struct sdl3_sound_sample *sample;

	if (!filename || !data) return false;
	if (file_type == SDL3_AUDIO_FILE_WAV &&
			strncmp(filename, "wav:", 4) == 0) {
		cue_id = filename + 4;
		if (!sdl3_audio_cue_id_is_authored(cue_id)) return false;
		path_build(resolved_path, sizeof(resolved_path), ANGBAND_DIR_SOUNDS,
			cue_id);
		my_strcat(resolved_path, ".wav", sizeof(resolved_path));
		if (!file_exists(resolved_path)) return false;
	} else if (file_type == SDL3_AUDIO_FILE_SYNTH &&
			strncmp(filename, "synth:", 6) == 0) {
		cue_id = filename + 6;
		if (!sdl3_audio_cue_id_is_valid(cue_id)) return false;
		my_strcpy(resolved_path, filename, sizeof(resolved_path));
	} else {
		return false;
	}
	sample = mem_zalloc(sizeof(*sample));
	sample->filename = string_make(resolved_path);
	sample->cue_id = string_make(cue_id);
	if (!sample->filename || !sample->cue_id) {
		string_free(sample->filename);
		string_free(sample->cue_id);
		mem_free(sample);
		return false;
	}
	sample->category = sdl3_audio_category_for_path(cue_id);
	sample->file_type = file_type;
	sample->loop = file_type == SDL3_AUDIO_FILE_WAV &&
		sdl3_audio_category_is_background(sample->category);
	sample->next = sample_list;
	sample_list = sample;
	data->plat_data = sample;
	data->status = SOUND_ST_LOADED;
	return true;
}

/** Stop voices that refer to sample.  The stream must already be locked. */
static void stop_sample_voices(struct sdl3_sound_sample *sample)
{
	int i;

	for (i = 0; i < SDL3_AUDIO_VOICE_CAPACITY; i++) {
		if (voices[i].sample == sample) {
			voices[i].active = false;
			voices[i].sample = NULL;
			voices[i].frame_cursor = 0;
		}
	}
}

/** Return whether sample is assigned to a voice.  The stream must be locked. */
static bool sample_has_active_voice(const struct sdl3_sound_sample *sample)
{
	int i;

	for (i = 0; i < SDL3_AUDIO_VOICE_CAPACITY; i++) {
		if (voices[i].active && voices[i].sample == sample) return true;
	}
	return false;
}

static bool evict_one_sample(void)
{
	struct sdl3_sound_sample *sample;
	struct sdl3_sound_sample *oldest = NULL;
	float *pcm;
	bool stream_locked = false;

	if (audio_stream) {
		if (!SDL_LockAudioStream(audio_stream)) return false;
		stream_locked = true;
	}

	for (sample = sample_list; sample; sample = sample->next) {
		if (sample->pcm && !sample_has_active_voice(sample) && (!oldest ||
				sample->use_serial < oldest->use_serial)) {
			oldest = sample;
		}
	}
	if (!oldest) {
		if (stream_locked) SDL_UnlockAudioStream(audio_stream);
		return false;
	}
	pcm = oldest->pcm;
	oldest->pcm = NULL;
	oldest->frame_count = 0;
	oldest->runtime_load_failed = false;
	loaded_sample_count--;
	if (stream_locked) SDL_UnlockAudioStream(audio_stream);
	sdl3_audio_wav_free(pcm);
	return true;
}

static bool generate_synth_sample(struct sdl3_sound_sample *sample)
{
	const float tau = 6.2831853071795864769f;
	struct sdl3_audio_synth_profile profile;
	size_t frames;
	size_t fade_frames;
	size_t frame;
	float amplitude;
	float phase = 0.0f;

	if (!sdl3_audio_make_synth_profile(sample->cue_id, &profile)) {
		return false;
	}
	frames = (size_t)SDL3_AUDIO_FREQUENCY * (size_t)profile.duration_ms /
		1000;
	if (!frames || frames > SIZE_MAX /
			(sizeof(float) * SDL3_AUDIO_CHANNELS)) {
		return false;
	}
	sample->pcm = SDL_malloc(frames * sizeof(float) * SDL3_AUDIO_CHANNELS);
	if (!sample->pcm) return false;
	sample->frame_count = frames;
	amplitude = (float)profile.amplitude_percent / 100.0f;
	fade_frames = SDL3_AUDIO_FREQUENCY / 200; /* Five milliseconds. */
	if (fade_frames * 2 > frames) fade_frames = frames / 2;
	for (frame = 0; frame < frames; frame++) {
		float envelope = 1.0f;
		float value;

		if (fade_frames && frame < fade_frames) {
			envelope = (float)frame / (float)fade_frames;
		} else if (fade_frames && frame + fade_frames >= frames) {
			envelope = (float)(frames - frame - 1) /
				(float)fade_frames;
		}
		float progress = frames > 1 ? (float)frame / (float)(frames - 1) :
			0.0f;
		float frequency = (float)profile.frequency_hz +
			((float)profile.end_frequency_hz -
			(float)profile.frequency_hz) * progress;

		phase += tau * frequency / (float)SDL3_AUDIO_FREQUENCY;
		value = sinf(phase) * amplitude * envelope;
		sample->pcm[frame * SDL3_AUDIO_CHANNELS] = value;
		sample->pcm[frame * SDL3_AUDIO_CHANNELS + 1] = value;
	}
	return true;
}

static bool prepare_sample(struct sdl3_sound_sample *sample)
{
	bool prepared = false;

	if (!sample || sample->runtime_load_failed) return false;
	if (sample->pcm) {
		sample->use_serial = ++use_serial;
		return true;
	}
	while (loaded_sample_count >= SDL3_AUDIO_CACHE_CAPACITY) {
		if (!evict_one_sample()) return false;
	}
	if (sample->file_type == SDL3_AUDIO_FILE_WAV) {
		prepared = sdl3_audio_wav_decode(sample->filename,
			sample->category == SDL3_AUDIO_MUSIC, &sample->pcm,
			&sample->frame_count);
		if (!prepared) {
			SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO,
				"Could not decode authored cue '%s'; using synth fallback: %s",
				sample->cue_id, SDL_GetError());
			sample->loop = false;
			prepared = generate_synth_sample(sample);
		}
	} else {
		prepared = generate_synth_sample(sample);
	}
	if (!prepared) {
		SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Could not prepare '%s': %s",
			sample->cue_id, SDL_GetError());
		sample->runtime_load_failed = true;
		return false;
	}
	loaded_sample_count++;
	sample->use_serial = ++use_serial;
	return true;
}

/** Select a voice.  The stream must already be locked. */
static struct sdl3_sound_voice *select_voice(
		enum sdl3_audio_category category)
{
	struct sdl3_sound_voice *oldest = NULL;
	bool background = sdl3_audio_category_is_background(category);
	int first = background ? 0 : 1;
	int limit = background ? 1 :
		SDL3_AUDIO_VOICE_CAPACITY;
	int i;

	for (i = first; i < limit; i++) {
		if (!voices[i].active) return &voices[i];
		if (!oldest || voices[i].use_serial < oldest->use_serial) {
			oldest = &voices[i];
		}
	}
	return oldest;
}

static bool queue_sample_sdl3(struct sdl3_sound_sample *sample,
		unsigned int delay_ms, bool apply_rate_limit)
{
	struct sdl3_sound_voice *voice;
	Uint64 now;
	unsigned int limit;
	int category_volume;
	bool background;

	if (!sample) return false;
	background = sdl3_audio_category_is_background(sample->category);
	if (background) {
		/* A departed location must stop sounding even if the new sample is
		 * muted or cannot be prepared. Remember the destination for resume. */
		if (current_background != sample) sdl3_audio_stop_background();
		current_background = sample;
	}
	if (!audio_should_run()) return true;
	if (!audio_settings.movement_enabled &&
			strstr(sample->filename, "/movement/")) {
		return true;
	}
	category_volume = sdl3_audio_category_volume(&audio_settings,
		sample->category);
	if (!sdl3_audio_category_is_audible(&audio_settings, sample->category) ||
			category_volume <= 0) return true;
	now = SDL_GetTicks();
	limit = sdl3_audio_rate_limit_ms(sample->filename);
	/* Background repeats are coalesced below by their active voice. A muted
	 * or stopped loop must resume immediately, regardless of its last start. */
	if (apply_rate_limit && !background && sample->last_play_ms &&
			now - sample->last_play_ms < limit) return true;
	if (!ensure_audio_runtime() || !prepare_sample(sample)) return false;
	if (!SDL_LockAudioStream(audio_stream)) return false;
	if (background &&
			voices[0].active &&
			voices[0].sample == sample) {
		SDL_UnlockAudioStream(audio_stream);
		sample->last_play_ms = now;
		return true;
	}
	voice = select_voice(sample->category);
	if (voice) {
		voice->sample = sample;
		voice->frame_cursor = 0;
		voice->delay_frames = (size_t)SDL3_AUDIO_FREQUENCY * delay_ms /
			1000;
		voice->category = sample->category;
		voice->use_serial = ++use_serial;
		voice->active = true;
	}
	SDL_UnlockAudioStream(audio_stream);
	if (!voice) return false;
	sample->last_play_ms = now;
	return true;
}

static bool play_sample_sdl3(struct sdl3_sound_sample *sample)
{
	return queue_sample_sdl3(sample, 0, true);
}

static bool play_sound_sdl3(struct sound_data *data)
{
	return play_sample_sdl3(data ? data->plat_data : NULL);
}

static bool play_sound_sequence_sdl3(struct sound_data *const *data,
		const struct sound_sequence *sequence)
{
	bool success = true;
	uint8_t i;

	if (!data || !sequence || !sequence->count ||
			sequence->count > SOUND_SEQUENCE_MAX_STEPS) {
		return false;
	}
	for (i = 0; i < sequence->count; i++) {
		struct sdl3_sound_sample *sample = data[i] ? data[i]->plat_data : NULL;

		if (!sample) continue;
		/* Sequence timing is intentional presentation data. Per-sample spam
		 * limits apply to ordinary events, not to authored steps within one
		 * semantic event. */
		if (!queue_sample_sdl3(sample, sequence->offsets_ms[i], false)) {
			success = false;
		}
	}
	return success;
}

static bool unload_sound_sdl3(struct sound_data *data)
{
	struct sdl3_sound_sample *sample = data ? data->plat_data : NULL;
	struct sdl3_sound_sample **link = &sample_list;

	if (!sample) return true;
	if (current_background == sample) current_background = NULL;
	while (*link && *link != sample) link = &(*link)->next;
	if (*link == sample) *link = sample->next;
	if (sample->pcm && audio_stream) {
		if (SDL_LockAudioStream(audio_stream)) {
			stop_sample_voices(sample);
			SDL_UnlockAudioStream(audio_stream);
		} else {
			/* Ensure the callback cannot retain the sample after a failed lock. */
			close_audio_runtime();
		}
	}
	if (sample->pcm) {
		sdl3_audio_wav_free(sample->pcm);
		loaded_sample_count--;
	}
	string_free(sample->filename);
	string_free(sample->cue_id);
	mem_free(sample);
	data->plat_data = NULL;
	data->status = SOUND_ST_UNKNOWN;
	return true;
}

static bool close_audio_sdl3(void)
{
	close_audio_runtime();
	current_background = NULL;
	module_open = false;
	return true;
}

static const struct sound_file_type *supported_files_sdl3(void)
{
	return supported_sound_files;
}

errr init_sound_sdl3(struct sound_hooks *hooks, int argc, char **argv)
{
	int i;

	if (!hooks) return 1;
	verbose = false;
	for (i = 1; i < argc; i++) {
		if (streq(argv[i], "-v")) verbose = true;
	}
	hooks->open_audio_hook = open_audio_sdl3;
	hooks->close_audio_hook = close_audio_sdl3;
	hooks->resolve_sound_file_hook = resolve_sound_sdl3;
	hooks->load_sound_hook = load_sound_sdl3;
	hooks->unload_sound_hook = unload_sound_sdl3;
	hooks->play_sound_hook = play_sound_sdl3;
	hooks->play_sound_sequence_hook = play_sound_sequence_sdl3;
	hooks->supported_files_hook = supported_files_sdl3;
	return 0;
}

#endif /* SOUND_SDL3 */
