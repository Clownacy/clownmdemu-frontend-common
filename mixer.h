#include <stddef.h>

#include "core/clowncommon/clowncommon.h"
#include "core/clownmdemu.h"

#ifndef MIXER_HEADER
#define MIXER_HEADER

#include "clownresampler/clownresampler.h"

#define MIXER_OUTPUT_SAMPLE_RATE CLOWNMDEMU_CDDA_SAMPLE_RATE
#define MIXER_DIVIDE_BY_LOWEST_FRAMERATE CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE
#define MIXER_CHANNEL_COUNT CC_MAX(CC_MAX(CC_MAX(CLOWNMDEMU_FM_CHANNEL_COUNT, CLOWNMDEMU_PSG_CHANNEL_COUNT), CLOWNMDEMU_PCM_CHANNEL_COUNT), CLOWNMDEMU_CDDA_CHANNEL_COUNT)

#define MIXER_FIXED_POINT_FRACTIONAL_SIZE (1 << 16)
#define MIXER_TO_FIXED_POINT_FROM_INTEGER(X) ((X) * MIXER_FIXED_POINT_FRACTIONAL_SIZE)
#define MIXER_FIXED_POINT_MULTIPLY(MULTIPLICAND, MULTIPLIER) ((MULTIPLICAND) * (MULTIPLIER) / MIXER_FIXED_POINT_FRACTIONAL_SIZE)

typedef struct Mixer_Constant
{
	ClownResampler_Precomputed resampler_precomputed;
} Mixer_Constant;

typedef struct Mixer_Source
{
	cc_u8f channels;
	cc_s16l *buffer;
	size_t capacity;
	size_t write_index;
	ClownResampler_LowestLevel_Configuration resampler;
} Mixer_Source;

typedef struct Mixer_State
{
	Mixer_Source fm, psg, pcm, cdda;
} Mixer_State;

typedef void (*Mixer_Callback)(void *user_data, const cc_s16l *audio_samples, size_t total_frames);

void Mixer_Constant_Initialise(Mixer_Constant *constant);
cc_bool Mixer_Initialise(Mixer_State *state, cc_bool pal_mode);
void Mixer_Deinitialise(Mixer_State *state);
void Mixer_Begin(Mixer_State *state);
cc_s16l* Mixer_AllocateFMSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocatePSGSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocatePCMSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocateCDDASamples(Mixer_State *state, size_t total_frames);
void Mixer_End(Mixer_State *state, const Mixer_Constant *constant, Mixer_Callback callback, const void *user_data);

#ifdef __cplusplus

// MSVC is FUCKING SHIT, setting '__cplusplus' to the wrong value in a direct violation of the C++ standard.
#ifdef _MSVC_LANG
#define MIXER_CPLUSPLUS _MSVC_LANG
#else
#define MIXER_CPLUSPLUS __cplusplus
#endif

#include <cassert>
#include <cstddef>
#if MIXER_CPLUSPLUS >= 201103L
#include <functional>
#endif

class Mixer
{
public:
	class Constant : public Mixer_Constant
	{
	public:
		Constant()
		{
			Mixer_Constant_Initialise(this);
		}
	};

protected:
	bool initialised;
	const Constant &constant;
	Mixer_State state;

public:
	typedef Mixer_Callback Callback;

	Mixer(const Constant &constant, const bool pal_mode)
		: initialised(Mixer_Initialise(&state, pal_mode))
		, constant(constant)
	{}
	Mixer(const Mixer &other) = delete;
	Mixer(Mixer &&other) = delete;
	Mixer& operator=(const Mixer &other) = delete;
	Mixer& operator=(Mixer &&other) = delete;

	~Mixer()
	{
		assert(initialised);
		Mixer_Deinitialise(&state);
	}

	bool Initialised() const
	{
		return initialised;
	}

	void Begin()
	{
		assert(Initialised());
		Mixer_Begin(&state);
	}

	cc_s16l* AllocateFMSamples(const std::size_t total_frames)
	{
		assert(Initialised());
		return Mixer_AllocateFMSamples(&state, total_frames);
	}

	cc_s16l* AllocatePSGSamples(const std::size_t total_frames)
	{
		assert(Initialised());
		return Mixer_AllocatePSGSamples(&state, total_frames);
	}

