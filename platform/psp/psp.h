// (c) Copyright 2007 notaz, All rights reserved.
// Free for non-commercial use.

// For commercial use, separate licencing terms must be obtained.

#include <pspctrl.h>

void psp_init(void);
void psp_finish(void);

void psp_msleep(int ms);

// vram usage map:
// 000000-044000 fb0
// 044000-088000 fb1
// 088000-0cc000 depth (?)
// 0cc000-126000 emu draw buffers: 512*240 + 512*240*2

#define VRAMOFFS_FB0    0x00000000
#define VRAMOFFS_FB1    0x00044000
#define VRAMOFFS_DEPTH  0x00088000
#define VRAMOFFS_STUFF  0x000cc000

#define VRAM_FB0        ((void *) (0x44000000+VRAMOFFS_FB0))
#define VRAM_FB1        ((void *) (0x44000000+VRAMOFFS_FB1))
#define VRAM_STUFF      ((void *) (0x44000000+VRAMOFFS_STUFF))

#define VRAM_CACHED_STUFF   ((void *) (0x04000000+VRAMOFFS_STUFF))

#define GU_CMDLIST_SIZE (16*1024)

extern unsigned int guCmdList[GU_CMDLIST_SIZE];
extern int psp_unhandled_suspend;

void *psp_video_get_active_fb(void);
void  psp_video_switch_to_single(void);
void  psp_video_flip(int wait_vsync);
extern void *psp_screen;

unsigned int psp_pad_read(int blocking);

int psp_get_cpu_clock(void);
int psp_set_cpu_clock(int clock);

char *psp_get_status_line(void);

void psp_wait_suspend(void);
void psp_resume_suspend(void);

/* shorter btn names */
#define PBTN_UP       PSP_CTRL_UP
#define PBTN_LEFT     PSP_CTRL_LEFT
#define PBTN_RIGHT    PSP_CTRL_RIGHT
#define PBTN_DOWN     PSP_CTRL_DOWN
#define PBTN_L        PSP_CTRL_LTRIGGER
#define PBTN_R        PSP_CTRL_RTRIGGER
#define PBTN_TRIANGLE PSP_CTRL_TRIANGLE
#define PBTN_CIRCLE   PSP_CTRL_CIRCLE
#define PBTN_X        PSP_CTRL_CROSS
#define PBTN_SQUARE   PSP_CTRL_SQUARE
#define PBTN_SELECT   PSP_CTRL_SELECT
#define PBTN_START    PSP_CTRL_START
#define PBTN_NOTE     PSP_CTRL_NOTE // doesn't seem to work?

/* fake 'nub' btns */
#define PBTN_NUB_UP    (1 << 28)
#define PBTN_NUB_RIGHT (1 << 29)
#define PBTN_NUB_DOWN  (1 << 30)
#define PBTN_NUB_LEFT  (1 << 31)

#define PBTN_NORTH PBTN_TRIANGLE
#define PBTN_SOUTH PBTN_X
#define PBTN_WEST  PBTN_SQUARE
#define PBTN_EAST  PBTN_CIRCLE

unsigned long wait_for_input(unsigned int interesting, int is_key_config);
void menu_draw_begin(void);
void menu_darken_bg(void *dst, const void *src, int pixels, int darker);
void menu_draw_end(void);

#define SCREEN_WIDTH  512
#define SCREEN_HEIGHT 272
#define SCREEN_BUFFER psp_screen

#define read_buttons(which) \
   wait_for_input(which, 0)
#define read_buttons_async(which) \
   (psp_pad_read(0) & (which))
#define clear_screen() \
   memset(SCREEN_BUFFER, 0, SCREEN_WIDTH*SCREEN_HEIGHT*2)
#define darken_screen() \
   menu_darken_bg(psp_screen, psp_screen, SCREEN_WIDTH*SCREEN_HEIGHT, 0)
