/*
 SDL - Simple DirectMedia Layer
 Copyright (C) 1997, 1998  Sam Lantinga

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
 5635-34 Springhouse Dr.
 Pleasanton, CA 94588 (USA)
 slouken@libsdl.org
 */
#include "SDL.h"
#include "SDL_endian.h"
#include "SDL_audio.h"
#include "../SDL_audiomem.h"
#include "../SDL_audio_c.h"
#include "SDL_ahi_audio.h"

// Amiga includes.
#include <proto/exec.h>
#include <proto/ahi.h>

static struct MsgPort *ahiPort = NULL;
;
static struct AHIRequest *ahiReq[2] = { NULL, NULL };
static int ahiReqSent[2] = { 0, 0 };

static BYTE *soundBuffer[2] = { NULL, NULL };
static BYTE currentSoundBuffer = 0;

static Uint32 mixingFrequency = 0;
static Uint32 mixingType = 0;
static Uint32 sampleBufferSize = 0;

/* Audio driver functions */
static int AHI_OpenAudio(SDL_AudioSpec *spec);
static void AHI_WaitAudio(void);
static void AHI_PlayAudio(void);
static Uint8* AHI_GetAudioBuf(void);
static void AHI_CloseAudio(void);

/* Audio driver bootstrap functions */

static int AHI_Available(void) {
	struct MsgPort *ahiPort = NULL;
	struct AHIRequest *ahiReq = NULL;
	int available = 0;

	ahiPort = (struct MsgPort*) CreateMsgPort();
	if (!ahiPort) {
		SDL_LogWarningMsgchar("Could not create a Message Port for AHI\n");
	} else {
		ahiReq = (struct AHIRequest*) CreateIORequest(ahiPort, sizeof(struct AHIRequest));
		if (!ahiReq) {
			SDL_LogWarningMsgchar("Could not create an IO Request for AHI\n");
		} else {
			// Open at least version 4.
			ahiReq->ahir_Version = 4;
			if (OpenDevice(AHINAME, AHI_DEFAULT_UNIT,(struct IORequest *)ahiReq, 0) != 0) {
				SDL_LogWarningMsgchar("Unable to open AHI Device\n");
			} else {
#ifdef DEBUG_AUDIO                 
        		SDL_LogDebugMsgchar("[AHI_Available]: AHI is available\n");
#endif
				available = 1; // Success.
				CloseDevice((struct IORequest* )ahiReq);
			}
		}
	}

	if (ahiReq) {
		DeleteIORequest(ahiReq);
	}

	if (ahiPort) {
		DeleteMsgPort(ahiPort);
	}

	return available;
}

static void AHI_DeleteDevice(void) {
	if (current_audio) {
#ifdef DEBUG_AUDIO
        SDL_LogDebugMsgchar("[Audio_DeleteDevice]: current_audio is not NULL\n");
#endif
		//SDL_free(current_audio->hidden);
		SDL_free(current_audio);
	}
}

static SDL_AudioDevice* AHI_CreateDevice(int devindex) {
	SDL_AudioDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_AudioDevice*) SDL_malloc(sizeof(SDL_AudioDevice));
	if (device) {
		SDL_memset(device, 0, (sizeof *device));
	}
	if ((device == NULL)) {
		SDL_OutOfMemory();
		if (device) {
			SDL_free(device);
		}
		return (0);
	}

	/* Set the function pointers */
	device->OpenAudio = AHI_OpenAudio;
	device->WaitAudio = AHI_WaitAudio;
	device->PlayAudio = AHI_PlayAudio;
	device->GetAudioBuf = AHI_GetAudioBuf;
	device->CloseAudio = AHI_CloseAudio;

	device->free = AHI_DeleteDevice;

	return device;
}

AudioBootStrap AHI_bootstrap = { "AHI", "AmigaOS AHI", AHI_Available, AHI_CreateDevice };

