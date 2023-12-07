#include <stddef.h>

#include "clownmdemu/clowncommon/clowncommon.h"
#include "clownmdemu/clownmdemu.h"

#ifndef MIXER_HEADER
#define MIXER_HEADER

#define CLOWNRESAMPLER_STATIC
#define CLOWNRESAMPLER_NO_HIGH_LEVEL_API
#define CLOWNRESAMPLER_NO_LOW_LEVEL_API
#include "clownresampler/clownresampler.h"

#define MIXER_FM_CHANNEL_COUNT 2
#define MIXER_PSG_CHANNEL_COUNT 1

#ifndef MIXER_FORMAT
#error "You need to define MIXER_FORMAT before including `mixer.h`."
#endif

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
	Mixer_Source fm, psg;

	cc_u32f output_length;
} Mixer_State;

typedef struct Mixer
{
	const Mixer_Constant *constant;
	Mixer_State *state;
} Mixer;

#endif /* MIXER_HEADER */

#ifdef MIXER_IMPLEMENTATION

#define CLOWNRESAMPLER_IMPLEMENTATION
#include "clownresampler/clownresampler.h"

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

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || (defined(__cplusplus) && __cplusplus >= 201103L)
#define MIXER_HAS_LONG_LONG
#endif

/* MulDiv Stuff */

#ifndef MIXER_HAS_LONG_LONG
typedef struct Mixer_SplitInteger
{
	cc_u32f splits[3];
} Mixer_SplitInteger;

static void Mixer_Add(Mixer_SplitInteger* const output, const cc_u32f value)
{
	const cc_u32f upper = value / 0x10000;
	const cc_u32f lower = value % 0x10000;

	output->splits[2] += lower;
	output->splits[1] += upper + output->splits[2] / 0x10000;
	output->splits[0] += output->splits[1] / 0x10000;

	output->splits[2] %= 0x10000;
	output->splits[1] %= 0x10000;
	output->splits[0] %= 0x10000;
}

static void Mixer_Subtract(Mixer_SplitInteger* const minuend, const Mixer_SplitInteger* const subtrahend)
{
	cc_u32f carry;
	cc_u8f i;

	carry = 0;

	for (i = CC_COUNT_OF(minuend->splits); i-- != 0; )
	{
		const cc_u32f difference = minuend->splits[i] - subtrahend->splits[i] + carry;

		/* Isolate and sign-extend the overflow to obtain the new carry. */
		carry = difference >> 16;
		carry = (carry & 0x7FFF) - (carry & 0x8000);

		/* Store result. */
		minuend->splits[i] = difference % 0x10000;
	}
}

static cc_bool Mixer_GreaterThan(const Mixer_SplitInteger* const a, const Mixer_SplitInteger* const b)
{
	cc_u8f i;

	for (i = 0; i < CC_COUNT_OF(a->splits); ++i)
	{
		if (a->splits[i] > b->splits[i])
			return cc_true;
		else if (a->splits[i] < b->splits[i])
			return cc_false;
	}

	return cc_false;
}

static void Mixer_LeftShift(Mixer_SplitInteger* const value)
{
	cc_u32f carry;
	cc_u8f i;

	carry = 0;

	for (i = CC_COUNT_OF(value->splits); i-- != 0; )
	{
		const cc_u32f new_carry = value->splits[i] >> 15;
		value->splits[i] <<= 1;
		value->splits[i] |= carry;
		carry = new_carry;
		value->splits[i] %= 0x10000;
	}
}

static void Mixer_RightShift(Mixer_SplitInteger* const value)
{
	cc_u32f carry;
	cc_u8f i;

	carry = 0;

	for (i = 0; i < CC_COUNT_OF(value->splits); ++i)
	{
		const cc_u32f new_carry = value->splits[i] << 15;
		value->splits[i] >>= 1;
		value->splits[i] |= carry;
		carry = new_carry;
		value->splits[i] %= 0x10000;
	}
}

static void Mixer_Multiply(Mixer_SplitInteger* const output, const cc_u32f input_a, const cc_u32f input_b)
{
	const cc_u32f a_upper = input_a / 0x10000;
	const cc_u32f a_lower = input_a % 0x10000;
	const cc_u32f b_upper = input_b / 0x10000;
	const cc_u32f b_lower = input_b % 0x10000;

	output->splits[2] = a_lower * b_lower;
	output->splits[1] = a_upper * b_lower + a_lower * b_upper;
	output->splits[0] = a_upper * b_upper;

	output->splits[1] += output->splits[2] / 0x10000;
	output->splits[0] += output->splits[1] / 0x10000;

	output->splits[2] %= 0x10000;
	output->splits[1] %= 0x10000;
	output->splits[0] %= 0x10000;
}

