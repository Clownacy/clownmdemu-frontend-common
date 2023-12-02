#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "clownmdemu/clowncommon/clowncommon.h"
#include "clownmdemu/clownmdemu.h"

#ifndef MIXER_HEADER
#define MIXER_HEADER

#define CLOWNRESAMPLER_STATIC
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

typedef struct Mixer_State
{
	/* The '+1' is just a lazy way of performing a rough ceiling division. */
	cc_s16l fm_input_buffer[MIXER_FM_CHANNEL_COUNT * (1 + CC_MAX(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_NTSC), CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_PAL)))];
	size_t fm_input_buffer_write_index;
	size_t fm_input_buffer_read_index;
	ClownResampler_HighLevel_State fm_resampler;

	cc_s16l psg_input_buffer[MIXER_PSG_CHANNEL_COUNT * (1 + CC_MAX(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC), CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_PAL)))];
	size_t psg_input_buffer_write_index;
	size_t psg_input_buffer_read_index;
	ClownResampler_HighLevel_State psg_resampler;

	MIXER_FORMAT output_buffer[0x400 * MIXER_FM_CHANNEL_COUNT];
	MIXER_FORMAT *output_buffer_pointer;

	cc_u32f fm_sample_rate, psg_sample_rate, low_pass_filter_sample_rate, output_sample_rate;
} Mixer_State;

typedef struct Mixer
{
	const Mixer_Constant *constant;
	Mixer_State *state;
} Mixer;

#endif /* MIXER_HEADER */

#ifdef MIXER_IMPLEMENTATION

#define CLOWNRESAMPLER_IMPLEMENTATION
#define CLOWNRESAMPLER_STATIC
#include "clownresampler/clownresampler.h"

/* See clownresampler's documentation for more information. */
#define MIXER_DOWNSAMPLE_CAP 4

#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || (defined(__cplusplus) && __cplusplus >= 201103L)
#define MIXER_HAS_LONG_LONG
#endif

#ifndef MIXER_HAS_LONG_LONG
typedef struct Mixer_SplitInteger
{
	unsigned long splits[3];
} Mixer_SplitInteger;

static void Mixer_Subtract(Mixer_SplitInteger* const minuend, const Mixer_SplitInteger* const subtrahend)
{
	unsigned long carry;
	unsigned int i;

	carry = 0;

	for (i = CC_COUNT_OF(minuend->splits); i-- != 0; )
	{
		const unsigned long difference = minuend->splits[i] - subtrahend->splits[i] + carry;

		/* Isolate and sign-extend the overflow to obtain the new carry. */
		carry = difference >> 16;
		carry = (carry & 0x7FFF) - (carry & 0x8000);

		/* Store result. */
		minuend->splits[i] = difference % 0x10000;
	}
}

static cc_bool Mixer_GreaterThan(const Mixer_SplitInteger* const a, const Mixer_SplitInteger* const b)
{
	unsigned int i;

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
	unsigned long carry;
	unsigned int i;

	carry = 0;

	for (i = CC_COUNT_OF(value->splits); i-- != 0; )
	{
		const unsigned long new_carry = value->splits[i] >> 15;
		value->splits[i] <<= 1;
		value->splits[i] |= carry;
		carry = new_carry;
		value->splits[i] %= 0x10000;
	}
}

static void Mixer_RightShift(Mixer_SplitInteger* const value)
{
	unsigned long carry;
	unsigned int i;

	carry = 0;

	for (i = 0; i < CC_COUNT_OF(value->splits); ++i)
	{
		const unsigned long new_carry = value->splits[i] << 15;
		value->splits[i] >>= 1;
		value->splits[i] |= carry;
		carry = new_carry;
		value->splits[i] %= 0x10000;
	}
}

static void Mixer_Multiply(Mixer_SplitInteger* const output, const cc_u32f input_a, const cc_u32f input_b)
{
	const unsigned long a_upper = input_a / 0x10000;
	const unsigned long a_lower = input_a % 0x10000;
	const unsigned long b_upper = input_b / 0x10000;
	const unsigned long b_lower = input_b % 0x10000;

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
	unsigned int shift_amount;
	unsigned long result;

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
	return (unsigned long long)a * b / c;
#else
	Mixer_SplitInteger d;
	Mixer_Multiply(&d, a, b); /* Lit af. */
	return Mixer_Divide(&d, c);
#endif
}

