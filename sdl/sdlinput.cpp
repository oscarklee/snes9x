/***********************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.

  See CREDITS file to find the copyright owners of this file.

  SDL Input/Audio/Video code (many lines of code come from snes9x & drnoksnes)
  (c) Copyright 2011         Makoto Sugano (makoto.sugano@gmail.com)

  Snes9x homepage: http://www.snes9x.com/

  Permission to use, copy, modify and/or distribute Snes9x in both binary
  and source form, for non-commercial purposes, is hereby granted without
  fee, providing that this license information and copyright notice appear
  with all copies and any derived work.

  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software or it's derivatives.

  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes,
  but is not limited to, charging money for Snes9x or software derived from
  Snes9x, including Snes9x or derivatives in commercial game bundles, and/or
  using Snes9x as a promotion for your commercial product.

  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.

  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
 ***********************************************************************************/

#include "sdl_snes9x.h"

#include "snes9x.h"
#include "port.h"
#include "controls.h"
#include "apu/apu.h"
#include "memmap.h"
#include "cheats.h"
#include "display.h"

#include <map>

#define SPEED_SLOW_PERCENT 25
#define SPEED_FAST_PERCENT 175

using namespace std;
std::map <string, int> name_sdlkeysym;
static std::map<SDL_JoystickID, SDL_Joystick*> open_joysticks;
static std::map<SDL_JoystickID, int> instance_to_pad;

static bool button4_held = false;
static bool button5_held = false;

ConfigFile::secvec_t	keymaps;

s9xcommand_t S9xInitCommandT (const char *n)
{
	s9xcommand_t	cmd;

	cmd.type         = S9xBadMapping;
	cmd.multi_press  = 0;
	cmd.button_norpt = 0;
	cmd.port[0]      = 0xff;
	cmd.port[1]      = 0;
	cmd.port[2]      = 0;
	cmd.port[3]      = 0;

	return (cmd);
}

char * S9xGetDisplayCommandName (s9xcommand_t cmd)
{
	return (strdup("None"));
}

void S9xHandleDisplayCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{
	return;
}

// domaemon: 2) here we send the keymapping request to the SNES9X
// domaemon: MapInput (J, K, M)
bool8 S9xMapInput (const char *n, s9xcommand_t *cmd)
{
	int	i, j, d;
	char	*c;

	// domaemon: linking PseudoPointer# and command
	if (!strncmp(n, "PseudoPointer", 13))
	{
		if (n[13] >= '1' && n[13] <= '8' && n[14] == '\0')
		{
			return (S9xMapPointer(PseudoPointerBase + (n[13] - '1'), *cmd, false));
		}
		else
		{
			goto unrecog;
		}
	}

	// domaemon: linking PseudoButton# and command
	if (!strncmp(n, "PseudoButton", 12))
	{
		if (isdigit(n[12]) && (j = strtol(n + 12, &c, 10)) < 256 && (c == NULL || *c == '\0'))
		{
			return (S9xMapButton(PseudoButtonBase + j, *cmd, false));
		}
		else
		{
			goto unrecog;
		}
	}

	if (!(isdigit(n[1]) && isdigit(n[2]) && n[3] == ':'))
		goto unrecog;

	switch (n[0])
	{
		case 'J': // domaemon: joysticks input mapping
		{
			d = ((n[1] - '0') * 10 + (n[2] - '0')) << 24;
			d |= 0x80000000;
			i = 4;
			
			if (!strncmp(n + i, "Axis", 4))	// domaemon: joystick axis
			{
				d |= 0x8000; // Axis mode
				i += 4;
			}
			else if (n[i] == 'B') // domaemon: joystick button
			{	
				i++;
			}
			else
			{
				goto unrecog;
			}
			
			d |= j = strtol(n + i, &c, 10); // Axis or Button id
			if ((c != NULL && *c != '\0') || j > 0x3fff)
				goto unrecog;
			
			if (d & 0x8000)
				return (S9xMapAxis(d, *cmd, false));
			
			return (S9xMapButton(d, *cmd, false));
		}

		case 'K':
		{
			d = 0x00000000;
			
			for (i = 4; n[i] != '\0' && n[i] != '+'; i++) ;
			
			if (n[i] == '\0' || i == 4) {
				// domaemon: if no mod keys are found.
				i = 4;
			}
			else
			{
				// domaemon: mod keys are not supported now.
				goto unrecog;
			}

			string keyname (n + i); // domaemon: SDL_keysym in string format.
			
			d |= name_sdlkeysym[keyname];
			return (S9xMapButton(d, *cmd, false));
		}

		case 'M':
		{
			d = 0x40000000;

			if (!strncmp(n + 4, "Pointer", 7))
			{
				d |= 0x8000;
				
				if (n[11] == '\0')
					return (S9xMapPointer(d, *cmd, true));
				
				i = 11;
			}
			else if (n[4] == 'B')
			{
				i = 5;
			}
			else
			{
				goto unrecog;
			}
			
			d |= j = strtol(n + i, &c, 10);
			
			if ((c != NULL && *c != '\0') || j > 0x7fff)
				goto unrecog;
			
			if (d & 0x8000)
				return (S9xMapPointer(d, *cmd, true));

			return (S9xMapButton(d, *cmd, false));
		}
	
		default:
			break;
	}

unrecog:
	char	*err = new char[strlen(n) + 34];

	sprintf(err, "Unrecognized input device name '%s'", n);
	perror(err);
	delete [] err;

	return (false);
}

