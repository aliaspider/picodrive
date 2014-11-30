// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <string.h>
#include "psp.h"
#include "emu.h"
#include "menu.h"
#include "../common/menu.h"
#include "../common/emu.h"
#include "../common/config.h"
#include "../common/lprintf.h"

#ifdef GPROF
#include <pspprof.h>
#endif

#ifdef GCOV
#include <stdio.h>
#include <stdlib.h>

void dummy(void)
{
	engineState = atoi(romFileName);
	setbuf(NULL, NULL);
	getenv(NULL);
}
#endif

int pico_main(void)
{
	psp_init();

	emu_prepareDefaultConfig();
	emu_ReadConfig(0, 0);
	config_readlrom(PicoConfigFile);

	emu_Init();
	menu_init();
   // moved to emu_Loop(), after CPU clock change..

	engineState = PGS_Menu;

	for (;;)
	{
		switch (engineState)
		{
			case PGS_Menu:
#ifndef GPROF
				menu_loop();
#else
				strcpy(romFileName, loadedRomFName);
				engineState = PGS_ReloadRom;
#endif
				break;

			case PGS_ReloadRom:
				if (emu_ReloadRom(romFileName)) {
               engineState = PGS_Running;
				} else {
					lprintf("PGS_ReloadRom == 0\n");
					engineState = PGS_Menu;
				}
				break;

			case PGS_Suspending:
				while (engineState == PGS_Suspending)
					psp_wait_suspend();
				break;

			case PGS_SuspendWake:
				psp_unhandled_suspend = 0;
				psp_resume_suspend();
				emu_HandleResume();
				engineState = engineStateSuspend;
				break;

			case PGS_RestartRun:
				engineState = PGS_Running;

			case PGS_Running:
				if (psp_unhandled_suspend) {
					psp_unhandled_suspend = 0;
					psp_resume_suspend();
					emu_HandleResume();
					break;
				}
				emu_Loop();
#ifdef GPROF
				goto endloop;
#endif
				break;

			case PGS_Quit:
				goto endloop;

			default:
				lprintf("engine got into unknown state (%i), exitting\n", engineState);
				goto endloop;
		}
	}

	endloop:

	emu_Deinit();
#ifdef GPROF
	gprof_cleanup();
#endif
#ifndef GCOV
	psp_finish();
#endif

	return 0;
}

