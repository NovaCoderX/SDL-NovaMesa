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
  
#include "SDL.h"
#include "../SDL_events_c.h"
#include "SDL_cursor_c.h"

#include "amiga_mouse_c.h"
#include "amiga_events_c.h"
#include "../../video/amigaos/amiga_video_c.h"

// Amiga includes.
#include <proto/exec.h>
#include <proto/keymap.h>
#include <proto/lowlevel.h>


/* The implementation dependent data for the window manager cursor */
struct WMcursor {
	int unused;
};
 
static UWORD _blankCursor[] = {
  0x0000, 0x0000,   // reserved, must be NULL
  0x0000, 0x0000,   // 1 row of image data
  0x0000, 0x0000    // reserved, must be NULL
};

void Amiga_FreeWMCursor(WMcursor *cursor)
{
    if (cursor) {
        SDL_free(cursor);
        cursor = NULL;
    }
}

WMcursor *Amiga_CreateWMCursor(Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y)
{
	WMcursor *cursor;
	
#ifdef DEBUG_CURSOR
	SDL_LogDebugMsgchar("[Amiga_CreateWMCursor] - Amiga_CreateWMCursor()\n");
#endif
    
    // Allocate the cursor memory
	cursor = (WMcursor *)SDL_malloc(sizeof(WMcursor));
	if ( cursor == NULL ) {
		SDL_OutOfMemory();
		return(NULL);
	}
	
	SDL_memset(cursor, 0, sizeof(*cursor));
	

	return(cursor);	
}

int Amiga_ShowWMCursor(WMcursor *cursor)
{
	/* Don't do anything if the display is dead */
	if (!AMIGA_WINDOW) {
		return(0);
	}

    if ( cursor == NULL ) {
#ifdef DEBUG_CURSOR        
	    SDL_LogDebugMsgchar("[Amiga_ShowWMCursor]: Hiding hardware cursor\n");
#endif             
		SetPointer(AMIGA_WINDOW,(UWORD *)_blankCursor,1,1,0,0);
	} else {
#ifdef DEBUG_CURSOR
	    SDL_LogDebugMsgchar("[Amiga_ShowWMCursor]: Showing hardware cursor\n");
#endif   
		ClearPointer(AMIGA_WINDOW);
	}
	
	return(1);
}

void Amiga_WarpWMCursor(Uint16 x, Uint16 y)
{
	struct Screen *screen;
	int screenX, screenY;

	/* Don't do anything if the display is dead */
	if (!AMIGA_WINDOW) {
		return;
	}

#ifdef DEBUG_CURSOR
	SDL_LogDebugMsgchar("[Amiga_WarpWMCursor]: x = %d, y = %d\n", x, y);
#endif

	// SDL gives us coordinates relative to the inner window, the input device
	// wants them relative to the screen.
	screenX = AMIGA_WINDOW->LeftEdge + AMIGA_WINDOW->BorderLeft + (int)x;
	screenY = AMIGA_WINDOW->TopEdge + AMIGA_WINDOW->BorderTop + (int)y;

	// Clamp, so that what we report back matches where the pointer will
	// actually end up if the caller asked for something out of range.
	screen = AMIGA_WINDOW->WScreen;
	if (screen) {
		if (screenX < 0) {
			screenX = 0;
		} else if (screenX >= screen->Width) {
			screenX = screen->Width - 1;
		}

		if (screenY < 0) {
			screenY = 0;
		} else if (screenY >= screen->Height) {
			screenY = screen->Height - 1;
		}
	}

	Amiga_WarpSystemPointer(screenX, screenY);

	// SDL_WarpMouse() expects the warp to generate a motion event so that SDL's
	// own idea of where the pointer is stays in step.
	//
	// In relative mode we must NOT do that. The application is reading deltas
	// accumulated by the input handler, and a synthetic absolute motion here
	// would be turned into one large bogus delta - the classic warp to centre
	// double count. In that mode the warp is only ever about keeping the real
	// pointer inside the window, which the injected event has already done.
	if (!Amiga_GetRelativeMouseMode()) {
		SDL_PrivateMouseMotion(0, 0, x, y);
	}
}

void Amiga_UpdateMouse(void)
{
	/* Don't do anything if the display is dead */
	if (AMIGA_WINDOW) {
        SDL_Lock_EventThread();
        
#ifdef DEBUG_CURSOR
	SDL_LogDebugMsgchar("[Amiga_UpdateMouse]: x = %d, y = %d\n", AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
#endif        
        
        SDL_PrivateMouseMotion(0,0, AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
        
        SDL_GetRelativeMouseState(NULL, NULL); /* Clear first large delta */
        
        SDL_Unlock_EventThread();
	}    
}   