/* TODO: Namespace these! */
static size_t Mixer_FMResamplerInputCallback(void* const user_data, cc_s16l* const buffer, const size_t buffer_size)
{
	const Mixer* const mixer = (const Mixer*)user_data;

	const size_t frames_to_do = CC_MIN(buffer_size, (mixer->state->fm_input_buffer_write_index - mixer->state->fm_input_buffer_read_index) / MIXER_FM_CHANNEL_COUNT);

	memcpy(buffer, &mixer->state->fm_input_buffer[mixer->state->fm_input_buffer_read_index], frames_to_do * sizeof(*mixer->state->fm_input_buffer) * MIXER_FM_CHANNEL_COUNT);

	mixer->state->fm_input_buffer_read_index += frames_to_do * MIXER_FM_CHANNEL_COUNT;

	return frames_to_do;
}

static size_t Mixer_PSGResamplerInputCallback(void* const user_data, cc_s16l* const buffer, const size_t buffer_size)
{
	const Mixer* const mixer = (const Mixer*)user_data;

	const size_t frames_to_do = CC_MIN(buffer_size, (mixer->state->psg_input_buffer_write_index - mixer->state->psg_input_buffer_read_index) / MIXER_PSG_CHANNEL_COUNT);

	memcpy(buffer, &mixer->state->psg_input_buffer[mixer->state->psg_input_buffer_read_index], frames_to_do * sizeof(*mixer->state->psg_input_buffer) * MIXER_PSG_CHANNEL_COUNT);

	mixer->state->psg_input_buffer_read_index += frames_to_do * MIXER_PSG_CHANNEL_COUNT;

	return frames_to_do;
}

/* There is no need for clamping in either of these callbacks because the
   samples are output low enough to never exceed the 16-bit limit. */

static cc_bool Mixer_FMResamplerOutputCallback(void* const user_data, const cc_s32f* const frame, const cc_u8f channels)
{
	const Mixer* const mixer = (const Mixer*)user_data;

	cc_u8f i;
	const cc_s32f *frame_pointer;

	(void)channels;

	/* Copy the samples directly into the output buffer. */
	frame_pointer = frame;

	for (i = 0; i < MIXER_FM_CHANNEL_COUNT; ++i)
		*mixer->state->output_buffer_pointer++ = (MIXER_FORMAT)*frame_pointer++;

	return mixer->state->output_buffer_pointer != &mixer->state->output_buffer[CC_COUNT_OF(mixer->state->output_buffer)];
}

static cc_bool Mixer_PSGResamplerOutputCallback(void* const user_data, const cc_s32f* const frame, const cc_u8f channels)
{
	const Mixer* const mixer = (const Mixer*)user_data;
	const MIXER_FORMAT sample = (MIXER_FORMAT)*frame;

	cc_u8f i;

	(void)channels;

	/* Upsample from mono to stereo and mix with the FM samples that are already in the output buffer. */
	for (i = 0; i < MIXER_FM_CHANNEL_COUNT; ++i)
		*mixer->state->output_buffer_pointer++ += sample;

	return mixer->state->output_buffer_pointer != &mixer->state->output_buffer[CC_COUNT_OF(mixer->state->output_buffer)];
}

static void Mixer_Constant_Initialise(Mixer_Constant* const constant)
{
	/* Compute clownresampler's lookup tables.*/
	ClownResampler_Precompute(&constant->resampler_precomputed);
}

static void Mixer_State_Initialise(Mixer_State* const state, const cc_u32f output_sample_rate, const cc_bool pal_mode, const cc_bool low_pass_filter)
{
	state->fm_sample_rate = pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_PAL))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_FM_SAMPLE_RATE_NTSC));
	state->psg_sample_rate = pal_mode
		? CLOWNMDEMU_MULTIPLY_BY_PAL_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_PAL_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_PAL))
		: CLOWNMDEMU_MULTIPLY_BY_NTSC_FRAMERATE(CLOWNMDEMU_DIVIDE_BY_NTSC_FRAMERATE(CLOWNMDEMU_PSG_SAMPLE_RATE_NTSC));
	state->low_pass_filter_sample_rate = low_pass_filter ? 22000 : output_sample_rate;
	state->output_sample_rate = output_sample_rate;

	ClownResampler_HighLevel_Init(&state->fm_resampler, MIXER_FM_CHANNEL_COUNT, state->fm_sample_rate * MIXER_DOWNSAMPLE_CAP, state->output_sample_rate, state->low_pass_filter_sample_rate);
	ClownResampler_HighLevel_Init(&state->psg_resampler, MIXER_PSG_CHANNEL_COUNT, state->psg_sample_rate * MIXER_DOWNSAMPLE_CAP, state->output_sample_rate, state->low_pass_filter_sample_rate);
}