// domaemon: SetupDefaultKeymap -> MapInput (JS) -> MapDisplayInput (KB)
void S9xSetupDefaultKeymap (void)
{
	s9xcommand_t	cmd;

	S9xUnmapAllControls();

	for (ConfigFile::secvec_t::iterator i = keymaps.begin(); i != keymaps.end(); i++)
	{
		cmd = S9xInitCommandT(i->second.c_str());

		if (cmd.type == S9xBadMapping)
		{
			cmd = S9xGetCommandT(i->second.c_str());
			if (cmd.type == S9xBadMapping)
			{
				std::string	s("Unrecognized command '");
				s += i->second + "'";
				perror(s.c_str());
				continue;
			}
		}

		if (!S9xMapInput(i->first.c_str(), &cmd))
		{
			std::string	s("Could not map '");
			s += i->second + "' to '" + i->first + "'";
			perror(s.c_str());
			continue;
		}
	}

	keymaps.clear();
}

// domaemon: FIXME, just collecting the essentials.
// domaemon: *) here we define the keymapping.
void S9xParseInputConfig (ConfigFile &conf, int pass)
{
	keymaps.clear();
	if (!conf.GetBool("Unix::ClearAllControls", false))
	{
		// Using 'Joypad# Axis'
		keymaps.push_back(strpair_t("J00:Axis0",      "Joypad1 Axis Left/Right T=50%"));
		keymaps.push_back(strpair_t("J00:Axis1",      "Joypad1 Axis Up/Down T=50%"));

		keymaps.push_back(strpair_t("J00:B0",         "Joypad1 B"));
		keymaps.push_back(strpair_t("J00:B1",         "Joypad1 A"));
		keymaps.push_back(strpair_t("J00:B2",         "Joypad1 X"));
		keymaps.push_back(strpair_t("J00:B3",         "Joypad1 Y"));
		keymaps.push_back(strpair_t("J00:B6",         "Joypad1 L"));
		keymaps.push_back(strpair_t("J00:B7",         "Joypad1 R"));
		keymaps.push_back(strpair_t("J00:B8",         "Joypad1 Select"));
		keymaps.push_back(strpair_t("J00:B9",         "Joypad1 Start"));
		keymaps.push_back(strpair_t("J00:B11",        "QuickSave000"));
		keymaps.push_back(strpair_t("J00:B12",        "QuickLoad000"));
		keymaps.push_back(strpair_t("J00:B13",        "Joypad1 Up"));
		keymaps.push_back(strpair_t("J00:B14",        "Joypad1 Down"));
		keymaps.push_back(strpair_t("J00:B15",        "Joypad1 Left"));
		keymaps.push_back(strpair_t("J00:B16",        "Joypad1 Right"));

		keymaps.push_back(strpair_t("K00:SDLK_RIGHT",        "Joypad1 Right"));
		keymaps.push_back(strpair_t("K00:SDLK_LEFT",         "Joypad1 Left"));
		keymaps.push_back(strpair_t("K00:SDLK_DOWN",         "Joypad1 Down"));
		keymaps.push_back(strpair_t("K00:SDLK_UP",           "Joypad1 Up"));
		keymaps.push_back(strpair_t("K00:SDLK_RETURN",       "Joypad1 Start"));
		keymaps.push_back(strpair_t("K00:SDLK_SPACE",        "Joypad1 Select"));
		keymaps.push_back(strpair_t("K00:SDLK_d",            "Joypad1 A"));
		keymaps.push_back(strpair_t("K00:SDLK_c",            "Joypad1 B"));
		keymaps.push_back(strpair_t("K00:SDLK_s",            "Joypad1 X"));
		keymaps.push_back(strpair_t("K00:SDLK_x",            "Joypad1 Y"));
		keymaps.push_back(strpair_t("K00:SDLK_a",            "Joypad1 L"));
		keymaps.push_back(strpair_t("K00:SDLK_z",            "Joypad1 R"));

		// domaemon: *) GetSDLKeyFromName().
		name_sdlkeysym["SDLK_s"] = SDLK_s;
		name_sdlkeysym["SDLK_d"] = SDLK_d;
		name_sdlkeysym["SDLK_x"] = SDLK_x;
		name_sdlkeysym["SDLK_c"] = SDLK_c;
		name_sdlkeysym["SDLK_a"] = SDLK_a;
		name_sdlkeysym["SDLK_z"] = SDLK_z;
		name_sdlkeysym["SDLK_UP"] = SDLK_UP;
		name_sdlkeysym["SDLK_DOWN"] = SDLK_DOWN;
		name_sdlkeysym["SDLK_RIGHT"] = SDLK_RIGHT;
		name_sdlkeysym["SDLK_LEFT"] = SDLK_LEFT;
		name_sdlkeysym["SDLK_RETURN"] = SDLK_RETURN;
		name_sdlkeysym["SDLK_SPACE"] = SDLK_SPACE;
		name_sdlkeysym["SDLK_q"] = SDLK_q;
	}

	return;
}

