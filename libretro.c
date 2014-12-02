/*
 * libretro core glue for PicoDrive
 * (C) notaz, 2013
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>


#include <pico/pico_int.h>
#include "platform/psp/port_config.h"
//#include <pico/state.h>
#include "../common/input.h"
#include "libretro.h"

static retro_log_printf_t log_cb;
retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb;

#ifdef _MSC_VER
static short sndBuffer[2 * 44100 / 50];
#else
static short __attribute__((aligned(4))) sndBuffer[2 * 44100 / 50];
#endif

static void snd_write(int len);

#ifdef _WIN32
#define SLASH '\\'
#else
#define SLASH '/'
#endif

/* functions called by the core */

void lprintf(const char* fmt, ...)
{
   char buffer[256];
   va_list ap;
   va_start(ap, fmt);
   vsprintf(buffer, fmt, ap);
   va_end(ap);
   /* TODO - add 'level' param for warning/error messages? */
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s", buffer);
}


/* libretro */
void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] =
   {
      //{ "region", "Region; Auto|NTSC|PAL" },
      { "picodrive_input1", "Input device 1; 3 button pad|6 button pad|None" },
      { "picodrive_input2", "Input device 2; 3 button pad|6 button pad|None" },
      { "picodrive_sprlim", "No sprite limit; disabled|enabled" },
      { "picodrive_ramcart", "MegaCD RAM cart; disabled|enabled" },
#ifdef DRC_SH2
      { "picodrive_drc", "Dynamic recompilers; enabled|disabled" },
#endif
      { NULL, NULL },
   };

   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}
void retro_set_audio_sample(retro_audio_sample_t cb)
{
   (void)cb;
}
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}
void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_get_system_info(struct retro_system_info* info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "PicoDrive 1.51b";
   info->library_version = VERSION;
   info->valid_extensions = "bin|gen|smd|md|cue|iso|sms";
   info->need_fullpath = true;
}

void retro_get_system_av_info(struct retro_system_av_info* info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = Pico.m.pal ? 50 : 60;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = 320;
   info->geometry.base_height  = 240;
   info->geometry.max_width    = 320;
   info->geometry.max_height   = 240;
   info->geometry.aspect_ratio = 0.0f;
}

/* savestates */
struct savestate_state
{
   const char* load_buf;
   char* save_buf;
   size_t size;
   size_t pos;
};

size_t state_read(void* p, size_t size, size_t nmemb, void* file)
{
   struct savestate_state* state = file;
   size_t bsize = size * nmemb;

   if (state->pos + bsize > state->size)
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "savestate error: %u/%u\n",
                state->pos + bsize, state->size);
      bsize = state->size - state->pos;
      if ((int)bsize <= 0)
         return 0;
   }

   memcpy(p, state->load_buf + state->pos, bsize);
   state->pos += bsize;
   return bsize;
}

size_t state_write(void* p, size_t size, size_t nmemb, void* file)
{
   struct savestate_state* state = file;
   size_t bsize = size * nmemb;

   if (state->pos + bsize > state->size)
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "savestate error: %u/%u\n",
                state->pos + bsize, state->size);
      bsize = state->size - state->pos;
      if ((int)bsize <= 0)
         return 0;
   }

   memcpy(state->save_buf + state->pos, p, bsize);
   state->pos += bsize;
   return bsize;
}

size_t state_skip(void* p, size_t size, size_t nmemb, void* file)
{
   struct savestate_state* state = file;
   size_t bsize = size * nmemb;

   state->pos += bsize;
   return bsize;
}

size_t state_eof(void* file)
{
   struct savestate_state* state = file;

   return state->pos >= state->size;
}

int state_fseek(void* file, long offset, int whence)
{
   struct savestate_state* state = file;

   switch (whence)
   {
   case SEEK_SET:
      state->pos = offset;
      break;
   case SEEK_CUR:
      state->pos += offset;
      break;
   case SEEK_END:
      state->pos = state->size + offset;
      break;
   }
   return (int)state->pos;
}

/* savestate sizes vary wildly depending if cd/32x or
 * carthw is active, so run the whole thing to get size */
size_t retro_serialize_size(void)
{
   // struct savestate_state state = { 0, };
   // int ret;

   // ret = PicoStateFP(&state, 1, NULL, state_skip, NULL, state_fseek);
   // if (ret != 0)
   return 0;

   // return state.pos;
}

