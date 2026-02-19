#ifndef CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H
#define CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H

#include "core/clowncommon/clowncommon.h"
#include "core/clownmdemu.h"

typedef struct Cheat_DecodedCheat
{
	unsigned long address;
	unsigned short value;
} Cheat_DecodedCheat;

#ifdef __cplusplus
extern "C" {
#endif

void Cheat_ApplyRAMPatches(ClownMDEmu *clownmdemu);
void Cheat_ResetCheats(cc_u16l *rom, size_t rom_length);
cc_bool Cheat_AddCheat(cc_u16l *rom, size_t rom_length, unsigned int index, cc_bool enabled, const char *code);

#ifdef __cplusplus
}
#endif

#endif /* CLOWNMDEMU_FRONTEND_COMMON_CHEAT_H */
