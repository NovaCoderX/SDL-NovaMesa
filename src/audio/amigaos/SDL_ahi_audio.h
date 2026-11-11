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

#ifndef _SDL_ahiaudio_h
#define _SDL_ahiaudio_h



#include "../SDL_sysaudio.h"


struct SDL_PrivateAudioData {
	struct AHIRequest *audio_req[2];
	struct MsgPort *audio_port;
	Sint32 freq,type,bytespersample,size;
	Uint8 *mixbuf[2];           /* The app mixing buffer */
	int current_buffer;
	Uint32 playing;
};



#endif /* _SDL_ahiaudio_h */
