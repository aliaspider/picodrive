// (c) Copyright 2006-2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h> // tolower

#include "libretro_emu.h"

#include <pico/pico_int.h>
#include <pico/patch.h>
#include <pico/cd/cue.h>

static char loadedRomFName[512] = { 0, };

// utilities
static void strlwr_(char* string)
{
   char* p;
   for (p = string; *p; p++)
      *p = (char)tolower(*p);
}

static void get_ext(char* file, char* ext)
{
   char* p;

   p = file + strlen(file) - 4;
   if (p < file) p = file;
   strncpy(ext, p, 4);
   ext[4] = 0;
   strlwr_(ext);
}

static char* biosfiles_us[] = { "us_scd1_9210", "us_scd2_9306", "SegaCDBIOS9303" };
static char* biosfiles_eu[] = { "eu_mcd1_9210", "eu_mcd2_9306", "eu_mcd2_9303"   };
static char* biosfiles_jp[] = { "jp_mcd1_9112", "jp_mcd1_9111" };

static int emu_getMainDir(char* dst, int len)
{
   if (len > 0) *dst = 0;
   return 0;
}

static int emu_findBios(int region, char** bios_file)
{
   static char bios_path[1024];
   int i, count;
   char** files;
   FILE* f = NULL;

   if (region == 4)   // US
   {
      files = biosfiles_us;
      count = sizeof(biosfiles_us) / sizeof(char*);
   }
   else if (region == 8)     // EU
   {
      files = biosfiles_eu;
      count = sizeof(biosfiles_eu) / sizeof(char*);
   }
   else if (region == 1 || region == 2)
   {
      files = biosfiles_jp;
      count = sizeof(biosfiles_jp) / sizeof(char*);
   }
   else
      return 0;

   for (i = 0; i < count; i++)
   {
      emu_getMainDir(bios_path, sizeof(bios_path));
      strcat(bios_path, files[i]);
      strcat(bios_path, ".bin");
      f = fopen(bios_path, "rb");
      if (f) break;
   }

   if (f)
   {
      lprintf("using bios: %s\n", bios_path);
      fclose(f);
      if (bios_file) *bios_file = bios_path;
      return 1;
   }
   else
   {
      lprintf("no %s BIOS files found, read docs\n",
              region != 4 ? (region == 8 ? "EU" : "JAP") : "USA");

      return 0;
   }
}

static unsigned char id_header[0x100];

/* checks if fname points to valid MegaCD image
 * if so, checks for suitable BIOS */
static int emu_cdCheck(int* pregion, char* fname_in)
{
   unsigned char buf[32];
   pm_file* cd_f;
   int region = 4; // 1: Japan, 4: US, 8: Europe
   char ext[5], *fname = fname_in;
   cue_track_type type = CT_UNKNOWN;
   cue_data_t* cue_data = NULL;

   get_ext(fname_in, ext);
   if (strcasecmp(ext, ".cue") == 0)
   {
      cue_data = cue_parse(fname_in);
      if (cue_data != NULL)
      {
         fname = cue_data->tracks[1].fname;
         type  = cue_data->tracks[1].type;
      }
      else
         return -1;
   }

   cd_f = pm_open(fname);
   if (cue_data != NULL)
      cue_destroy(cue_data);

   if (cd_f == NULL) return 0; // let the upper level handle this

   if (pm_read(buf, 32, cd_f) != 32)
   {
      pm_close(cd_f);
      return -1;
   }

   if (!strncasecmp("SEGADISCSYSTEM", (char*)buf + 0x00, 14))
   {
      if (type && type != CT_ISO)
         elprintf(EL_STATUS, ".cue has wrong type: %i", type);
      type = CT_ISO;       // Sega CD (ISO)
   }
   if (!strncasecmp("SEGADISCSYSTEM", (char*)buf + 0x10, 14))
   {
      if (type && type != CT_BIN)
         elprintf(EL_STATUS, ".cue has wrong type: %i", type);
      type = CT_BIN;       // Sega CD (BIN)
   }

   if (type == CT_UNKNOWN)
   {
      pm_close(cd_f);
      return 0;
   }

   pm_seek(cd_f, (type == CT_ISO) ? 0x100 : 0x110, SEEK_SET);
   pm_read(id_header, sizeof(id_header), cd_f);

   /* it seems we have a CD image here. Try to detect region now.. */
   pm_seek(cd_f, (type == CT_ISO) ? 0x100 + 0x10B : 0x110 + 0x10B, SEEK_SET);
   pm_read(buf, 1, cd_f);
   pm_close(cd_f);

   if (buf[0] == 0x64) region = 8; // EU
   if (buf[0] == 0xa1) region = 1; // JAP

   lprintf("detected %s Sega/Mega CD image with %s region\n",
           type == CT_BIN ? "BIN" : "ISO",
           region != 4 ? (region == 8 ? "EU" : "JAP") : "USA");

   if (pregion != NULL) *pregion = region;

   return type;
}