	cc_s16l* AllocatePCMSamples(const std::size_t total_frames)
	{
		assert(Initialised());
		return Mixer_AllocatePCMSamples(&state, total_frames);
	}

	cc_s16l* AllocateCDDASamples(const std::size_t total_frames)
	{
		assert(Initialised());
		return Mixer_AllocateCDDASamples(&state, total_frames);
	}

	void End(const Callback callback, const void* const user_data)
	{
		assert(Initialised());
		Mixer_End(&state, &constant, callback, user_data);
	}

#if MIXER_CPLUSPLUS >= 201103L
	using CallbackFunctional = std::function<void(const cc_s16l *audio_samples, std::size_t total_frames)>;
	void End(const CallbackFunctional &callback)
	{
		End(
			[](void* const user_data, const cc_s16l* const audio_samples, const std::size_t total_frames)
			{
				const auto &callback = *static_cast<const CallbackFunctional*>(user_data);
				callback(audio_samples, total_frames);
			}, &callback
		);
	}
#endif

	void SetPALMode(const bool enabled)
	{
		assert(Initialised());
		Mixer_Deinitialise(&state);
		initialised = Mixer_Initialise(&state, enabled);
	}
};

#endif

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
	ClownResampler_LowestLevel_Configure(&source->resampler, input_sample_rate, MIXER_OUTPUT_SAMPLE_RATE, MIXER_OUTPUT_SAMPLE_RATE);

	source->channels = channels;
	/* The '+1' is just a lazy way of performing a rough ceiling division. */
	source->capacity = 1 + MIXER_DIVIDE_BY_LOWEST_FRAMERATE(input_sample_rate);
	source->buffer = (cc_s16l*)MIXER_CALLOC(1, (source->resampler.integer_stretched_kernel_radius * 2 + source->capacity) * source->channels * sizeof(cc_s16l));
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
	const size_t integer_stretched_kernel_diameter = source->resampler.integer_stretched_kernel_radius * 2;

	/* To make the resampler happy, we need to maintain some padding frames. */
	/* See clownresampler's documentation for more information. */

	/* Copy the end of each buffer to its beginning, since we never used all of it. */
	MIXER_MEMMOVE(source->buffer, Mixer_Source_Buffer(source, source->write_index), integer_stretched_kernel_diameter * source->channels * sizeof(cc_s16l));

	/* Blank the remainder of the buffers so that they can be mixed into. */
	MIXER_MEMSET(Mixer_Source_Buffer(source, integer_stretched_kernel_diameter), 0, source->write_index * source->channels * sizeof(cc_s16l));

	source->write_index = 0;
}

static cc_s16l* Mixer_Source_AllocateFrames(Mixer_Source* const source, const size_t total_frames)
{
	cc_s16l* const allocated_samples = Mixer_Source_Buffer(source, source->resampler.integer_stretched_kernel_radius * 2 + source->write_index);

	source->write_index += total_frames;

	MIXER_ASSERT(source->write_index <= source->capacity);

	return allocated_samples;
}

static size_t Mixer_Source_GetTotalAllocatedFrames(const Mixer_Source* const source)
{
	return source->write_index;
}

static void Mixer_Source_GetFrame(Mixer_Source* const source, const ClownResampler_Precomputed* const resampler_precomputed, cc_s32f* const frame, const cc_u32f position)
{
	MIXER_MEMSET(frame, 0, source->channels * sizeof(*frame));
	ClownResampler_LowestLevel_Resample(&source->resampler, resampler_precomputed, frame, source->channels, source->buffer, CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_FLOOR(position), position % CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE);
}

/* Mixer API */

void Mixer_Constant_Initialise(Mixer_Constant* const constant)
{
	/* Compute ClownResampler's lookup tables.*/
	ClownResampler_Precompute(&constant->resampler_precomputed);
}

static cc_u32f Mixer_GetCorrectedSampleRate(const cc_u32f sample_rate_ntsc, const cc_u32f sample_rate_pal, const cc_bool pal_mode)
{
	return pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(sample_rate_pal))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(sample_rate_ntsc));
}

cc_bool Mixer_Initialise(Mixer_State* const state, const cc_bool pal_mode)
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

