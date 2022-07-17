// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_BIOS_EXPORT_H
#define _BROILER_BIOS_EXPORT_H

extern char bios_rom[0];
extern char bios_rom_end[0];

#define bios_rom_size	(bios_rom_end - bios_rom)

#endif