bool retro_serialize(void* data, size_t size)
{
   // struct savestate_state state = { 0, };
   // int ret;

   // state.save_buf = data;
   // state.size = size;
   // state.pos = 0;

   // ret = PicoStateFP(&state, 1, NULL, state_write,
   //    NULL, state_fseek);
   // return ret == 0;
   return false;
}

bool retro_unserialize(const void* data, size_t size)
{
   // struct savestate_state state = { 0, };
   // int ret;

   // state.load_buf = data;
   // state.size = size;
   // state.pos = 0;

   // ret = PicoStateFP(&state, 0, state_read, NULL,
   //    state_eof, state_fseek);
   // return ret == 0;
   return false;
}

/* cheats - TODO */
void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char* code)
{
}

/* multidisk support */
static bool disk_ejected;
static unsigned int disk_current_index;
static unsigned int disk_count;
static struct disks_state
{
   char* fname;
} disks[8];

static bool disk_set_eject_state(bool ejected)
{
   // TODO?
   disk_ejected = ejected;
   return true;
}

static bool disk_get_eject_state(void)
{
   return disk_ejected;
}

static unsigned int disk_get_image_index(void)
{
   return disk_current_index;
}

static bool disk_set_image_index(unsigned int index)
{
   cd_img_type cd_type;
   int ret;

   if (index >= sizeof(disks) / sizeof(disks[0]))
      return false;

   if (disks[index].fname == NULL)
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "missing disk #%u\n", index);

      // RetroArch specifies "no disk" with index == count,
      // so don't fail here..
      disk_current_index = index;
      return true;
   }

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "switching to disk %u: \"%s\"\n", index,
             disks[index].fname);

   // ret = -1;
   // cd_type = PicoCdCheck(disks[index].fname, NULL);
   //   cd_type = emu_cdCheck(NULL, disks[index].fname);
   // if (cd_type != CIT_NOT_CD)
   //    ret = cdd_load(disks[index].fname, cd_type);
   // if (ret != 0) {
   //      if (log_cb)
   //         log_cb(RETRO_LOG_ERROR, "Load failed, invalid CD image?\n");
   //    return 0;
   // }

   disk_current_index = index;
   return true;
}

static unsigned int disk_get_num_images(void)
{
   return disk_count;
}

static bool disk_replace_image_index(unsigned index,
                                     const struct retro_game_info* info)
{
   bool ret = true;

   if (index >= sizeof(disks) / sizeof(disks[0]))
      return false;

   if (disks[index].fname != NULL)
      free(disks[index].fname);
   disks[index].fname = NULL;

   if (info != NULL)
   {
      disks[index].fname = strdup(info->path);
      if (index == disk_current_index)
         ret = disk_set_image_index(index);
   }

   return ret;
}

static bool disk_add_image_index(void)
{
   if (disk_count >= sizeof(disks) / sizeof(disks[0]))
      return false;

   disk_count++;
   return true;
}

static struct retro_disk_control_callback disk_control =
{
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

static void disk_tray_open(void)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "cd tray open\n");
   disk_ejected = 1;
}

static void disk_tray_close(void)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "cd tray close\n");
   disk_ejected = 0;
}


static const char* const biosfiles_us[] =
{
   "us_scd2_9306", "SegaCDBIOS9303", "us_scd1_9210", "bios_CD_U"
};
static const char* const biosfiles_eu[] =
{
   "eu_mcd2_9306", "eu_mcd2_9303", "eu_mcd1_9210", "bios_CD_E"
};
static const char* const biosfiles_jp[] =
{
   "jp_mcd2_921222", "jp_mcd1_9112", "jp_mcd1_9111", "bios_CD_J"
};

static void make_system_path(char* buf, size_t buf_size,
                             const char* name, const char* ext)
{
   const char* dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
      snprintf(buf, buf_size, "%s%c%s%s", dir, SLASH, name, ext);
   else
      snprintf(buf, buf_size, "%s%s", name, ext);
}

