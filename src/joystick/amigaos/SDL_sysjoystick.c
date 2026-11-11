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



/* This is the system specific header for the SDL joystick API */


#include "SDL.h"
#include "SDL_events.h"
#include "SDL_error.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"


// Amiga includes.
#include <proto/exec.h>
#include <proto/lowlevel.h>
#include <proto/graphics.h>

#include <libraries/lowlevel.h>


/* Amiga specific data */
// NOTE - LowLevelBase does not need opening, the linker's auto-open support
// takes care of it the first time we reference it.


/* How many frames we let ReadJoyPort() autosense settle before trusting it. */
#define JOYSTICK_SETTLE_FRAMES 20

/* The two classic joyports. */
#define MAX_JOYSTICKS 2

/* SDL device index -> physical joyport.
 *
 * Physical port 0 is normally the mouse port and port 1 is the traditional
 * joystick port, so we present port 1 as SDL joystick 0. Games that only ever
 * try SDL_JoystickOpen(0) then get the stick rather than the mouse, which is
 * what nearly all of them do. */
#define PORT_FOR_INDEX(index) (1 - (index))


static ULONG joybut[]=
{
	JPF_BUTTON_RED,
	JPF_BUTTON_BLUE,
	JPF_BUTTON_PLAY,
	JPF_BUTTON_YELLOW,
	JPF_BUTTON_GREEN,
	JPF_BUTTON_FORWARD,
	JPF_BUTTON_REVERSE,
};

struct joystick_hwdata
{
	ULONG joystate;
	int port; /* Physical joyport this stick reads from. */
};


static int numberOfJoysticks = 0;
static char joystickNames[MAX_JOYSTICKS][64];
static ULONG joystickTypes[MAX_JOYSTICKS]; /* JP_TYPE_#? cached at init/open. */


/* Cache the autosensed controller type and name for a device index.
 *
 * SDL_SYS_JoystickName() is served from this cache rather than re-polling the
 * hardware, because each fresh autosense costs JOYSTICK_SETTLE_FRAMES frames -
 * around 400ms at 50Hz - and applications enumerating joystick names call it
 * freely. */
static void UpdateCachedPortInfo(int index, ULONG data)
{
	ULONG type = data & JP_TYPE_MASK;

	joystickTypes[index] = type;

	if (type == JP_TYPE_JOYSTK || type == JP_TYPE_UNKNOWN) {
		SDL_strlcpy(joystickNames[index], "Generic joystick", sizeof(joystickNames[index]));
	} else if (type == JP_TYPE_GAMECTLR) {
		SDL_strlcpy(joystickNames[index], "Generic gamepad", sizeof(joystickNames[index]));
	} else if (type == JP_TYPE_MOUSE) {
		SDL_strlcpy(joystickNames[index], "Generic mouse", sizeof(joystickNames[index]));
	} else {
		SDL_strlcpy(joystickNames[index], "Unknown controller", sizeof(joystickNames[index]));
	}

#ifdef DEBUG_JOYSTICK
	SDL_LogDebugMsgchar("[UpdateCachedPortInfo]: Joystick %d (port %d) is a %s\n",
		index, PORT_FOR_INDEX(index), joystickNames[index]);
#endif
}


int SDL_SYS_JoystickInit(void)
{
	ULONG portData[MAX_JOYSTICKS];
	int i;

	numberOfJoysticks = 0;

	if (LowLevelBase) {
		/* There is no way to ask how many ports exist, there are always two. */
		numberOfJoysticks = MAX_JOYSTICKS;
	}

	if (numberOfJoysticks > 0) {
		/* Let the ReadJoyPort() autosense settle. Both ports are read in the
		 * same pass so this costs JOYSTICK_SETTLE_FRAMES frames in total
		 * rather than that many per port. */
		for (i = 0; i < JOYSTICK_SETTLE_FRAMES; i++) {
			portData[0] = ReadJoyPort(PORT_FOR_INDEX(0));
			portData[1] = ReadJoyPort(PORT_FOR_INDEX(1));
			WaitTOF();
		}

		for (i = 0; i < numberOfJoysticks; i++) {
			UpdateCachedPortInfo(i, portData[i]);
		}
	}

#ifdef DEBUG_JOYSTICK
	SDL_LogDebugMsgchar("[SDL_SYS_JoystickInit]: %d joysticks available.\n", numberOfJoysticks);
#endif

	return numberOfJoysticks;
}


/* Function to get the device-dependent name of a joystick */
const char *SDL_SYS_JoystickName(int index)
{
	if (index < 0 || index >= numberOfJoysticks) {
		return NULL;
	}

	/* Served from the cache filled at init/open - deliberately no hardware
	 * polling here, this can be called every frame. */
	return joystickNames[index];
}


