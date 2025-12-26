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

enum
{
	/* The last enum is special; these are not. */
	MIXER_SOURCE_FM,
	MIXER_SOURCE_PCM,
	MIXER_SOURCE_CDDA,

	/* By orienting everything around the PSG, we avoid the need to resample the PSG! */
	MIXER_SOURCE_PSG,

	/* Ignore this; this is just the total number of enums. */
	MIXER_SOURCE_TOTAL
};

typedef struct Mixer_State
{
	Mixer_Source sources[MIXER_SOURCE_TOTAL];
} Mixer_State;

typedef void (*Mixer_Callback)(void *user_data, const cc_s16l *audio_samples, size_t total_frames);

cc_bool Mixer_Initialise(Mixer_State *state);
void Mixer_Deinitialise(Mixer_State *state);
void Mixer_Begin(Mixer_State *state);
cc_s16l* Mixer_AllocateFMSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocatePSGSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocatePCMSamples(Mixer_State *state, size_t total_frames);
cc_s16l* Mixer_AllocateCDDASamples(Mixer_State *state, size_t total_frames);
void Mixer_End(Mixer_State *state, Mixer_Callback callback, const void *user_data);

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
protected:
	Mixer_State state;
	bool initialised;

public:
	typedef Mixer_Callback Callback;

	Mixer()
	{
		initialised = Mixer_Initialise(&state);
	}
	Mixer(const Mixer &other) = delete;
	Mixer(Mixer &&other) = delete;
	Mixer& operator=(const Mixer &other) = delete;
	Mixer& operator=(Mixer &&other) = delete;

	~Mixer()
	{
		if (initialised)
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
		Mixer_End(&state, callback, user_data);
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

static cc_bool Mixer_Source_Initialise(Mixer_Source* const source, const cc_u8f channels, const size_t capacity)
{
	source->channels = channels;
	source->capacity = capacity;
	source->buffer = (cc_s16l*)MIXER_CALLOC(1, source->capacity * source->channels * sizeof(cc_s16l));
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
	/* Blank the buffers so that they can be mixed into. */
	MIXER_MEMSET(source->buffer, 0, source->write_index * source->channels * sizeof(cc_s16l));

	source->write_index = 0;
}

static cc_s16l* Mixer_Source_AllocateFrames(Mixer_Source* const source, const size_t total_frames)
{
	cc_s16l* const allocated_samples = Mixer_Source_Buffer(source, source->write_index);

	source->write_index += total_frames;

	MIXER_ASSERT(source->write_index <= source->capacity);

	return allocated_samples;
}

static size_t Mixer_Source_GetTotalAllocatedFrames(const Mixer_Source* const source)
{
	return source->write_index;
}

static cc_s16l* Mixer_Source_GetFrame(Mixer_Source* const source, const cc_u32f position)
{
	const cc_u8f total_channels = source->channels;
	const cc_u32f position_integral = position / MIXER_FIXED_POINT_FRACTIONAL_SIZE;
	const cc_u32f frame_position = position_integral * total_channels;

	return &source->buffer[frame_position];
}

/* Mixer API */

cc_bool Mixer_Initialise(Mixer_State* const state)
{
	static const struct
	{
		cc_u32l sample_rate_ntsc, sample_rate_pal;
		cc_u8l channel_count;
	} metadata[MIXER_SOURCE_TOTAL] = {
		{CLOWNMDEMU_FM_SAMPLE_RATE_NTSC,  CLOWNMDEMU_FM_SAMPLE_RATE_PAL,  CLOWNMDEMU_FM_CHANNEL_COUNT  }, /* MIXER_SOURCE_FM   */
		{CLOWNMDEMU_PCM_SAMPLE_RATE,      CLOWNMDEMU_PCM_SAMPLE_RATE,     CLOWNMDEMU_PCM_CHANNEL_COUNT }, /* MIXER_SOURCE_PCM  */
		{CLOWNMDEMU_CDDA_SAMPLE_RATE,     CLOWNMDEMU_CDDA_SAMPLE_RATE,    CLOWNMDEMU_CDDA_CHANNEL_COUNT}, /* MIXER_SOURCE_CDDA */
		{CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC, CLOWNMDEMU_PSG_SAMPLE_RATE_PAL, CLOWNMDEMU_PSG_CHANNEL_COUNT }, /* MIXER_SOURCE_PSG  */
	};

	cc_bool successes[MIXER_SOURCE_TOTAL], success = cc_true;
	cc_u8f i;

	for (i = 0; i < CC_COUNT_OF(state->sources); ++i)
	{
		const cc_u32f sample_rate = CC_MAX(metadata[i].sample_rate_ntsc, metadata[i].sample_rate_pal);
		/* The '+1' is just a lazy way of performing a rough ceiling division. */
		const size_t capacity = 1 + MIXER_DIVIDE_BY_LOWEST_FRAMERATE(sample_rate);
		successes[i] = Mixer_Source_Initialise(&state->sources[i], metadata[i].channel_count, capacity);

		success = success && successes[i];
	}

	if (success)
		return cc_true;

	for (i = 0; i < CC_COUNT_OF(state->sources); ++i)
		if (successes[i])
			Mixer_Source_Deinitialise(&state->sources[i]);

	return cc_false;
}

void Mixer_Deinitialise(Mixer_State* const state)
{
	cc_u8f i;

	for (i = 0; i < CC_COUNT_OF(state->sources); ++i)
		Mixer_Source_Deinitialise(&state->sources[i]);
}

void Mixer_Begin(Mixer_State* const state)
{
	cc_u8f i;

	for (i = 0; i < CC_COUNT_OF(state->sources); ++i)
		Mixer_Source_NewFrame(&state->sources[i]);
}

cc_s16l* Mixer_AllocateFMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->sources[MIXER_SOURCE_FM], total_frames);
}

