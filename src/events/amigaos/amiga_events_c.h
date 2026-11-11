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

#ifndef _SDL_amiga_events_h
#define _SDL_amiga_events_h

#include "../SDL_sysevents.h"
#include "../SDL_events_c.h"


/* Functions to be exported */
int Amiga_InitOSKeymap(void);
void Amiga_QuitOSKeymap(void);
void Amiga_PumpEvents(void);
SDL_GrabMode Amiga_GrabInput(SDL_GrabMode mode);
void Amiga_SetRelativeMouseMode(SDL_bool toggle);
void Amiga_CheckMouseMode(void);
SDL_bool Amiga_GetRelativeMouseMode(void);
void Amiga_WarpSystemPointer(int x, int y);

#endif /* _SDL_amiga_events_h */
