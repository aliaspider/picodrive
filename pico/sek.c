// This is part of Pico Library

// (c) Copyright 2004 Dave, All rights reserved.
// (c) Copyright 2006 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.


#include "pico_int.h"


int SekCycleCnt = 0; // cycles done in this frame
int SekCycleAim = 0; // cycle aim
unsigned int SekCycleCntT = 0;


/* context */
M68K_CONTEXT PicoCpuFM68k;


/* callbacks */
static void SekIntAckF68K(unsigned level)
{
   if (level == 4)
   {
      Pico.video.pending_ints  =  0;
      elprintf(EL_INTS, "hack: @ %06x [%i]", SekPc, SekCycleCnt);
   }
   else if (level == 6)
   {
      Pico.video.pending_ints &= ~0x20;
      elprintf(EL_INTS, "vack: @ %06x [%i]", SekPc, SekCycleCnt);
   }
   PicoCpuFM68k.interrupts[0] = 0;
}

PICO_INTERNAL void SekInit(void)
{
   void* oldcontext = g_m68kcontext;
   g_m68kcontext = &PicoCpuFM68k;
   memset(&PicoCpuFM68k, 0, sizeof(PicoCpuFM68k));
   fm68k_init();
   PicoCpuFM68k.iack_handler = SekIntAckF68K;
   PicoCpuFM68k.sr = 0x2704; // Z flag
   g_m68kcontext = oldcontext;
}


// Reset the 68000:
PICO_INTERNAL int SekReset(void)
{
   if (Pico.rom == NULL) return 1;

   g_m68kcontext = &PicoCpuFM68k;
   fm68k_reset();

   return 0;
}

void SekStepM68k(void)
{
   SekCycleAim = SekCycleCnt + 1;
   SekCycleCnt += fm68k_emulate(1, 0, 0);
}

PICO_INTERNAL void SekSetRealTAS(int use_real)
{
   // TODO
}


/* idle loop detection, not to be used in CD mode */

static int* idledet_addrs = NULL;
static int idledet_count = 0, idledet_bads = 0;
int idledet_start_frame = 0;

void SekInitIdleDet(void)
{
   void* tmp = realloc(idledet_addrs, 0x200 * 4);
   if (tmp == NULL)
   {
      free(idledet_addrs);
      idledet_addrs = NULL;
   }
   else
      idledet_addrs = tmp;
   idledet_count = idledet_bads = 0;
   idledet_start_frame = Pico.m.frame_count + 360;
#ifdef IDLE_STATS
   idlehit_addrs[0] = 0;
#endif

   fm68k_emulate(0, 0, 1);
}

int SekIsIdleCode(unsigned short* dst, int bytes)
{
   // printf("SekIsIdleCode %04x %i\n", *dst, bytes);
   switch (bytes)
   {
   case 2:
      if ((*dst & 0xf000) != 0x6000)     // not another branch
         return 1;
      break;
   case 4:
      if ((*dst & 0xfff8) == 0x4a10
            ||   // tst.b ($aX)      // there should be no need to wait
            (*dst & 0xfff8) == 0x4a28 || // tst.b ($xxxx,a0) // for byte change anywhere
            (*dst & 0xff3f) == 0x4a38 || // tst.x ($xxxx.w); tas ($xxxx.w)
            (*dst & 0xc1ff) == 0x0038 || // move.x ($xxxx.w), dX
            (*dst & 0xf13f) == 0xb038)   // cmp.x ($xxxx.w), dX
         return 1;
      break;
   case 6:
      if (((dst[1] & 0xe0) == 0xe0 && (  // RAM and
               *dst == 0x4a39 ||            //   tst.b ($xxxxxxxx)
               *dst == 0x4a79 ||            //   tst.w ($xxxxxxxx)
               *dst == 0x4ab9 ||            //   tst.l ($xxxxxxxx)
               (*dst & 0xc1ff) == 0x0039 || //   move.x ($xxxxxxxx), dX
               (*dst & 0xf13f) == 0xb039)) || //   cmp.x ($xxxxxxxx), dX
            *dst == 0x0838 ||            // btst $X, ($xxxx.w) [6 byte op]
            (*dst & 0xffbf) == 0x0c38)   // cmpi.{b,w} $X, ($xxxx.w)
         return 1;
      break;
   case 8:
      if (((dst[2] & 0xe0) == 0xe0 && (  // RAM and
               *dst == 0x0839 ||            //   btst $X, ($xxxxxxxx.w) [8 byte op]
               (*dst & 0xffbf) == 0x0c39)) || //   cmpi.{b,w} $X, ($xxxxxxxx)
            *dst == 0x0cb8)              // cmpi.l $X, ($xxxx.w)
         return 1;
      break;
   case 12:
      if ((*dst & 0xf1f8) == 0x3010 && // move.w (aX), dX
            (dst[1] & 0xf100) == 0x0000 && // arithmetic
            (dst[3] & 0xf100) == 0x0000) // arithmetic
         return 1;
      break;
   }

   return 0;
}

int SekRegisterIdlePatch(unsigned int pc, int oldop, int newop, void* ctx)
{
   int is_main68k = 1;
   is_main68k = ctx == &PicoCpuFM68k;

   pc &= ~0xff000000;
   elprintf(EL_IDLE, "idle: patch %06x %04x %04x %c %c #%i", pc, oldop, newop,
            (newop & 0x200) ? 'n' : 'y', is_main68k ? 'm' : 's', idledet_count);

   if (pc > Pico.romsize && !(PicoAHW & PAHW_SVP))
   {
      if (++idledet_bads > 128) return 2; // remove detector
      return 1; // don't patch
   }

   if (idledet_count >= 0x200 && (idledet_count & 0x1ff) == 0)
   {
      void* tmp = realloc(idledet_addrs, (idledet_count + 0x200) * 4);
      if (tmp == NULL) return 1;
      idledet_addrs = tmp;
   }

   if (pc < Pico.romsize)
      idledet_addrs[idledet_count++] = pc;

   return 0;
}

void SekFinishIdleDet(void)
{
   fm68k_emulate(0, 0, 2);

   while (idledet_count > 0)
   {
      unsigned short* op = (unsigned short*)&Pico.rom[idledet_addrs[--idledet_count]];
      if ((*op & 0xfd00) == 0x7100)
         *op &= 0xff, *op |= 0x6600;
      else if ((*op & 0xfd00) == 0x7500)
         *op &= 0xff, *op |= 0x6700;
      else if ((*op & 0xfd00) == 0x7d00)
         *op &= 0xff, *op |= 0x6000;
      else
         elprintf(EL_STATUS | EL_IDLE, "idle: don't know how to restore %04x", *op);
   }
}
