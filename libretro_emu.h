#ifndef LIBRETRO_EMU_H
#define LIBRETRO_EMU_H
// (c) Copyright 2006-2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include "port_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int  emu_ReloadRom(char* rom_fname);
void EmuScanPrepare(void);
void clearArea(void);
void vidResetMode(void);

#ifdef __cplusplus
}
#endif


#endif // LIBRETRO_EMU_H
