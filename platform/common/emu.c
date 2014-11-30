// (c) Copyright 2006-2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h> // tolower
#ifndef NO_SYNC
#include <unistd.h>
#endif

#include "emu.h"
#include "menu.h"
#include "fonts.h"
#include "lprintf.h"
#include "config.h"
#include "common.h"

#include <pico/pico_int.h>
#include <pico/patch.h>
#include <pico/cd/cue.h>

char *PicoConfigFile = "config.cfg";
currentConfig_t currentConfig, defaultConfig;
int rom_loaded = 0;
char noticeMsg[64] = { 0, };
int state_slot = 0;
int config_slot = 0, config_slot_current = 0;
char loadedRomFName[512] = { 0, };
int kb_combo_keys = 0, kb_combo_acts = 0;	// keys and actions which need button combos
int pico_inp_mode = 0;

unsigned char *movie_data = NULL;
static int movie_size = 0;

// provided by platform code:
extern void emu_noticeMsgUpdated(void);
extern int  emu_getMainDir(char *dst, int len);
extern void menu_romload_prepare(const char *rom_name);
extern void menu_romload_end(void);


// utilities
static void strlwr_(char *string)
{
	char *p;
	for (p = string; *p; p++)
		*p = (char)tolower(*p);
}

static int try_rfn_cut(char *fname)
{
	FILE *tmp;
	char *p;

	p = fname + strlen(fname) - 1;
	for (; p > fname; p--)
		if (*p == '.') break;
	*p = 0;

	if((tmp = fopen(fname, "rb"))) {
		fclose(tmp);
		return 1;
	}
	return 0;
}

static void get_ext(char *file, char *ext)
{
	char *p;

	p = file + strlen(file) - 4;
	if (p < file) p = file;
	strncpy(ext, p, 4);
	ext[4] = 0;
	strlwr_(ext);
}

char *biosfiles_us[] = { "us_scd1_9210", "us_scd2_9306", "SegaCDBIOS9303" };
char *biosfiles_eu[] = { "eu_mcd1_9210", "eu_mcd2_9306", "eu_mcd2_9303"   };
char *biosfiles_jp[] = { "jp_mcd1_9112", "jp_mcd1_9111" };

int emu_findBios(int region, char **bios_file)
{
	static char bios_path[1024];
	int i, count;
	char **files;
	FILE *f = NULL;

	if (region == 4) { // US
		files = biosfiles_us;
		count = sizeof(biosfiles_us) / sizeof(char *);
	} else if (region == 8) { // EU
		files = biosfiles_eu;
		count = sizeof(biosfiles_eu) / sizeof(char *);
	} else if (region == 1 || region == 2) {
		files = biosfiles_jp;
		count = sizeof(biosfiles_jp) / sizeof(char *);
	} else {
		return 0;
	}

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

	if (f) {
		lprintf("using bios: %s\n", bios_path);
		fclose(f);
		if (bios_file) *bios_file = bios_path;
		return 1;
	} else {
		sprintf(menuErrorMsg, "no %s BIOS files found, read docs",
			region != 4 ? (region == 8 ? "EU" : "JAP") : "USA");
		lprintf("%s\n", menuErrorMsg);
		return 0;
	}
}

static unsigned char id_header[0x100];

/* checks if fname points to valid MegaCD image
 * if so, checks for suitable BIOS */
