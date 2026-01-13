/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include "s9x_sound_driver_sdl.hpp"
#include "SDL_audio.h"

static int g_volume = 100;

void S9xSetVolume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    g_volume = volume;
}

int S9xGetVolume(void)
{
    return g_volume;
}

bool S9xSDLSoundDriver::write_samples(int16_t *data, int samples)
{
    std::lock_guard<std::mutex> lock(mutex);
    bool retval = true;
    auto empty = buffer.space_empty();
    if (samples > empty)
    {
        retval = false;
        buffer.dump(buffer.buffer_size / 2 - empty);
    }
    buffer.push(data, samples);

    return retval;
}

void S9xSDLSoundDriver::mix(unsigned char *output, int bytes)
{
    std::lock_guard<std::mutex> lock(mutex);
    int16_t *out = (int16_t *)output;
    int count = bytes >> 1;

    if (buffer.avail() >= count)
        buffer.read(out, count);
    else
    {
        int avail = buffer.avail();
        buffer.read(out, avail);
        memset(output + (avail << 1), 0, bytes - (avail << 1));
    }

    if (g_volume < 100)
    {
        for (int i = 0; i < count; i++)
        {
            out[i] = (int16_t)((int32_t)out[i] * g_volume / 100);
        }
    }
}

S9xSDLSoundDriver::S9xSDLSoundDriver() = default;

S9xSDLSoundDriver::~S9xSDLSoundDriver()
{
    S9xSDLSoundDriver::deinit();
}

void S9xSDLSoundDriver::init()
{
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    stop();
}

void S9xSDLSoundDriver::deinit()
{
    stop();
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void S9xSDLSoundDriver::start()
{
    SDL_PauseAudio(0);
}

void S9xSDLSoundDriver::stop()
{
    SDL_PauseAudio(1);
}

bool S9xSDLSoundDriver::open_device(int playback_rate, int buffer_size)
{
    audiospec = {};
    audiospec.freq = playback_rate;
    audiospec.channels = 2;
    audiospec.format = AUDIO_S16SYS;
    audiospec.samples = audiospec.freq * buffer_size / 8 / 1000; // 1/8th buffer per callback
    audiospec.callback = [](void *userdata, uint8_t *stream, int len) {
        ((S9xSDLSoundDriver *)userdata)->mix((unsigned char *)stream, len);
    };

    audiospec.userdata = this;

    printf("SDL sound driver initializing...\n");
    printf("    --> (Frequency: %dhz, Latency: %dms)...",
           audiospec.freq,
           (audiospec.samples * 1000 / audiospec.freq));

    if (SDL_OpenAudio(&audiospec, nullptr) < 0)
    {
        printf("Failed\n");
        return false;
    }

    printf("OK\n");
    if (buffer_size < 32)
        buffer_size = 32;

    buffer.resize(buffer_size * 4 * audiospec.freq / 1000);

    return true;
}

int S9xSDLSoundDriver::space_free()
{
    auto space_empty = buffer.space_empty();
    return space_empty;
}

std::pair<int, int> S9xSDLSoundDriver::buffer_level()
{
    std::pair<int, int> level = { buffer.space_empty(), buffer.buffer_size };
    return level;
}