// (c) Copyright 2007 notaz, All rights reserved.


#include "../pico_int.h"


int SekCycleCntS68k = 0; // cycles done in this frame
int SekCycleAimS68k = 0; // cycle aim


/* context */
M68K_CONTEXT PicoCpuFS68k;


static int new_irq_level(int level)
{
   int level_new = 0, irqs;
   Pico_mcd->m.s68k_pend_ints &= ~(1 << level);
   irqs = Pico_mcd->m.s68k_pend_ints;
   irqs &= Pico_mcd->s68k_regs[0x33];
   while ((irqs >>= 1)) level_new++;

   return level_new;
}

static void SekIntAckFS68k(unsigned level)
{
   int level_new = new_irq_level(level);
   elprintf(EL_INTS, "s68kACK %i -> %i", level, level_new);

   PicoCpuFS68k.interrupts[0] = level_new;
}


PICO_INTERNAL void SekInitS68k(void)
{
   void* oldcontext = g_m68kcontext;
   g_m68kcontext = &PicoCpuFS68k;
   memset(&PicoCpuFS68k, 0, sizeof(PicoCpuFS68k));
   fm68k_init();
   PicoCpuFS68k.iack_handler = SekIntAckFS68k;
   PicoCpuFS68k.sr = 0x2704; // Z flag
   g_m68kcontext = oldcontext;
}

// Reset the 68000:
PICO_INTERNAL int SekResetS68k(void)
{
   if (Pico.rom == NULL) return 1;

   void* oldcontext = g_m68kcontext;
   g_m68kcontext = &PicoCpuFS68k;
   fm68k_reset();
   g_m68kcontext = oldcontext;

   return 0;
}

PICO_INTERNAL int SekInterruptS68k(int irq)
{
   int irqs, real_irq = 1;
   Pico_mcd->m.s68k_pend_ints |= 1 << irq;
   irqs = Pico_mcd->m.s68k_pend_ints >> 1;
   while ((irqs >>= 1)) real_irq++;

   PicoCpuFS68k.interrupts[0] = real_irq;

   return 0;
}

