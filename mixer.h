#include <stddef.h>

#include "core/clowncommon/clowncommon.h"
#include "core/clownmdemu.h"

#ifndef MIXER_HEADER
#define MIXER_HEADER

#define MIXER_OUTPUT_SAMPLE_RATE CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC
#define MIXER_DIVIDE_BY_LOWEST_FRAMERATE CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE
#define MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME MIXER_DIVIDE_BY_LOWEST_FRAMERATE(MIXER_OUTPUT_SAMPLE_RATE)
#define MIXER_CHANNEL_COUNT CC_MAX(CC_MAX(CC_MAX(CLOWNMDEMU_FM_CHANNEL_COUNT, CLOWNMDEMU_PSG_CHANNEL_COUNT), CLOWNMDEMU_PCM_CHANNEL_COUNT), CLOWNMDEMU_CDDA_CHANNEL_COUNT)

#define MIXER_FIXED_POINT_FRACTIONAL_SIZE (1 << 16)
#define MIXER_TO_FIXED_POINT_FROM_INTEGER(X) ((X) * MIXER_FIXED_POINT_FRACTIONAL_SIZE)
#define MIXER_FIXED_POINT_MULTIPLY(MULTIPLICAND, MULTIPLIER) ((MULTIPLICAND) * (MULTIPLIER) / MIXER_FIXED_POINT_FRACTIONAL_SIZE)

typedef struct Mixer_Source
{
	cc_u8f channels;
	cc_s16l *buffer;
	size_t capacity;
	size_t write_index;
} Mixer_Source;

typedef struct Mixer_State
{
	Mixer_Source fm, psg, pcm, cdda;
} Mixer_State;

#endif /* MIXER_HEADER */

#ifdef MIXER_IMPLEMENTATION

#ifndef MIXER_ASSERT
#include <assert.h>
#define MIXER_ASSERT assert
#endif

#ifndef MIXER_FREE
#include <stdlib.h>
#define MIXER_FREE free
#endif

#ifndef MIXER_CALLOC
#include <stdlib.h>
#define MIXER_CALLOC calloc
#endif

#ifndef MIXER_MEMMOVE
#include <string.h>
#define MIXER_MEMMOVE memmove
#endif

#ifndef MIXER_MEMSET
#include <string.h>
#define MIXER_MEMSET memset
#endif

/* Mixer Source */

static cc_bool Mixer_Source_Initialise(Mixer_Source* const source, const cc_u8f channels, const cc_u32f input_sample_rate)
{
	source->channels = channels;
	/* The '+1' is just a lazy way of performing a rough ceiling division. */
	source->capacity = 1 + MIXER_DIVIDE_BY_LOWEST_FRAMERATE(input_sample_rate);
	source->buffer = (cc_s16l*)MIXER_CALLOC(1, (1 + source->capacity) * source->channels * sizeof(cc_s16l));
	source->write_index = 0;

	return source->buffer != NULL;
}

static void Mixer_Source_Deinitialise(Mixer_Source* const source)
{
	MIXER_FREE(source->buffer);
}

static cc_s16l* Mixer_Source_Buffer(Mixer_Source* const source, const size_t index)
{
	return &source->buffer[index * source->channels];
}

static void Mixer_Source_NewFrame(Mixer_Source* const source)
{
	/* To make the resampler happy, we need to maintain some padding frames. */
	/* See clownresampler's documentation for more information. */

	/* Copy the end of each buffer to its beginning, since we never used all of it. */
	MIXER_MEMMOVE(source->buffer, Mixer_Source_Buffer(source, source->write_index), 1 * source->channels * sizeof(cc_s16l));

	/* Blank the remainder of the buffers so that they can be mixed into. */
	MIXER_MEMSET(Mixer_Source_Buffer(source, 1), 0, source->write_index * source->channels * sizeof(cc_s16l));

	source->write_index = 0;
}

