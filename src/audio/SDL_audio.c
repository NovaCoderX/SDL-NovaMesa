/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/



/* Allow access to a raw mixing buffer */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_error.h"
#include "SDL_audio_c.h"
#include "SDL_audiomem.h"
#include "SDL_sysaudio.h"



/* Available audio drivers */
static AudioBootStrap *bootstrap[] = {
#ifdef SDL_AUDIO_DRIVER_AMIGA
	&AHI_bootstrap,
#endif 
	NULL
};

SDL_AudioDevice *current_audio = NULL;

/* Various local functions */
int SDL_AudioInit(const char *driver_name);
void SDL_AudioQuit(void);


static int audio_configured = 0;


/* The general mixing thread function */
int SDL_RunAudio(void *audiop)
{
	SDL_AudioDevice *audio = (SDL_AudioDevice *)audiop;
	Uint8 *stream;
	int    stream_len;
	void  *udata;
	void (*fill)(void *userdata,Uint8 *stream, int len);
	int    silence;

	int started = 0;


    // Audio must be opened from the Audio Thread.
    audio->opened = audio->OpenAudio(&audio->spec);
	if (!audio->opened ) {
		SDL_CloseAudio();
		return(-1);
	}

	
    /* Set up the mixing function */
	fill  = audio->spec.callback;
	udata = audio->spec.userdata;


	audio_configured = 1;


	SDL_mutexP(audio->mixer_lock);



	if ( audio->convert.needed ) {
		if ( audio->convert.src_format == AUDIO_U8 ) {
			silence = 0x80;
		} else {
			silence = 0;
		}
		stream_len = audio->convert.len;
	} else {
		silence = audio->spec.silence;
		stream_len = audio->spec.size;
	}
	stream = audio->fake_stream;


	
	SDL_mutexV(audio->mixer_lock);


  

	/* Loop, filling the audio buffers */
	while (audio->enabled) {

		/* Wait for new current buffer to finish playing */
		if ( stream == audio->fake_stream ) {
			SDL_Delay((audio->spec.samples*1000)/audio->spec.freq);
		} else {

			if ( started > 1 )

			audio->WaitAudio();
		}
        
		/* Fill the current buffer with sound */
		if ( audio->convert.needed ) {
			if ( audio->convert.buf ) {
				stream = audio->convert.buf;
			} else {
				continue;
			}
		} else {
			stream = audio->GetAudioBuf();
			if ( stream == NULL ) {
				stream = audio->fake_stream;
			}
		}
		memset(stream, silence, stream_len);

		if ( ! audio->paused ) {
			SDL_mutexP(audio->mixer_lock);
			(*fill)(udata, stream, stream_len);
			SDL_mutexV(audio->mixer_lock);
		}

		/* Convert the audio if necessary */
		if ( audio->convert.needed ) {
			SDL_ConvertAudio(&audio->convert);
			stream = audio->GetAudioBuf();
			if ( stream == NULL ) {
				stream = audio->fake_stream;
			} 
			SDL_memcpy(stream, audio->convert.buf, audio->convert.len_cvt);
		} 
 
		/* Ready current buffer for play and change current buffer */
		if ( stream != audio->fake_stream ) {
			audio->PlayAudio();

            /* AmigaOS don't have to wait the first time audio is played! */
			started++;
		} 
	}
	
	/* Wait for the audio to drain.. */
	if ( audio->WaitDone ) {
		audio->WaitDone();
	}


    // Audio must be closed from the Audio Thread.
	audio->CloseAudio();
	audio->opened = 0;


	return(0);
}

static void SDL_LockAudio_Default(void)
{
	if ( current_audio->thread && (SDL_ThreadID() == current_audio->threadid) ) {
		return;
	}
	SDL_mutexP(current_audio->mixer_lock);
}

static void SDL_UnlockAudio_Default(void)
{
	if ( current_audio->thread && (SDL_ThreadID() == current_audio->threadid) ) {
		return;
	}
	SDL_mutexV(current_audio->mixer_lock);
}

