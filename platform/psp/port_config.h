// port specific settings

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#define VERSION "1.51b"

#define CASE_SENSITIVE_FS 0
#define DONT_OPEN_MANY_FILES 1 // work around the stupid PSP ~10 open file limit
#define REDUCE_IO_CALLS 1      // another workaround
#define SIMPLE_WRITE_SOUND 0

// draw.c
#define USE_BGR555 1
#define OVERRIDE_HIGHCOL 1

// draw2.c
#define START_ROW  0 // which row of tiles to start rendering at?
#define END_ROW   28 // ..end
#define DRAW2_OVERRIDE_LINE_WIDTH 512

// pico.c
#ifndef __LIBRETRO__
extern void blit1(void);
#define DRAW_FINISH_FUNC blit1
#endif
#define CAN_HANDLE_240_LINES	1

// logging emu events
#define EL_LOGMASK (EL_STATUS|EL_IDLE) // (EL_STATUS|EL_ANOMALY|EL_UIO|EL_SRAMIO) // xffff

//#define dprintf(f,...) printf("%05i:%03i: " f "\n",Pico.m.frame_count,Pico.m.scanline,##__VA_ARGS__)
#define dprintf(x...)

// platform
#define PLAT_MAX_KEYS 32
#define PLAT_HAVE_JOY 0
#define PATH_SEP      "/"
#define PATH_SEP_C    '/'

#ifdef __LIBRETRO__
void lprintf(const char *fmt, ...);
#elif defined(PSP)
#include <stdio.h>
#include "psp.h"
#define lprintf printf
#endif


#endif //PORT_CONFIG_H