static cc_s16l* Mixer_Source_AllocateFrames(Mixer_Source* const source, const size_t total_frames)
{
	cc_s16l* const allocated_samples = Mixer_Source_Buffer(source, 1 + source->write_index);

	source->write_index += total_frames;

	MIXER_ASSERT(source->write_index <= source->capacity);

	return allocated_samples;
}

static size_t Mixer_Source_GetTotalAllocatedFrames(const Mixer_Source* const source)
{
	return source->write_index;
}

static void Mixer_Source_GetFrame(Mixer_Source* const source, cc_s16f* const frame, const cc_u32f position)
{
	const cc_u8f total_channels = source->channels;
	const cc_u32f position_integral = position / MIXER_FIXED_POINT_FRACTIONAL_SIZE;
	const cc_s32f position_fractional = position % MIXER_FIXED_POINT_FRACTIONAL_SIZE;
	const cc_u32f frame_position = position_integral * total_channels;

	cc_u8f i;

	for (i = 0; i < total_channels; ++i)
	{
		const cc_u32f sample_position = frame_position + i;
		const cc_s16f sample_base = source->buffer[sample_position];

		frame[i] = sample_base;
	}
}

/* Mixer API */

static cc_u32f Mixer_GetCorrectedSampleRate(const cc_u32f sample_rate_ntsc, const cc_u32f sample_rate_pal, const cc_bool pal_mode)
{
	return pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(sample_rate_pal))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(sample_rate_ntsc));
}

static cc_bool Mixer_Initialise(Mixer_State* const state, const cc_bool pal_mode)
{
	const cc_u32f fm_sample_rate = Mixer_GetCorrectedSampleRate(CLOWNMDEMU_FM_SAMPLE_RATE_NTSC, CLOWNMDEMU_FM_SAMPLE_RATE_PAL, pal_mode);
	const cc_u32f psg_sample_rate = Mixer_GetCorrectedSampleRate(CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC, CLOWNMDEMU_PSG_SAMPLE_RATE_PAL, pal_mode);
	const cc_u32f pcm_sample_rate = Mixer_GetCorrectedSampleRate(CLOWNMDEMU_PCM_SAMPLE_RATE, CLOWNMDEMU_PCM_SAMPLE_RATE, pal_mode);
	const cc_u32f cdda_sample_rate = Mixer_GetCorrectedSampleRate(CLOWNMDEMU_CDDA_SAMPLE_RATE, CLOWNMDEMU_CDDA_SAMPLE_RATE, pal_mode);

	const cc_bool fm_success = Mixer_Source_Initialise(&state->fm, CLOWNMDEMU_FM_CHANNEL_COUNT, fm_sample_rate);
	const cc_bool psg_success = Mixer_Source_Initialise(&state->psg, CLOWNMDEMU_PSG_CHANNEL_COUNT, psg_sample_rate);
	const cc_bool pcm_success = Mixer_Source_Initialise(&state->pcm, CLOWNMDEMU_PCM_CHANNEL_COUNT, pcm_sample_rate);
	const cc_bool cdda_success = Mixer_Source_Initialise(&state->cdda, CLOWNMDEMU_CDDA_CHANNEL_COUNT, cdda_sample_rate);

	if (fm_success && psg_success && pcm_success && cdda_success)
		return cc_true;

	if (fm_success)
		Mixer_Source_Deinitialise(&state->fm);
	if (psg_success)
		Mixer_Source_Deinitialise(&state->psg);
	if (pcm_success)
		Mixer_Source_Deinitialise(&state->pcm);
	if (cdda_success)
		Mixer_Source_Deinitialise(&state->cdda);

	return cc_false;
}

static void Mixer_Deinitialise(Mixer_State* const state)
{
	Mixer_Source_Deinitialise(&state->fm);
	Mixer_Source_Deinitialise(&state->psg);
	Mixer_Source_Deinitialise(&state->pcm);
	Mixer_Source_Deinitialise(&state->cdda);
}