static void AHI_WaitAudio(void) {
	if (!CheckIO((struct IORequest* )ahiReq[currentSoundBuffer])) {
		WaitIO((struct IORequest* )ahiReq[currentSoundBuffer]);
	}
}

static void AHI_PlayAudio(void) {
	if (ahiReqSent[currentSoundBuffer]) {
		WaitIO((struct IORequest* ) ahiReq[currentSoundBuffer]);
	}

	ahiReq[currentSoundBuffer]->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
	ahiReq[currentSoundBuffer]->ahir_Std.io_Command = CMD_WRITE;
	ahiReq[currentSoundBuffer]->ahir_Std.io_Data = soundBuffer[currentSoundBuffer];
	ahiReq[currentSoundBuffer]->ahir_Std.io_Length = sampleBufferSize;
	ahiReq[currentSoundBuffer]->ahir_Std.io_Offset = 0;
	ahiReq[currentSoundBuffer]->ahir_Type = mixingType;
	ahiReq[currentSoundBuffer]->ahir_Frequency = mixingFrequency;
	ahiReq[currentSoundBuffer]->ahir_Position = 0x8000;
	ahiReq[currentSoundBuffer]->ahir_Volume = 0x10000;
	ahiReq[currentSoundBuffer]->ahir_Link =
			(ahiReqSent[currentSoundBuffer ^ 1]) ? ahiReq[currentSoundBuffer ^ 1] : NULL;

	SendIO((struct IORequest* )ahiReq[currentSoundBuffer]);

	ahiReqSent[currentSoundBuffer] = 1;

	// Flip.
	currentSoundBuffer ^= 1;
}

static Uint8* AHI_GetAudioBuf(void) {
	return (soundBuffer[currentSoundBuffer]);
}

static void AHI_CloseAudio(void) {
	if (ahiReqSent[currentSoundBuffer]) {
		AbortIO((struct IORequest* ) ahiReq[currentSoundBuffer]);
		WaitIO((struct IORequest* ) ahiReq[currentSoundBuffer]);
	}

	if (ahiReqSent[currentSoundBuffer ^ 1]) {
		AbortIO((struct IORequest* ) ahiReq[currentSoundBuffer ^ 1]);
		WaitIO((struct IORequest* ) ahiReq[currentSoundBuffer ^ 1]);
	}

	if (ahiReq[1]) {
		FreeVec(ahiReq[1]);
		ahiReq[1] = NULL;
	}

	if (ahiReq[0]) {
		CloseDevice((struct IORequest* ) ahiReq[0]);
		DeleteIORequest(ahiReq[0]);
		ahiReq[0] = NULL;
	}

	if (soundBuffer[0]) {
		FreeVec((APTR ) soundBuffer[0]);
		soundBuffer[0] = NULL;
	}

	if (soundBuffer[1]) {
		FreeVec((APTR ) soundBuffer[1]);
		soundBuffer[1] = NULL;
	}

	if (ahiPort) {
		DeleteMsgPort(ahiPort);
		ahiPort = NULL;
	}
}

