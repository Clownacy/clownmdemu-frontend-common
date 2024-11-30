#ifndef INCLUDE_GUARD_FD584506_85DF_4C8A_8CF8_18F0A76BBFD2
#define INCLUDE_GUARD_FD584506_85DF_4C8A_8CF8_18F0A76BBFD2

#include <stddef.h>

#include "clowncd/clowncd.h"

#define CDREADER_SECTOR_SIZE 2048

typedef cc_u32f CDReader_SectorIndex;
typedef cc_u16f CDReader_TrackIndex;
typedef size_t  CDReader_FrameIndex;
typedef cc_u8l  CDReader_Sector[CDREADER_SECTOR_SIZE];

typedef enum CDReader_PlaybackSetting
{
	CDREADER_PLAYBACK_ALL,
	CDREADER_PLAYBACK_ONCE,
	CDREADER_PLAYBACK_REPEAT
} CDReader_PlaybackSetting;

typedef struct CDReader_State
{
	ClownCD clowncd;
	cc_bool open;
	CDReader_PlaybackSetting playback_setting;
	cc_bool audio_playing;
} CDReader_State;

typedef struct CDReader_StateBackup
{
	CDReader_TrackIndex track_index;
	CDReader_SectorIndex sector_index;
	CDReader_FrameIndex frame_index;
	CDReader_PlaybackSetting playback_setting;
	cc_bool audio_playing;
} CDReader_StateBackup;

#ifdef __cplusplus
extern "C" {
#endif

void CDReader_Initialise(CDReader_State *state);
void CDReader_Deinitialise(CDReader_State *state);
void CDReader_Open(CDReader_State *state, void *stream, const char *path, const ClownCD_FileCallbacks *callbacks);
void CDReader_Close(CDReader_State *state);
cc_bool CDReader_IsOpen(const CDReader_State *state);
cc_bool CDReader_SeekToSector(CDReader_State *state, CDReader_SectorIndex sector_index);
cc_bool CDReader_SeekToFrame(CDReader_State *state, CDReader_FrameIndex frame_index);
void CDReader_ReadSector(CDReader_State *state, CDReader_Sector *sector);
void CDReader_ReadSectorAt(CDReader_State *state, CDReader_Sector *sector, CDReader_SectorIndex sector_index);
cc_bool CDReader_PlayAudio(CDReader_State *state, CDReader_TrackIndex track_index, CDReader_PlaybackSetting setting);
cc_u32f CDReader_ReadAudio(CDReader_State *state, cc_s16l *sample_buffer, cc_u32f total_frames);
void CDReader_GetStateBackup(CDReader_State *state, CDReader_StateBackup *backup);
cc_bool CDReader_SetStateBackup(CDReader_State *state, const CDReader_StateBackup *backup);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_GUARD_FD584506_85DF_4C8A_8CF8_18F0A76BBFD2 */
