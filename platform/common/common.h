// platform specific things for common menu code

#include "../psp/psp.h"

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