static void Mixer_Begin(const Mixer* const mixer)
{
	/* Reset the audio buffers so that they can be mixed into. */
	memset(mixer->state->fm_input_buffer, 0, sizeof(mixer->state->fm_input_buffer));
	memset(mixer->state->psg_input_buffer, 0, sizeof(mixer->state->psg_input_buffer));
	mixer->state->fm_input_buffer_write_index = 0;
	mixer->state->psg_input_buffer_write_index = 0;
}

static cc_s16l* Mixer_AllocateFMSamples(const Mixer* const mixer, const size_t total_frames)
{
	cc_s16l* const allocated_samples = &mixer->state->fm_input_buffer[mixer->state->fm_input_buffer_write_index];

	mixer->state->fm_input_buffer_write_index += total_frames * MIXER_FM_CHANNEL_COUNT;

	assert(mixer->state->fm_input_buffer_write_index <= CC_COUNT_OF(mixer->state->fm_input_buffer));

	return allocated_samples;
}

static cc_s16l* Mixer_AllocatePSGSamples(const Mixer* const mixer, const size_t total_frames)
{
	cc_s16l* const allocated_samples = &mixer->state->psg_input_buffer[mixer->state->psg_input_buffer_write_index];

	mixer->state->psg_input_buffer_write_index += total_frames * MIXER_PSG_CHANNEL_COUNT;

	assert(mixer->state->psg_input_buffer_write_index <= CC_COUNT_OF(mixer->state->psg_input_buffer));

	return allocated_samples;
}

static void Mixer_End(const Mixer* const mixer, const cc_u32f numerator, const cc_u32f denominator, void (* const callback)(const void *user_data, MIXER_FORMAT *audio_samples, size_t total_frames), const void* const user_data)
{
	size_t frames_to_output;

	/* Cap the sample rate since the resamplers can only downsample by so much. */
	const cc_u32f adjusted_fm_sample_rate = CC_MIN(mixer->state->fm_sample_rate * MIXER_DOWNSAMPLE_CAP, Mixer_MulDiv(mixer->state->fm_sample_rate, numerator, denominator));
	const cc_u32f adjusted_psg_sample_rate = CC_MIN(mixer->state->psg_sample_rate * MIXER_DOWNSAMPLE_CAP, Mixer_MulDiv(mixer->state->psg_sample_rate, numerator, denominator));

	ClownResampler_HighLevel_Adjust(&mixer->state->fm_resampler, adjusted_fm_sample_rate, mixer->state->output_sample_rate, mixer->state->low_pass_filter_sample_rate);
	ClownResampler_HighLevel_Adjust(&mixer->state->psg_resampler, adjusted_psg_sample_rate, mixer->state->output_sample_rate, mixer->state->low_pass_filter_sample_rate);

	/* Resample, mix, and output the audio for this frame. */
	mixer->state->fm_input_buffer_read_index = 0;
	mixer->state->psg_input_buffer_read_index = 0;

	do
	{
		size_t total_resampled_fm_frames;
		size_t total_resampled_psg_frames;

		/* Resample the FM and PSG outputs and mix them together into a single buffer. */
		mixer->state->output_buffer_pointer = mixer->state->output_buffer;
		ClownResampler_HighLevel_Resample(&mixer->state->fm_resampler, &mixer->constant->resampler_precomputed, Mixer_FMResamplerInputCallback, Mixer_FMResamplerOutputCallback, mixer);
		total_resampled_fm_frames = (mixer->state->output_buffer_pointer - mixer->state->output_buffer) / MIXER_FM_CHANNEL_COUNT;

		mixer->state->output_buffer_pointer = mixer->state->output_buffer;
		ClownResampler_HighLevel_Resample(&mixer->state->psg_resampler, &mixer->constant->resampler_precomputed, Mixer_PSGResamplerInputCallback, Mixer_PSGResamplerOutputCallback, mixer);
		total_resampled_psg_frames = (mixer->state->output_buffer_pointer - mixer->state->output_buffer) / MIXER_FM_CHANNEL_COUNT;

		/* In case there's a mismatch between the number of FM and PSG frames, output the smaller of the two. */
		frames_to_output = CC_MIN(total_resampled_fm_frames, total_resampled_psg_frames);

		/* Push the resampled, mixed audio to the device for playback. */
		callback(user_data, mixer->state->output_buffer, frames_to_output);

		/* If the resampler has run out of data, then we're free to exit this loop. */
	} while (frames_to_output == CC_COUNT_OF(mixer->state->output_buffer) / MIXER_FM_CHANNEL_COUNT);
}

#endif /* MIXER_IMPLEMENTATION */