static void emu_shutdownMCD(void)
{
   if ((PicoAHW & PAHW_MCD) && Pico_mcd != NULL)
      Stop_CD();
   PicoAHW &= ~PAHW_MCD;
}

// note: this function might mangle rom_fname
int emu_ReloadRom(char* rom_fname)
{
   unsigned int rom_size = 0;
   char* used_rom_name = rom_fname;
   unsigned char* rom_data = NULL;
   char ext[5];
   pm_file* rom = NULL;
   int ret, cd_state, cd_region;

   lprintf("emu_ReloadRom(%s)\n", rom_fname);

   get_ext(rom_fname, ext);

   // detect wrong extensions
   if (!strcmp(ext, ".srm") || !strcmp(ext, "s.gz")
         || !strcmp(ext, ".mds"))   // s.gz ~ .mds.gz
   {
      lprintf("Not a ROM/CD selected.\n");
      return 0;
   }

   PicoPatchUnload();

   emu_shutdownMCD();

   // check for MegaCD image
   cd_state = emu_cdCheck(&cd_region, rom_fname);
   if (cd_state >= 0 && cd_state != CIT_NOT_CD)
   {
      PicoAHW |= PAHW_MCD;
      // valid CD image, check for BIOS..

      // we need to have config loaded at this point

      if (PicoRegionOverride)
      {
         cd_region = PicoRegionOverride;
         lprintf("overrided region to %s\n",
                 cd_region != 4 ? (cd_region == 8 ? "EU" : "JAP") : "USA");
      }
      if (!emu_findBios(cd_region, &used_rom_name))
      {
         // bios_help() ?
         PicoAHW &= ~PAHW_MCD;
         return 0;
      }

      get_ext(used_rom_name, ext);
   }
   else
   {
      if (PicoAHW & PAHW_MCD) Stop_CD();
      PicoAHW &= ~PAHW_MCD;
   }

   rom = pm_open(used_rom_name);
   if (!rom)
   {
      lprintf("Failed to open ROM/CD image\n");
      goto fail;
   }

   if (cd_state < 0)
   {
      lprintf("Invalid CD image\n");
      goto fail;
   }

   PicoCartUnload();

   if ((ret = PicoCartLoad(rom, &rom_data, &rom_size)))
   {
      if (ret == 2) lprintf("Out of memory\n");
      else if (ret == 3) lprintf("Read failed\n");
      else               lprintf("PicoCartLoad() failed.\n");
      goto fail;
   }
   pm_close(rom);
   rom = NULL;

   // detect wrong files (Pico crashes on very small files), also see if ROM EP is good
   if (rom_size <= 0x200 || strncmp((char*)rom_data, "Pico", 4) == 0 ||
         ((*(unsigned char*)(rom_data + 4) << 16) | (*(unsigned short*)(
                  rom_data + 6))) >= (int)rom_size)
   {
      if (rom_data) free(rom_data);
      lprintf("Not a ROM selected.\n");
      goto fail;
   }

   // load config for this ROM (do this before insert to get correct region)
   if (!(PicoAHW & PAHW_MCD))
      memcpy(id_header, rom_data + 0x100, sizeof(id_header));

   lprintf("PicoCartInsert(%p, %d);\n", rom_data, rom_size);
   if (PicoCartInsert(rom_data, rom_size))
   {
      lprintf("Failed to load ROM.\n");
      goto fail;
   }

   // insert CD if it was detected
   if (cd_state != CIT_NOT_CD)
   {
      ret = Insert_CD(rom_fname, cd_state);
      if (ret != 0)
      {
         lprintf("Insert_CD() failed, invalid CD image?\n");
         goto fail;
      }
   }

   if (PicoPatches)
   {
      PicoPatchPrepare();
      PicoPatchApply();
   }

   PicoOpt &= ~POPT_DIS_VDP_FIFO;
   if (Pico.m.pal)
      lprintf("PAL SYSTEM / 50 FPS\n");
   else
      lprintf("NTSC SYSTEM / 60 FPS\n");

   // load SRAM for this ROM

   //  emu_SaveLoadGame(1, 1);

   strncpy(loadedRomFName, rom_fname, sizeof(loadedRomFName) - 1);
   loadedRomFName[sizeof(loadedRomFName) - 1] = 0;
   return 1;

fail:
   if (rom != NULL) pm_close(rom);
   return 0;
}