cc_s16l* Mixer_AllocatePSGSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->sources[MIXER_SOURCE_PSG], total_frames);
}

cc_s16l* Mixer_AllocatePCMSamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->sources[MIXER_SOURCE_PCM], total_frames);
}

cc_s16l* Mixer_AllocateCDDASamples(Mixer_State* const state, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&state->sources[MIXER_SOURCE_CDDA], total_frames);
}

void Mixer_End(Mixer_State* const state, const Mixer_Callback callback, const void* const user_data)
{
	cc_s16l output_buffer[MIXER_MAXIMUM_AUDIO_FRAMES_PER_FRAME * MIXER_CHANNEL_COUNT];
	cc_s16l *output_buffer_pointer = output_buffer;

	size_t available_frames[MIXER_SOURCE_TOTAL];
	cc_u32f position[MIXER_SOURCE_TOTAL - 1], ratio[MIXER_SOURCE_TOTAL - 1];

	cc_u8f i;
	cc_u32f frame_index;

	for (i = 0; i < CC_COUNT_OF(available_frames); ++i)
		available_frames[i] = Mixer_Source_GetTotalAllocatedFrames(&state->sources[i]);

	for (i = 0; i < CC_COUNT_OF(position); ++i)
	{
		position[i] = 0;
		ratio[i] = MIXER_TO_FIXED_POINT_FROM_INTEGER(available_frames[i]) / available_frames[MIXER_SOURCE_TOTAL - 1];
	}

	/* Resample, mix, and output the audio for this frame. */
	for (frame_index = 0; frame_index < available_frames[MIXER_SOURCE_TOTAL - 1]; ++frame_index)
	{
		/* We use a macro instead of a loop so that the division is optimised to a bit-shift. */
		/* Beware: This code assumes that the sources are stereo! */
#define MIXER_DO_SOURCE(SOURCE, VOLUME_DIVISOR) \
		{ \
			const cc_s16l* const input_frame = Mixer_Source_GetFrame(&state->sources[SOURCE], position[SOURCE]); \
\
			for (i = 0; i < CC_COUNT_OF(frame); ++i) \
				frame[i] += input_frame[i] / (VOLUME_DIVISOR); \
\
			position[SOURCE] += ratio[SOURCE]; \
		}

		/* Mix the FM, PSG, PCM, and CDDA to produce the final audio. */
		const cc_s16l psg_sample = state->sources[MIXER_SOURCE_PSG].buffer[frame_index * CLOWNMDEMU_PSG_CHANNEL_COUNT] / CLOWNMDEMU_PSG_VOLUME_DIVISOR;

		cc_s32f frame[MIXER_CHANNEL_COUNT];

		for (i = 0; i < CC_COUNT_OF(frame); ++i)
			frame[i] = psg_sample;

		MIXER_DO_SOURCE(MIXER_SOURCE_FM,   CLOWNMDEMU_FM_VOLUME_DIVISOR  );
		MIXER_DO_SOURCE(MIXER_SOURCE_PCM,  CLOWNMDEMU_PCM_VOLUME_DIVISOR );
		MIXER_DO_SOURCE(MIXER_SOURCE_CDDA, CLOWNMDEMU_CDDA_VOLUME_DIVISOR);

		/* Clamp output to S16 sample range. */
		for (i = 0; i < CC_COUNT_OF(frame); ++i)
			*output_buffer_pointer++ = CC_CLAMP(-0x7FFF, 0x7FFF, frame[i]);
	}

	/* Output resampled and mixed samples. */
	callback((void*)user_data, output_buffer, available_frames[MIXER_SOURCE_TOTAL - 1]);
}

#endif /* MIXER_IMPLEMENTATION */
