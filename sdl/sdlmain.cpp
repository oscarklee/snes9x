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

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "sdl_snes9x.h"

#include "snes9x.h"
#include "memmap.h"
#include "apu/apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "controls.h"
#include "cheats.h"
#include "movie.h"
#include "logger.h"
#include "display.h"
#include "conffile.h"
#include "fscompat.h"
#ifdef NETPLAY_SUPPORT
#include "netplay.h"
#endif
#ifdef DEBUGGER
#include "debug.h"
#endif

#ifdef NETPLAY_SUPPORT
#ifdef _DEBUG
#define NP_DEBUG 2
#endif
#endif

static std::string	s9x_base_dir;
static std::string	rom_filename;
static std::string	snapshot_filename;
static std::string	play_smv_filename;
static std::string	record_smv_filename;

S9xSDLSoundDriver *SoundDriver = nullptr;
uint32           sound_buffer_size; // used in sdlaudio

static const char	dirNames[LAST_DIR][32] =
{
	"",				// DEFAULT_DIR
	"",				// HOME_DIR
	"",				// ROMFILENAME_DIR
	"rom",			// ROM_DIR
	"sram",			// SRAM_DIR
	"savestate",	// SNAPSHOT_DIR
	"screenshot",	// SCREENSHOT_DIR
	"spc",			// SPC_DIR
	"cheat",		// CHEAT_DIR
	"patch",		// PATCH_DIR
	"bios",			// BIOS_DIR
	"log",			// LOG_DIR
	"sat"			// SAT_DIR
};

#ifdef NETPLAY_SUPPORT
static uint32	joypads[8];
static uint32	old_joypads[8];
#endif

void S9xParseInputConfig(ConfigFile &, int pass); // defined in sdlinput

static long log2 (long);
static void NSRTControllerSetup (void);
static int make_snes9x_dirs (void);

static long log2 (long num)
{
	long	n = 0;

	while (num >>= 1)
		n++;

	return (n);
}

void S9xExtraUsage (void) // domaemon: ExtraUsage -> ExtraDisplayUsage
{
	/*                               12345678901234567890123456789012345678901234567890123456789012345678901234567890 */

	S9xMessage(S9X_INFO, S9X_USAGE, "-multi                          Enable multi cartridge system");
	S9xMessage(S9X_INFO, S9X_USAGE, "-carta <filename>               ROM in slot A (use with -multi)");
	S9xMessage(S9X_INFO, S9X_USAGE, "-cartb <filename>               ROM in slot B (use with -multi)");
	S9xMessage(S9X_INFO, S9X_USAGE, "");

	S9xMessage(S9X_INFO, S9X_USAGE, "-buffersize                     Sound generating buffer size in millisecond");
	S9xMessage(S9X_INFO, S9X_USAGE, "");

	S9xMessage(S9X_INFO, S9X_USAGE, "-loadsnapshot                   Load snapshot file at start");
	S9xMessage(S9X_INFO, S9X_USAGE, "-playmovie <filename>           Start emulator playing the .smv file");
	S9xMessage(S9X_INFO, S9X_USAGE, "-recordmovie <filename>         Start emulator recording the .smv file");
	S9xMessage(S9X_INFO, S9X_USAGE, "-dumpstreams                    Save audio/video data to disk");
	S9xMessage(S9X_INFO, S9X_USAGE, "-dumpmaxframes <num>            Stop emulator after saving specified number of");
	S9xMessage(S9X_INFO, S9X_USAGE, "                                frames (use with -dumpstreams)");
	S9xMessage(S9X_INFO, S9X_USAGE, "");

	S9xExtraDisplayUsage();
}

/*
 * domaemon: arg is parsed as ParseArg -> ParseDisplayArg
 */