static void S9xOpenJoystick(int index)
{
    SDL_Joystick *joy = SDL_JoystickOpen(index);
    if (joy)
    {
        SDL_JoystickID instance = SDL_JoystickInstanceID(joy);
        if (open_joysticks.find(instance) == open_joysticks.end())
        {
            open_joysticks[instance] = joy;
            // Assign the first available pad index (0-7)
            int pad = 0;
            for (pad = 0; pad < 8; pad++)
            {
                bool taken = false;
                for (auto const& [inst, p] : instance_to_pad)
                {
                    if (p == pad) { taken = true; break; }
                }
                if (!taken) break;
            }
            instance_to_pad[instance] = pad;

            printf("Joystick connected: %s\n", SDL_JoystickName(joy));
            printf("  Instance ID: %d, Pad Slot: %d\n", (int)instance, pad);
            printf("  %d-axis %d-buttons %d-balls %d-hats\n",
                   SDL_JoystickNumAxes(joy),
                   SDL_JoystickNumButtons(joy),
                   SDL_JoystickNumBalls(joy),
                   SDL_JoystickNumHats(joy));
        }
        else
        {
            SDL_JoystickClose(joy);
        }
    }
}

static void S9xCloseJoystick(SDL_JoystickID instance)
{
    if (open_joysticks.count(instance))
    {
        printf("Joystick disconnected (Instance %d, Pad %d)\n", (int)instance, instance_to_pad[instance]);
        SDL_JoystickClose(open_joysticks[instance]);
        open_joysticks.erase(instance);
        instance_to_pad.erase(instance);
    }
}

void S9xInitInputDevices (void)
{
	// domaemon: 1) initializing the joystic subsystem
	SDL_InitSubSystem (SDL_INIT_JOYSTICK);
    SDL_JoystickEventState(SDL_ENABLE);

	int num_joysticks = SDL_NumJoysticks();

	if (num_joysticks == 0)
	{
		printf("joystick: No joystick found. Waiting for connection...\n");
	}
	else
	{
		for (int i = 0; i < num_joysticks; i++)
		{
            S9xOpenJoystick(i);
		}
	}
}