static int AHI_OpenAudio(SDL_AudioSpec *spec) {
	// Check the spec
	if (spec->channels > 2) {
		SDL_LogWarningMsgchar("More than 2 AHI channels not currently supported, forcing conversion...\n");
		spec->channels = 2;
	}

	if (spec->channels < 1) {
		SDL_SetError("AHI channel number not supported");
		return -1;
	}

	/* Determine the audio parameters from the AudioSpec */
	switch (spec->format & 0xFF) {
	case 8:
		/* Signed 8 bit audio data */
#ifdef DEBUG_AUDIO            
            SDL_LogDebugMsgchar("[AHI_OpenAudio]: Signed 8 bit audio data\n");
#endif                  
		spec->format = AUDIO_S8;
		if (spec->channels == 1) {
			mixingType = AHIST_M8S;
#ifdef DEBUG_AUDIO				
                SDL_LogDebugMsgchar("[AHI_OpenAudio]: mixingType = AHIST_M8S\n");
#endif                 
		} else {
			mixingType = AHIST_S8S;
#ifdef DEBUG_AUDIO                
                SDL_LogDebugMsgchar("[AHI_OpenAudio]: mixingType = AHIST_S8S\n");
#endif                 
		}
		break;

	case 16:
		/* Signed 16 bit audio data */
#ifdef DEBUG_AUDIO            
			SDL_LogDebugMsgchar("[AHI_OpenAudio]: Signed 16 bit audio data\n");
#endif 			
		spec->format = AUDIO_S16MSB;
		if (spec->channels == 1) {
			mixingType = AHIST_M16S;
#ifdef DEBUG_AUDIO				
                SDL_LogDebugMsgchar("[AHI_OpenAudio]: mixingType = AHIST_M16S\n");
#endif                 
		} else {
			mixingType = AHIST_S16S;
#ifdef DEBUG_AUDIO				
                SDL_LogDebugMsgchar("[AHI_OpenAudio]: mixingType = AHIST_S16S\n");
#endif                
		}
		break;

	default:
		SDL_SetError("Unsupported audio format");
		return (-1);
	}

	/* Update the fragment size as size in bytes */
	SDL_CalculateAudioSpec(spec);

	/* Set output frequency and size */
	mixingFrequency = spec->freq;
#ifdef DEBUG_AUDIO		
	SDL_LogDebugMsgchar("[AHI_OpenAudio]: Calculated mixingFrequency = %ld\n", mixingFrequency);
#endif

	sampleBufferSize = spec->size;
#ifdef DEBUG_AUDIO		
    SDL_LogDebugMsgchar("[AHI_OpenAudio]: Calculated sampleBufferSize = %ld\n", sampleBufferSize);
#endif	

	ahiPort = (struct MsgPort*) CreateMsgPort();
	if (!ahiPort) {
		SDL_SetError("Could not create a Message Port for AHI");
		return -1;
	}

	ahiReq[0] = (struct AHIRequest*) CreateIORequest(ahiPort, sizeof(struct AHIRequest));
	if (!ahiReq[0]) {
		SDL_SetError("Could not create an IO Request for AHI");
		DeleteMsgPort(ahiPort);
		return -1;
	}

	// Open at least version 4.
	ahiReq[0]->ahir_Version = 4;

	if (OpenDevice(AHINAME, AHI_DEFAULT_UNIT, (struct IORequest*)ahiReq[0], 0) != 0) {
		SDL_SetError("Unable to open AHI Device: %s version 4");
		DeleteIORequest((struct IORequest* )ahiReq[0]);
		DeleteMsgPort(ahiPort);
		return -1;
	}

	/* Allocate mixing buffer */
	soundBuffer[0] = (BYTE*) AllocVec(sampleBufferSize, MEMF_PUBLIC|MEMF_CLEAR);
	if (!soundBuffer[0]) {
		SDL_SetError("Could not allocation sound buffer memory for AHI");
		return -1;
	}

	soundBuffer[1] = (BYTE*) AllocVec(sampleBufferSize, MEMF_PUBLIC|MEMF_CLEAR);
	if (!soundBuffer[1]) {
		SDL_SetError("Could not allocation sound buffer memory for AHI");
		return -1;
	}

	// Make a copy of the request (for double buffering)
	ahiReq[1] = (struct AHIRequest*) AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);
	if (!ahiReq[1]) {
		SDL_SetError("Could not create IO request copy for AHI");
		return -1;
	}

	CopyMem(ahiReq[0], ahiReq[1], sizeof(struct AHIRequest));

	currentSoundBuffer = 0;
	ahiReqSent[0] = 0;
	ahiReqSent[1] = 0;

	/* We're ready to rock and roll. :-) */
#ifdef DEBUG_AUDIO
    SDL_LogDebugMsgchar("[AHI_OpenAudio]: Audio initialized\n");
#endif	

	return 1;
}

