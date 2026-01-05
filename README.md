# Snes9x SDL2 - Optimized for Raspberry Pi Zero 2W

This is a specialized fork of the latest **Snes9x (v1.63 core)**, specifically modified and optimized for performance on resource-constrained devices like the **Raspberry Pi Zero 2W** (aarch64). 

It features a modernized **SDL2 frontend** integrated from legacy Snes9x ports, providing high-performance video, audio, and input handling on Linux-based systems without the need for a full X11 or GTK desktop environment.

## Key Features
- **SDL2 Integration**: Native SDL2 support for video, audio, and input.
- **Optimized for aarch64**: Pre-configured flags for the Cortex-A53 processor.
- **ROM Selection Menu**: Built-in graphical menu to browse and launch games using your gamepad.
- **Gamepad Hotplugging**: Connect or disconnect Bluetooth gamepads dynamically while the emulator is running.
- **Minimal Dependencies**: Designed to run efficiently on bare-metal Linux or minimal OS distributions.

## Dependencies
Ensure you have the following libraries installed:
- `libsdl2-dev`
- `zlib1g-dev`
- `libpng-dev`
- `libasound2-dev` (optional, for ALSA backend through SDL)

## Build Instructions

### Configuration
Navigate to the `sdl` directory to configure the build:

```bash
cd snes9x/sdl
./configure --build=aarch64-unknown-linux-gnu
```

### Compilation
The project uses specific compiler optimizations to achieve high performance on the Pi Zero. We use the **gnu++17** standard. 

**IMPORTANT**: Due to memory limitations on the Raspberry Pi Zero, you **must** compile using a single thread (`-j1`). Multi-threaded compilation will likely cause the system to run out of memory.

```bash
make -j1 CFLAGS="-O3 -mcpu=cortex-a53 -funsafe-math-optimizations" \
         CXXFLAGS="-O3 -mcpu=cortex-a53 -funsafe-math-optimizations -std=gnu++17"
```

## Usage

### Launching Games
You can launch a ROM directly from the command line:
```bash
./snes9x ~/.snes9x/rom/SuperMarioWorld.zip
```

### Options
- `-aspect-ratio <ratio>`: Forces a specific aspect ratio (e.g. `4:3`, `16:9`, or `1.33`). This affects both the menu and the games.
- `-res <WIDTHxHEIGHT>`: Sets the window/screen resolution.
- `-fullscreen`: Enables fullscreen mode.

### ROM Selection Menu
If you execute `./snes9x` without parameters, the emulator will launch a graphical menu listing all ROMs available in `~/.snes9x/rom`.
- **Supported extensions**: `.sfc`, `.zip`.
- **Navigation**: Use the **D-pad** (up/down) to move and **Action Buttons** (A/B/X/Y) to select.
- **Home Button**: 
  - If in the menu: Quits the application.
  - If in a game: Returns to the ROM selection menu (properly freeing memory and saving SRAM).

## Standards 
This repository contains specific architectural decisions that contribute to its stability and performance:

1.  **Methodology**: The project development follows a methodical approach: code investigation -> core understanding -> implementation with quality standards -> remote compilation -> error resolution.
2.  **Audio Implementation**: Audio is handled via an SDL callback system. The emulator core generates samples into an internal resampler, which the SDL thread then pulls and mixes. 
3.  **Input Hotplug**: Input events (added/removed) are handled dynamically using SDL instance IDs mapped to SNES pad slots.
4.  **Memory Management**: When returning to the menu from a game, `Memory.Deinit()` is called, which has been enhanced to explicitly clear and shrink vector storage to prevent memory fragmentation on low-RAM devices.
5.  **Build System**: The `sdl/Makefile` is a customized version that handles mixed C and C++ sources with standard-specific flags (avoiding `gnu++17` for C-only files like `unzip`).

When working on this project, maintain the modularity and reuse patterns already established in `sdlmain.cpp`, `sdlvideo.cpp`, and `sdlinput.cpp`.
