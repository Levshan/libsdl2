/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2012 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_config.h"

#if SDL_AUDIO_DRIVER_BEOSAUDIO

/* Allow access to the audio stream on BeOS */

#include <SoundPlayer.h>

#include "../../main/beos/SDL_BeApp.h"

extern "C"
{

#include "SDL_audio.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"
#include "../../thread/beos/SDL_systhread_c.h"
#include "SDL_beaudio.h"

}


/* !!! FIXME: have the callback call the higher level to avoid code dupe. */
/* The BeOS callback for handling the audio buffer */
static void
FillSound(void *device, void *stream, size_t len,
          const media_raw_audio_format & format)
{
    SDL_AudioDevice *audio = (SDL_AudioDevice *) device;

    /* Silence the buffer, since it's ours */
    SDL_memset(stream, audio->spec.silence, len);

    /* Only do soemthing if audio is enabled */
    if (!audio->enabled)
        return;

    if (!audio->paused) {
        if (audio->convert.needed) {
            SDL_mutexP(audio->mixer_lock);
            (*audio->spec.callback) (audio->spec.userdata,
                                     (Uint8 *) audio->convert.buf,
                                     audio->convert.len);
            SDL_mutexV(audio->mixer_lock);
            SDL_ConvertAudio(&audio->convert);
            SDL_memcpy(stream, audio->convert.buf, audio->convert.len_cvt);
        } else {
            SDL_mutexP(audio->mixer_lock);
            (*audio->spec.callback) (audio->spec.userdata,
                                     (Uint8 *) stream, len);
            SDL_mutexV(audio->mixer_lock);
        }
    }
}

static void
BEOSAUDIO_CloseDevice(_THIS)
{
    if (_this->hidden != NULL) {
        if (_this->hidden->audio_obj) {
            _this->hidden->audio_obj->Stop();
            delete _this->hidden->audio_obj;
            _this->hidden->audio_obj = NULL;
        }

        delete _this->hidden;
        _this->hidden = NULL;
    }
}

static int
BEOSAUDIO_OpenDevice(_THIS, const char *devname, int iscapture)
{
    int valid_datatype = 0;
    media_raw_audio_format format;
    SDL_AudioFormat test_format = SDL_FirstAudioFormat(_this->spec.format);

    /* Initialize all variables that we clean on shutdown */
    _this->hidden = new SDL_PrivateAudioData;
    if (_this->hidden == NULL) {
        SDL_OutOfMemory();
        return 0;
    }
    SDL_memset(_this->hidden, 0, (sizeof *_this->hidden));

    /* Parse the audio format and fill the Be raw audio format */
    SDL_memset(&format, '\0', sizeof(media_raw_audio_format));
    format.byte_order = B_MEDIA_LITTLE_ENDIAN;
    format.frame_rate = (float) _this->spec.freq;
    format.channel_count = _this->spec.channels;        /* !!! FIXME: support > 2? */
    while ((!valid_datatype) && (test_format)) {
        valid_datatype = 1;
        _this->spec.format = test_format;
        switch (test_format) {
        case AUDIO_S8:
            format.format = media_raw_audio_format::B_AUDIO_CHAR;
            break;

        case AUDIO_U8:
            format.format = media_raw_audio_format::B_AUDIO_UCHAR;
            break;

        case AUDIO_S16LSB:
            format.format = media_raw_audio_format::B_AUDIO_SHORT;
            break;

        case AUDIO_S16MSB:
            format.format = media_raw_audio_format::B_AUDIO_SHORT;
            format.byte_order = B_MEDIA_BIG_ENDIAN;
            break;

        case AUDIO_S32LSB:
            format.format = media_raw_audio_format::B_AUDIO_INT;
            break;

        case AUDIO_S32MSB:
            format.format = media_raw_audio_format::B_AUDIO_INT;
            format.byte_order = B_MEDIA_BIG_ENDIAN;
            break;

        case AUDIO_F32LSB:
            format.format = media_raw_audio_format::B_AUDIO_FLOAT;
            break;

        case AUDIO_F32MSB:
            format.format = media_raw_audio_format::B_AUDIO_FLOAT;
            format.byte_order = B_MEDIA_BIG_ENDIAN;
            break;

        default:
            valid_datatype = 0;
            test_format = SDL_NextAudioFormat();
            break;
        }
    }

    format.buffer_size = _this->spec.samples;

    if (!valid_datatype) {      /* shouldn't happen, but just in case... */
        BEOSAUDIO_CloseDevice(_this);
        SDL_SetError("Unsupported audio format");
        return 0;
    }

    /* Calculate the final parameters for this audio specification */
    SDL_CalculateAudioSpec(&_this->spec);

    /* Subscribe to the audio stream (creates a new thread) */
    sigset_t omask;
    SDL_MaskSignals(&omask);
    _this->hidden->audio_obj = new BSoundPlayer(&format, "SDL Audio",
                                                FillSound, NULL, _this);
    SDL_UnmaskSignals(&omask);

    if (_this->hidden->audio_obj->Start() == B_NO_ERROR) {
        _this->hidden->audio_obj->SetHasData(true);
    } else {
        BEOSAUDIO_CloseDevice(_this);
        SDL_SetError("Unable to start Be audio");
        return 0;
    }

    /* We're running! */
    return 1;
}

static void
BEOSAUDIO_Deinitialize(void)
{
    SDL_QuitBeApp();
}

static int
BEOSAUDIO_Init(SDL_AudioDriverImpl * impl)
{
    /* Initialize the Be Application, if it's not already started */
    if (SDL_InitBeApp() < 0) {
        return 0;
    }

    /* Set the function pointers */
    impl->OpenDevice = BEOSAUDIO_OpenDevice;
    impl->CloseDevice = BEOSAUDIO_CloseDevice;
    impl->Deinitialize = BEOSAUDIO_Deinitialize;
    impl->ProvidesOwnCallbackThread = 1;
    impl->OnlyHasDefaultOutputDevice = 1;

    return 1;   /* this audio target is available. */
}

extern "C"
{
    extern AudioBootStrap BEOSAUDIO_bootstrap;
}
AudioBootStrap BEOSAUDIO_bootstrap = {
    "baudio", "BeOS BSoundPlayer", BEOSAUDIO_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_BEOSAUDIO */

/* vi: set ts=4 sw=4 expandtab: */
