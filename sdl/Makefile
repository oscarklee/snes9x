S9XDEBUGGER=1
S9XZIP=1
S9XJMA=1

# Fairly good and special-char-safe descriptor of the os being built on.
OS         = `uname -s -r -m|sed \"s/ /-/g\"|tr \"[A-Z]\" \"[a-z]\"|tr \"/()\" \"___\"`
BUILDDIR   = .

OBJECTS    = ../apu/apu.o ../apu/bapu/dsp/sdsp.o ../apu/bapu/smp/smp.o ../apu/bapu/smp/smp_state.o ../bsx.o ../c4.o ../c4emu.o ../cheats.o ../cheats2.o ../clip.o ../conffile.o ../controls.o ../cpu.o ../cpuexec.o ../cpuops.o ../crosshairs.o ../dma.o ../dsp.o ../dsp1.o ../dsp2.o ../dsp3.o ../dsp4.o ../fxinst.o ../fxemu.o ../gfx.o ../globals.o ../memmap.o ../msu1.o ../movie.o ../obc1.o ../ppu.o ../stream.o ../sa1.o ../sa1cpu.o ../screenshot.o ../sdd1.o ../sdd1emu.o ../seta.o ../seta010.o ../seta011.o ../seta018.o ../snapshot.o ../snes9x.o ../spc7110.o ../srtc.o ../tile.o ../tileimpl-n1x1.o ../tileimpl-n2x1.o ../tileimpl-h2x1.o ../filter/2xsai.o ../filter/blit.o ../filter/epx.o ../filter/hq2x.o ../filter/snes_ntsc.o ../statemanager.o ../sha256.o ../bml.o ../fscompat.o ../common/audio/s9x_sound_driver_sdl.o logger.o sdlmain.o sdlvideo.o sdlinput.o menu/MenuCarousel.o menu/BoxartManager.o menu/StringMatcher.o
DEFS       = 

ifdef S9XDEBUGGER
OBJECTS   += ../debug.o ../fxdbg.o
endif

ifdef S9XZIP
OBJECTS   += ../loadzip.o ../unzip/ioapi.o ../unzip/unzip.o
INCLUDES   = -I../unzip/
endif

ifdef S9XJMA
OBJECTS   += ../jma/7zlzma.o ../jma/crc32.o ../jma/iiostrm.o ../jma/inbyte.o ../jma/jma.o ../jma/lzma.o ../jma/lzmadec.o ../jma/s9x-jma.o ../jma/winout.o
endif

CCC        = g++
CC         = gcc
GASM       = g++
INCLUDES   += -I. -I.. -I../apu/ -I../apu/bapu -I../jma/ -I../filter/ -Imenu/ `pkg-config --cflags sdl2 SDL2_image zlib libpng libcurl`

# Optimization flags for Raspberry Pi Zero (ARMv8 / AArch64)
# CCFLAGS    = -O3 -mcpu=cortex-a53 -funsafe-math-optimizations -std=gnu++14
# CFLAGS     = -O3 -mcpu=cortex-a53 -funsafe-math-optimizations

COMMON_FLAGS = -O3 -fomit-frame-pointer -fno-exceptions -fno-rtti -pedantic -Wall -W -Wno-unused-parameter -DJOYSTICK_SUPPORT -DZLIB -DJMA_SUPPORT -DHAVE_LIBPNG -DHAVE_MKSTEMP -DHAVE_STRINGS_H -DHAVE_SYS_IOCTL_H -DHAVE_STDINT_H -DRIGHTSHIFT_IS_SAR -DUNZIP_SUPPORT $(DEFS)
CCFLAGS    = $(COMMON_FLAGS) -std=gnu++17
CFLAGS     = $(COMMON_FLAGS)

.SUFFIXES: .o .cpp .c .cc .h .m .i .s .obj

all: snes9x

snes9x: $(OBJECTS)
	$(CCC) $(LDFLAGS) $(INCLUDES) -o $@ $(OBJECTS) -lm `pkg-config --libs sdl2 SDL2_image zlib libpng libcurl`

menu/%.o: menu/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../jma/s9x-jma.o: ../jma/s9x-jma.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/7zlzma.o: ../jma/7zlzma.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/crc32.o: ../jma/crc32.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/iiostrm.o: ../jma/iiostrm.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/inbyte.o: ../jma/inbyte.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/jma.o: ../jma/jma.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/lzma.o: ../jma/lzma.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/lzmadec.o: ../jma/lzmadec.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@
../jma/winout.o: ../jma/winout.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) -fexceptions $*.cpp -o $@

.cpp.o:
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $*.cpp -o $@

.c.o:
	$(CC) $(INCLUDES) -c $(CFLAGS) $*.c -o $@

../%.o: ../%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../%.o: ../%.c
	$(CC) $(INCLUDES) -c $(CFLAGS) $< -o $@

../filter/%.o: ../filter/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../apu/%.o: ../apu/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../apu/bapu/dsp/%.o: ../apu/bapu/dsp/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../apu/bapu/smp/%.o: ../apu/bapu/smp/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../common/audio/%.o: ../common/audio/%.cpp
	$(CCC) $(INCLUDES) -c $(CCFLAGS) $< -o $@

../unzip/%.o: ../unzip/%.c
	$(CC) $(INCLUDES) -c $(CFLAGS) $< -o $@

.cpp.S:
	$(GASM) $(INCLUDES) -S $(CCFLAGS) $*.cpp -o $@

.cpp.i:
	$(GASM) $(INCLUDES) -E $(CCFLAGS) $*.cpp -o $@

.S.o:
	$(GASM) $(INCLUDES) -c $(CCFLAGS) $*.S -o $@

.S.i:
	$(GASM) $(INCLUDES) -c -E $(CCFLAGS) $*.S -o $@

.s.o:
	@echo Compiling $*.s
	sh-elf-as -little $*.s -o $@

.obj.o:
	cp $*.obj $*.o

clean:
	rm -f $(OBJECTS) snes9x menu/*.o