static cc_u32f Mixer_Divide(Mixer_SplitInteger* const dividend, const cc_u32f divisor_raw)
{
	Mixer_SplitInteger divisor;
	cc_u8f shift_amount;
	cc_u32f result;

	divisor.splits[0] = 0;
	divisor.splits[1] = divisor_raw / 0x10000;
	divisor.splits[2] = divisor_raw % 0x10000;

	shift_amount = 0;

	while (Mixer_GreaterThan(dividend, &divisor))
	{
		Mixer_LeftShift(&divisor);
		++shift_amount;
	}

	result = 0;

	for (;;)
	{
		do
		{
			if (shift_amount == 0)
				return result;

			Mixer_RightShift(&divisor);
			--shift_amount;

			result <<= 1;
		} while (Mixer_GreaterThan(&divisor, dividend));

		do
		{
			Mixer_Subtract(dividend, &divisor);
			++result;
		} while (!Mixer_GreaterThan(&divisor, dividend));
	}
}
#endif

static cc_u32f Mixer_MulDiv(const cc_u32f a, const cc_u32f b, const cc_u32f c)
{
#ifdef MIXER_HAS_LONG_LONG
	return CC_DIVIDE_ROUND((unsigned long long)a * b, c);
#else
	Mixer_SplitInteger d;
	Mixer_Multiply(&d, a, b); /* Lit af. */
	Mixer_Add(&d, c / 2); /* This causes the division to round to the nearest value. */
	return Mixer_Divide(&d, c);
#endif
}

/* Mixer Source */

static cc_bool Mixer_Source_Initialise(Mixer_Source* const source, const cc_u8f channels, const cc_u32f input_sample_rate, const cc_u32f output_sample_rate, const cc_u32f low_pass_filter_sample_rate)
{
	ClownResampler_LowestLevel_Configure(&source->resampler, input_sample_rate, output_sample_rate, low_pass_filter_sample_rate);

	source->channels = channels;
	/* The '+1' is just a lazy way of performing a rough ceiling division. */
	source->capacity = 1 + CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(input_sample_rate);
	source->buffer = (cc_s16l*)MIXER_CALLOC(1, (source->resampler.integer_stretched_kernel_radius * 2 + source->capacity) * source->channels * sizeof(MIXER_FORMAT));
	source->write_index = 0;

	return source->buffer != NULL;
}

static void Mixer_Source_Deinitialise(Mixer_Source* const source)
{
	MIXER_FREE(source->buffer);
}

static MIXER_FORMAT* Mixer_Source_Buffer(Mixer_Source* const source, const size_t index)
{
	return &source->buffer[index * source->channels];
}

static void Mixer_Source_NewFrame(Mixer_Source* const source)
{
	const size_t integer_stretched_kernel_diameter = source->resampler.integer_stretched_kernel_radius * 2;

	/* To make the resampler happy, we need to maintain some padding frames. */
	/* See clownresampler's documentation for more information. */

	/* Copy the end of each buffer to its beginning, since we never used all of it. */
	MIXER_MEMMOVE(source->buffer, Mixer_Source_Buffer(source, source->write_index), integer_stretched_kernel_diameter * source->channels * sizeof(MIXER_FORMAT));

	/* Blank the remainder of the buffers so that they can be mixed into. */
	MIXER_MEMSET(Mixer_Source_Buffer(source, integer_stretched_kernel_diameter), 0, source->write_index * source->channels * sizeof(MIXER_FORMAT));

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

static void Mixer_Source_GetFrame(Mixer_Source* const source, const ClownResampler_Precomputed* const precomputed, cc_s32f* const frame, const cc_u32f position)
{
	MIXER_MEMSET(frame, 0, source->channels * sizeof(*frame));
	ClownResampler_LowestLevel_Resample(&source->resampler, precomputed, frame, source->channels, source->buffer, CLOWNRESAMPLER_TO_INTEGER_FROM_FIXED_POINT_FLOOR(position), position % CLOWNRESAMPLER_FIXED_POINT_FRACTIONAL_SIZE);
}

/* Mixer API */

static void Mixer_Constant_Initialise(Mixer_Constant* const constant)
{
	/* Compute clownresampler's lookup tables.*/
	ClownResampler_Precompute(&constant->resampler_precomputed);
}

static cc_bool Mixer_State_Initialise(Mixer_State* const state, const cc_u32f output_sample_rate, const cc_bool pal_mode, const cc_bool low_pass_filter)
{
	const cc_u32f fm_sample_rate = pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_PAL))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_NTSC));
	const cc_u32f psg_sample_rate = pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_PAL))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC));
	const cc_u32f low_pass_filter_sample_rate = low_pass_filter ? 22000 : output_sample_rate;

	const cc_bool fm_success = Mixer_Source_Initialise(&state->fm, MIXER_FM_CHANNEL_COUNT, fm_sample_rate, output_sample_rate, low_pass_filter_sample_rate);
	const cc_bool psg_success = Mixer_Source_Initialise(&state->psg, MIXER_PSG_CHANNEL_COUNT, psg_sample_rate, output_sample_rate, low_pass_filter_sample_rate);

	if (fm_success && psg_success)
	{
		state->output_length = pal_mode ? CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(output_sample_rate) : CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(output_sample_rate);

		return cc_true;
	}

	if (fm_success)
		Mixer_Source_Deinitialise(&state->fm);
	else if (psg_success)
		Mixer_Source_Deinitialise(&state->psg);

	return cc_false;
}