//static void romfname_ext(char* dst, const char* prefix, const char* ext)
//{
//   char* p;
//   int prefix_len = 0;

//   // make save filename
//   p = loadedRomFName + strlen(loadedRomFName) - 1;
//   for (; p >= loadedRomFName && *p != PATH_SEP_C; p--);
//   p++;
//   *dst = 0;
//   if (prefix)
//   {
//      int len = emu_getMainDir(dst, 512);
//      strcpy(dst + len, prefix);
//      prefix_len = len + strlen(prefix);
//   }

//   strncpy(dst + prefix_len, p, 511 - prefix_len);
//   dst[511 - 8] = 0;
//   if (dst[strlen(dst) - 4] == '.') dst[strlen(dst) - 4] = 0;
//   if (ext) strcat(dst, ext);
//}


//void emu_setSaveStateCbs(void)
//{
//   areaRead  = (arearw*) fread;
//   areaWrite = (arearw*) fwrite;
//   areaEof   = (areaeof*) feof;
//   areaSeek  = (areaseek*) fseek;
//   areaClose = (areaclose*) fclose;
//}

//int emu_SaveLoadGame(int load, int sram)
//{
//   int ret = 0;
//   char* saveFname;

//   // make save filename
//   saveFname = emu_GetSaveFName(load, sram, state_slot);
//   if (saveFname == NULL)
//   {
//      if (!sram)
//      {
//         strcpy(noticeMsg, load ? "LOAD FAILED (missing file)" : "SAVE FAILED  ");
//         emu_noticeMsgUpdated();
//      }
//      return -1;
//   }

//   lprintf("saveLoad (%i, %i): %s\n", load, sram, saveFname);

//   if (sram)
//   {
//      FILE* sramFile;
//      int sram_size;
//      unsigned char* sram_data;
//      int truncate = 1;
//      if (PicoAHW & PAHW_MCD)
//      {
//         if (PicoOpt & POPT_EN_MCD_RAMCART)
//         {
//            sram_size = 0x12000;
//            sram_data = SRam.data;
//            if (sram_data)
//               memcpy32((int*)sram_data, (int*)Pico_mcd->bram, 0x2000 / 4);
//         }
//         else
//         {
//            sram_size = 0x2000;
//            sram_data = Pico_mcd->bram;
//            truncate  = 0; // the .brm may contain RAM cart data after normal brm
//         }
//      }
//      else
//      {
//         sram_size = SRam.end - SRam.start + 1;
//         if (Pico.m.sram_reg & 4) sram_size = 0x2000;
//         sram_data = SRam.data;
//      }
//      if (!sram_data) return 0; // SRam forcefully disabled for this game

//      if (load)
//      {
//         sramFile = fopen(saveFname, "rb");
//         if (!sramFile) return -1;
//         fread(sram_data, 1, sram_size, sramFile);
//         fclose(sramFile);
//         if ((PicoAHW & PAHW_MCD) && (PicoOpt & POPT_EN_MCD_RAMCART))
//            memcpy32((int*)Pico_mcd->bram, (int*)sram_data, 0x2000 / 4);
//      }
//      else
//      {
//         // sram save needs some special processing
//         // see if we have anything to save
//         for (; sram_size > 0; sram_size--)
//            if (sram_data[sram_size - 1]) break;

//         if (sram_size)
//         {
//            sramFile = fopen(saveFname, truncate ? "wb" : "r+b");
//            if (!sramFile) sramFile = fopen(saveFname, "wb"); // retry
//            if (!sramFile) return -1;
//            ret = fwrite(sram_data, 1, sram_size, sramFile);
//            ret = (ret != sram_size) ? -1 : 0;
//            fclose(sramFile);
//         }
//      }
//      return ret;
//   }
//   else
//   {
//      void* PmovFile = NULL;

//      if ((PmovFile = fopen(saveFname, load ? "rb" : "wb")))
//         emu_setSaveStateCbs();

//      if (PmovFile)
//      {
//         ret = PmovState(load ? 6 : 5, PmovFile);
//         areaClose(PmovFile);
//         PmovFile = 0;
//         if (load) Pico.m.dirtyPal = 1;
//      }
//      else  ret = -1;
//      if (!ret)
//         strcpy(noticeMsg, load ? "GAME LOADED  " : "GAME SAVED        ");
//      else
//      {
//         strcpy(noticeMsg, load ? "LOAD FAILED  " : "SAVE FAILED       ");
//         ret = -1;
//      }

//      emu_noticeMsgUpdated();
//      return ret;
//   }
//}

