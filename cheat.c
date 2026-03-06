#include "cheat.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define CheatManager_IsROMCheat(CHEAT) (((CHEAT)->address & 0xFFFFFF) < rom_length * 2)
#define CheatManager_IsRAMCheat(CHEAT) (((CHEAT)->address & 0xFFFFFF) >= 0xE00000)

static char CheatManager_DecodeGameGenieCharacter(const char character)
{
	switch (character)
	{
		case 'A':
		case 'a':
			return 0x00;
		case 'B':
		case 'b':
			return 0x01;
		case 'C':
		case 'c':
			return 0x02;
		case 'D':
		case 'd':
			return 0x03;
		case 'E':
		case 'e':
			return 0x04;
		case 'F':
		case 'f':
			return 0x05;
		case 'G':
		case 'g':
			return 0x06;
		case 'H':
		case 'h':
			return 0x07;
		case 'J':
		case 'j':
			return 0x08;
		case 'K':
		case 'k':
			return 0x09;
		case 'L':
		case 'l':
			return 0x0A;
		case 'M':
		case 'm':
			return 0x0B;
		case 'N':
		case 'n':
			return 0x0C;
		case 'P':
		case 'p':
			return 0x0D;
		case 'R':
		case 'r':
			return 0x0E;
		case 'S':
		case 's':
			return 0x0F;
		case 'T':
		case 't':
			return 0x10;
		case 'V':
		case 'v':
			return 0x11;
		case 'W':
		case 'w':
			return 0x12;
		case 'X':
		case 'x':
			return 0x13;
		case 'Y':
		case 'y':
			return 0x14;
		case 'Z':
		case 'z':
			return 0x15;
		case '0':
			return 0x16;
		case '1':
			return 0x17;
		case '2':
			return 0x18;
		case '3':
			return 0x19;
		case '4':
			return 0x1A;
		case '5':
			return 0x1B;
		case '6':
			return 0x1C;
		case '7':
			return 0x1D;
		case '8':
			return 0x1E;
		case '9':
			return 0x1F;
	}

	return -1;
}

static cc_bool CheatManager_DecodeGameGenie(CheatManager_DecodedCheat* const cheat, const char* const code)
{
	unsigned int i;
	char encoded_characters[8];
	int total_read_characters;
	unsigned int decoded_bytes[5], current_decoded_byte = 0;
	unsigned int combiner = 0, total_combined_bits = 0;

	/* Read characters from string. */
	if (sscanf(code, " %c%c%c%c - %c%c%c%c %n",
		&encoded_characters[0],
		&encoded_characters[1],
		&encoded_characters[2],
		&encoded_characters[3],
		&encoded_characters[4],
		&encoded_characters[5],
		&encoded_characters[6],
		&encoded_characters[7],
		&total_read_characters) != 8)
		return cc_false;

	/* Make sure that the entire code is processed! */
	if ((size_t)total_read_characters != strlen(code))
		return cc_false;

	/* Decode characters to 5-bit integers and combine them into 8-bit integers. */
	for (i = 0; i < CC_COUNT_OF(encoded_characters); ++i)
	{
		const char decoded_integer = CheatManager_DecodeGameGenieCharacter(encoded_characters[i]);

		if (decoded_integer < 0)
			return cc_false;

		combiner <<= 5;
		combiner |= decoded_integer;

		total_combined_bits += 5;

		if (total_combined_bits >= 8)
		{
			total_combined_bits -= 8;
			decoded_bytes[current_decoded_byte++] = (combiner >> total_combined_bits) & 0xFF;
		}
	}

	/* Combine (and unscramble) 8-bit integers into address and value. */
	cheat->address = (unsigned long)decoded_bytes[2] << 16
		| (unsigned long)decoded_bytes[1] << 8
		| decoded_bytes[4];
	cheat->value = ((unsigned int)decoded_bytes[3] & 7) << 13
		| ((unsigned int)decoded_bytes[3] & 0xF8) << 5
		| decoded_bytes[0];

	return cc_true;
}

static cc_bool CheatManager_DecodeActionReplay(CheatManager_DecodedCheat* const cheat, const char* const code)
{
	const size_t code_length = strlen(code);

	int total_read_characters;

	/* Typical emulator format. */
	if (sscanf(code, " %6lX%*1[: ]%4hX %n", &cheat->address, &cheat->value, &total_read_characters) == 2 && (size_t)total_read_characters == code_length)
		return cc_true;

	/* Format used by the real Action Replay. */
	{
		unsigned long first_value, second_value;
		int first_value_start, first_value_end, second_value_start, second_value_end;

		if (sscanf(code, " %n%5lX%n %n%5lX%n %n", &first_value_start, &first_value, &first_value_end, &second_value_start, &second_value, &second_value_end, &total_read_characters) == 2 && (size_t)total_read_characters == code_length)
		{
			const int first_value_length = first_value_end - first_value_start;
			const int second_value_length = second_value_end - second_value_start;

			if (first_value_length == 5 && second_value_length == 5)
			{
				cheat->address = first_value << 4 | second_value >> 16;
				cheat->value = second_value & 0xFFFF;
				return cc_true;
			}
		}
	}

	return cc_false;
}

