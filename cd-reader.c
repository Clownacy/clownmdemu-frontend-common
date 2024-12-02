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

	state->clowncd = ClownCD_OpenAlreadyOpen(stream, path, callbacks);
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
	ClownCD_CueTrackType track_type;

	if (!CDReader_IsOpen(state))
		return cc_false;

	track_type = ClownCD_SeekTrackIndex(&state->clowncd, 1, 1);

	if (track_type != CLOWNCD_CUE_TRACK_MODE1_2048 && track_type != CLOWNCD_CUE_TRACK_MODE1_2352)
		return cc_false;

	return ClownCD_SeekSector(&state->clowncd, sector_index);
}

cc_bool CDReader_ReadSector(CDReader_State* const state, CDReader_Sector* const sector)
{
	cc_bool success = cc_false;

	if (CDReader_IsOpen(state) && ClownCD_ReadSector(&state->clowncd, *sector))
		success = cc_true;

	if (!success)
		memset(sector, 0, sizeof(*sector));

	return success;
}

cc_bool CDReader_ReadSectorAt(CDReader_State* const state, CDReader_Sector* const sector, const CDReader_SectorIndex sector_index)
{
	cc_bool success = cc_false;

	if (CDReader_IsOpen(state))
	{
		CDReader_StateBackup backup;
		CDReader_GetStateBackup(state, &backup);

		if (CDReader_SeekToSector(state, sector_index) && ClownCD_ReadSector(&state->clowncd, *sector))
			success = cc_true;

		if (!CDReader_SetStateBackup(state, &backup))
			success = cc_false;
	}

	if (!success)
		memset(sector, 0, sizeof(*sector));

	return success;
}

cc_bool CDReader_PlayAudio(CDReader_State* const state, const CDReader_TrackIndex track_index, const CDReader_PlaybackSetting setting)
{
	if (!CDReader_IsOpen(state))
		return cc_false;

	state->audio_playing = cc_false;

	if (ClownCD_SeekTrackIndex(&state->clowncd, track_index, 1) != CLOWNCD_CUE_TRACK_AUDIO)
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

void CDReader_GetStateBackup(CDReader_State* const state, CDReader_StateBackup* const backup)
{
	backup->track_index = state->clowncd.track.current_track;
	backup->sector_index = state->clowncd.track.current_sector;
	backup->frame_index = state->clowncd.track.current_frame;
	backup->playback_setting = state->playback_setting;
	backup->audio_playing = state->audio_playing;
}

cc_bool CDReader_SetStateBackup(CDReader_State* const state, const CDReader_StateBackup* const backup)
{
	if (!CDReader_IsOpen(state))
		return cc_false;

	if (ClownCD_SetState(&state->clowncd, backup->track_index, 1, backup->sector_index, backup->frame_index) == CLOWNCD_CUE_TRACK_INVALID)
		return cc_false;

	state->playback_setting = backup->playback_setting;
	state->audio_playing = backup->audio_playing;

	return cc_true;
}

cc_bool CDReader_IsMegaCDGame(CDReader_State* const state)
{
	static const char disc_identifier[] = {'S', 'E', 'G', 'A', 'D', 'I', 'S', 'C', 'S', 'Y', 'S', 'T', 'E', 'M'};
	CDReader_Sector first_sector;

	CDReader_ReadSectorAt(state, &first_sector, 0);
	return memcmp(first_sector, disc_identifier, sizeof(disc_identifier)) == 0;
}