int emu_cdCheck(int *pregion, char *fname_in)
{
	unsigned char buf[32];
	pm_file *cd_f;
	int region = 4; // 1: Japan, 4: US, 8: Europe
	char ext[5], *fname = fname_in;
	cue_track_type type = CT_UNKNOWN;
	cue_data_t *cue_data = NULL;

	get_ext(fname_in, ext);
	if (strcasecmp(ext, ".cue") == 0) {
		cue_data = cue_parse(fname_in);
		if (cue_data != NULL) {
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

	if (pm_read(buf, 32, cd_f) != 32) {
		pm_close(cd_f);
		return -1;
	}

	if (!strncasecmp("SEGADISCSYSTEM", (char *)buf+0x00, 14)) {
		if (type && type != CT_ISO)
			elprintf(EL_STATUS, ".cue has wrong type: %i", type);
		type = CT_ISO;       // Sega CD (ISO)
	}
	if (!strncasecmp("SEGADISCSYSTEM", (char *)buf+0x10, 14)) {
		if (type && type != CT_BIN)
			elprintf(EL_STATUS, ".cue has wrong type: %i", type);
		type = CT_BIN;       // Sega CD (BIN)
	}

	if (type == CT_UNKNOWN) {
		pm_close(cd_f);
		return 0;
	}

	pm_seek(cd_f, (type == CT_ISO) ? 0x100 : 0x110, SEEK_SET);
	pm_read(id_header, sizeof(id_header), cd_f);

	/* it seems we have a CD image here. Try to detect region now.. */
	pm_seek(cd_f, (type == CT_ISO) ? 0x100+0x10B : 0x110+0x10B, SEEK_SET);
	pm_read(buf, 1, cd_f);
	pm_close(cd_f);

	if (buf[0] == 0x64) region = 8; // EU
	if (buf[0] == 0xa1) region = 1; // JAP

	lprintf("detected %s Sega/Mega CD image with %s region\n",
		type == CT_BIN ? "BIN" : "ISO", region != 4 ? (region == 8 ? "EU" : "JAP") : "USA");

	if (pregion != NULL) *pregion = region;

	return type;
}

static int extract_text(char *dest, const unsigned char *src, int len, int swab)
{
	char *p = dest;
	int i;

	if (swab) swab = 1;

	for (i = len - 1; i >= 0; i--)
	{
		if (src[i^swab] != ' ') break;
	}
	len = i + 1;

	for (i = 0; i < len; i++)
	{
		unsigned char s = src[i^swab];
		if (s >= 0x20 && s < 0x7f && s != '#' && s != '|' &&
			s != '[' && s != ']' && s != '\\')
		{
			*p++ = s;
		}
		else
		{
			sprintf(p, "\\%02x", s);
			p += 3;
		}
	}

	return p - dest;
}

char *emu_makeRomId(void)
{
	static char id_string[3+0xe*3+0x3*3+0x30*3+3];
	int pos, swab = 1;

	if (PicoAHW & PAHW_MCD) {
		strcpy(id_string, "CD|");
		swab = 0;
	}
	else strcpy(id_string, "MD|");
	pos = 3;

	pos += extract_text(id_string + pos, id_header + 0x80, 0x0e, swab); // serial
	id_string[pos] = '|'; pos++;
	pos += extract_text(id_string + pos, id_header + 0xf0, 0x03, swab); // region
	id_string[pos] = '|'; pos++;
	pos += extract_text(id_string + pos, id_header + 0x50, 0x30, swab); // overseas name
	id_string[pos] = 0;

	return id_string;
}

// buffer must be at least 150 byte long
void emu_getGameName(char *str150)
{
	int ret, swab = (PicoAHW & PAHW_MCD) ? 0 : 1;
	char *s, *d;

	ret = extract_text(str150, id_header + 0x50, 0x30, swab); // overseas name

	for (s = d = str150 + 1; s < str150+ret; s++)
	{
		if (*s == 0) break;
		if (*s != ' ' || d[-1] != ' ')
			*d++ = *s;
	}
	*d = 0;
}

// note: this function might mangle rom_fname
int emu_ReloadRom(char *rom_fname)
{
	unsigned int rom_size = 0;
	char *used_rom_name = rom_fname;
	unsigned char *rom_data = NULL;
	char ext[5];
	pm_file *rom = NULL;
	int ret, cd_state, cd_region, cfg_loaded = 0;

	lprintf("emu_ReloadRom(%s)\n", rom_fname);

	get_ext(rom_fname, ext);

	// detect wrong extensions
	if (!strcmp(ext, ".srm") || !strcmp(ext, "s.gz") || !strcmp(ext, ".mds")) { // s.gz ~ .mds.gz
		sprintf(menuErrorMsg, "Not a ROM/CD selected.");
		return 0;
	}

	PicoPatchUnload();

	// check for movie file
	if (movie_data) {
		free(movie_data);
		movie_data = 0;
	}
	if (!strcmp(ext, ".gmv"))
	{
		// check for both gmv and rom
		int dummy;
		FILE *movie_file = fopen(rom_fname, "rb");
		if(!movie_file) {
			sprintf(menuErrorMsg, "Failed to open movie.");
			return 0;
		}
		fseek(movie_file, 0, SEEK_END);
		movie_size = ftell(movie_file);
		fseek(movie_file, 0, SEEK_SET);
		if(movie_size < 64+3) {
			sprintf(menuErrorMsg, "Invalid GMV file.");
			fclose(movie_file);
			return 0;
		}
		movie_data = malloc(movie_size);
		if(movie_data == NULL) {
			sprintf(menuErrorMsg, "low memory.");
			fclose(movie_file);
			return 0;
		}
		fread(movie_data, 1, movie_size, movie_file);
		fclose(movie_file);
		if (strncmp((char *)movie_data, "Gens Movie TEST", 15) != 0) {
			sprintf(menuErrorMsg, "Invalid GMV file.");
			return 0;
		}
		dummy = try_rfn_cut(rom_fname) || try_rfn_cut(rom_fname);
		if (!dummy) {
			sprintf(menuErrorMsg, "Could't find a ROM for movie.");
			return 0;
		}
		get_ext(rom_fname, ext);
		lprintf("gmv loaded for %s\n", rom_fname);
	}
	else if (!strcmp(ext, ".pat"))
	{
		int dummy;
		PicoPatchLoad(rom_fname);
		dummy = try_rfn_cut(rom_fname) || try_rfn_cut(rom_fname);
		if (!dummy) {
			sprintf(menuErrorMsg, "Could't find a ROM to patch.");
			return 0;
		}
		get_ext(rom_fname, ext);
	}

	emu_shutdownMCD();

	// check for MegaCD image
	cd_state = emu_cdCheck(&cd_region, rom_fname);
	if (cd_state >= 0 && cd_state != CIT_NOT_CD)
	{
		PicoAHW |= PAHW_MCD;
		// valid CD image, check for BIOS..

		// we need to have config loaded at this point
		ret = emu_ReadConfig(1, 1);
		if (!ret) emu_ReadConfig(0, 1);
		cfg_loaded = 1;

		if (PicoRegionOverride) {
			cd_region = PicoRegionOverride;
			lprintf("overrided region to %s\n", cd_region != 4 ? (cd_region == 8 ? "EU" : "JAP") : "USA");
		}
		if (!emu_findBios(cd_region, &used_rom_name)) {
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
	if (!rom) {
		sprintf(menuErrorMsg, "Failed to open ROM/CD image");
		goto fail;
	}

	if (cd_state < 0) {
		sprintf(menuErrorMsg, "Invalid CD image");
		goto fail;
	}

	menu_romload_prepare(used_rom_name); // also CD load

	PicoCartUnload();
	rom_loaded = 0;

	if ( (ret = PicoCartLoad(rom, &rom_data, &rom_size)) ) {
		if      (ret == 2) sprintf(menuErrorMsg, "Out of memory");
		else if (ret == 3) sprintf(menuErrorMsg, "Read failed");
		else               sprintf(menuErrorMsg, "PicoCartLoad() failed.");
		lprintf("%s\n", menuErrorMsg);
		goto fail2;
	}
	pm_close(rom);
	rom = NULL;

	// detect wrong files (Pico crashes on very small files), also see if ROM EP is good
	if (rom_size <= 0x200 || strncmp((char *)rom_data, "Pico", 4) == 0 ||
	  ((*(unsigned char *)(rom_data+4)<<16)|(*(unsigned short *)(rom_data+6))) >= (int)rom_size) {
		if (rom_data) free(rom_data);
		sprintf(menuErrorMsg, "Not a ROM selected.");
		goto fail2;
	}

	// load config for this ROM (do this before insert to get correct region)
	if (!(PicoAHW & PAHW_MCD))
		memcpy(id_header, rom_data + 0x100, sizeof(id_header));
	if (!cfg_loaded) {
		ret = emu_ReadConfig(1, 1);
		if (!ret) emu_ReadConfig(0, 1);
	}

	lprintf("PicoCartInsert(%p, %d);\n", rom_data, rom_size);
	if (PicoCartInsert(rom_data, rom_size)) {
		sprintf(menuErrorMsg, "Failed to load ROM.");
		goto fail2;
	}

	// insert CD if it was detected
	if (cd_state != CIT_NOT_CD) {
		ret = Insert_CD(rom_fname, cd_state);
		if (ret != 0) {
			sprintf(menuErrorMsg, "Insert_CD() failed, invalid CD image?");
			lprintf("%s\n", menuErrorMsg);
			goto fail2;
		}
	}

	menu_romload_end();

	if (PicoPatches) {
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
		if (movie_data[0xF] >= 'A') {
			if (movie_data[0x16] & 0x80) {
				PicoRegionOverride = 8;
			} else {
				PicoRegionOverride = 4;
			}
			PicoReset();
			// TODO: bits 6 & 5
		}
		movie_data[0x18+30] = 0;
		sprintf(noticeMsg, "MOVIE: %s", (char *) &movie_data[0x18]);
	}
	else
	{
		PicoOpt &= ~POPT_DIS_VDP_FIFO;
		if (Pico.m.pal) {
			strcpy(noticeMsg, "PAL SYSTEM / 50 FPS");
		} else {
			strcpy(noticeMsg, "NTSC SYSTEM / 60 FPS");
		}
	}
	emu_noticeMsgUpdated();

	// load SRAM for this ROM
	if (currentConfig.EmuOpt & EOPT_USE_SRAM)
		emu_SaveLoadGame(1, 1);

	strncpy(loadedRomFName, rom_fname, sizeof(loadedRomFName)-1);
	loadedRomFName[sizeof(loadedRomFName)-1] = 0;
	rom_loaded = 1;
	return 1;

fail2:
	menu_romload_end();
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

static void romfname_ext(char *dst, const char *prefix, const char *ext)
{
	char *p;
	int prefix_len = 0;

	// make save filename
	p = loadedRomFName+strlen(loadedRomFName)-1;
	for (; p >= loadedRomFName && *p != PATH_SEP_C; p--); p++;
	*dst = 0;
	if (prefix) {
		int len = emu_getMainDir(dst, 512);
		strcpy(dst + len, prefix);
		prefix_len = len + strlen(prefix);
	}
#ifdef UIQ3
	else p = loadedRomFName; // backward compatibility
#endif
	strncpy(dst + prefix_len, p, 511-prefix_len);
	dst[511-8] = 0;
	if (dst[strlen(dst)-4] == '.') dst[strlen(dst)-4] = 0;
	if (ext) strcat(dst, ext);
}


static void make_config_cfg(char *cfg)
{
	int len;
	len = emu_getMainDir(cfg, 512);
	strncpy(cfg + len, PicoConfigFile, 512-6-1-len);
	if (config_slot != 0)
	{
		char *p = strrchr(cfg, '.');
		if (p == NULL) p = cfg + strlen(cfg);
		sprintf(p, ".%i.cfg", config_slot);
	}
	cfg[511] = 0;
}

void emu_packConfig(void)
{
	currentConfig.s_PicoOpt = PicoOpt;
	currentConfig.s_PsndRate = PsndRate;
	currentConfig.s_PicoRegion = PicoRegionOverride;
	currentConfig.s_PicoAutoRgnOrder = PicoAutoRgnOrder;
	currentConfig.s_PicoCDBuffers = PicoCDBuffers;
}

void emu_unpackConfig(void)
{
	PicoOpt = currentConfig.s_PicoOpt;
	PsndRate = currentConfig.s_PsndRate;
	PicoRegionOverride = currentConfig.s_PicoRegion;
	PicoAutoRgnOrder = currentConfig.s_PicoAutoRgnOrder;
	PicoCDBuffers = currentConfig.s_PicoCDBuffers;
}

static void emu_setDefaultConfig(void)
{
	memcpy(&currentConfig, &defaultConfig, sizeof(currentConfig));
	emu_unpackConfig();
}


int emu_ReadConfig(int game, int no_defaults)
{
	char cfg[512];
	int ret;

	if (!game)
	{
		if (!no_defaults)
			emu_setDefaultConfig();
		make_config_cfg(cfg);
		ret = config_readsect(cfg, NULL);
	}
	else
	{
		char *sect = emu_makeRomId();

		// try new .cfg way
		if (config_slot != 0)
		     sprintf(cfg, "game.%i.cfg", config_slot);
		else strcpy(cfg,  "game.cfg");

		ret = -1;
		if (config_havesect(cfg, sect))
		{
			// read user's config
			int vol = currentConfig.volume;
			emu_setDefaultConfig();
			ret = config_readsect(cfg, sect);
			currentConfig.volume = vol; // make vol global (bah)
		}
		else
		{
			// read global config, and apply game_def.cfg on top
			make_config_cfg(cfg);
			config_readsect(cfg, NULL);
			ret = config_readsect("game_def.cfg", sect);
		}

		if (ret == 0)
		{
			lprintf("loaded cfg from sect \"%s\"\n", sect);
		}
	}

	// some sanity checks
	if (currentConfig.CPUclock < 10 || currentConfig.CPUclock > 4096) currentConfig.CPUclock = 200;
#ifdef PSP
	if (currentConfig.gamma < -4 || currentConfig.gamma >  16) currentConfig.gamma = 0;
	if (currentConfig.gamma2 < 0 || currentConfig.gamma2 > 2)  currentConfig.gamma2 = 0;
#else
	if (currentConfig.gamma < 10 || currentConfig.gamma > 300) currentConfig.gamma = 100;
#endif
	if (currentConfig.volume < 0 || currentConfig.volume > 99) currentConfig.volume = 50;
#ifdef __GP2X__
	// if volume keys are unbound, bind them to volume control
	if (!currentConfig.KeyBinds[23] && !currentConfig.KeyBinds[22]) {
		currentConfig.KeyBinds[23] = 1<<29; // vol up
		currentConfig.KeyBinds[22] = 1<<30; // vol down
	}
#endif
	if (ret == 0) config_slot_current = config_slot;
	return (ret == 0);
}


int emu_WriteConfig(int is_game)
{
	char cfg[512], *game_sect = NULL;
	int ret, write_lrom = 0;

	if (!is_game)
	{
		make_config_cfg(cfg);
		write_lrom = 1;
	} else {
		if (config_slot != 0)
		     sprintf(cfg, "game.%i.cfg", config_slot);
		else strcpy(cfg,  "game.cfg");
		game_sect = emu_makeRomId();
		lprintf("emu_WriteConfig: sect \"%s\"\n", game_sect);
	}

	lprintf("emu_WriteConfig: %s ", cfg);
	ret = config_writesect(cfg, game_sect);
	if (write_lrom) config_writelrom(cfg);
#ifndef NO_SYNC
	sync();
#endif
	lprintf((ret == 0) ? "(ok)\n" : "(failed)\n");

	if (ret == 0) config_slot_current = config_slot;
	return ret == 0;
}


void emu_writelrom(void)
{
	char cfg[512];
	make_config_cfg(cfg);
	config_writelrom(cfg);
#ifndef NO_SYNC
	sync();
#endif
}

#ifndef UIQ3
void emu_textOut8(int x, int y, const char *text)
{
	int i,l,len=strlen(text);
	unsigned char *screen = (unsigned char *)SCREEN_BUFFER + x + y*SCREEN_WIDTH;

	/* always using built-in font */
	for (i = 0; i < len; i++)
	{
		for (l=0;l<8;l++)
		{
			unsigned char fd = fontdata8x8[((text[i])*8)+l];
			if (fd&0x80) screen[l*SCREEN_WIDTH+0]=0xf0;
			if (fd&0x40) screen[l*SCREEN_WIDTH+1]=0xf0;
			if (fd&0x20) screen[l*SCREEN_WIDTH+2]=0xf0;
			if (fd&0x10) screen[l*SCREEN_WIDTH+3]=0xf0;
			if (fd&0x08) screen[l*SCREEN_WIDTH+4]=0xf0;
			if (fd&0x04) screen[l*SCREEN_WIDTH+5]=0xf0;
			if (fd&0x02) screen[l*SCREEN_WIDTH+6]=0xf0;
			if (fd&0x01) screen[l*SCREEN_WIDTH+7]=0xf0;
		}
		screen += 8;
	}
}

void emu_textOut16(int x, int y, const char *text)
{
	int i,l,len=strlen(text);
	unsigned short *screen = (unsigned short *)SCREEN_BUFFER + x + y*SCREEN_WIDTH;

	for (i = 0; i < len; i++)
	{
		for (l=0;l<8;l++)
		{
			unsigned char fd = fontdata8x8[((text[i])*8)+l];
			if(fd&0x80) screen[l*SCREEN_WIDTH+0]=0xffff;
			if(fd&0x40) screen[l*SCREEN_WIDTH+1]=0xffff;
			if(fd&0x20) screen[l*SCREEN_WIDTH+2]=0xffff;
			if(fd&0x10) screen[l*SCREEN_WIDTH+3]=0xffff;
			if(fd&0x08) screen[l*SCREEN_WIDTH+4]=0xffff;
			if(fd&0x04) screen[l*SCREEN_WIDTH+5]=0xffff;
			if(fd&0x02) screen[l*SCREEN_WIDTH+6]=0xffff;
			if(fd&0x01) screen[l*SCREEN_WIDTH+7]=0xffff;
		}
		screen += 8;
	}
}
#endif

#ifdef PSP
#define MAX_COMBO_KEY 23
#else
#define MAX_COMBO_KEY 31
#endif

void emu_findKeyBindCombos(void)
{
	int act, u;

	// find out which keys and actions are combos
	kb_combo_keys = kb_combo_acts = 0;
	for (act = 0; act < 32; act++)
	{
		int keyc = 0, keyc2 = 0;
		if (act == 16 || act == 17) continue; // player2 flag
		if (act > 17)
		{
			for (u = 0; u <= MAX_COMBO_KEY; u++)
				if (currentConfig.KeyBinds[u] & (1 << act)) keyc++;
		}
		else
		{
			for (u = 0; u <= MAX_COMBO_KEY; u++)
				if ((currentConfig.KeyBinds[u] & 0x30000) == 0 && // pl. 1
					(currentConfig.KeyBinds[u] & (1 << act))) keyc++;
			for (u = 0; u <= MAX_COMBO_KEY; u++)
				if ((currentConfig.KeyBinds[u] & 0x30000) == 1 && // pl. 2
					(currentConfig.KeyBinds[u] & (1 << act))) keyc2++;
			if (keyc2 > keyc) keyc = keyc2;
		}
		if (keyc > 1)
		{
			// loop again and mark those keys and actions as combo
			for (u = 0; u <= MAX_COMBO_KEY; u++)
			{
				if (currentConfig.KeyBinds[u] & (1 << act)) {
					kb_combo_keys |= 1 << u;
					kb_combo_acts |= 1 << act;
				}
			}
		}
	}

	// printf("combo keys/acts: %08x %08x\n", kb_combo_keys, kb_combo_acts);
}


void emu_updateMovie(void)
{
	int offs = Pico.m.frame_count*3 + 0x40;
	if (offs+3 > movie_size) {
		free(movie_data);
		movie_data = 0;
		strcpy(noticeMsg, "END OF MOVIE.");
		lprintf("END OF MOVIE.\n");
		emu_noticeMsgUpdated();
	} else {
		// MXYZ SACB RLDU
		PicoPad[0] = ~movie_data[offs]   & 0x8f; // ! SCBA RLDU
		if(!(movie_data[offs]   & 0x10)) PicoPad[0] |= 0x40; // C
		if(!(movie_data[offs]   & 0x20)) PicoPad[0] |= 0x10; // A
		if(!(movie_data[offs]   & 0x40)) PicoPad[0] |= 0x20; // B
		PicoPad[1] = ~movie_data[offs+1] & 0x8f; // ! SCBA RLDU
		if(!(movie_data[offs+1] & 0x10)) PicoPad[1] |= 0x40; // C
		if(!(movie_data[offs+1] & 0x20)) PicoPad[1] |= 0x10; // A
		if(!(movie_data[offs+1] & 0x40)) PicoPad[1] |= 0x20; // B
		PicoPad[0] |= (~movie_data[offs+2] & 0x0A) << 8; // ! MZYX
		if(!(movie_data[offs+2] & 0x01)) PicoPad[0] |= 0x0400; // X
		if(!(movie_data[offs+2] & 0x04)) PicoPad[0] |= 0x0100; // Z
		PicoPad[1] |= (~movie_data[offs+2] & 0xA0) << 4; // ! MZYX
		if(!(movie_data[offs+2] & 0x10)) PicoPad[1] |= 0x0400; // X
		if(!(movie_data[offs+2] & 0x40)) PicoPad[1] |= 0x0100; // Z
	}
}


static int try_ropen_file(const char *fname)
{
	FILE *f;

	f = fopen(fname, "rb");
	if (f) {
		fclose(f);
		return 1;
	}
	return 0;
}

char *emu_GetSaveFName(int load, int is_sram, int slot)
{
	static char saveFname[512];
	char ext[16];

	if (is_sram)
	{
		romfname_ext(saveFname, (PicoAHW&1) ? "brm"PATH_SEP : "srm"PATH_SEP, (PicoAHW&1) ? ".brm" : ".srm");
		if (load) {
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
		if(slot > 0 && slot < 10) sprintf(ext, ".%i", slot);
      strcat(ext, ".mds");

		romfname_ext(saveFname, "mds" PATH_SEP, ext);
		if (load) {
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
   areaRead  = (arearw *) fread;
   areaWrite = (arearw *) fwrite;
   areaEof   = (areaeof *) feof;
   areaSeek  = (areaseek *) fseek;
   areaClose = (areaclose *) fclose;
}

int emu_SaveLoadGame(int load, int sram)
{
	int ret = 0;
	char *saveFname;

	// make save filename
	saveFname = emu_GetSaveFName(load, sram, state_slot);
	if (saveFname == NULL) {
		if (!sram) {
			strcpy(noticeMsg, load ? "LOAD FAILED (missing file)" : "SAVE FAILED  ");
			emu_noticeMsgUpdated();
		}
		return -1;
	}

	lprintf("saveLoad (%i, %i): %s\n", load, sram, saveFname);

	if (sram)
	{
		FILE *sramFile;
		int sram_size;
		unsigned char *sram_data;
		int truncate = 1;
		if (PicoAHW & PAHW_MCD)
		{
			if (PicoOpt&POPT_EN_MCD_RAMCART) {
				sram_size = 0x12000;
				sram_data = SRam.data;
				if (sram_data)
					memcpy32((int *)sram_data, (int *)Pico_mcd->bram, 0x2000/4);
			} else {
				sram_size = 0x2000;
				sram_data = Pico_mcd->bram;
				truncate  = 0; // the .brm may contain RAM cart data after normal brm
			}
		} else {
			sram_size = SRam.end-SRam.start+1;
			if(Pico.m.sram_reg & 4) sram_size=0x2000;
			sram_data = SRam.data;
		}
		if (!sram_data) return 0; // SRam forcefully disabled for this game

		if (load)
		{
			sramFile = fopen(saveFname, "rb");
			if(!sramFile) return -1;
			fread(sram_data, 1, sram_size, sramFile);
			fclose(sramFile);
			if ((PicoAHW & PAHW_MCD) && (PicoOpt&POPT_EN_MCD_RAMCART))
				memcpy32((int *)Pico_mcd->bram, (int *)sram_data, 0x2000/4);
		} else {
			// sram save needs some special processing
			// see if we have anything to save
			for (; sram_size > 0; sram_size--)
				if (sram_data[sram_size-1]) break;

			if (sram_size) {
				sramFile = fopen(saveFname, truncate ? "wb" : "r+b");
				if (!sramFile) sramFile = fopen(saveFname, "wb"); // retry
				if (!sramFile) return -1;
				ret = fwrite(sram_data, 1, sram_size, sramFile);
				ret = (ret != sram_size) ? -1 : 0;
				fclose(sramFile);
#ifndef NO_SYNC
				sync();
#endif
			}
		}
		return ret;
	}
	else
	{
		void *PmovFile = NULL;

      if( (PmovFile = fopen(saveFname, load ? "rb" : "wb")) )
         emu_setSaveStateCbs();

		if(PmovFile) {
			ret = PmovState(load ? 6 : 5, PmovFile);
			areaClose(PmovFile);
			PmovFile = 0;
			if (load) Pico.m.dirtyPal=1;
#ifndef NO_SYNC
			else sync();
#endif
		}
		else	ret = -1;
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

void emu_changeFastForward(int set_on)
{
	static void *set_PsndOut = NULL;
	static int set_Frameskip, set_EmuOpt, is_on = 0;

	if (set_on && !is_on) {
		set_PsndOut = PsndOut;
		set_Frameskip = currentConfig.Frameskip;
		set_EmuOpt = currentConfig.EmuOpt;
		PsndOut = NULL;
		currentConfig.Frameskip = 8;
		currentConfig.EmuOpt &= ~4;
		currentConfig.EmuOpt |= 0x40000;
		is_on = 1;
		strcpy(noticeMsg, "FAST FORWARD   ");
		emu_noticeMsgUpdated();
	}
	else if (!set_on && is_on) {
		PsndOut = set_PsndOut;
		currentConfig.Frameskip = set_Frameskip;
		currentConfig.EmuOpt = set_EmuOpt;
		PsndRerate(1);
		is_on = 0;
	}
}

void emu_RunEventsPico(unsigned int events)
{
	if (events & (1 << 3)) {
		pico_inp_mode++;
		if (pico_inp_mode > 2) pico_inp_mode = 0;
		switch (pico_inp_mode) {
			case 2: strcpy(noticeMsg, "Input: Pen on Pad      "); break;
			case 1: strcpy(noticeMsg, "Input: Pen on Storyware"); break;
			case 0: strcpy(noticeMsg, "Input: Joytick         ");
				PicoPicohw.pen_pos[0] = PicoPicohw.pen_pos[1] = 0x8000;
				break;
		}
		emu_noticeMsgUpdated();
	}
	if (events & (1 << 4)) {
		PicoPicohw.page--;
		if (PicoPicohw.page < 0) PicoPicohw.page = 0;
		sprintf(noticeMsg, "Page %i                 ", PicoPicohw.page);
		emu_noticeMsgUpdated();
	}
	if (events & (1 << 5)) {
		PicoPicohw.page++;
		if (PicoPicohw.page > 6) PicoPicohw.page = 6;
		sprintf(noticeMsg, "Page %i                 ", PicoPicohw.page);
		emu_noticeMsgUpdated();
	}
}

void emu_DoTurbo(int *pad, int acts)
{
	static int turbo_pad = 0;
	static unsigned char turbo_cnt[3] = { 0, 0, 0 };
	int inc = currentConfig.turbo_rate * 2;

	if (acts & 0x1000) {
		turbo_cnt[0] += inc;
		if (turbo_cnt[0] >= 60)
			turbo_pad ^= 0x10, turbo_cnt[0] = 0;
	}
	if (acts & 0x2000) {
		turbo_cnt[1] += inc;
		if (turbo_cnt[1] >= 60)
			turbo_pad ^= 0x20, turbo_cnt[1] = 0;
	}
	if (acts & 0x4000) {
		turbo_cnt[2] += inc;
		if (turbo_cnt[2] >= 60)
			turbo_pad ^= 0x40, turbo_cnt[2] = 0;
	}
	*pad |= turbo_pad & (acts >> 8);
}