void CheatManager_UndoROMPatches(CheatManager* const manager, cc_u16l* const rom, const size_t rom_length)
{
	unsigned int i;

	for (i = manager->total_cheats; i-- != 0; )
		if (manager->cheats[i].enabled && CheatManager_IsROMCheat(&manager->cheats[i].code))
			rom[manager->cheats[i].code.address / 2] = manager->cheats[i].old_rom_value;
}

void CheatManager_ApplyROMPatches(CheatManager* const manager, cc_u16l* const rom, const size_t rom_length)
{
	unsigned int i;

	for (i = 0; i < manager->total_cheats; ++i)
	{
		if (manager->cheats[i].enabled && CheatManager_IsROMCheat(&manager->cheats[i].code))
		{
			manager->cheats[i].old_rom_value = rom[manager->cheats[i].code.address / 2];
			rom[manager->cheats[i].code.address / 2] = manager->cheats[i].code.value;
		}
	}
}

void CheatManager_ApplyRAMPatches(CheatManager* const manager, ClownMDEmu* const clownmdemu)
{
	unsigned int i;

	for (i = 0; i < manager->total_cheats; ++i)
		if (manager->cheats[i].enabled && CheatManager_IsRAMCheat(&manager->cheats[i].code))
			clownmdemu->state.m68k.ram[(manager->cheats[i].code.address / 2) % CC_COUNT_OF(clownmdemu->state.m68k.ram)] = manager->cheats[i].code.value;
}

cc_bool CheatManager_DecodeCheat(CheatManager_DecodedCheat *const decoded_cheat, const char *const code)
{
	if (CheatManager_DecodeGameGenie(decoded_cheat, code))
		return cc_true;

	if (CheatManager_DecodeActionReplay(decoded_cheat, code))
		return cc_true;

	/*libretro_callbacks.log(RETRO_LOG_ERROR, "Cheat code '%s' is in an unrecognised format.\n", code);*/
	return cc_false;
}

void CheatManager_ResetCheats(CheatManager* const manager, cc_u16l* const rom, const size_t rom_length)
{
	CheatManager_UndoROMPatches(manager, rom, rom_length);
	memset(&manager->cheats, 0, sizeof(manager->cheats));
	manager->total_cheats = 0;
}

cc_bool CheatManager_AddDecodedCheat(CheatManager* const manager, cc_u16l* const rom, const size_t rom_length, const unsigned int index, const cc_bool enabled, const CheatManager_DecodedCheat* const decoded_cheat)
{
	if (index >= CC_COUNT_OF(manager->cheats))
	{
		/*libretro_callbacks.log(RETRO_LOG_ERROR, "Cheat code %u (%s) has an index which exceeds the size of the cheat code buffer (%lu)!\n", index, code, (unsigned long)CC_COUNT_OF(cheats));*/
		return cc_false;
	}

	if (decoded_cheat->address % 2 != 0)
	{
		/*libretro_callbacks.log(RETRO_LOG_ERROR, "Cheat code %u (%s) decodes to an odd address (0x%06lX), which is invalid!\n", index, code, decoded_cheat.address);*/
		return cc_false;
	}

	CheatManager_UndoROMPatches(manager, rom, rom_length);

	/* Code is valid; add to the list. */
	manager->cheats[index].code = *decoded_cheat;
	manager->cheats[index].enabled = enabled;

	manager->total_cheats = CC_MIN(manager->total_cheats, index);

	CheatManager_ApplyROMPatches(manager, rom, rom_length);

	return cc_true;
}

cc_bool CheatManager_AddCheat(CheatManager* const manager, cc_u16l *const rom, const size_t rom_length, const unsigned int index, const cc_bool enabled, const char* const code)
{
	CheatManager_DecodedCheat decoded_cheat;

	if (!CheatManager_DecodeCheat(&decoded_cheat, code))
		return cc_false;

	/*libretro_callbacks.log(RETRO_LOG_INFO, "Cheat code %u (%s) decoded to '%06lX-%04X'.\n", index, code, decoded_cheat.address, decoded_cheat.value);*/

	return CheatManager_AddDecodedCheat(manager, rom, rom_length, index, enabled, &decoded_cheat);
}
