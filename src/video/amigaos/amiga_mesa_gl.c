/*
 SDL - Simple DirectMedia Layer
 Copyright (C) 1997, 1998, 1999, 2000, 2001  Sam Lantinga

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

/* NovaMesa implementation of SDL OpenGL support */
#include "SDL.h"
#include "SDL_video.h"
#include "amiga_video_c.h"

#if defined(SDL_VIDEO_OPENGL) && defined(SDL_VIDEO_DRIVER_AMIGA)

#include <GL/amiga_mesa.h>
static AMesaContext *context = NULL;

int NovaMesa_Init(void) {
	context = amesa_create_context(AMIGA_WINDOW);
	if (!context) {
		SDL_SetError("Could not create OpenGL context");
		return -1;
	}

	// All is cool.
	return 0;
}

int NovaMesa_MakeCurrent(void) {
	if (context) {
		amesa_make_current(context);
		return 0;
	} else {
		SDL_LogWarningMsgchar("The OpenGL context is invalid\n");
		return -1;
	}
}

void NovaMesa_Quit(void) {
	if (context) {
		amesa_destroy_context(context);
		context = NULL;
	}
}

void NovaMesa_SwapBuffers(void) {
	if (context) {
		amesa_swap_buffers(context);
	}
}

int NovaMesa_LoadLibrary(const char *path) {
	current_video->gl_config.driver_loaded = 1;
	return 0;
}

#endif