static Uint16 SDL_ParseAudioFormat(const char *string)
{
	Uint16 format = 0;

	switch (*string) {
	    case 'U':
		++string;
		format |= 0x0000;
		break;
	    case 'S':
		++string;
		format |= 0x8000;
		break;
	    default:
		return 0;
	}
	switch (SDL_atoi(string)) {
	    case 8:
		string += 1;
		format |= 8;
		break;
	    case 16:
		string += 2;
		format |= 16;
		if ( SDL_strcmp(string, "LSB") == 0
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
		     || SDL_strcmp(string, "SYS") == 0
#endif
		    ) {
			format |= 0x0000;
		}
		if ( SDL_strcmp(string, "MSB") == 0
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
		     || SDL_strcmp(string, "SYS") == 0
#endif
		    ) {
			format |= 0x1000;
		}
		break;
	    default:
		return 0;
	}
	return format;
}


int SDL_AudioInit(const char *driver_name)
{
	SDL_AudioDevice *audio;
	int i = 0, index;

	/* Check to make sure we don't overwrite 'current_audio' */
	if ( current_audio != NULL ) {
		SDL_AudioQuit();
	}

    /* Select the proper audio driver */
	index = 0;
	audio = NULL;
    if ( driver_name != NULL ) {
		for ( i=0; bootstrap[i]; ++i ) {
			if (SDL_strcasecmp(bootstrap[i]->name, driver_name) == 0) {
				if ( bootstrap[i]->available() ) {
					audio=bootstrap[i]->create(index);
					break;
				}
			}
		}
	} else {
		for ( i=0; bootstrap[i]; ++i ) {
			if ( bootstrap[i]->available() ) {
				audio = bootstrap[i]->create(index);
				if ( audio != NULL ) {
					break;
				}
			}
		}
	}
	
	if ( audio == NULL ) {
		SDL_SetError("No available audio device");
		return(-1);

	}


	current_audio = audio;
	if ( current_audio ) {
		current_audio->name = bootstrap[i]->name;
		if ( !current_audio->LockAudio && !current_audio->UnlockAudio ) {
			current_audio->LockAudio = SDL_LockAudio_Default;
			current_audio->UnlockAudio = SDL_UnlockAudio_Default;
		}
	}

	return(0);
}