//PSP
#define VRAMOFFS_STUFF  0x00100000
#define VRAM_STUFF      ((void *) (0x44000000+VRAMOFFS_STUFF))
#define VRAM_CACHED_STUFF   ((void *) (0x04000000+VRAMOFFS_STUFF))

unsigned char* PicoDraw2FB = (unsigned char*)VRAM_CACHED_STUFF +
                             8;  // +8 to be able to skip border with 1 quadword..
//#define PICO_PEN_ADJUST_X 4
//#define PICO_PEN_ADJUST_Y 2
//static int pico_pen_x = 320/2, pico_pen_y = 240/2;

extern void amips_clut(unsigned short* dst, unsigned char* src,
                       unsigned short* pal, int count);
extern void amips_clut_6bit(unsigned short* dst, unsigned char* src,
                            unsigned short* pal, int count);

static void (*amips_clut_f)(unsigned short* dst, unsigned char* src,
                            unsigned short* pal, int count) = NULL;


// pointers must be word aligned, gammaa_val = -4..16, black_lvl = {0,1,2}
void do_pal_convert(unsigned short* dest, unsigned short* src, int gammaa_val,
                    int black_lvl);

static unsigned short __attribute__((aligned(16))) localPal[0x100];
static int dynamic_palette = 0, need_pal_upload = 0, blit_16bit_mode = 0;

static void do_pal_update(int allow_sh, int allow_as)
{
   unsigned int* dpal = (void*)localPal;
   int i;

   //for (i = 0x3f/2; i >= 0; i--)
   // dpal[i] = ((spal[i]&0x000f000f)<< 1)|((spal[i]&0x00f000f0)<<3)|((spal[i]&0x0f000f00)<<4);
   do_pal_convert(localPal, Pico.cram, 0, 0);

   Pico.m.dirtyPal = 0;
   need_pal_upload = 1;

   if (allow_sh && (Pico.video.reg[0xC] & 8)) // shadow/hilight?
   {
      // shadowed pixels
      for (i = 0x3f / 2; i >= 0; i--)
         dpal[0x20 | i] = dpal[0x60 | i] = (dpal[i] >> 1) & 0x7bcf7bcf;
      // hilighted pixels
      for (i = 0x3f; i >= 0; i--)
      {
         int t = localPal[i] & 0xf79e;
         t += 0x4208;
         if (t & 0x20) t |= 0x1e;
         if (t & 0x800) t |= 0x780;
         if (t & 0x10000) t |= 0xf000;
         t &= 0xf79e;
         localPal[0x80 | i] = (unsigned short)t;
      }
      localPal[0xe0] = 0;
      localPal[0xf0] = 0x001f;
   }
   else if (allow_as && (rendstatus & PDRAW_SPR_LO_ON_HI))
      memcpy32((int*)dpal + 0x80 / 2, (void*)localPal, 0x40 * 2 / 4);
}


static void do_slowmode_lines(int line_to)
{
   int line = 0, line_len = (Pico.video.reg[12] & 1) ? 320 : 256;
   unsigned short* dst = (unsigned short*)VRAM_STUFF + 512 * 240 / 2;
   unsigned char*  src = (unsigned char*)VRAM_CACHED_STUFF + 16;
   if (!(Pico.video.reg[1] & 8))
   {
      line = 8;
      dst += 512 * 8;
      src += 512 * 8;
   }

   for (; line < line_to; line++, dst += 512, src += 512)
      amips_clut_f(dst, src, localPal, line_len);
}

void EmuScanPrepare(void)
{
   need_pal_upload = 1;

   HighCol = (unsigned char*)VRAM_CACHED_STUFF + 8;
   if (!(Pico.video.reg[1] & 8)) HighCol += 8 * 512;

   if (dynamic_palette > 0)
      dynamic_palette--;

   if (Pico.m.dirtyPal)
      do_pal_update(1, 1);
   if ((rendstatus & PDRAW_SPR_LO_ON_HI) && !(Pico.video.reg[0xC] & 8))
      amips_clut_f = amips_clut_6bit;
   else amips_clut_f = amips_clut;
}

static int EmuScanSlowBegin(unsigned int num)
{
   if (!(Pico.video.reg[1] & 8)) num += 8;

   if (!dynamic_palette)
      HighCol = (unsigned char*)VRAM_CACHED_STUFF + num * 512 + 8;

   return 0;
}

