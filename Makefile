# Makefile for RISC OS (Norcroft NG / acorn)
# Attack of the PETSCII Robots - RISC OS Wimp + sprite port
#
# Builds AOF objects only on this machine. The final AIF link is done on
# RISC OS (see the "link" target / instructions below).
#
# Compiler setup: set NGROOT to the root of the Norcroft NG cross suite
# (containing bin/ and external/clib/include/).

NGROOT ?= $(HOME)/g/Norcroft
CC = $(NGROOT)/bin/ncc-riscos
INC = -I$(NGROOT)/external/clib/include -Ithirdparty

# Game definition set (C89; ncc-riscos is C89-only).
GAMEFLAGS = -DPLATFORM_NAME=\"riscos\" \
	-DPLATFORM_SCREEN_WIDTH=320 \
	-DPLATFORM_SCREEN_HEIGHT=200 \
	-DPLATFORM_MAP_WINDOW_TILES_WIDTH=11 \
	-DPLATFORM_MAP_WINDOW_TILES_HEIGHT=7 \
	-DPLATFORM_MODULE_BASED_AUDIO \
	-DPLATFORM_IMAGE_BASED_TILES \
	-DPLATFORM_IMAGE_SUPPORT \
	-DPLATFORM_SPRITE_SUPPORT \
	-DPLATFORM_COLOR_SUPPORT \
	-DPLATFORM_CURSOR_SUPPORT \
	-DPLATFORM_CURSOR_SHAPE_SUPPORT \
	-DPLATFORM_FADE_SUPPORT \
	-DPLATFORM_LIVE_MAP_SUPPORT \
	-DPLATFORM_PRELOAD_SUPPORT \
	-DOPTIMIZED_MAP_RENDERING \
	-DPLATFORM_MAP_COUNT=14 \
	-DUSING_EXTERNAL_RENDERING

CFLAGS = -c $(INC)
GAMECFLAGS = -c $(INC) $(GAMEFLAGS)

OBJS = petrobots.o PlatformRISCOS.o palette.o modplay.o inflate.o

all: $(OBJS)

petrobots.o: petrobots.c petrobots.h Platform.h
	$(CC) $(GAMECFLAGS) -o $@ petrobots.c

PlatformRISCOS.o: PlatformRISCOS.c Platform.h palette.h thirdparty/modplay.h
	$(CC) $(GAMECFLAGS) -o $@ PlatformRISCOS.c

palette.o: palette.c palette.h Platform.h
	$(CC) $(CFLAGS) -o $@ palette.c

modplay.o: thirdparty/modplay.c thirdparty/modplay.h
	$(CC) $(CFLAGS) -DUSING_EXTERNAL_RENDERING -o $@ thirdparty/modplay.c

inflate.o: thirdparty/inflate.c thirdparty/inflate.h
	$(CC) $(CFLAGS) -o $@ thirdparty/inflate.c

# Final AIF link (on RISC OS, DDE). Run on a RISC OS machine with the
# DDE "SharedCLibrary" stubs:
#   armlink -aof -o PETSCIIRobots.o petrobots.o PlatformRISCOS.o palette.o modplay.o inflate.o <stubs>/stubs.a
#   armlink -bin -o !PETSCIIRobots.PETSCIIRobots PETSCIIRobots.o ~/g/Norcroft/lib/stubs.a
# (or use the DDE "link" frontend + Resolver).
link:
	@echo "Linking must be done on RISC OS (DDE armlink). See comments in this Makefile."

clean:
	rm -f $(OBJS)

.PHONY: all link clean