void Mixer_Deinitialise(Mixer_State* const state)
{
	Mixer_Source_Deinitialise(&state->fm);
	Mixer_Source_Deinitialise(&state->psg);
	Mixer_Source_Deinitialise(&state->pcm);
	Mixer_Source_Deinitialise(&state->cdda);
}

void Mixer_Begin(Mixer_State* const state)
{
	Mixer_Source_NewFrame(&state->fm);
	Mixer_Source_NewFrame(&state->psg);
	Mixer_Source_NewFrame(&state->pcm);
	Mixer_Source_NewFrame(&state->cdda);
}

cc_s16l* Mixer_AllocateFMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->fm, total_frames);
}

cc_s16l* Mixer_AllocatePSGSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->psg, total_frames);
}

cc_s16l* Mixer_AllocatePCMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->pcm, total_frames);
}

cc_s16l* Mixer_AllocateCDDASamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->cdda, total_frames);
}

void Mixer_End(Mixer_State* const state, const Mixer_Constant* const constant, const Mixer_Callback callback, const void* const user_data)
{
	const ClownResampler_Precomputed* const clownresampler_precomputed = &constant->resampler_precomputed;

	const size_t available_fm_frames = Mixer_Source_GetTotalAllocatedFrames(&state->fm);
	const size_t available_psg_frames = Mixer_Source_GetTotalAllocatedFrames(&state->psg);
	const size_t available_pcm_frames = Mixer_Source_GetTotalAllocatedFrames(&state->pcm);
	const size_t available_cdda_frames = Mixer_Source_GetTotalAllocatedFrames(&state->cdda);

	/* By orienting everything around the CDDA, we avoid the need to resample the CDDA! */
	cc_s16l* const output_buffer = state->cdda.buffer;
	const cc_u32f output_length = available_cdda_frames;

	const cc_u32f fm_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_fm_frames) / output_length;
	const cc_u32f psg_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_psg_frames) / output_length;
	const cc_u32f pcm_ratio = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_pcm_frames) / output_length;

	cc_s16l *output_buffer_pointer = output_buffer;
	cc_u32f fm_position, psg_position, pcm_position;
	cc_u32f i;

	/* Resample, mix, and output the audio for this frame. */
	for (i = 0, fm_position = 0, psg_position = 0, pcm_position = 0; i < output_length; ++i, fm_position += fm_ratio, psg_position += psg_ratio, pcm_position += pcm_ratio)
	{
		cc_s32f fm_frame[CLOWNMDEMU_FM_CHANNEL_COUNT];
		cc_s32f psg_frame[CLOWNMDEMU_PSG_CHANNEL_COUNT];
		cc_s32f pcm_frame[CLOWNMDEMU_PCM_CHANNEL_COUNT];

		Mixer_Source_GetFrame(&state->fm, clownresampler_precomputed, fm_frame, fm_position);
		Mixer_Source_GetFrame(&state->psg, clownresampler_precomputed, psg_frame, psg_position);
		Mixer_Source_GetFrame(&state->pcm, clownresampler_precomputed, pcm_frame, pcm_position);

		/* Mix the FM, PSG, PCM, and CDDA to produce the final audio. */
		*output_buffer_pointer = fm_frame[0] / CLOWNMDEMU_FM_VOLUME_DIVISOR + psg_frame[0] / CLOWNMDEMU_PSG_VOLUME_DIVISOR + pcm_frame[0] / CLOWNMDEMU_PCM_VOLUME_DIVISOR + *output_buffer_pointer / CLOWNMDEMU_CDDA_VOLUME_DIVISOR;
		++output_buffer_pointer;
		*output_buffer_pointer = fm_frame[1] / CLOWNMDEMU_FM_VOLUME_DIVISOR + psg_frame[0] / CLOWNMDEMU_PSG_VOLUME_DIVISOR + pcm_frame[1] / CLOWNMDEMU_PCM_VOLUME_DIVISOR + *output_buffer_pointer / CLOWNMDEMU_CDDA_VOLUME_DIVISOR;
		++output_buffer_pointer;
	}

	/* Output resampled and mixed samples. */
	callback((void*)user_data, output_buffer, output_length);
}

#endif /* MIXER_IMPLEMENTATION */