void S9xProcessEvents (bool8 block)
{
	SDL_Event event;
	bool8 quit_state = FALSE;
    static uint32 event_count = 0;
    static uint32 last_log_time = 0;

	while (block ? SDL_WaitEvent(&event) : SDL_PollEvent(&event))
	{
        event_count++;
		switch (event.type) {
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			// domaemon: not sure it's the best idea, but reserving the SDLK_q for quit.
			if (event.key.keysym.sym == SDLK_q)
			{
				quit_state = TRUE;
			}
			else if (g_state == STATE_MENU && event.type == SDL_KEYDOWN)
			{
				if (event.key.keysym.sym == SDLK_LEFT)
				{
					S9xMenuMoveLeft();
				}
				else if (event.key.keysym.sym == SDLK_RIGHT)
				{
					S9xMenuMoveRight();
				}
				else if (event.key.keysym.sym == SDLK_UP)
				{
					S9xMenuMoveUp();
				}
				else if (event.key.keysym.sym == SDLK_DOWN)
				{
					S9xMenuMoveDown();
				}
				else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE)
				{
					S9xMenuLoadSelected();
				}
			}
			else
			{
				S9xReportButton(event.key.keysym.mod << 16 | // keyboard mod
						event.key.keysym.sym, // keyboard ksym
						event.type == SDL_KEYDOWN); // press or release
			}
			break;

/***** Joystick starts *****/

        case SDL_JOYDEVICEADDED:
            S9xOpenJoystick(event.jdevice.which);
            break;

        case SDL_JOYDEVICEREMOVED:
            S9xCloseJoystick(event.jdevice.which);
            break;

		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
            if (event.jbutton.button == 4 || event.jbutton.button == 5)
            {
                if (event.jbutton.button == 4) button4_held = (event.type == SDL_JOYBUTTONDOWN);
                if (event.jbutton.button == 5) button5_held = (event.type == SDL_JOYBUTTONDOWN);

                uint32 baseFrameTime = Settings.PAL ? Settings.FrameTimePAL : Settings.FrameTimeNTSC;
                if (button4_held) Settings.FrameTime = baseFrameTime * 100 / SPEED_SLOW_PERCENT;
                else if (button5_held) Settings.FrameTime = baseFrameTime * 100 / SPEED_FAST_PERCENT;
                else Settings.FrameTime = baseFrameTime;
                break;
            }

            if (event.type == SDL_JOYBUTTONDOWN)
            {
                if (event.jbutton.button == 10) // HOME
                {
                    if (g_state == STATE_MENU)
                    {
                        printf ("Quit Event. Bye.\n");
                        S9xExit();
                    }
                    else
                    {
                        Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR).c_str());
                        S9xSaveCheatFile(S9xGetFilename(".cht", CHEAT_DIR).c_str());
                        Settings.StopEmulation = TRUE;
                        S9xSetSoundMute(TRUE);
                        S9xMenuInit();
                        return;
                    }
                }

                if (g_state == STATE_MENU)
                {
                    if (event.jbutton.button == 15) // D-Pad Left
                    {
                        S9xMenuMoveLeft();
                    }
                    else if (event.jbutton.button == 16) // D-Pad Right
                    {
                        S9xMenuMoveRight();
                    }
                    else if (event.jbutton.button == 13) // D-Pad Up
                    {
                        S9xMenuMoveUp();
                    }
                    else if (event.jbutton.button == 14) // D-Pad Down
                    {
                        S9xMenuMoveDown();
                    }
                    else if (event.jbutton.button <= 3) // A/B/X/Y to select
                    {
                        S9xMenuLoadSelected();
                    }
                    return;
                }
            }

            if (instance_to_pad.count(event.jbutton.which))
            {
                int pad = instance_to_pad[event.jbutton.which];
                S9xReportButton(0x80000000 | // joystick button
                        (pad << 24) | // pad index
                        event.jbutton.button, // joystick button code
                        event.type == SDL_JOYBUTTONDOWN); // press or release
            }
			break;

		case SDL_JOYAXISMOTION:
            if (instance_to_pad.count(event.jaxis.which))
            {
                int pad = instance_to_pad[event.jaxis.which];
                S9xReportAxis(0x80008000 | // joystick axis
                          (pad << 24) | // pad index
                          event.jaxis.axis, // joystick axis
                          event.jaxis.value); // axis value
            }
			break;

/***** Joystick ends *****/

		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			quit_state = TRUE;
			break;
		}

		if (block)
			break;
	}

    uint32 now = SDL_GetTicks();
    if (now - last_log_time >= 5000)
    {
        if (event_count > 0)
        {
            event_count = 0;
        }
        last_log_time = now;
    }
	
	if (quit_state == TRUE)
	{
		printf ("Quit Event. Bye.\n");
		S9xExit();
	}
}

void S9xDeinitInputDevices(void)
{
    for (auto it = open_joysticks.begin(); it != open_joysticks.end(); ++it)
    {
        SDL_JoystickClose(it->second);
    }
    open_joysticks.clear();
    instance_to_pad.clear();
}

bool S9xPollButton (uint32 id, bool *pressed)
{
	return (false);
}

