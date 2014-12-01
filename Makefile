# pspdev is expected to be in path
PSPSDK = $(shell psp-config --pspsdk-path)

CFLAGS += -I../.. -I. -D_ASM_DRAW_C_AMIPS
CFLAGS += -Wall -Winline -G0 -Wno-char-subscripts

ifeq ($(DEBUG),1)
CFLAGS += -g -O0
else
CFLAGS += -O2 -ftracer -fstrength-reduce -ffast-math
endif

CFLAGS += -I. -Iplatform/psp


# frontend and stuff
OBJS += platform/psp/main.o platform/psp/psp_emu.o platform/psp/menu.o platform/psp/psp.o platform/psp/asm_utils.o

# common
OBJS += platform/common/emu.o platform/common/menu.o platform/common/fonts.o platform/common/config.o
OBJS += platform/common/input.o

# Pico

OBJS += pico/area.o pico/cart.o pico/memory.o pico/misc.o pico/pico.o pico/sek.o pico/videoport.o \
		pico/draw2.o pico/draw.o pico/patch.o pico/draw_amips.o pico/memory_amips.o \
		pico/misc_amips.o
# Pico - CD
OBJS += pico/cd/pico.o pico/cd/memory.o pico/cd/sek.o pico/cd/LC89510.o \
		pico/cd/cd_sys.o pico/cd/cd_file.o pico/cd/cue.o pico/cd/gfx_cd.o \
		pico/cd/area.o pico/cd/misc.o pico/cd/pcm.o pico/cd/buffering.o
# Pico - carthw
OBJS += pico/carthw/carthw.o pico/carthw/svp/svp.o pico/carthw/svp/memory.o \
		pico/carthw/svp/ssp16.o
# Pico - Pico
OBJS += pico/pico/pico.o pico/pico/memory.o pico/pico/xpcm.o

# Pico - sound

OBJS += pico/sound/sound.o

OBJS += pico/sound/mix.o
OBJS += pico/sound/sn76496.o pico/sound/ym2612.o

# CPU cores

OBJS += cpu/cz80/cz80.o

OBJS_BASE := $(OBJS)

OBJS += cpu/fame/famec.o

LIBS += -lm -lpspgu -lpsppower -lpspaudio -lpsprtc
LDFLAGS +=

# target
TARGET = PicoDrive
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PicoDrive
PSP_EBOOT_ICON = platform/psp/data/icon.png

BUILD_PRX = 1

include $(PSPSDK)/lib/build.mak

.c.o:
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

AS := psp-as

.s.o:
	@echo ">>>" $<
	$(AS) -march=allegrex -mtune=allegrex $< -o $@


cpu/fame/famec.o : cpu/fame/famec.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -Wno-unused -c $< -o $@

pico/misc.o : pico/misc.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@ -D_ASM_MISC_C_AMIPS

pico/memory.o : pico/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@ -D_ASM_MEMORY_C -D_ASM_MEMORY_C_AMIPS

pico/cd/memory.o : pico/cd/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

pico/cd/gfx_cd.o : pico/cd/gfx_cd.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

# cleanup

fast_clean:
	$(RM) $(OBJS_BASE)
#	$(RM) $(OBJS)
