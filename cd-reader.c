#include "cd-reader.h"

#include <string.h>

void CDReader_Initialise(CDReader_State* const state)
{
	state->open = cc_false;
	state->playback_setting = CDREADER_PLAYBACK_ALL;
	state->audio_playing = cc_false;
}

void CDReader_Deinitialise(CDReader_State* const state)
{
	CDReader_Close(state);
}

void CDReader_Open(CDReader_State* const state, void* const stream, const char* const path, const ClownCD_FileCallbacks* const callbacks)
{
	if (CDReader_IsOpen(state))
		CDReader_Close(state);

	ClownCD_OpenAlreadyOpen(&state->clowncd, stream, path, callbacks);
	state->open = cc_true;
	state->audio_playing = cc_false;
}

void CDReader_Close(CDReader_State* const state)
{
	if (!CDReader_IsOpen(state))
		return;

	ClownCD_Close(&state->clowncd);
	state->open = cc_false;
}

cc_bool CDReader_IsOpen(const CDReader_State* const state)
{
	return state->open;
}

cc_bool CDReader_SeekToSector(CDReader_State* const state, const CDReader_SectorIndex sector_index)
{
	if (!CDReader_IsOpen(state))
		return cc_false;

	if (!ClownCD_SeekTrackIndex(&state->clowncd, 1, 1))
		return cc_false;

	return ClownCD_SeekSector(&state->clowncd, sector_index);
}

static size_t AttemptReadSector(CDReader_State* const state, cc_u16l* const buffer)
{
	cc_u16f i;

	if (!ClownCD_BeginSectorStream(&state->clowncd))
		return 0;

	for (i = 0; i < CDREADER_SECTOR_SIZE / 2; ++i)
	{
		unsigned char bytes[2];

		/* Sanely handle a partial read. */
		bytes[1] = 0;

		if (ClownCD_ReadSectorStream(&state->clowncd, bytes, CC_COUNT_OF(bytes)) == 0)
			break;

		buffer[i] = (cc_u16l)bytes[0] << 8 | bytes[1];
	}

	ClownCD_EndSectorStream(&state->clowncd);

	return i;
}

cc_bool CDReader_ReadSector(CDReader_State* const state, cc_u16l* const buffer)
{
	size_t words_read = 0;

	if (CDReader_IsOpen(state))
		words_read = AttemptReadSector(state, buffer);

	memset(buffer + words_read, 0, (CDREADER_SECTOR_SIZE / 2 - words_read) * sizeof(cc_u16l));

	return words_read != 0;
}

cc_bool CDReader_PlayAudio(CDReader_State* const state, const CDReader_TrackIndex track_index, const CDReader_PlaybackSetting setting)
{
	if (!CDReader_IsOpen(state))
		return cc_false;

	state->audio_playing = cc_false;

	if (!ClownCD_SeekTrackIndex(&state->clowncd, track_index, 1))
		return cc_false;

	state->audio_playing = cc_true;
	state->playback_setting = setting;

	return cc_true;
}

cc_bool CDReader_SeekToFrame(CDReader_State* const state, const CDReader_FrameIndex frame_index)
{
	if (!ClownCD_SeekAudioFrame(&state->clowncd, frame_index))
	{
		state->audio_playing = cc_false;
		return cc_false;
	}

	return cc_true;
}

cc_u32f CDReader_ReadAudio(CDReader_State* const state, cc_s16l* const sample_buffer, const cc_u32f total_frames)
{
	cc_u32f frames_read = 0;

	if (!CDReader_IsOpen(state))
		return 0;

	if (!state->audio_playing)
		return 0;

	while (frames_read != total_frames)
	{
		frames_read += ClownCD_ReadFrames(&state->clowncd, &sample_buffer[frames_read * 2], total_frames - frames_read);

		if (frames_read != total_frames)
		{
			switch (state->playback_setting)
			{
				case CDREADER_PLAYBACK_ALL:
					if (!CDReader_PlayAudio(state, state->clowncd.track.current_track + 1, state->playback_setting))
						state->audio_playing = cc_false;
					break;

				case CDREADER_PLAYBACK_ONCE:
					state->audio_playing = cc_false;
					/* Fallthrough */
				case CDREADER_PLAYBACK_REPEAT:
					if (!CDReader_SeekToFrame(state, 0))
						state->audio_playing = cc_false;
					break;
			}

			if (!state->audio_playing)
				break;
		}
	}

	return frames_read;
}

void CDReader_SaveState(const CDReader_State* const state, CDReader_StateBackup* const backup)
{
	backup->track_index = state->clowncd.track.current_track;
	backup->frame_index = state->clowncd.track.current_frame;
	backup->playback_setting = state->playback_setting;
	backup->audio_playing = state->audio_playing;
}

cc_bool CDReader_LoadState(CDReader_State* const state, const CDReader_StateBackup* const backup)
{
	if (!CDReader_IsOpen(state))
		return cc_false;

	if (!ClownCD_SetState(&state->clowncd, backup->track_index, 1, backup->frame_index))
		return cc_false;

	state->playback_setting = backup->playback_setting;
	state->audio_playing = backup->audio_playing;

	return cc_true;
}

cc_bool CDReader_ReadMegaCDHeaderSector(CDReader_State* const state, unsigned char* const buffer)
{
	cc_bool success = cc_false;

	if (CDReader_IsOpen(state))
	{
		CDReader_StateBackup backup;
		CDReader_SaveState(state, &backup);

		if (CDReader_SeekToSector(state, 0) && ClownCD_ReadSector(&state->clowncd, buffer))
			success = cc_true;

		CDReader_LoadState(state, &backup);
	}

	if (!success)
		memset(buffer, 0, CDREADER_SECTOR_SIZE);

	return success;
}

cc_bool CDReader_IsMegaCDGame(CDReader_State* const state)
{
	static const unsigned char disc_identifier[] = {'S', 'E', 'G', 'A', 'D', 'I', 'S', 'C', 'S', 'Y', 'S', 'T', 'E', 'M'};
	unsigned char first_sector[CDREADER_SECTOR_SIZE];

	CDReader_ReadMegaCDHeaderSector(state, first_sector);
	return memcmp(first_sector, disc_identifier, sizeof(disc_identifier)) == 0;
}

cc_bool CDReader_IsDefinitelyACD(CDReader_State* const state)
{
	return state->clowncd.type != CLOWNCD_DISC_RAW_2048 || CDReader_IsMegaCDGame(state);
}
