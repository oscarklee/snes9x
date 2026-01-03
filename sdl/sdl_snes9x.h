#include <SDL2/SDL.h>
#include "port.h"
#include "conffile.h"
#include "common/audio/s9x_sound_driver_sdl.hpp"
#include <vector>
#include <string>

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

void S9xMenuInit(void);
void S9xMenuDraw(void);
void S9xMenuLoadSelected(void);
bool8 S9xLoadROM(const char *filename);
void S9xDeinitInputDevices(void);




