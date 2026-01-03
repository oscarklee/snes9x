#include <SDL2/SDL.h>
#include "port.h"
#include "conffile.h"
#include "common/audio/s9x_sound_driver_sdl.hpp"

typedef std::pair<std::string, std::string>	strpair_t;
extern ConfigFile::secvec_t	keymaps;
extern S9xSDLSoundDriver *SoundDriver;