static void Mixer_Begin(Mixer_State* const state)
{
	Mixer_Source_NewFrame(&state->fm);
	Mixer_Source_NewFrame(&state->psg);
	Mixer_Source_NewFrame(&state->pcm);
	Mixer_Source_NewFrame(&state->cdda);
}

static cc_s16l* Mixer_AllocateFMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->fm, total_frames);
}

static cc_s16l* Mixer_AllocatePSGSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->psg, total_frames);
}

static cc_s16l* Mixer_AllocatePCMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->pcm, total_frames);
}

static cc_s16l* Mixer_AllocateCDDASamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->cdda, total_frames);
}

static void Mixer_End(Mixer_State* const state, void (* const callback)(void *user_data, const cc_s16l *audio_samples, size_t total_frames), const void* const user_data)
{
	const size_t available_fm_frames = Mixer_Source_GetTotalAllocatedFrames(&state->fm);
	const size_t available_psg_frames = Mixer_Source_GetTotalAllocatedFrames(&state->psg);
	const size_t available_pcm_frames = Mixer_Source_GetTotalAllocatedFrames(&state->pcm);
	const size_t available_cdda_frames = Mixer_Source_GetTotalAllocatedFrames(&state->cdda);

	/* By orienting everything around the PSG, we avoid the need to resample the PSG! */
	const cc_u32f output_length = available_psg_frames;

	const cc_u32f fm_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_fm_frames) / output_length;
	const cc_u32f pcm_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_pcm_frames) / output_length;
	const cc_u32f cdda_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_cdda_frames) / output_length;

	cc_s16l output_buffer[MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * MIXER_CHANNEL_COUNT];
	cc_s16l *output_buffer_pointer = output_buffer;
	cc_u32f fm_position, pcm_position, cdda_position;
	cc_u32f i;

	/* Resample, mix, and output the audio for this frame. */
	for (i = 0, fm_position = 0, pcm_position = 0, cdda_position = 0; i < output_length; ++i, fm_position += fm_ratio, pcm_position += pcm_ratio, cdda_position += cdda_ratio)
	{
		cc_s16f fm_frame[CLOWNMDEMU_FM_CHANNEL_COUNT];
		cc_s16f pcm_frame[CLOWNMDEMU_PCM_CHANNEL_COUNT];
		cc_s16f cdda_frame[CLOWNMDEMU_CDDA_CHANNEL_COUNT];

		Mixer_Source_GetFrame(&state->fm, fm_frame, fm_position);
		Mixer_Source_GetFrame(&state->pcm, pcm_frame, pcm_position);
		Mixer_Source_GetFrame(&state->cdda, cdda_frame, cdda_position);

		/* Mix the FM, PSG, PCM, and CDDA to produce the final audio. */
		*output_buffer_pointer = fm_frame[0] / CLOWNMDEMU_FM_VOLUME_DIVISOR + state->psg.buffer[i] / CLOWNMDEMU_PSG_VOLUME_DIVISOR + pcm_frame[0] / CLOWNMDEMU_PCM_VOLUME_DIVISOR + cdda_frame[0] / CLOWNMDEMU_CDDA_VOLUME_DIVISOR;
		++output_buffer_pointer;
		*output_buffer_pointer = fm_frame[1] / CLOWNMDEMU_FM_VOLUME_DIVISOR + state->psg.buffer[i] / CLOWNMDEMU_PSG_VOLUME_DIVISOR + pcm_frame[1] / CLOWNMDEMU_PCM_VOLUME_DIVISOR + cdda_frame[1] / CLOWNMDEMU_CDDA_VOLUME_DIVISOR;
		++output_buffer_pointer;
	}

	/* Output resampled and mixed samples. */
	callback((void*)user_data, output_buffer, output_length);
}

#endif /* MIXER_IMPLEMENTATION */
