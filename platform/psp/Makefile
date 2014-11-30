# pspdev is expected to be in path
PSPSDK = $(shell psp-config --pspsdk-path)

# settings
#use_musashi = 1
#use_mz80 = 1
for_15fw = 0
# :!touch platform/psp/psp.c


CFLAGS += -I../.. -I. -D_ASM_DRAW_C_AMIPS
CFLAGS += -Wall -Winline -G0
#CFLAGS += -DLPRINTF_STDIO
#CFLAGS += -pg
ifeq ($(DEBUG),1)
CFLAGS += -g -O0
else
CFLAGS += -O2 -ftracer -fstrength-reduce -ffast-math
endif

ifeq "$(for_15fw)" "1"
CFLAGS += -DFW15
endif


# frontend and stuff
OBJS += main.o psp_emu.o menu.o psp.o asm_utils.o

# common
OBJS += platform/common/emu.o platform/common/menu.o platform/common/fonts.o platform/common/config.o
OBJS += platform/common/input.o

# Pico

OBJS += pico/area.o pico/cart.o pico/memory.o pico/misc.o pico/pico.o pico/sek.o pico/videoport.o \
		pico/draw2.o pico/draw.o pico/patch.o pico/draw_amips.o pico/memory_amips.o \
		pico/misc_amips.o pico/debug.o
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
ifeq "$(use_musashi)" "1"
CFLAGS += -DEMU_M68K
OBJS += cpu/musashi/m68kops.o cpu/musashi/m68kcpu.o
else
CFLAGS += -DEMU_F68K
OBJS += cpu/fame/famec.o
endif
# z80
ifeq "$(use_mz80)" "1"
CFLAGS += -D_USE_MZ80
OBJS += cpu/mz80/mz80.o
else
CFLAGS += -D_USE_CZ80
OBJS += cpu/cz80/cz80.o
endif

vpath %.c = ../..
vpath %.s = ../..

LIBS += -lm -lpspgu -lpsppower -lpspaudio -lpsprtc
LDFLAGS += -Wl,-Map=PicoDrive.map

CFLAGS += -ISDK/
LDFLAGS += -LSDK/

# target
TARGET = PicoDrive
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PicoDrive
PSP_EBOOT_ICON = data/icon.png

ifneq "$(for_15fw)" "1"
BUILD_PRX = 1
endif

CUSTOM_CLEAN = myclean

include $(PSPSDK)/lib/build.mak

.c.o:
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

AS := psp-as

.s.o:
	@echo ">>>" $<
	$(AS) -march=allegrex -mtune=allegrex $< -o $@

../../cpu/musashi/m68kops.c :
	make -C ../../cpu/musashi

cpu/fame/famec.o : ../../cpu/fame/famec.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -Wno-unused -c $< -o $@

pico/misc.o : ../../pico/misc.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@ -D_ASM_MISC_C_AMIPS

pico/memory.o : ../../pico/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@ -D_ASM_MEMORY_C -D_ASM_MEMORY_C_AMIPS

pico/cd/memory.o : ../../pico/cd/memory.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

pico/cd/gfx_cd.o : ../../pico/cd/gfx_cd.c
	@echo ">>>" $<
	$(CC) $(CFLAGS) -c $< -o $@

# cleanup

myclean:
	$(RM) PicoDrive.map
	make -C ../../cpu/musashi clean