static void Mixer_State_Deinitialise(Mixer_State* const state)
{
	Mixer_Source_Deinitialise(&state->fm);
	Mixer_Source_Deinitialise(&state->psg);
}

static void Mixer_Begin(const Mixer* const mixer)
{
	Mixer_Source_NewFrame(&mixer->state->fm);
	Mixer_Source_NewFrame(&mixer->state->psg);
}

static cc_s16l* Mixer_AllocateFMSamples(const Mixer* const mixer, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&mixer->state->fm, total_frames);
}

static cc_s16l* Mixer_AllocatePSGSamples(const Mixer* const mixer, const size_t total_frames)
{
	return Mixer_Source_AllocateFrames(&mixer->state->psg, total_frames);
}

static void Mixer_End(const Mixer* const mixer, const cc_u32f numerator, const cc_u32f denominator, void (* const callback)(void *user_data, MIXER_FORMAT *audio_samples, size_t total_frames), const void* const user_data)
{
	cc_u32f i;

	const cc_u32f adjusted_output_length = Mixer_MulDiv(mixer->state->output_length, denominator, numerator);
	const size_t available_fm_frames = Mixer_Source_GetTotalAllocatedFrames(&mixer->state->fm);
	const size_t available_psg_frames = Mixer_Source_GetTotalAllocatedFrames(&mixer->state->psg);
	const cc_u32f fm_ratio = CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(available_fm_frames) / adjusted_output_length;
	const cc_u32f psg_ratio = CLOWNRESAMPLER_TO_FIXED_POINT_FROM_INTEGER(available_psg_frames) / adjusted_output_length;

	MIXER_FORMAT output_buffer[CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(48000)][MIXER_FM_CHANNEL_COUNT];
	cc_s16l (*output_buffer_pointer)[MIXER_FM_CHANNEL_COUNT] = output_buffer;

#if 0 /* This isn't necessary, right? */
	ClownResampler_LowestLevel_Configure(&mixer->state->fm_resampler, Mixer_MulDiv(mixer->state->fm_sample_rate, numerator, denominator), mixer->state->output_sample_rate, mixer->state->low_pass_filter_sample_rate);
	ClownResampler_LowestLevel_Configure(&mixer->state->psg_resampler, Mixer_MulDiv(mixer->state->psg_sample_rate, numerator, denominator), mixer->state->output_sample_rate, mixer->state->low_pass_filter_sample_rate);
#endif

	/* Resample, mix, and output the audio for this frame. */
	for (i = 0; i < adjusted_output_length; ++i)
	{
		cc_s32f fm_frame[MIXER_FM_CHANNEL_COUNT];
		cc_s32f psg_frame[MIXER_PSG_CHANNEL_COUNT];

		Mixer_Source_GetFrame(&mixer->state->fm, &mixer->constant->resampler_precomputed, fm_frame, i * fm_ratio);
		Mixer_Source_GetFrame(&mixer->state->psg, &mixer->constant->resampler_precomputed, psg_frame, i * psg_ratio);

		/* Upsample the PSG to stereo and mix it with the FM to produce the final audio. */
		/* There is no need for clamping because the samples are output at a low-enough volume to never exceed the 16-bit limit. */
		(*output_buffer_pointer)[0] = fm_frame[0] + psg_frame[0];
		(*output_buffer_pointer)[1] = fm_frame[1] + psg_frame[0];
		++output_buffer_pointer;

		if (output_buffer_pointer == &output_buffer[CC_COUNT_OF(output_buffer)])
		{
			/* The buffer is full, so flush it. */
			callback((void*)user_data, &output_buffer[0][0], CC_COUNT_OF(output_buffer));
			output_buffer_pointer = output_buffer;
		}
	}

	/* Push whatever samples remain in the output buffer. */
	callback((void*)user_data, &output_buffer[0][0], output_buffer_pointer - output_buffer);
}

#endif /* MIXER_IMPLEMENTATION */
