#ifndef SDL_SNES9X_H
#define SDL_SNES9X_H

#include <SDL2/SDL.h>
#include "port.h"
#include "conffile.h"
#include "common/audio/s9x_sound_driver_sdl.hpp"
#include <vector>
#include <string>

class MenuCarousel;

typedef std::pair<std::string, std::string>	strpair_t;
extern ConfigFile::secvec_t	keymaps;
extern S9xSDLSoundDriver *SoundDriver;

enum {
    STATE_MENU,
    STATE_GAME
};

extern int g_state;
extern std::vector<std::string> g_rom_list;
extern int g_menu_selection;
extern MenuCarousel* g_carousel;

void S9xMenuInit(void);
void S9xMenuDraw(void);
void S9xMenuUpdate(float deltaTime);
void S9xMenuLoadSelected(void);
void S9xMenuMoveLeft(void);
void S9xMenuMoveRight(void);
void S9xMenuMoveUp(void);
void S9xMenuMoveDown(void);
bool8 S9xLoadROM(const char *filename);
void S9xDeinitInputDevices(void);
bool8 S9xSaveWithRotation(void);
void S9xDeleteCurrentSaveAndReload(void);
void S9xSetVolume(int volume);
int S9xGetVolume(void);

SDL_Renderer* S9xGetRenderer(void);
void S9xGetViewport(int &x, int &y, int &w, int &h);

#endif