static const char* find_bios(int* region, const char* cd_fname)
{
   const char* const* files;
   static char path[256];
   int i, count;
   FILE* f = NULL;

   if (*region == 4)   // US
   {
      files = biosfiles_us;
      count = sizeof(biosfiles_us) / sizeof(char*);
   }
   else if (*region == 8)     // EU
   {
      files = biosfiles_eu;
      count = sizeof(biosfiles_eu) / sizeof(char*);
   }
   else if (*region == 1 || *region == 2)
   {
      files = biosfiles_jp;
      count = sizeof(biosfiles_jp) / sizeof(char*);
   }
   else
      return NULL;

   for (i = 0; i < count; i++)
   {
      make_system_path(path, sizeof(path), files[i], ".bin");
      f = fopen(path, "rb");
      if (f != NULL)
         break;

      make_system_path(path, sizeof(path), files[i], ".zip");
      f = fopen(path, "rb");
      if (f != NULL)
         break;
   }

   if (f != NULL)
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "using bios: %s\n", path);
      fclose(f);
      return path;
   }

   return NULL;
}
#include "libretro_emu.h"
bool retro_load_game(const struct retro_game_info* info)
{
   static char carthw_path[256];
   size_t i;

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

   for (i = 0; i < sizeof(disks) / sizeof(disks[0]); i++)
   {
      if (disks[i].fname != NULL)
      {
         free(disks[i].fname);
         disks[i].fname = NULL;
      }
   }



   char romFileName[1024];
   strcpy(romFileName, info->path);
   emu_ReloadRom(romFileName);

   disk_current_index = 0;
   disk_count = 1;
   disks[0].fname = strdup(info->path);

   make_system_path(carthw_path, sizeof(carthw_path), "carthw", ".cfg");


//   unsigned int rom_size = 0;
//   unsigned char* rom_data = NULL;
//   pm_file* rom = pm_open(info->path);
//   PicoCartUnload();
//   PicoCartLoad(rom, &rom_data, &rom_size);
//   pm_close(rom);
//   rom = NULL;
   /* new */

   PicoReset();

   //   PicoLoopPrepare();

   vidResetMode();
   clearArea();
   Pico.m.dirtyPal = 1;


   if (PicoAHW & PAHW_MCD)
   {
      // prepare CD buffer
      PicoCDBufferInit();
   }

   PicoWriteSound = snd_write;
   memset(sndBuffer, 0, sizeof(sndBuffer));
   PsndOut = sndBuffer;
   PsndRerate(0);

   g_m68kcontext = &PicoCpuFM68k;
//   g_m68kcontext = &PicoCpuFS68k;
   return true;
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info* info, size_t num_info)
{
   return false;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return Pico.m.pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

void* retro_get_memory_data(unsigned id)
{
   if (id != RETRO_MEMORY_SAVE_RAM)
      return NULL;

   if (PicoAHW & PAHW_MCD)
      return Pico_mcd->bram;
   else
      return SRam.data;
}

size_t retro_get_memory_size(unsigned id)
{
   // unsigned int i;
   // int sum;

   // if (id != RETRO_MEMORY_SAVE_RAM)
   //    return 0;

   // if (PicoAHW & PAHW_MCD)
   //    // bram
   //    return 0x2000;

   // if (Pico.m.frame_count == 0)
   //    return SRam.size;

   // // if game doesn't write to sram, don't report it to
   // // libretro so that RA doesn't write out zeroed .srm
   // for (i = 0, sum = 0; i < SRam.size; i++)
   //    sum |= SRam.data[i];

   // return (sum != 0) ? SRam.size : 0;
   return 0;
}

void retro_reset(void)
{
   PicoReset();
}

static const unsigned short retro_pico_map[] =
{
   // 1 << GBTN_B,
   // 1 << GBTN_A,
   // 1 << GBTN_MODE,
   // 1 << GBTN_START,
   // 1 << GBTN_UP,
   // 1 << GBTN_DOWN,
   // 1 << GBTN_LEFT,
   // 1 << GBTN_RIGHT,
   // 1 << GBTN_C,
   // 1 << GBTN_Y,
   // 1 << GBTN_X,
   // 1 << GBTN_Z,
   0,
};
#define RETRO_PICO_MAP_LEN (sizeof(retro_pico_map) / sizeof(retro_pico_map[0]))

static void snd_write(int len)
{
   audio_batch_cb(PsndOut, len);
}

//static enum input_device input_name_to_val(const char *name)
//{
// if (strcmp(name, "3 button pad") == 0)
//    return PICO_INPUT_PAD_3BTN;
// if (strcmp(name, "6 button pad") == 0)
//    return PICO_INPUT_PAD_6BTN;
// if (strcmp(name, "None") == 0)
//    return PICO_INPUT_NOTHING;

//   if (log_cb)
//      log_cb(RETRO_LOG_WARN, "invalid picodrive_input: '%s'\n", name);
// return PICO_INPUT_PAD_3BTN;
//}

static void update_variables(void)
{
   struct retro_variable var;

   // var.value = NULL;
   // var.key = "picodrive_input1";
   // if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   //    PicoSetInputDevice(0, input_name_to_val(var.value));

   // var.value = NULL;
   // var.key = "picodrive_input2";
   // if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   //    PicoSetInputDevice(1, input_name_to_val(var.value));

   var.value = NULL;
   var.key = "picodrive_sprlim";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         PicoOpt |= POPT_DIS_SPRITE_LIM;
      else
         PicoOpt &= ~POPT_DIS_SPRITE_LIM;
   }

   var.value = NULL;
   var.key = "picodrive_ramcart";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         PicoOpt |= POPT_EN_MCD_RAMCART;
      else
         PicoOpt &= ~POPT_EN_MCD_RAMCART;
   }
}