char *SDL_AudioDriverName(char *namebuf, int maxlen)
{
	if ( current_audio != NULL ) {
		strncpy(namebuf, current_audio->name, maxlen-1);
		namebuf[maxlen-1] = '\0';
		return(namebuf);
	}
	return(NULL);
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	SDL_AudioDevice *audio;
    const char *env; 
    int priority = 1; // Default.
        
	/* Start up the audio driver, if necessary */
	if ( ! current_audio ) {
		if ( (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) ||
		     (current_audio == NULL) ) {
			return(-1);
		}
	}
	audio = current_audio;


#ifdef DEBUG_AUDIO
    SDL_LogDebugMsgchar("[SDL_OpenAudio]: Starting up the audio driver\n");
#endif

	if (audio->opened) {
		SDL_SetError("Audio device is already opened");
		return(-1);
	}

	/* Verify some parameters */
	if ( desired->callback == NULL ) {
		SDL_SetError("SDL_OpenAudio() was passed a NULL callback");
		return(-1);
	}
	
    if ( desired->freq == 0 ) {
		env = SDL_getenv("SDL_AUDIO_FREQUENCY");
		if ( env ) {
			desired->freq = SDL_atoi(env);
		}
    }
	
    if ( desired->freq == 0 ) {
		/* Pick some default audio frequency */
		desired->freq = MIX_DEFAULT_FREQUENCY;
	}
	
	if ( desired->format == 0 ) {
		env = SDL_getenv("SDL_AUDIO_FORMAT");
		if ( env ) {
			desired->format = SDL_ParseAudioFormat(env);
		}
	}
	
	if ( desired->format == 0 ) {
		/* Pick some default audio format */
		desired->format = MIX_DEFAULT_FORMAT;
	}
	
	if ( desired->channels == 0 ) {
		env = SDL_getenv("SDL_AUDIO_CHANNELS");
		if ( env ) {
			desired->channels = (Uint8)SDL_atoi(env);
		}
	}
	
	if ( desired->channels == 0 ) {
		/* Pick a default number of channels */
		desired->channels = MIX_DEFAULT_CHANNELS;
	}
	
	switch ( desired->channels ) {
	    case 1:	/* Mono */
	    case 2:	/* Stereo */
        case 4:	/* surround */
	    case 6:	/* surround with center and lfe */
		break;
	    default:
		SDL_SetError("1 (mono) and 2 (stereo) 4 Channel 6 Channel surround supported");
		return(-1);
	}
	
    if ( desired->samples == 0 ) {
		env = SDL_getenv("SDL_AUDIO_SAMPLES");
		if ( env ) {
			desired->samples = (Uint16)SDL_atoi(env);
		}
	}
	
	if ( desired->samples == 0 ) {
		/* Pick a default of ~46 ms at desired frequency */
		int samples = (desired->freq / 1000) * 46;
		int power2 = 1;
		while ( power2 < samples ) {
			power2 *= 2;
		}
		desired->samples = power2;
	}


	/* Create a semaphore for locking the sound buffers */
	audio->mixer_lock = SDL_CreateMutex();
	if ( audio->mixer_lock == NULL ) {
		SDL_SetError("Couldn't create mixer lock");
		SDL_CloseAudio();
		return(-1);
	}


	/* Calculate the silence and size of the audio specification */
	SDL_CalculateAudioSpec(desired);

	/* Open the audio subsystem */
	SDL_memcpy(&audio->spec, desired, sizeof(audio->spec));
	audio->convert.needed = 0;
	audio->enabled = 1;
	audio->paused  = 1;
 


	SDL_mutexP(audio->mixer_lock);

	audio->thread = SDL_CreateThread(SDL_RunAudio, audio);
	if ( audio->thread == NULL ) {
		SDL_mutexV(audio->mixer_lock);
		SDL_CloseAudio();
		SDL_SetError("Couldn't create the audio thread");
		return(-1);
	}
	
	//  Set the thread id.
	audio->threadid = SDL_GetThreadID(audio->thread);

	while(!audio_configured)
		SDL_Delay(100);
  
	/* If the audio driver changes the buffer size, accept it */
	if ( audio->spec.samples != desired->samples ) {
		desired->samples = audio->spec.samples;
		SDL_CalculateAudioSpec(desired);
	}

	/* Allocate a fake audio memory buffer */
	audio->fake_stream = SDL_AllocAudioMem(audio->spec.size);
	if ( audio->fake_stream == NULL ) {
		SDL_CloseAudio();
		SDL_OutOfMemory();
		return(-1);
	}

	/* See if we need to do any conversion */
	if ( memcmp(desired, &audio->spec, sizeof(audio->spec)) == 0 ) {
		/* Just copy over the desired audio specification */
		if ( obtained != NULL ) {
			SDL_memcpy(obtained, &audio->spec, sizeof(audio->spec));
		}
	} else {
		/* Copy over the audio specification if possible */
		if ( obtained != NULL ) {
			SDL_memcpy(obtained, &audio->spec, sizeof(audio->spec));
		} else {
			/* Build an audio conversion block */
			if ( SDL_BuildAudioCVT(&audio->convert,
				desired->format, desired->channels,
						desired->freq,
				audio->spec.format, audio->spec.channels,
						audio->spec.freq) < 0 ) {
				SDL_CloseAudio();
				return(-1);
			}
			
			if (audio->convert.needed) {
				SDL_LogWarningMsgchar("Audio conversion needed\n");
                
                audio->convert.len = desired->size;
				audio->convert.buf =(Uint8 *)SDL_AllocAudioMem(audio->convert.len*audio->convert.len_mult+100);
				if ( audio->convert.buf == NULL ) {
					SDL_CloseAudio();
					SDL_OutOfMemory();
					return(-1);
				}
			}
		}
	}

	SDL_mutexV(audio->mixer_lock);
	
	
	// Set audio thread priority.
	env = SDL_getenv("SDL_AUDIO_PRIORITY");
	if (env) {
		priority = SDL_atoi(env);
	}

    SDL_SetThreadPriority(audio->thread, priority);

	return(0);
}