void S9xParseArg (char **argv, int &i, int argc)
{
	if (!strcasecmp(argv[i], "-multi"))
		Settings.Multi = TRUE;
	else
	if (!strcasecmp(argv[i], "-carta"))
	{
		if (i + 1 < argc)
			strncpy(Settings.CartAName, argv[++i], PATH_MAX);
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-cartb"))
	{
		if (i + 1 < argc)
			strncpy(Settings.CartBName, argv[++i], PATH_MAX);
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-buffersize"))
	{
		if (i + 1 < argc)
			sound_buffer_size = atoi(argv[++i]);
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-loadsnapshot"))
	{
		if (i + 1 < argc)
			snapshot_filename = argv[++i];
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-playmovie"))
	{
		if (i + 1 < argc)
			play_smv_filename = argv[++i];
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-recordmovie"))
	{
		if (i + 1 < argc)
			record_smv_filename = argv[++i];
		else
			S9xUsage();
	}
	else
	if (!strcasecmp(argv[i], "-dumpstreams"))
		Settings.DumpStreams = TRUE;
	else
	if (!strcasecmp(argv[i], "-dumpmaxframes"))
		Settings.DumpStreamsMaxFrames = atoi(argv[++i]);
	else
		S9xParseDisplayArg(argv, i, argc);
}

static void NSRTControllerSetup (void)
{
	if (!strncmp((const char *) Memory.NSRTHeader + 24, "NSRT", 4))
	{
		// First plug in both, they'll change later as needed
		S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
		S9xSetController(1, CTL_JOYPAD, 1, 0, 0, 0);

		switch (Memory.NSRTHeader[29])
		{
			case 0x00:	// Everything goes
				break;

			case 0x10:	// Mouse in Port 0
				S9xSetController(0, CTL_MOUSE,      0, 0, 0, 0);
				break;

			case 0x01:	// Mouse in Port 1
				S9xSetController(1, CTL_MOUSE,      1, 0, 0, 0);
				break;

			case 0x03:	// Super Scope in Port 1
				S9xSetController(1, CTL_SUPERSCOPE, 0, 0, 0, 0);
				break;

			case 0x06:	// Multitap in Port 1
				S9xSetController(1, CTL_MP5,        1, 2, 3, 4);
				break;

			case 0x66:	// Multitap in Ports 0 and 1
				S9xSetController(0, CTL_MP5,        0, 1, 2, 3);
				S9xSetController(1, CTL_MP5,        4, 5, 6, 7);
				break;

			case 0x08:	// Multitap in Port 1, Mouse in new Port 1
				S9xSetController(1, CTL_MOUSE,      1, 0, 0, 0);
				// There should be a toggle here for putting in Multitap instead
				break;

			case 0x04:	// Pad or Super Scope in Port 1
				S9xSetController(1, CTL_SUPERSCOPE, 0, 0, 0, 0);
				// There should be a toggle here for putting in a pad instead
				break;

			case 0x05:	// Justifier - Must ask user...
				S9xSetController(1, CTL_JUSTIFIER,  1, 0, 0, 0);
				// There should be a toggle here for how many justifiers
				break;

			case 0x20:	// Pad or Mouse in Port 0
				S9xSetController(0, CTL_MOUSE,      0, 0, 0, 0);
				// There should be a toggle here for putting in a pad instead
				break;

			case 0x22:	// Pad or Mouse in Port 0 & 1
				S9xSetController(0, CTL_MOUSE,      0, 0, 0, 0);
				S9xSetController(1, CTL_MOUSE,      1, 0, 0, 0);
				// There should be a toggles here for putting in pads instead
				break;

			case 0x24:	// Pad or Mouse in Port 0, Pad or Super Scope in Port 1
				// There should be a toggles here for what to put in, I'm leaving it at gamepad for now
				break;

			case 0x27:	// Pad or Mouse in Port 0, Pad or Mouse or Super Scope in Port 1
				// There should be a toggles here for what to put in, I'm leaving it at gamepad for now
				break;

			// Not Supported yet
			case 0x99:	// Lasabirdie
				break;

			case 0x0A:	// Barcode Battler
				break;
		}
	}
}

/*
 * domaemon: config is parsed as
 *
 * ParsePortConfig -> ParseInputConfig
 * ParsePortConfig -> ParseDisplayConfig
 */

void S9xParsePortConfig (ConfigFile &conf, int pass)
{
	s9x_base_dir                = conf.GetString("Unix::BaseDir",             s9x_base_dir);
	snapshot_filename           = conf.GetString("Unix::SnapshotFilename",    "");
	play_smv_filename           = conf.GetString("Unix::PlayMovieFilename",   "");
	record_smv_filename         = conf.GetString("Unix::RecordMovieFilename", "");
	sound_buffer_size           = conf.GetUInt     ("Unix::SoundBufferSize",     100);

	// domaemon: default input configuration
	S9xParseInputConfig(conf, 1);

	std::string section = S9xParseDisplayConfig(conf, 1);

	ConfigFile::secvec_t	sec = conf.GetSection((section + " Controls").c_str());
	for (ConfigFile::secvec_t::iterator c = sec.begin(); c != sec.end(); c++)
		keymaps.push_back(*c);
}

static int make_snes9x_dirs (void)
{
	mkdir(s9x_base_dir.c_str(), 0755);

	for (int i = 0; i < LAST_DIR; i++)
	{
		if (dirNames[i][0])
		{
			std::string s = s9x_base_dir + SLASH_STR + dirNames[i];
			mkdir(s.c_str(), 0755);
		}
	}

	return (0);
}

std::string S9xGetDirectory (enum s9x_getdirtype dirtype)
{
	if (dirNames[dirtype][0])
		return s9x_base_dir + SLASH_STR + dirNames[dirtype];
	else
	{
		switch (dirtype)
		{
			case DEFAULT_DIR:
				return s9x_base_dir;

			case HOME_DIR:
				return std::string(getenv("HOME"));

			case ROMFILENAME_DIR:
			{
				std::string s = Memory.ROMFilename;
				auto pos = s.find_last_of(SLASH_CHAR);
				if (pos != std::string::npos)
					return s.substr(0, pos);
				return ".";
			}

			default:
				return "";
		}
	}
}

std::string S9xGetFilenameInc (std::string ex, enum s9x_getdirtype dirtype)
{
	struct stat		buf;
	SplitPath path = splitpath(Memory.ROMFilename);
	std::string d = S9xGetDirectory(dirtype);
	std::string s;

	unsigned int	i = 0;

	do
	{
		char idx[4];
		sprintf(idx, "%03d", i++);
		s = d + SLASH_STR + path.stem + "." + idx + ex;
	}
	while (stat(s.c_str(), &buf) == 0 && i < 1000);

	return s;
}

const char * S9xSelectFilename (const char *def, const char *dir1, const char *ext1, const char *title)
{
	static char	s[PATH_MAX + 1];
	char		buffer[PATH_MAX + 1];

	printf("\n%s (default: %s): ", title, def);
	fflush(stdout);

	if (fgets(buffer, PATH_MAX + 1, stdin))
	{
		char	*p = buffer;
		while (isspace(*p))
			p++;
		if (!*p)
		{
			strncpy(buffer, def, PATH_MAX + 1);
			buffer[PATH_MAX] = 0;
			p = buffer;
		}

		char	*q = strrchr(p, '\n');
		if (q)
			*q = 0;

		SplitPath path = splitpath(p);
		std::string res = makepath(path.drive, path.dir.empty() ? dir1 : path.dir, path.stem, path.ext.empty() ? ext1 : path.ext);
		strncpy(s, res.c_str(), PATH_MAX);
		s[PATH_MAX] = 0;

		return (s);
	}

	return (NULL);
}

const char * S9xChooseFilename (bool8 read_only)
{
	SplitPath path = splitpath(Memory.ROMFilename);
	std::string def = path.stem + ".frz";
	char		title[64];

	sprintf(title, "%s snapshot filename", read_only ? "Select load" : "Choose save");

	S9xSetSoundMute(TRUE);
	const char *filename = S9xSelectFilename(def.c_str(), S9xGetDirectory(SNAPSHOT_DIR).c_str(), "frz", title);
	S9xSetSoundMute(FALSE);

	return (filename);
}

const char * S9xChooseMovieFilename (bool8 read_only)
{
	SplitPath path = splitpath(Memory.ROMFilename);
	std::string def = path.stem + ".smv";
	char		title[64];

	sprintf(title, "Choose movie %s filename", read_only ? "playback" : "record");

	S9xSetSoundMute(TRUE);
	const char *filename = S9xSelectFilename(def.c_str(), S9xGetDirectory(HOME_DIR).c_str(), "smv", title);
	S9xSetSoundMute(FALSE);

	return (filename);
}

bool8 S9xOpenSnapshotFile (const char *filename, bool8 read_only, STREAM *file)
{
	std::string s;

	SplitPath path = splitpath(filename);

	if (!path.drive.empty() || (path.dir.length() > 0 && path.dir[0] == SLASH_CHAR) || (path.dir.length() > 1 && path.dir[0] == '.' && path.dir[1] == SLASH_CHAR))
	{
		s = filename;
	}
	else
		s = S9xGetDirectory(SNAPSHOT_DIR) + SLASH_STR + path.stem + path.ext;

	if (path.ext.empty())
		s += ".frz";

	if ((*file = OPEN_STREAM(s.c_str(), read_only ? "rb" : "wb")))
		return (TRUE);

	return (FALSE);
}

void S9xCloseSnapshotFile (STREAM file)
{
	CLOSE_STREAM(file);
}

bool8 S9xInitUpdate (void)
{
	return (TRUE);
}

bool8 S9xDeinitUpdate (int width, int height)
{
	S9xPutImage(width, height);
	return (TRUE);
}

bool8 S9xContinueUpdate (int width, int height)
{
	return (TRUE);
}

void S9xAutoSaveSRAM (void)
{
	Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR).c_str());
}

void S9xToggleSoundChannel (int channel)
{
	static uint8 sound_switch = 255;

	if (channel == 8)
		Settings.MSU1 = !Settings.MSU1;
	else
		sound_switch ^= (1 << channel);

	S9xSetSoundControl(sound_switch);
}

bool8 S9xOpenSoundDevice (void)
{
	if (SoundDriver)
	{
		SoundDriver->deinit();
		delete SoundDriver;
	}

	SoundDriver = new S9xSDLSoundDriver();
	SoundDriver->init();

	if (!SoundDriver->open_device(Settings.SoundPlaybackRate, sound_buffer_size))
	{
		delete SoundDriver;
		SoundDriver = nullptr;
		return FALSE;
	}

	SoundDriver->start();

	return TRUE;
}

void S9xSyncSpeed (void)
{
  // doemaemon: not sure how crucial this is atm.
	if (Settings.SoundSync && SoundDriver)
	{
		while (SoundDriver->space_free() < (int)Settings.SoundPlaybackRate * (int)Settings.FrameTime / 1000000)
			usleep(0);
	}

	if (Settings.DumpStreams)
		return;

#ifdef NETPLAY_SUPPORT
	if (Settings.NetPlay && NetPlay.Connected)
	{
	#if defined(NP_DEBUG) && NP_DEBUG == 2
		printf("CLIENT: SyncSpeed @%d\n", S9xGetMilliTime());
	#endif

		S9xNPSendJoypadUpdate(old_joypads[0]);
		for (int J = 0; J < 8; J++)
			joypads[J] = S9xNPGetJoypad(J);

		if (!S9xNPCheckForHeartBeat())
		{
			NetPlay.PendingWait4Sync = !S9xNPWaitForHeartBeatDelay(100);
		#if defined(NP_DEBUG) && NP_DEBUG == 2
			if (NetPlay.PendingWait4Sync)
				printf("CLIENT: PendingWait4Sync1 @%d\n", S9xGetMilliTime());
		#endif

			IPPU.RenderThisFrame = TRUE;
			IPPU.SkippedFrames = 0;
		}
		else
		{
			NetPlay.PendingWait4Sync = !S9xNPWaitForHeartBeatDelay(200);
		#if defined(NP_DEBUG) && NP_DEBUG == 2
			if (NetPlay.PendingWait4Sync)
				printf("CLIENT: PendingWait4Sync2 @%d\n", S9xGetMilliTime());
		#endif

			if (IPPU.SkippedFrames < NetPlay.MaxFrameSkip)
			{
				IPPU.RenderThisFrame = FALSE;
				IPPU.SkippedFrames++;
			}
			else
			{
				IPPU.RenderThisFrame = TRUE;
				IPPU.SkippedFrames = 0;
			}
		}

		if (!NetPlay.PendingWait4Sync)
		{
			NetPlay.FrameCount++;
			S9xNPStepJoypadHistory();
		}

		return;
	}
#endif

	if (Settings.HighSpeedSeek > 0)
		Settings.HighSpeedSeek--;

	if (Settings.TurboMode)
	{
		if ((++IPPU.FrameSkip >= Settings.TurboSkipFrames) && !Settings.HighSpeedSeek)
		{
			IPPU.FrameSkip = 0;
			IPPU.SkippedFrames = 0;
			IPPU.RenderThisFrame = TRUE;
		}
		else
		{
			IPPU.SkippedFrames++;
			IPPU.RenderThisFrame = FALSE;
		}

		return;
	}

	static struct timeval	next1 = { 0, 0 };
	struct timeval			now;

	while (gettimeofday(&now, NULL) == -1) ;

	// If there is no known "next" frame, initialize it now.
	if (next1.tv_sec == 0)
	{
		next1 = now;
		next1.tv_usec++;
	}

	// If we're on AUTO_FRAMERATE, we'll display frames always only if there's excess time.
	// Otherwise we'll display the defined amount of frames.
	unsigned	limit = (Settings.SkipFrames == AUTO_FRAMERATE) ? (timercmp(&next1, &now, <) ? 10 : 1) : Settings.SkipFrames;

	IPPU.RenderThisFrame = (++IPPU.SkippedFrames >= limit) ? TRUE : FALSE;

	if (IPPU.RenderThisFrame)
		IPPU.SkippedFrames = 0;
	else
	{
		// If we were behind the schedule, check how much it is.
		if (timercmp(&next1, &now, <))
		{
			unsigned	lag = (now.tv_sec - next1.tv_sec) * 1000000 + now.tv_usec - next1.tv_usec;
			if (lag >= 500000)
			{
				// More than a half-second behind means probably pause.
				// The next line prevents the magic fast-forward effect.
				next1 = now;
			}
		}
	}

	// Delay until we're completed this frame.
	// Can't use setitimer because the sound code already could be using it. We don't actually need it either.
	while (timercmp(&next1, &now, >))
	{
		// If we're ahead of time, sleep a while.
		unsigned	timeleft = (next1.tv_sec - now.tv_sec) * 1000000 + next1.tv_usec - now.tv_usec;
		usleep(timeleft);

		while (gettimeofday(&now, NULL) == -1) ;
		// Continue with a while-loop because usleep() could be interrupted by a signal.
	}

	// Calculate the timestamp of the next frame.
	next1.tv_usec += Settings.FrameTime;
	if (next1.tv_usec >= 1000000)
	{
		next1.tv_sec += next1.tv_usec / 1000000;
		next1.tv_usec %= 1000000;
	}
}

void S9xExit (void)
{
	S9xMovieShutdown();

	S9xSetSoundMute(TRUE);
	Settings.StopEmulation = TRUE;

#ifdef NETPLAY_SUPPORT
	if (Settings.NetPlay)
		S9xNPDisconnect();
#endif

	Memory.SaveSRAM(S9xGetFilename(".srm", SRAM_DIR).c_str());
	S9xSaveCheatFile(S9xGetFilename(".cht", CHEAT_DIR).c_str());
	S9xResetSaveTimer(FALSE);

	S9xUnmapAllControls();
	S9xDeinitDisplay();
	Memory.Deinit();
	S9xDeinitAPU();

	exit(0);
}

#ifdef DEBUGGER
static void sigbrkhandler (int)
{
	CPU.Flags |= DEBUG_MODE_FLAG;
	signal(SIGINT, (SIG_PF) sigbrkhandler);
}
#endif

int main (int argc, char **argv)
{
	if (argc < 2)
		S9xUsage();

	printf("\n\nSnes9x " VERSION " for unix/SDL\n");

	s9x_base_dir = std::string(getenv("HOME")) + SLASH_STR + ".snes9x";

	memset(&Settings, 0, sizeof(Settings));
	Settings.MouseMaster = TRUE;
	Settings.SuperScopeMaster = TRUE;
	Settings.JustifierMaster = TRUE;
	Settings.MultiPlayer5Master = TRUE;
	Settings.FrameTimePAL = 20000;
	Settings.FrameTimeNTSC = 16667;
	Settings.SixteenBitSound = TRUE;
	Settings.Stereo = TRUE;
	Settings.SoundPlaybackRate = 32000;
	Settings.SoundInputRate = 32000;
	Settings.Transparency = TRUE;
	Settings.AutoDisplayMessages = TRUE;
	Settings.InitialInfoStringTimeout = 120;
	Settings.HDMATimingHack = 100;
	Settings.BlockInvalidVRAMAccessMaster = TRUE;
	Settings.StopEmulation = TRUE;
	Settings.WrongMovieStateProtection = TRUE;
	Settings.DumpStreamsMaxFrames = -1;
	Settings.StretchScreenshots = 1;
	Settings.SnapshotScreenshots = TRUE;
	Settings.SkipFrames = AUTO_FRAMERATE;
	Settings.TurboSkipFrames = 15;
	Settings.CartAName[0] = 0;
	Settings.CartBName[0] = 0;

#ifdef NETPLAY_SUPPORT
	Settings.ServerName[0] = 0;
#endif

	CPU.Flags = 0;

	S9xLoadConfigFiles(argv, argc);
	const char *rom = S9xParseArgs(argv, argc);
	if (rom)
		rom_filename = rom;

	make_snes9x_dirs();

	if (!Memory.Init() || !S9xInitAPU())
	{
		fprintf(stderr, "Snes9x: Memory allocation failure - not enough RAM/virtual memory available.\nExiting...\n");
		Memory.Deinit();
		S9xDeinitAPU();
		exit(1);
	}

	S9xInitSound(sound_buffer_size);
	S9xSetSoundMute(TRUE);

	S9xReportControllers();

#ifdef GFX_MULTI_FORMAT
	S9xSetRenderPixelFormat(RGB565);
#endif

	uint32	saved_flags = CPU.Flags;
	bool8	loaded = FALSE;

	if (Settings.Multi)
	{
		loaded = Memory.LoadMultiCart(Settings.CartAName, Settings.CartBName);

		if (!loaded)
		{
			std::string s1, s2;

			if (Settings.CartAName[0])
			{
				SplitPath path = splitpath(Settings.CartAName);
				s1 = S9xGetDirectory(ROM_DIR) + SLASH_STR + path.stem + path.ext;
			}

			if (Settings.CartBName[0])
			{
				SplitPath path = splitpath(Settings.CartBName);
				s2 = S9xGetDirectory(ROM_DIR) + SLASH_STR + path.stem + path.ext;
			}

			loaded = Memory.LoadMultiCart(s1.c_str(), s2.c_str());
		}
	}
	else
	if (!rom_filename.empty())
	{
		loaded = Memory.LoadROM(rom_filename.c_str());

		if (!loaded)
		{
			SplitPath path = splitpath(rom_filename);
			std::string s = S9xGetDirectory(ROM_DIR) + SLASH_STR + path.stem + path.ext;
			loaded = Memory.LoadROM(s.c_str());
		}
	}

	if (!loaded)
	{
		fprintf(stderr, "Error opening the ROM file.\n");
		exit(1);
	}

	NSRTControllerSetup();
	Memory.LoadSRAM(S9xGetFilename(".srm", SRAM_DIR).c_str());
	S9xLoadCheatFile(S9xGetFilename(".cht", CHEAT_DIR).c_str());

	CPU.Flags = saved_flags;
	Settings.StopEmulation = FALSE;

#ifdef DEBUGGER
	struct sigaction sa;
	sa.sa_handler = sigbrkhandler;
#ifdef SA_RESTART
	sa.sa_flags = SA_RESTART;
#else
	sa.sa_flags = 0;
#endif
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
#endif

	S9xInitInputDevices();
	S9xInitDisplay(argc, argv);
	S9xSetupDefaultKeymap();

#ifdef NETPLAY_SUPPORT
	if (strlen(Settings.ServerName) == 0)
	{
		char	*server = getenv("S9XSERVER");
		if (server)
		{
			strncpy(Settings.ServerName, server, 127);
			Settings.ServerName[127] = 0;
		}
	}

	char	*port = getenv("S9XPORT");
	if (Settings.Port >= 0 && port)
		Settings.Port = atoi(port);
	else
	if (Settings.Port < 0)
		Settings.Port = -Settings.Port;

	if (Settings.NetPlay)
	{
		NetPlay.MaxFrameSkip = 10;

		if (!S9xNPConnectToServer(Settings.ServerName, Settings.Port, Memory.ROMName))
		{
			fprintf(stderr, "Failed to connect to server %s on port %d.\n", Settings.ServerName, Settings.Port);
			S9xExit();
		}

		fprintf(stderr, "Connected to server %s on port %d as player #%d playing %s.\n", Settings.ServerName, Settings.Port, NetPlay.Player, Memory.ROMName);
	}
#endif

	if (!play_smv_filename.empty())
	{
		uint32	flags = CPU.Flags & (DEBUG_MODE_FLAG | TRACE_FLAG);
		if (S9xMovieOpen(play_smv_filename.c_str(), TRUE) != SUCCESS)
			exit(1);
		CPU.Flags |= flags;
	}
	else
	if (!record_smv_filename.empty())
	{
		uint32	flags = CPU.Flags & (DEBUG_MODE_FLAG | TRACE_FLAG);
		if (S9xMovieCreate(record_smv_filename.c_str(), 0xFF, MOVIE_OPT_FROM_RESET, NULL, 0) != SUCCESS)
			exit(1);
		CPU.Flags |= flags;
	}
	else
	if (!snapshot_filename.empty())
	{
		uint32	flags = CPU.Flags & (DEBUG_MODE_FLAG | TRACE_FLAG);
		if (!S9xUnfreezeGame(snapshot_filename.c_str()))
			exit(1);
		CPU.Flags |= flags;
	}

	sprintf(String, "\"%s\" %s: %s", Memory.ROMName, TITLE, VERSION);

	// domaemon: setting the title on the window bar
	S9xSetTitle(String);

	S9xSetSoundMute(FALSE);

#ifdef NETPLAY_SUPPORT
	bool8	NP_Activated = Settings.NetPlay;
#endif

	while (1)
	{
	#ifdef NETPLAY_SUPPORT
		if (NP_Activated)
		{
			if (NetPlay.PendingWait4Sync && !S9xNPWaitForHeartBeatDelay(100))
			{
				S9xProcessEvents(FALSE);
				continue;
			}

			for (int J = 0; J < 8; J++)
				old_joypads[J] = MovieGetJoypad(J);

			for (int J = 0; J < 8; J++)
				MovieSetJoypad(J, joypads[J]);

			if (NetPlay.Connected)
			{
				if (NetPlay.PendingWait4Sync)
				{
					NetPlay.PendingWait4Sync = FALSE;
					NetPlay.FrameCount++;
					S9xNPStepJoypadHistory();
				}
			}
			else
			{
				fprintf(stderr, "Lost connection to server.\n");
				S9xExit();
			}
		}
	#endif

	#ifdef DEBUGGER
		if (!Settings.Paused || (CPU.Flags & (DEBUG_MODE_FLAG | SINGLE_STEP_FLAG)))
	#else
		if (!Settings.Paused)
	#endif
			S9xMainLoop();

	#ifdef NETPLAY_SUPPORT
		if (NP_Activated)
		{
			for (int J = 0; J < 8; J++)
				MovieSetJoypad(J, old_joypads[J]);
		}
	#endif

	#ifdef DEBUGGER
		if (Settings.Paused || (CPU.Flags & DEBUG_MODE_FLAG))
	#else
		if (Settings.Paused)
	#endif
			S9xSetSoundMute(TRUE);

	#ifdef DEBUGGER
		if (CPU.Flags & DEBUG_MODE_FLAG)
			S9xDoDebug();
		else
	#endif
		if (Settings.Paused)
		{
			S9xProcessEvents(FALSE);
			usleep(100000);
		}

		S9xProcessEvents(FALSE);

	#ifdef DEBUGGER
		if (!Settings.Paused && !(CPU.Flags & DEBUG_MODE_FLAG))
	#else
		if (!Settings.Paused)
	#endif
			S9xSetSoundMute(FALSE);
	}

	return (0);
}