void retro_run(void)
{
   bool updated = false;
   int pad, i;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   input_poll_cb();

   PicoPad[0] = PicoPad[1] = 0;
   for (pad = 0; pad < 2; pad++)
      for (i = 0; i < RETRO_PICO_MAP_LEN; i++)
         if (input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, i))
            PicoPad[pad] |= retro_pico_map[i];

   PicoSkipFrame = 0;

//   PicoScanBegin = EmuScanSlowBegin;
//   PicoScanEnd = EmuScanSlowEnd;

   EmuScanPrepare();

   PicoFrame();

//   static unsigned int __attribute__((aligned(16))) d_list[256];
//   void* const texture_vram_p = (void*) (0x44200000 - (512 * 256)); // max VRAM address - frame size

//   sceKernelDcacheWritebackRange(HighPal,256 * 2);
//   sceKernelDcacheWritebackRange(PicoDraw2FB, 320 * 240 );

//   sceGuStart(GU_DIRECT, d_list);
//   sceGuCopyImage(GU_PSM_4444, 0, 0, 160, 240, 160, PicoDraw2FB, 0, 0, 256, texture_vram_p);

//   sceGuTexSync();
//   sceGuTexImage(0, 512, 256, 512, texture_vram_p);
//   sceGuTexMode(GU_PSM_T8, 0, 0, GU_FALSE);
//   sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
//   sceGuDisable(GU_BLEND);
//   sceGuClutMode(GU_PSM_5551, 0, 0xFF, 0);
//   sceGuClutLoad(32, HighPal);

//   sceGuFinish();

//   video_cb(texture_vram_p, 320, 240, 1024);

//   video_cb(((void*)-1), 320, 240, 1024);

}

static void check_system_specs(void)
{
   /* TODO - set different performance level for 32X - 6 for ARM dynarec, higher for interpreter core */
   unsigned level = 5;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
   struct retro_log_callback log;
   int level;

   level = 0;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_control);

    PicoOpt = POPT_EN_STEREO|POPT_EN_FM|POPT_EN_PSG|POPT_EN_Z80
       | POPT_EN_MCD_PCM|POPT_EN_MCD_CDDA|POPT_EN_MCD_GFX
   //     | POPT_EN_32X|POPT_EN_PWM
       | POPT_ACC_SPRITES|POPT_DIS_32C_BORDER;

//   PicoOpt = 0x0f | POPT_EN_MCD_PCM | POPT_EN_MCD_CDDA | POPT_EN_MCD_GFX |
//             POPT_ACC_SPRITES;
   PsndRate = 44100;
   PicoAutoRgnOrder = 0x184; // US, EU, JP
   PicoRegionOverride = 0x0;
   PicoCDBuffers = 64;

   PicoInit();
//   PicoDrawSetColorFormat(1); // 0=BGR444, 1=RGB555, 2=8bit(HighPal pal)


   PicoMessage = NULL;
   PicoMCDopenTray = NULL;
   PicoMCDcloseTray = NULL;
   // PicoMessage = plat_status_msg_busy_next;
   // PicoMCDopenTray = disk_tray_open;
   // PicoMCDcloseTray = disk_tray_close;

   update_variables();
}

void retro_deinit(void)
{
   PicoExit();
}
