/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/


#ifndef _amiga_video_h
#define _amiga_video_h

#include "SDL_sysvideo.h"

#if (__GNUC__ > 3)
#include <proto/intuition.h>
#else
#include <intuition/intuition.h>
#endif
 
struct SDL_PrivateVideoData {
    struct Window *window;
};

// Shortcuts to SDL_PrivateVideoData
#define AMIGA_WINDOW (current_video->hidden->window)

#ifdef SDL_VIDEO_OPENGL
extern int NovaMesa_Init(void);
extern void NovaMesa_Quit(void);
extern int NovaMesa_MakeCurrent(void);
extern void NovaMesa_SwapBuffers(void);
extern void *NovaMesa_GetProcAddress(const char *proc);
extern int NovaMesa_LoadLibrary(const char *path);
#endif


#endif /* _amiga_video_h */