static int EmuScanSlowEnd(unsigned int num)
{
   if (!(Pico.video.reg[1] & 8)) num += 8;

   if (Pico.m.dirtyPal)
   {
      if (!dynamic_palette)
      {
         do_slowmode_lines(num);
         dynamic_palette = 3; // last for 2 more frames
      }
      do_pal_update(1, 1);
   }

   if (dynamic_palette)
   {
      int line_len = (Pico.video.reg[12] & 1) ? 320 : 256;
      void* dst = (char*)VRAM_STUFF + 512 * 240 + 512 * 2 * num;
      amips_clut_f(dst, HighCol + 8, localPal, line_len);
   }

   return 0;
}
#include "pspgu.h"
#include "pspkernel.h"
#include "libretro.h"

static void blitscreen_clut(void)
{
   static unsigned int __attribute__((aligned(16))) d_list[256];
   sceGuStart(GU_DIRECT, d_list);

   if (dynamic_palette)
   {
      if (!blit_16bit_mode)   // the current mode is not 16bit
      {
         sceGuTexMode(GU_PSM_5650, 0, 0, 0);
         sceGuTexImage(0, 512, 512, 512, (char*)VRAM_STUFF + 512 * 240);

         blit_16bit_mode = 1;
      }
   }
   else
   {
      if (blit_16bit_mode)
      {
         sceGuClutMode(GU_PSM_5650, 0, 0xff, 0);
         sceGuTexMode(GU_PSM_T8, 0, 0, 0); // 8-bit image
         sceGuTexImage(0, 512, 512, 512, (char*)VRAM_STUFF + 16);
         blit_16bit_mode = 0;
      }

      if ((PicoOpt & 0x10) && Pico.m.dirtyPal)
         do_pal_update(0, 0);

      sceKernelDcacheWritebackAll();

      if (need_pal_upload)
      {
         need_pal_upload = 0;
         sceGuClutLoad((256 / 8), localPal); // upload 32*8 entries (256)
      }
   }
   sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
   sceGuDisable(GU_BLEND);

   sceGuFinish();

   extern retro_video_refresh_t video_cb;

   video_cb(((void*) - 1), 320, 240, 1024);

}


//static void cd_leds(void)
//{
//   unsigned int reg, col_g, col_r, *p;

//   reg = Pico_mcd->s68k_regs[0];

//   p = (unsigned int *)((short *)psp_screen + 512*2+4+2);
//   col_g = (reg & 2) ? 0x06000600 : 0;
//   col_r = (reg & 1) ? 0x00180018 : 0;
//   *p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += 512/2 - 12/2;
//   *p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r; p += 512/2 - 12/2;
//   *p++ = col_g; *p++ = col_g; p+=2; *p++ = col_r; *p++ = col_r;
//}

//static void draw_pico_ptr(void)
//{
//   unsigned char *p = (unsigned char *)VRAM_STUFF + 16;

//   // only if pen enabled and for 8bit mode
//   if (pico_inp_mode == 0 || blit_16bit_mode) return;

//   p += 512 * (pico_pen_y + PICO_PEN_ADJUST_Y);
//   p += pico_pen_x + PICO_PEN_ADJUST_X;
//   p[  -1] = 0xe0; p[   0] = 0xf0; p[   1] = 0xe0;
//   p[ 511] = 0xf0; p[ 512] = 0xf0; p[ 513] = 0xf0;
//   p[1023] = 0xe0; p[1024] = 0xf0; p[1025] = 0xe0;
//}

/* called after rendering is done, but frame emulation is not finished */
void blit1(void)
{
   if (PicoOpt & 0x10)
   {
      int i;
      unsigned char* pd;
      // clear top and bottom trash
      for (pd = PicoDraw2FB + 8, i = 8; i > 0; i--, pd += 512)
         memset32((int*)pd, 0xe0e0e0e0, 320 / 4);
      for (pd = PicoDraw2FB + 512 * 232 + 8, i = 8; i > 0; i--, pd += 512)
         memset32((int*)pd, 0xe0e0e0e0, 320 / 4);
   }

   //   if (PicoAHW & PAHW_PICO)
   //      draw_pico_ptr();

   blitscreen_clut();
}


// clears whole screen or just the notice area (in all buffers)
void clearArea(void)
{
   memset32(VRAM_CACHED_STUFF, 0xe0e0e0e0, 512 * 240 / 4);
   memset32((int*)VRAM_CACHED_STUFF + 512 * 240 / 4, 0, 512 * 240 * 2 / 4);
}

void vidResetMode(void)
{
   // slow rend.
   PicoDrawSetColorFormat(-1);
   PicoScanBegin = EmuScanSlowBegin;
   PicoScanEnd = EmuScanSlowEnd;

   localPal[0xe0] = 0;
   localPal[0xf0] = 0x001f;
   Pico.m.dirtyPal = 1;
   blit_16bit_mode = dynamic_palette = 0;
}