int SDL_SYS_JoystickOpen(SDL_Joystick *joystick)
{
	ULONG data = 0;
	int port;
	int i;

#ifdef DEBUG_JOYSTICK
	SDL_LogDebugMsgchar("[SDL_SYS_JoystickOpen]: Opening joystick %d\n", joystick->index);
#endif

	if (joystick->index < 0 || joystick->index >= numberOfJoysticks) {
		SDL_SetError("Joystick index %d is out of range", joystick->index);
		return -1;
	}

	port = PORT_FOR_INDEX(joystick->index);

	if (!joystick->hwdata) {
		joystick->hwdata = SDL_malloc(sizeof(struct joystick_hwdata));
		if (!joystick->hwdata) {
			SDL_SetError("Couldn't allocate joystick data");
			return -1;
		}
	}

	SDL_memset(joystick->hwdata, 0, sizeof(struct joystick_hwdata));

	/* Autosense again on open, with a fresh settle, so that a controller
	 * plugged in after SDL_Init() is still picked up. Refresh the cache to
	 * match while we are here. */
	for (i = 0; i < JOYSTICK_SETTLE_FRAMES; i++) {
		data = ReadJoyPort(port);
		WaitTOF();
	}

	UpdateCachedPortInfo(joystick->index, data);

	if ((data & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL) {
		SDL_free(joystick->hwdata);
		joystick->hwdata = NULL;
		SDL_SetError("Joystick device %d is not available", joystick->index);
		return -1;
	}

	/* Reject mice, otherwise the application gets pointer movement reported as
	 * stick movement. With the index to port mapping above this should only
	 * ever happen for joystick 1. */
	if ((data & JP_TYPE_MASK) == JP_TYPE_MOUSE) {
#ifdef DEBUG_JOYSTICK
		SDL_LogDebugMsgchar("[SDL_SYS_JoystickOpen]: Rejecting mouse found on port %d\n", port);
#endif
		SDL_free(joystick->hwdata);
		joystick->hwdata = NULL;
		SDL_SetError("Mouse detected on port %d, rejecting as joystick", port);
		return -1;
	}

	if ((data & JP_TYPE_MASK) == JP_TYPE_GAMECTLR) {
		joystick->nbuttons = 7;
	} else {
		joystick->nbuttons = 3;
	}

	joystick->naxes = 2;
	joystick->nhats = 0;
	joystick->nballs = 0;

	joystick->hwdata->joystate = 0L;
	joystick->hwdata->port = port;

	return 0;
}


void SDL_SYS_JoystickUpdate(SDL_Joystick *joystick)
{
	ULONG data;
	int i;

	if (!joystick->hwdata) {
		return;
	}

	data = ReadJoyPort(joystick->hwdata->port);

	if ((data & JP_TYPE_MASK) != JP_TYPE_NOTAVAIL) {
		/* Axis 1 (Y axis) */
		if (data & JPF_JOY_UP) {
			if (!(joystick->hwdata->joystate & JPF_JOY_UP)) {
				SDL_PrivateJoystickAxis(joystick, 1, -32768);
			}
		} else if (data & JPF_JOY_DOWN) {
			if (!(joystick->hwdata->joystate & JPF_JOY_DOWN)) {
				SDL_PrivateJoystickAxis(joystick, 1, 32767);
			}
		}

		if (!(data & (JPF_JOY_UP|JPF_JOY_DOWN)) && (joystick->hwdata->joystate & (JPF_JOY_UP|JPF_JOY_DOWN))) {
			// Reset movement for Y axis.
			SDL_PrivateJoystickAxis(joystick, 1, 0);
		}

		/* Axis 0 (X axis) */
		if (data & JPF_JOY_LEFT) {
			if (!(joystick->hwdata->joystate & JPF_JOY_LEFT)) {
				SDL_PrivateJoystickAxis(joystick, 0, -32768);
			}
		} else if (data & JPF_JOY_RIGHT) {
			if (!(joystick->hwdata->joystate & JPF_JOY_RIGHT)) {
				SDL_PrivateJoystickAxis(joystick, 0, 32767);
			}
		}

		if (!(data & (JPF_JOY_LEFT|JPF_JOY_RIGHT)) && (joystick->hwdata->joystate & (JPF_JOY_LEFT|JPF_JOY_RIGHT))) {
			// Reset movement for X axis.
			SDL_PrivateJoystickAxis(joystick, 0, 0);
		}

		/* Buttons */
		for (i = 0; i < joystick->nbuttons; i++) {
			if (data & joybut[i]) {
				/* On a plain 2/3 button stick the Blue line can raise a
				 * phantom Play, so mask it - but only there. On a real CD32
				 * pad Blue and Play are independent buttons and must not
				 * suppress each other. */
				if (i == 1 && joystick->nbuttons == 3) {
					data &= (~(joybut[2]));
				}

				if (!(joystick->hwdata->joystate & joybut[i])) {
					SDL_PrivateJoystickButton(joystick, i, SDL_PRESSED);
				}
			} else if (joystick->hwdata->joystate & joybut[i]) {
				SDL_PrivateJoystickButton(joystick, i, SDL_RELEASED);
			}
		}

		// Record new data.
		joystick->hwdata->joystate = data;
	} else if (joystick->hwdata->joystate) {
		/* The port stopped responding while input was being held. Synthesize
		 * the releases, otherwise the application sees a stuck direction or
		 * button for the rest of the session. */
		ULONG oldstate = joystick->hwdata->joystate;

#ifdef DEBUG_JOYSTICK
		SDL_LogDebugMsgchar("[SDL_SYS_JoystickUpdate]: Port %d stopped responding, releasing held input\n",
			joystick->hwdata->port);
#endif

		if (oldstate & (JPF_JOY_UP|JPF_JOY_DOWN)) {
			SDL_PrivateJoystickAxis(joystick, 1, 0);
		}

		if (oldstate & (JPF_JOY_LEFT|JPF_JOY_RIGHT)) {
			SDL_PrivateJoystickAxis(joystick, 0, 0);
		}

		for (i = 0; i < joystick->nbuttons; i++) {
			if (oldstate & joybut[i]) {
				SDL_PrivateJoystickButton(joystick, i, SDL_RELEASED);
			}
		}

		joystick->hwdata->joystate = 0L;
	}
}


/* Function to close a joystick after use */
void SDL_SYS_JoystickClose(SDL_Joystick *joystick)
{
	if (joystick->hwdata) {
		SDL_free(joystick->hwdata);
		joystick->hwdata = NULL;
	}
}


/* Function to perform any system-specific joystick related cleanup */
void SDL_SYS_JoystickQuit(void)
{
	numberOfJoysticks = 0;
}
