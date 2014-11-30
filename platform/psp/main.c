// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <string.h>
#include "psp.h"
#include "psp_emu.h"
#include "menu.h"
#include "../common/menu.h"
#include "../common/emu.h"
#include "../common/config.h"

int pico_main(const char *fileName)
{
	psp_init();

	emu_prepareDefaultConfig();
	emu_ReadConfig(0, 0);
	config_readlrom(PicoConfigFile);

	emu_Init();
	menu_init();
   // moved to emu_Loop(), after CPU clock change..

   if(fileName && *fileName)
   {
      strcpy(romFileName, fileName);
      engineState = PGS_ReloadRom;
   }
   else
      engineState = PGS_Menu;

	for (;;)
	{
		switch (engineState)
		{
         case PGS_Menu:
				menu_loop();
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
	psp_finish();

	return 0;
}

