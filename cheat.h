#ifndef CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H
#define CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H

#include "core/clowncommon/clowncommon.h"
#include "core/clownmdemu.h"

typedef struct CheatManager_DecodedCheat
{
	unsigned long address;
	unsigned short value;
} CheatManager_DecodedCheat;

typedef struct CheatManager
{
	struct
	{
		CheatManager_DecodedCheat code;
		cc_u16l old_rom_value;
		cc_bool enabled;
	} cheats[0x100];

	unsigned int total_cheats;
} CheatManager;

#ifdef __cplusplus
extern "C" {
#endif

void CheatManager_UndoROMPatches(CheatManager *manager, cc_u16l *rom, size_t rom_length);
void CheatManager_ApplyROMPatches(CheatManager *manager, cc_u16l *rom, size_t rom_length);
void CheatManager_ApplyRAMPatches(CheatManager *manager, ClownMDEmu *clownmdemu);

cc_bool CheatManager_DecodeCheat(CheatManager_DecodedCheat *decoded_cheat, const char *code);

void CheatManager_ResetCheats(CheatManager *manager, cc_u16l *rom, size_t rom_length);
cc_bool CheatManager_AddDecodedCheat(CheatManager *manager, cc_u16l *rom, size_t rom_length, unsigned int index, cc_bool enabled, const CheatManager_DecodedCheat *decoded_cheat);
cc_bool CheatManager_AddCheat(CheatManager *manager, cc_u16l *rom, size_t rom_length, unsigned int index, cc_bool enabled, const char *code);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <cstddef>

class CheatManagerCXX : private CheatManager
{
public:
	CheatManagerCXX()
		: CheatManager({})
	{}

	void UndoROMPatches(cc_u16l* const rom, const std::size_t rom_length)
	{
		CheatManager_UndoROMPatches(this, rom, rom_length);
	}

	void ApplyROMPatches(cc_u16l* const rom, const std::size_t rom_length)
	{
		CheatManager_ApplyROMPatches(this, rom, rom_length);
	}

	void ApplyRAMPatches(ClownMDEmu* const clownmdemu)
	{
		CheatManager_ApplyRAMPatches(this, clownmdemu);
	}

	static bool DecodeCheat(CheatManager_DecodedCheat* const decoded_cheat, const char* const code)
	{
		return CheatManager_DecodeCheat(decoded_cheat, code);
	}

	void ResetCheats(cc_u16l* const rom, const std::size_t rom_length)
	{
		CheatManager_ResetCheats(this, rom, rom_length);
	}

	bool AddDecodedCheat(cc_u16l *const rom, const std::size_t rom_length, const unsigned int index, const bool enabled, const CheatManager_DecodedCheat *const decoded_cheat)
	{
		return CheatManager_AddDecodedCheat(this, rom, rom_length, index, enabled, decoded_cheat);
	}

	bool AddCheat(cc_u16l* const rom, const std::size_t rom_length, const unsigned int index, const bool enabled, const char* const code)
	{
		return CheatManager_AddCheat(this, rom, rom_length, index, enabled, code);
	}
};

#endif

#endif /* CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H */