SDL_audiostatus SDL_GetAudioStatus(void)
{
	SDL_audiostatus status = SDL_AUDIO_STOPPED;
	
	if (current_audio && current_audio->enabled) {
		if ( current_audio->paused ) {
			status = SDL_AUDIO_PAUSED;
		} else {
			status = SDL_AUDIO_PLAYING;
		}
	}
	
	return(status);
}

void SDL_PauseAudio (int pause_on)
{
	if ( current_audio ) {
		current_audio->paused = pause_on;
	}
}

void SDL_LockAudio (void)
{
	/* Obtain a lock on the mixing buffers */
	if ( current_audio && current_audio->LockAudio ) {
		current_audio->LockAudio();
	}
}

void SDL_UnlockAudio (void)
{
	/* Release lock on the mixing buffers */
	if ( current_audio && current_audio->UnlockAudio ) {
		current_audio->UnlockAudio();
	}
}

void SDL_CloseAudio (void)
{
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SDL_AudioQuit(void)
{	
	if ( current_audio ) {
		if ( current_audio->thread != NULL ) {
			current_audio->enabled = 0; // Stop the audio thread processing.
	
            SDL_WaitThread(current_audio->thread, NULL);
		}
		
		if ( current_audio->mixer_lock != NULL ) {
			SDL_DestroyMutex(current_audio->mixer_lock);
		}
		
        if ( current_audio->fake_stream != NULL ) {
			SDL_FreeAudioMem(current_audio->fake_stream);
		}
		
        if ( current_audio->convert.needed ) {
			SDL_FreeAudioMem(current_audio->convert.buf);

		}

		/* Free the driver data */
		current_audio->free();
		current_audio = NULL;
	} 
	
	audio_configured = 0;
}

#define NUM_FORMATS	6
static int format_idx;
static int format_idx_sub;
static Uint16 format_list[NUM_FORMATS][NUM_FORMATS] = {
 { AUDIO_U8, AUDIO_S8, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB },
 { AUDIO_S8, AUDIO_U8, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB },
 { AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U16LSB, AUDIO_U16MSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_S16MSB, AUDIO_S16LSB, AUDIO_U16MSB, AUDIO_U16LSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_U16LSB, AUDIO_U16MSB, AUDIO_S16LSB, AUDIO_S16MSB, AUDIO_U8, AUDIO_S8 },
 { AUDIO_U16MSB, AUDIO_U16LSB, AUDIO_S16MSB, AUDIO_S16LSB, AUDIO_U8, AUDIO_S8 },
};

Uint16 SDL_FirstAudioFormat(Uint16 format)
{
	for ( format_idx=0; format_idx < NUM_FORMATS; ++format_idx ) {
		if ( format_list[format_idx][0] == format ) {
			break;
		}
	}
	format_idx_sub = 0;
	return(SDL_NextAudioFormat());
}

Uint16 SDL_NextAudioFormat(void)
{
	if ( (format_idx == NUM_FORMATS) || (format_idx_sub == NUM_FORMATS) ) {
		return(0);
	}
	return(format_list[format_idx][format_idx_sub++]);
}

void SDL_CalculateAudioSpec(SDL_AudioSpec *spec)
{
	switch (spec->format) {
		case AUDIO_U8:
			spec->silence = 0x80;
			break;
		default:
			spec->silence = 0x00;
			break;
	}
	spec->size = (spec->format&0xFF)/8;
	spec->size *= spec->channels;
	spec->size *= spec->samples;
}

void SDL_Audio_SetCaption(const char *caption)
{
	if ((current_audio) && (current_audio->SetCaption)) {
		current_audio->SetCaption(caption);
	}
}
