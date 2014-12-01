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

int rom_loaded = 0;
char noticeMsg[64] = { 0, };
int state_slot = 0;
int config_slot = 0, config_slot_current = 0;
char loadedRomFName[512] = { 0, };
int kb_combo_keys = 0, kb_combo_acts =
                       0; // keys and actions which need button combos
int pico_inp_mode = 0;

unsigned char* movie_data = NULL;
static int movie_size = 0;

// provided by platform code:
extern void emu_noticeMsgUpdated(void);
extern int  emu_getMainDir(char* dst, int len);
extern void menu_romload_prepare(const char* rom_name);
extern void menu_romload_end(void);

void emu_noticeMsgUpdated(void)
{
   lprintf("%s\n", noticeMsg);
}

// utilities
static void strlwr_(char* string)
{
   char* p;
   for (p = string; *p; p++)
      *p = (char)tolower(*p);
}

static int try_rfn_cut(char* fname)
{
   FILE* tmp;
   char* p;

   p = fname + strlen(fname) - 1;
   for (; p > fname; p--)
      if (*p == '.') break;
   *p = 0;

   if ((tmp = fopen(fname, "rb")))
   {
      fclose(tmp);
      return 1;
   }
   return 0;
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

char* biosfiles_us[] = { "us_scd1_9210", "us_scd2_9306", "SegaCDBIOS9303" };
char* biosfiles_eu[] = { "eu_mcd1_9210", "eu_mcd2_9306", "eu_mcd2_9303"   };
char* biosfiles_jp[] = { "jp_mcd1_9112", "jp_mcd1_9111" };

int emu_getMainDir(char *dst, int len)
{
   if (len > 0) *dst = 0;
   return 0;
}

int emu_findBios(int region, char** bios_file)
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

      bios_path[strlen(bios_path) - 4] = 0;
      strcat(bios_path, ".zip");
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
int emu_cdCheck(int* pregion, char* fname_in)
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

static int extract_text(char* dest, const unsigned char* src, int len, int swab)
{
   char* p = dest;
   int i;

   if (swab) swab = 1;

   for (i = len - 1; i >= 0; i--)
   {
      if (src[i ^ swab] != ' ') break;
   }
   len = i + 1;

   for (i = 0; i < len; i++)
   {
      unsigned char s = src[i ^ swab];
      if (s >= 0x20 && s < 0x7f && s != '#' && s != '|' &&
            s != '[' && s != ']' && s != '\\')
         *p++ = s;
      else
      {
         sprintf(p, "\\%02x", s);
         p += 3;
      }
   }

   return p - dest;
}

char* emu_makeRomId(void)
{
   static char id_string[3 + 0xe * 3 + 0x3 * 3 + 0x30 * 3 + 3];
   int pos, swab = 1;

   if (PicoAHW & PAHW_MCD)
   {
      strcpy(id_string, "CD|");
      swab = 0;
   }
   else strcpy(id_string, "MD|");
   pos = 3;

   pos += extract_text(id_string + pos, id_header + 0x80, 0x0e, swab); // serial
   id_string[pos] = '|';
   pos++;
   pos += extract_text(id_string + pos, id_header + 0xf0, 0x03, swab); // region
   id_string[pos] = '|';
   pos++;
   pos += extract_text(id_string + pos, id_header + 0x50, 0x30,
                       swab); // overseas name
   id_string[pos] = 0;

   return id_string;
}

// buffer must be at least 150 byte long
void emu_getGameName(char* str150)
{
   int ret, swab = (PicoAHW & PAHW_MCD) ? 0 : 1;
   char* s, *d;

   ret = extract_text(str150, id_header + 0x50, 0x30, swab); // overseas name

   for (s = d = str150 + 1; s < str150 + ret; s++)
   {
      if (*s == 0) break;
      if (*s != ' ' || d[-1] != ' ')
         *d++ = *s;
   }
   *d = 0;
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

   // check for movie file
   if (movie_data)
   {
      free(movie_data);
      movie_data = 0;
   }

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
   rom_loaded = 0;

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

   // additional movie stuff
   if (movie_data)
   {
      if (movie_data[0x14] == '6')
         PicoOpt |=  POPT_6BTN_PAD; // 6 button pad
      else PicoOpt &= ~POPT_6BTN_PAD;
      PicoOpt |= POPT_DIS_VDP_FIFO; // no VDP fifo timing
      if (movie_data[0xF] >= 'A')
      {
         if (movie_data[0x16] & 0x80)
            PicoRegionOverride = 8;
         else
            PicoRegionOverride = 4;
         PicoReset();
         // TODO: bits 6 & 5
      }
      movie_data[0x18 + 30] = 0;
      sprintf(noticeMsg, "MOVIE: %s", (char*) &movie_data[0x18]);
   }
   else
   {
      PicoOpt &= ~POPT_DIS_VDP_FIFO;
      if (Pico.m.pal)
         strcpy(noticeMsg, "PAL SYSTEM / 50 FPS");
      else
         strcpy(noticeMsg, "NTSC SYSTEM / 60 FPS");
   }
   emu_noticeMsgUpdated();

   // load SRAM for this ROM

//  emu_SaveLoadGame(1, 1);

   strncpy(loadedRomFName, rom_fname, sizeof(loadedRomFName) - 1);
   loadedRomFName[sizeof(loadedRomFName) - 1] = 0;
   rom_loaded = 1;
   return 1;

fail:
   if (rom != NULL) pm_close(rom);
   return 0;
}


void emu_shutdownMCD(void)
{
   if ((PicoAHW & PAHW_MCD) && Pico_mcd != NULL)
      Stop_CD();
   PicoAHW &= ~PAHW_MCD;
}

static void romfname_ext(char* dst, const char* prefix, const char* ext)
{
   char* p;
   int prefix_len = 0;

   // make save filename
   p = loadedRomFName + strlen(loadedRomFName) - 1;
   for (; p >= loadedRomFName && *p != PATH_SEP_C; p--);
   p++;
   *dst = 0;
   if (prefix)
   {
      int len = emu_getMainDir(dst, 512);
      strcpy(dst + len, prefix);
      prefix_len = len + strlen(prefix);
   }

   strncpy(dst + prefix_len, p, 511 - prefix_len);
   dst[511 - 8] = 0;
   if (dst[strlen(dst) - 4] == '.') dst[strlen(dst) - 4] = 0;
   if (ext) strcat(dst, ext);
}


static int try_ropen_file(const char* fname)
{
   FILE* f;

   f = fopen(fname, "rb");
   if (f)
   {
      fclose(f);
      return 1;
   }
   return 0;
}

char* emu_GetSaveFName(int load, int is_sram, int slot)
{
   static char saveFname[512];
   char ext[16];

   if (is_sram)
   {
      romfname_ext(saveFname, (PicoAHW & 1) ? "brm"PATH_SEP : "srm"PATH_SEP,
                   (PicoAHW & 1) ? ".brm" : ".srm");
      if (load)
      {
         if (try_ropen_file(saveFname)) return saveFname;
         // try in current dir..
         romfname_ext(saveFname, NULL, (PicoAHW & PAHW_MCD) ? ".brm" : ".srm");
         if (try_ropen_file(saveFname)) return saveFname;
         return NULL; // give up
      }
   }
   else
   {
      ext[0] = 0;
      if (slot > 0 && slot < 10) sprintf(ext, ".%i", slot);
      strcat(ext, ".mds");

      romfname_ext(saveFname, "mds" PATH_SEP, ext);
      if (load)
      {
         if (try_ropen_file(saveFname)) return saveFname;
         romfname_ext(saveFname, NULL, ext);
         if (try_ropen_file(saveFname)) return saveFname;
         // no gzipped states, search for non-gzipped
         return NULL;
      }
   }

   return saveFname;
}

int emu_checkSaveFile(int slot)
{
   return emu_GetSaveFName(1, 0, slot) ? 1 : 0;
}

void emu_setSaveStateCbs(void)
{
   areaRead  = (arearw*) fread;
   areaWrite = (arearw*) fwrite;
   areaEof   = (areaeof*) feof;
   areaSeek  = (areaseek*) fseek;
   areaClose = (areaclose*) fclose;
}

int emu_SaveLoadGame(int load, int sram)
{
   int ret = 0;
   char* saveFname;

   // make save filename
   saveFname = emu_GetSaveFName(load, sram, state_slot);
   if (saveFname == NULL)
   {
      if (!sram)
      {
         strcpy(noticeMsg, load ? "LOAD FAILED (missing file)" : "SAVE FAILED  ");
         emu_noticeMsgUpdated();
      }
      return -1;
   }

   lprintf("saveLoad (%i, %i): %s\n", load, sram, saveFname);

   if (sram)
   {
      FILE* sramFile;
      int sram_size;
      unsigned char* sram_data;
      int truncate = 1;
      if (PicoAHW & PAHW_MCD)
      {
         if (PicoOpt & POPT_EN_MCD_RAMCART)
         {
            sram_size = 0x12000;
            sram_data = SRam.data;
            if (sram_data)
               memcpy32((int*)sram_data, (int*)Pico_mcd->bram, 0x2000 / 4);
         }
         else
         {
            sram_size = 0x2000;
            sram_data = Pico_mcd->bram;
            truncate  = 0; // the .brm may contain RAM cart data after normal brm
         }
      }
      else
      {
         sram_size = SRam.end - SRam.start + 1;
         if (Pico.m.sram_reg & 4) sram_size = 0x2000;
         sram_data = SRam.data;
      }
      if (!sram_data) return 0; // SRam forcefully disabled for this game

      if (load)
      {
         sramFile = fopen(saveFname, "rb");
         if (!sramFile) return -1;
         fread(sram_data, 1, sram_size, sramFile);
         fclose(sramFile);
         if ((PicoAHW & PAHW_MCD) && (PicoOpt & POPT_EN_MCD_RAMCART))
            memcpy32((int*)Pico_mcd->bram, (int*)sram_data, 0x2000 / 4);
      }
      else
      {
         // sram save needs some special processing
         // see if we have anything to save
         for (; sram_size > 0; sram_size--)
            if (sram_data[sram_size - 1]) break;

         if (sram_size)
         {
            sramFile = fopen(saveFname, truncate ? "wb" : "r+b");
            if (!sramFile) sramFile = fopen(saveFname, "wb"); // retry
            if (!sramFile) return -1;
            ret = fwrite(sram_data, 1, sram_size, sramFile);
            ret = (ret != sram_size) ? -1 : 0;
            fclose(sramFile);
         }
      }
      return ret;
   }
   else
   {
      void* PmovFile = NULL;

      if ((PmovFile = fopen(saveFname, load ? "rb" : "wb")))
         emu_setSaveStateCbs();

      if (PmovFile)
      {
         ret = PmovState(load ? 6 : 5, PmovFile);
         areaClose(PmovFile);
         PmovFile = 0;
         if (load) Pico.m.dirtyPal = 1;
      }
      else  ret = -1;
      if (!ret)
         strcpy(noticeMsg, load ? "GAME LOADED  " : "GAME SAVED        ");
      else
      {
         strcpy(noticeMsg, load ? "LOAD FAILED  " : "SAVE FAILED       ");
         ret = -1;
      }

      emu_noticeMsgUpdated();
      return ret;
   }
}

void emu_RunEventsPico(unsigned int events)
{
   if (events & (1 << 3))
   {
      pico_inp_mode++;
      if (pico_inp_mode > 2) pico_inp_mode = 0;
      switch (pico_inp_mode)
      {
      case 2:
         strcpy(noticeMsg, "Input: Pen on Pad      ");
         break;
      case 1:
         strcpy(noticeMsg, "Input: Pen on Storyware");
         break;
      case 0:
         strcpy(noticeMsg, "Input: Joytick         ");
         PicoPicohw.pen_pos[0] = PicoPicohw.pen_pos[1] = 0x8000;
         break;
      }
      emu_noticeMsgUpdated();
   }
   if (events & (1 << 4))
   {
      PicoPicohw.page--;
      if (PicoPicohw.page < 0) PicoPicohw.page = 0;
      sprintf(noticeMsg, "Page %i                 ", PicoPicohw.page);
      emu_noticeMsgUpdated();
   }
   if (events & (1 << 5))
   {
      PicoPicohw.page++;
      if (PicoPicohw.page > 6) PicoPicohw.page = 6;
      sprintf(noticeMsg, "Page %i                 ", PicoPicohw.page);
      emu_noticeMsgUpdated();
   }
}