bool S9xPollAxis (uint32 id, int16 *value)
{
	return (false);
}

bool S9xPollPointer (uint32 id, int16 *x, int16 *y)
{
	return (false);
}

// domaemon: needed by SNES9X
void S9xHandlePortCommand (s9xcommand_t cmd, int16 data1, int16 data2)
{
	return;
}


#if 0

main()
{
	int i;
	SDL_Joystick * joysticks[4] = {NULL, NULL, NULL, NULL};
	SDL_Event event;
	SDL_Surface *screen;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0)
	{
		printf("Unable to initialize SDL: %s\n", SDL_GetError());
	}
  
	atexit(SDL_Quit);

	screen = SDL_SetVideoMode(64, 64, 16, 0);

	for (i = 0; i < SDL_NumJoysticks(); i++)
	{
		printf ("  %s\n", SDL_JoystickName(i));
		joysticks[i] = SDL_JoystickOpen (i);
	}

	SDL_JoystickEventState (SDL_ENABLE);

	while (SDL_WaitEvent (&event) != 0) 
	{
		switch (event.type) {
		case SDL_KEYDOWN:
			printf ("KEYDOWN\n");
			break;
		case SDL_JOYAXISMOTION:
			printf ("JOYAXISMOTION\n");
			break;
		case SDL_JOYBUTTONDOWN:
			printf ("JOYBUTTONDOWN\n");
			printf ("%d \n", event.jbutton.button);
			break;
		case SDL_JOYHATMOTION:
			switch (event.jhat.value) {
			case SDL_HAT_UP:
				printf ("SDL_HAT_UP\n");
				break;
			case SDL_HAT_DOWN:
				printf ("SDL_HAT_DOWN\n");
				break;
			case SDL_HAT_LEFT:
				printf ("SDL_HAT_LEFT\n");
				break;
			case SDL_HAT_RIGHT:
				printf ("SDL_HAT_RIGHT\n");
				break;
			}

			printf ("JOYHATMOTION\n");
			break;
		case SDL_QUIT:
			// domaemon: we come here when the window is getting closed.
			SDL_Quit();
		}
	}
}
	
// domaemon: 2) here we send the keymapping request to the SNES9X
bool8 S9xMapDisplayInput (const char *n, s9xcommand_t *cmd)
{
	int	i, d;

	if (!isdigit(n[1]) || !isdigit(n[2]) || n[3] != ':')
		goto unrecog;

	d = ((n[1] - '0') * 10 + (n[2] - '0')) << 24;

	switch (n[0])
	{
		case 'K':
		{
			int key;

			d |= 0x00000000;

			for (i = 4; n[i] != '\0' && n[i] != '+'; i++) ;

			if (n[i] == '\0' || i == 4) // domaemon: no mod keys.
				i = 4;

#if 0 // domaemon: mod keys not working properly.
                        else // domaemon: with mod keys
                        {
                                for (i = 4; n[i] != '+'; i++)
                                {
                                        switch (n[i])
                                        {
                                                case 'S': d |= KMOD_SHIFT  << 16; break;
                                                case 'C': d |= KMOD_CTRL   << 16; break;
                                                case 'A': d |= KMOD_ALT    << 16; break;
                                                case 'M': d |= KMOD_META   << 16; break;
                                                default:  goto unrecog;
                                        }
                                }
                                i++;
                        }
#endif

			string keyname (n + i); // domaemon: SDL_keysym in string format.
			key = name_sdlkeysym[keyname];

			d |= key;
			return (S9xMapButton(d, *cmd, false));

		}

		case 'M':
		{
			char	*c;
			int		j;

			d |= 0x40000000;

			if (!strncmp(n + 4, "Pointer", 7))
			{
				d |= 0x8000;

				if (n[11] == '\0')
					return (S9xMapPointer(d, *cmd, true));

				i = 11;
			}
			else
			if (n[4] == 'B')
				i = 5;
			else
				goto unrecog;

			d |= j = strtol(n + i, &c, 10);

			if ((c != NULL && *c != '\0') || j > 0x7fff)
				goto unrecog;

			if (d & 0x8000)
				return (S9xMapPointer(d, *cmd, true));

			return (S9xMapButton(d, *cmd, false));
		}

		default:
			break;
	}

unrecog:
	char	*err = new char[strlen(n) + 34];

	sprintf(err, "Unrecognized input device name '%s'", n);
	perror(err);
	delete [] err;

	return (false);
}

#endif
