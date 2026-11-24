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

#include "SDL.h"

#ifdef SDL_VIDEO_DRIVER_AMIGA_AGA

#ifdef DEBUG
#include <assert.h>
#endif

#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "amiga_video_c.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"
#include "../../events/amigaos/amiga_events_c.h"
#include "../../events/amigaos/amiga_mouse_c.h"

// Amiga includes.
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <graphics/videocontrol.h>
#include <utility/tagitem.h>
#include "amiga_c2p_aga.h"


#define AGA_DISPLAY_DEPTH 8

#define MIN_VIDEO_MODE_WIDTH 320
#define MIN_VIDEO_MODE_HEIGHT 200

#define MAX_VIDEO_MODE_WIDTH 640
#define MAX_VIDEO_MODE_HEIGHT 512

// Custom screen - Only used when running in full screen mode.
static struct Screen *_customScreen = NULL;

static SDL_Rect** _screenModeList = NULL;
static int _numScreenModes = 0;

// Size of the screen actually opened. This is the same as the SDL surface in fullscreen mode
// but can be bigger in the (virtual) windowed mode, where the surface is centred on the screen.
static int _screenWidth = 0;
static int _screenHeight = 0;
static int _surfaceLeft = 0;
static int _surfaceTop = 0;

// Only used in the (virtual) windowed mode, a screen sized chunky buffer that the smaller
// SDL surface is copied into before it is passed to the C2P (which works on whole screens).
static UBYTE *_chunkyBuffer = NULL;

// Available PAL screen modes.
#define NUMBER_OF_SCREEN_MODES  6
static SDL_Rect _screenModes[NUMBER_OF_SCREEN_MODES];

static char *_screenTitle = NULL;
static char *_windowTitle = NULL;

// Palette data.
static SDL_Color _currentPalette[256];

// C2P
static void *c2p[2] = {NULL, NULL};
static struct ScreenBuffer *_hardwareScreenBuffer[2] = {NULL, NULL};
static BYTE _currentScreenBuffer = 0;

// Finds the smallest of our modes that a (virtual) window of the passed size will fit inside.
static SDL_bool FindVirtualScreenSize(int width, int height, int *screenWidth, int *screenHeight) {
	int i;
	int best = -1;

	for (i = 0; i < NUMBER_OF_SCREEN_MODES; i++) {
		if ((_screenModes[i].w >= width) && (_screenModes[i].h >= height)) {
			if ((best < 0) || ((_screenModes[i].w * _screenModes[i].h) < (_screenModes[best].w * _screenModes[best].h))) {
				best = i;
			}
		}
	}

	if (best < 0) {
		return SDL_FALSE;
	}

	*screenWidth = _screenModes[best].w;
	*screenHeight = _screenModes[best].h;
	return SDL_TRUE;
}

static SDL_bool IsValidPALMode(ULONG modeId, int width, int height) {
	DisplayInfoHandle handle;
	struct DisplayInfo dispInfo;
	struct DimensionInfo dimsinfo;

	handle = FindDisplayInfo(modeId);
	if (!handle) {
		return SDL_FALSE;
	}

	if (!GetDisplayInfoData(handle, (UBYTE *)&dispInfo, sizeof(dispInfo), DTAG_DISP, 0)) {
		return SDL_FALSE;
	}

	if ((dispInfo.PropertyFlags & DIPF_IS_PAL) == 0) {
		return SDL_FALSE;
	}

	if (!GetDisplayInfoData(handle, (UBYTE *)&dimsinfo, sizeof(dimsinfo), DTAG_DIMS, 0)) {
		return SDL_FALSE;
	}

	if (dimsinfo.MaxDepth != AGA_DISPLAY_DEPTH) {
		return SDL_FALSE;
	}

	if ((dimsinfo.Nominal.MaxX + 1) != width) {
		return SDL_FALSE;
	}

	if ((dimsinfo.Nominal.MaxY + 1) < height) {
		return SDL_FALSE;
	}

	// All is cool.
	return SDL_TRUE;
}

static void buildScreenModeList(void) {
	_screenModes[0].x = 0;
	_screenModes[0].y = 0;
	_screenModes[0].w = 320;
	_screenModes[0].h = 200;

	_screenModes[1].x = 0;
	_screenModes[1].y = 0;
	_screenModes[1].w = 320;
	_screenModes[1].h = 240;

	_screenModes[2].x = 0;
	_screenModes[2].y = 0;
	_screenModes[2].w = 320;
	_screenModes[2].h = 256;

	_screenModes[3].x = 0;
	_screenModes[3].y = 0;
	_screenModes[3].w = 640;
	_screenModes[3].h = 400;

	_screenModes[4].x = 0;
	_screenModes[4].y = 0;
	_screenModes[4].w = 640;
	_screenModes[4].h = 480;

	_screenModes[5].x = 0;
	_screenModes[5].y = 0;
	_screenModes[5].w = 640;
	_screenModes[5].h = 512;
}

static int AGA_VideoInit(SDL_PixelFormat *vformat) {
	struct Screen *defaultScreen;
	ULONG width, height;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[AGA_VideoInit]: Called\n");
#endif

	// Check args.
	if (!current_video) {
		SDL_SetError("The current video device cannot be NULL");
		return -1;
	}

	// Before SetVideoMode is called, we use the WB screen as the default.
	defaultScreen = LockPubScreen(NULL);
	if (!defaultScreen) {
		SDL_SetError("Could not lock the default screen");
		return -1;
	}

	// Store a copy of the default screen's P96 attributes before we close it.
	width = defaultScreen->Width;
	height = defaultScreen->Height;
	
	// Finished with the screen for now.
	UnlockPubScreen(NULL, defaultScreen);
	
	// Setup the default display format.
	current_video->info.current_w = width;
	current_video->info.current_h = height;

	// Fill in some basic capabilities.
	current_video->info.hw_available = 0; // Can you create custom surfaces?
	current_video->info.wm_available = 1; // Can you talk to a window manager?
	current_video->info.blit_hw = 0; // Accelerated blits HW
	current_video->info.blit_hw_CC = 0; // Accelerated blits with Colorkey
	current_video->info.blit_hw_A = 0; // Accelerated blits with Alpha
	current_video->info.blit_sw = 0; // Accelerated blits SW
	current_video->info.blit_sw_CC = 0; // Accelerated blits with Colorkey
	current_video->info.blit_sw_A = 0; // Accelerated blits with Alpha
	current_video->info.blit_fill = 0; // Accelerated color fill
	
	// Seems to work best if we just default to an 8-bit depth instead of the actual depth in use.
	vformat->BitsPerPixel = AGA_DISPLAY_DEPTH;
	vformat->BytesPerPixel = 1;
	vformat->Rmask = 0;
	vformat->Gmask = 0;
	vformat->Bmask = 0;
	vformat->Amask = 0;
	
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[AGA_VideoInit]: Default screen attributes - width=%u, height=%u\n", width, height);
#endif

	buildScreenModeList();

	// We're done!
	return 0;
}

// Loads part of our stored palette into the custom screen.
static void LoadPalette(int firstcolor, int ncolors) {
	// Static, 12KB is a lot to put on an Amiga stack.
	static ULONG palette[256 * 3 + 2];
	int i;

	if (!_customScreen) {
		return;
	}

	for (i = 0; i < ncolors; i++) {
		// LoadRGB32 wants left justified 32-bit values.
		palette[i * 3 + 1] = _currentPalette[firstcolor + i].r * 0x01010101UL;
		palette[i * 3 + 2] = _currentPalette[firstcolor + i].g * 0x01010101UL;
		palette[i * 3 + 3] = _currentPalette[firstcolor + i].b * 0x01010101UL;
	}

	palette[0] = ((ULONG)ncolors << 16) + firstcolor;
	palette[((ncolors * 3) + 1)] = 0x00000000;
	LoadRGB32(&_customScreen->ViewPort, palette);
}

static void FreeScreenModeList(void) {
	int i;

	if (_screenModeList) {
		for (i = 0; i < _numScreenModes; ++i) {
			if (_screenModeList[i]) {
				SDL_free(_screenModeList[i]);
			}
		}

		SDL_free(_screenModeList);
		_screenModeList = NULL;
	}

	_numScreenModes = 0;
}

static SDL_Rect** AGA_ListModes(SDL_PixelFormat *format, Uint32 flags) {
    int i;

	// Reject any unsupported flags.
	if ((flags & SDL_HWSURFACE) == SDL_HWSURFACE) {
		if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
			return NULL;
		}
	}

	// No OpenGL on AGA.
    if ((flags & SDL_OPENGL) == SDL_OPENGL) {
        return NULL;
    }

	// Reset.
    FreeScreenModeList();

	//if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[AGA_ListModes]: Generating a list of screen modes with a depth of %u bits\n", format ? format->BitsPerPixel : 0);
#endif

		if (format && format->BitsPerPixel == AGA_DISPLAY_DEPTH) {
			for (i = 0; i < NUMBER_OF_SCREEN_MODES; i++) {
				_numScreenModes++;
				_screenModeList = (SDL_Rect**) SDL_realloc(_screenModeList, (_numScreenModes + 1) * sizeof(SDL_Rect*));
				if (_screenModeList) {
					_screenModeList[_numScreenModes] = NULL;
					_screenModeList[_numScreenModes - 1] = (SDL_Rect*) SDL_malloc(sizeof(SDL_Rect));
					if (_screenModeList[_numScreenModes - 1]) {
						_screenModeList[_numScreenModes - 1]->x = 0;
						_screenModeList[_numScreenModes - 1]->y = 0;
						_screenModeList[_numScreenModes - 1]->w = _screenModes[i].w;
						_screenModeList[_numScreenModes - 1]->h = _screenModes[i].h;;
					}
				}
			}
		}
	//}

    // SDL wants the list in reverse order, largest to smallest.
    if (_numScreenModes > 0) {
        for (i = 0; i < _numScreenModes / 2; ++i) {
            SDL_Rect* temp = _screenModeList[i];
            _screenModeList[i] = _screenModeList[_numScreenModes - 1 - i];
            _screenModeList[_numScreenModes - 1 - i] = temp;
        }
    }

	return _screenModeList;
}

static SDL_Rect** AGA_ListModesEx(SDL_PixelFormat *format, Uint32 flags) {
	// Every mode we have is a native one.
	return AGA_ListModes(format, flags);
}

static void AGA_ShutdownMode(void) {
    if (AMIGA_WINDOW) {
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[AGA_ShutdownMode]: Closing the window\n");
#endif
		CloseWindow(AMIGA_WINDOW);
		AMIGA_WINDOW = NULL;
	}

    if (c2p[0]) {
        c2p8_deinit_stub(c2p[0]);
        c2p[0] = NULL;
    }

    if (c2p[1]) {
        c2p8_deinit_stub(c2p[1]);
        c2p[1] = NULL;
    }

	if (_customScreen) {
		// We opened the screen and therefore must close it.
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[AGA_ShutdownMode]: Closing the custom screen\n");
#endif

		if (_hardwareScreenBuffer[0]) {
	        ChangeScreenBuffer (_customScreen, _hardwareScreenBuffer[0]);
	        WaitTOF();
	        WaitTOF();
	        FreeScreenBuffer (_customScreen, _hardwareScreenBuffer[0]);
	        _hardwareScreenBuffer[0] = NULL;
	    }

	    if (_hardwareScreenBuffer[1]) {
	        FreeScreenBuffer (_customScreen, _hardwareScreenBuffer[1]);
	        _hardwareScreenBuffer[1] = NULL;
	    }

		CloseScreen(_customScreen);
		_customScreen = NULL;
	}

	if (_chunkyBuffer) {
		SDL_free(_chunkyBuffer);
		_chunkyBuffer = NULL;
	}

    // Reset.
    _currentScreenBuffer = 0;
    _screenWidth = _screenHeight = 0;
    _surfaceLeft = _surfaceTop = 0;
	AMIGA_WINDOW = NULL;
}

static struct Screen* createCustomScreen(ULONG modeId, int width, int height, int depth) {
    struct Screen *screen;
	LONG errorCode = 0;
	
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("Trying to create a screen - width=%d, height=%d, depth=%d\n", width, height, depth);
#endif

	screen = OpenScreenTags(NULL, SA_Depth, depth, SA_DisplayID, modeId,
            SA_Top, 0, SA_Left, 0, SA_Width, width, SA_Height, height, SA_Type,
			CUSTOMSCREEN, SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Draggable, FALSE,
            SA_Exclusive, TRUE, SA_AutoScroll, FALSE, SA_ErrorCode, (ULONG)&errorCode, TAG_DONE);
            
#ifdef DEBUG_VIDEO
	if (!screen) {
		SDL_LogDebugMsgchar("OpenScreen failed: mode=%08lx %dx%d depth=%d err=%ld\n",
				modeId, width, height, depth, errorCode);
	}
#endif

	if (screen) {
		// The hardware fills the border (overscan) with colour 0, so any palette effect that
		// changes colour 0 (e.g. a damage flash) would also tint the border. Blank it instead.
		struct TagItem videoTags[] = {
			{ VTAG_BORDERBLANK_SET, TRUE },
			{ TAG_DONE, 0 }
		};

		if (VideoControl(screen->ViewPort.ColorMap, videoTags) == 0) {
			MakeScreen(screen);
			RethinkDisplay();
		}
#ifdef DEBUG_VIDEO
		else {
			SDL_LogDebugMsgchar("VideoControl failed, the border will not be blanked\n");
		}
#endif
	}

	return screen;
}

static struct Window* createWindow(int left, int top, int width, int height, struct Screen *screen) {
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("Trying to create a window - left=%d, top=%d, width=%d, height=%d\n", left, top, width, height);
#endif

	return OpenWindowTags(NULL,
						WA_CustomScreen, (ULONG)screen,
						WA_Left, left,
						WA_Top, top,
						WA_Width, width,
						WA_Height, height,
						WA_Backdrop, TRUE,
						WA_Borderless, TRUE,
						WA_Activate, TRUE,
						WA_SimpleRefresh, TRUE,
						WA_NoCareRefresh, TRUE,
						WA_ReportMouse, FALSE,
						WA_RMBTrap, TRUE,
						WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,
						TAG_END);

}

static SDL_Surface* createSurface(int width, int height, int depth, Uint32 flags) {
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("Creating a surface - width=%d, height=%d, depth=%d\n", width, height, depth);
#endif

    Uint32 rmask;
    Uint32 gmask;
    Uint32 bmask;
    Uint32 amask;

	// We only support 8-bit modes.
    rmask = 0;
    gmask = 0;
    bmask = 0;
    amask = 0;

   	return SDL_CreateRGBSurface(flags, width, height, depth, rmask, gmask, bmask, amask);
}

static SDL_Surface* AGA_SetVideoMode(SDL_Surface *currentSurface, int width, int height, int bpp, Uint32 flags) {
	ULONG modeId = INVALID_ID;
    LONG blackPen;
    int screenWidth = width;
    int screenHeight = height;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[AGA_SetVideoMode]: width=%d, height=%d, depth=%d\n", width, height, bpp);

	if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_SWSURFACE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_SWSURFACE requested\n");
	}

	if ((flags & SDL_HWSURFACE) != SDL_HWSURFACE) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_HWSURFACE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_HWSURFACE requested\n");
	}

	if ((flags & SDL_HWPALETTE) != SDL_HWPALETTE) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_HWPALETTE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_HWPALETTE requested\n");
	}

	if ((flags & SDL_DOUBLEBUF) != SDL_DOUBLEBUF) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_DOUBLEBUF NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_DOUBLEBUF requested\n");
	}

	if ((flags & SDL_FULLSCREEN) != SDL_FULLSCREEN) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_FULLSCREEN NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_FULLSCREEN requested\n");
	}

	if ((flags & SDL_OPENGL) != SDL_OPENGL) {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_OPENGL NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: SDL_OPENGL requested\n");
	}
#endif

	// Free any existing surface (note, this will be our SDL_VideoSurface).
	if (currentSurface) {
		SDL_FreeSurface(currentSurface);
		currentSurface = NULL;
	}

	// Free any existing mode.
	AGA_ShutdownMode();

	// No OpenGL on AGA.
	if ((flags & SDL_OPENGL) == SDL_OPENGL) {
		SDL_SetError("OpenGL is not supported by the AGA driver");
		return NULL;
	}

	if ((flags & SDL_FULLSCREEN) != SDL_FULLSCREEN) {
		// There is no real windowed mode, the 'window' is centred on the smallest screen it fits on.
		if (!FindVirtualScreenSize(width, height, &screenWidth, &screenHeight)) {
			SDL_SetError("Unsupported window attributes: width=%d, height=%d", width, height);
			return NULL;
		}

#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[AGA_SetVideoMode]: Using a %dx%d screen for the %dx%d virtual window\n", screenWidth, screenHeight, width, height);
#endif
	}

	{
        modeId = BestModeID(BIDTAG_NominalWidth, screenWidth,
                            BIDTAG_NominalHeight, screenHeight,
                	        BIDTAG_DesiredWidth, screenWidth,
                	        BIDTAG_DesiredHeight, screenHeight,
                	        BIDTAG_Depth, AGA_DISPLAY_DEPTH,
                	        BIDTAG_MonitorID, PAL_MONITOR_ID,
                	        TAG_END);

		if (modeId == INVALID_ID) {
			SDL_SetError("Could not find a suitable PAL mode for the attributes: width=%d, height=%d, depth=%d", screenWidth, screenHeight, AGA_DISPLAY_DEPTH);
			return NULL;
		}

		// Validate the mode.
		if (!IsValidPALMode(modeId, screenWidth, screenHeight)) {
			SDL_SetError("Could not find a suitable PAL mode for the attributes: width=%d, height=%d, depth=%d", screenWidth, screenHeight, AGA_DISPLAY_DEPTH);
			return NULL;
		}
        		
		_customScreen = createCustomScreen(modeId, screenWidth, screenHeight, AGA_DISPLAY_DEPTH);
        if (!_customScreen) {
            SDL_SetError("Could not create a screen for the mode attributes: width=%d, height=%d, depth=%d", screenWidth, screenHeight, AGA_DISPLAY_DEPTH);
            return NULL;
        }

        _screenWidth = screenWidth;
        _screenHeight = screenHeight;
        _surfaceLeft = (screenWidth - width) / 2;
        _surfaceTop = (screenHeight - height) / 2;

        // Set up double buffering.
    	_hardwareScreenBuffer[0] = AllocScreenBuffer(_customScreen, NULL, SB_SCREEN_BITMAP);
    	if (!_hardwareScreenBuffer[0]) {
    		SDL_SetError("Couldn't create a screen buffer for the requested mode");
    		return NULL;
    	}

    	_hardwareScreenBuffer[1] = AllocScreenBuffer(_customScreen, NULL, 0);
    	if (!_hardwareScreenBuffer[1]) {
    		SDL_SetError("Couldn't create a screen buffer for the requested mode");
    		return NULL;
    	}

    	c2p[0] = c2p8_reloc_stub(_hardwareScreenBuffer[0]->sb_BitMap);
    	c2p[1] = c2p8_reloc_stub(_hardwareScreenBuffer[1]->sb_BitMap);
    	if (!c2p[0] || !c2p[1]) {
    		SDL_SetError("Couldn't initialise the C2P for the requested mode");
    		return NULL;
    	}

    	// Buffer 0 (the screen's own bitmap) is on display, so we draw into buffer 1 first.
    	_currentScreenBuffer = 1;

    	// The virtual window needs a screen sized chunky buffer.
    	if ((screenWidth != width) || (screenHeight != height)) {
    		_chunkyBuffer = (UBYTE*) SDL_malloc(screenWidth * screenHeight);
    		if (!_chunkyBuffer) {
    			SDL_OutOfMemory();
    			return NULL;
    		}

    		// Note, the border uses pen 0.
    		SDL_memset(_chunkyBuffer, 0, screenWidth * screenHeight);
    	}
		
		// Create the window, in fullscreen mode this covers the whole screen.
		AMIGA_WINDOW = createWindow(_surfaceLeft, _surfaceTop, width, height, _customScreen);
		if (!AMIGA_WINDOW) {
			SDL_SetError("Could not create a window for the mode attributes: width=%d, height=%d", width, height);
			return NULL;
		}
		
        // Need to set the custom screen black because it could be bigger than what was asked for.
        blackPen = ObtainBestPen(_customScreen->ViewPort.ColorMap, 0, 0, 0, TAG_DONE);
        if (blackPen != -1) {
        	SetAPen(AMIGA_WINDOW->RPort, blackPen);
            RectFill(AMIGA_WINDOW->RPort, 0, 0, AMIGA_WINDOW->Width - 1, AMIGA_WINDOW->Height - 1);
            ReleasePen(_customScreen->ViewPort.ColorMap, blackPen);
        }
    }
    
  	// Create a new software surface, this new surface will NOT contain the passed surface flags!
   	currentSurface = createSurface(width, height, AGA_DISPLAY_DEPTH, flags);
    if (!currentSurface) {
        SDL_SetError("Could not create a surface with the following attributes: width=%d, height=%d, depth=%d", width, height, AGA_DISPLAY_DEPTH);
		return NULL;
    }
			
	// The C2P converts the surface in one go so it cannot have any padding.
	if (!_chunkyBuffer && (currentSurface->pitch != screenWidth)) {
		SDL_SetError("The surface pitch (%d) does not match the screen width (%d)", currentSurface->pitch, screenWidth);
		SDL_FreeSurface(currentSurface);
		return NULL;
	}

	// Add back any missing flags (that we care about).
	if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
		currentSurface->flags |= SDL_FULLSCREEN;
	}

    // Seed SDL with the real pointer position. The activation message will
    // eventually do this for us, but not until the first PumpEvents, which
    // leaves SDL believing the pointer is at 0,0 until then.
    Amiga_UpdateMouse();

    // Work out whether we should be in relative mode for this mode.
    Amiga_CheckMouseMode();

	// Make sure the new window is activated.
	if (_customScreen) {
		ScreenToFront(_customScreen);
	}

	WindowToFront(AMIGA_WINDOW);
	ActivateWindow(AMIGA_WINDOW);

	// We're done.
	return currentSurface;
}

static int AGA_SetColors(int firstcolor, int ncolors, SDL_Color *colors) {
#ifdef DEBUG_PALETTE
	SDL_LogDebugMsgchar("[AGA_SetColors]: first color = %d\n", firstcolor);
	SDL_LogDebugMsgchar("[AGA_SetColors]: number of colors = %d\n", ncolors);
#endif

    if ((firstcolor < 0) || (ncolors <= 0) || (firstcolor + ncolors > 256)) {
    	return 0;
    }

   	SDL_memcpy(_currentPalette + firstcolor, colors, ncolors * sizeof(SDL_Color));

   	// Apply the changes straight away (if there's no screen yet it gets the stored palette when it opens).
   	LoadPalette(firstcolor, ncolors);

	return 1;
}

static void AGA_UpdateRects(int numrects, SDL_Rect *rects) {
#ifdef DEBUG_UPDATE_RECTS
	SDL_LogDebugMsgchar("[AGA_UpdateRects]: Called\n");
#endif

	// Note - UpdateRects can be called before SetVideoMode!
	if (AMIGA_WINDOW) {
		int i;
		Sint16 dirtyStart, dirtyEnd;
		UBYTE *chunky;

		// Seed.
		dirtyStart = SDL_VideoSurface->h;
		dirtyEnd = 0;

		// Find dirty strip extents.
		for (i = 0; i < numrects; i++) {
			SDL_Rect *r = rects + i;
			if (!r) {
				continue;
			}

			if (r->y < dirtyStart) {
				dirtyStart = r->y;
			}

			if ((r->y + r->h) > dirtyEnd) {
				dirtyEnd = (r->y + r->h);
			}
		}

		// Clamp to the surface, a caller can pass rects that stick out.
		if (dirtyStart < 0) {
			dirtyStart = 0;
		}

		if (dirtyEnd > SDL_VideoSurface->h) {
			dirtyEnd = SDL_VideoSurface->h;
		}

		if (dirtyEnd <= dirtyStart) {
			// Nothing to do.
			return;
		}

#ifdef DEBUG_UPDATE_RECTS
    	SDL_LogDebugMsgchar("[AGA_UpdateRects]: dirtyStart = %d\n", dirtyStart);
    	SDL_LogDebugMsgchar("[AGA_UpdateRects]: dirtyEnd = %d\n", dirtyEnd);
#endif

		if (_chunkyBuffer) {
			// Virtual window, copy the dirty rows into the (persistent) screen sized buffer.
			UBYTE *src = (UBYTE*)SDL_VideoSurface->pixels + (dirtyStart * SDL_VideoSurface->pitch);
			UBYTE *dst = _chunkyBuffer + ((_surfaceTop + dirtyStart) * _screenWidth) + _surfaceLeft;

			for (i = dirtyStart; i < dirtyEnd; i++) {
				SDL_memcpy(dst, src, SDL_VideoSurface->w);
				src += SDL_VideoSurface->pitch;
				dst += _screenWidth;
			}

			chunky = _chunkyBuffer;
		} else {
			chunky = (UBYTE*)SDL_VideoSurface->pixels;
		}

		// Note, the whole screen is always converted because the back buffer is a frame behind.
		c2p8_stub(c2p[_currentScreenBuffer], _hardwareScreenBuffer[_currentScreenBuffer]->sb_BitMap,
				chunky, (ULONG)(_screenWidth * _screenHeight));

		if (ChangeScreenBuffer(_customScreen, _hardwareScreenBuffer[_currentScreenBuffer])) {
		   _currentScreenBuffer = _currentScreenBuffer ^ 1;
		}
	}
}

/* Note:  If we are terminated, this could be called in the middle of
 another SDL video routine -- notably UpdateRects.
 */
static void AGA_VideoQuit(void) {
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[AGA_VideoQuit]: Called\n");
#endif

	// Free any existing mode.
	AGA_ShutdownMode();

	if (current_video) {
		if (current_video->screen->pixels) {
			SDL_free(current_video->screen->pixels);
			current_video->screen->pixels = NULL;
		}
	}

	FreeScreenModeList();
	
    if (_screenTitle) {
        SDL_free(_screenTitle);
        _screenTitle = NULL;
    }
    
    if (_windowTitle) {
        SDL_free(_windowTitle);
        _windowTitle = NULL;
    }
}

/* If 'info' is the right version, this function fills it and returns 1.
 Otherwise, in case of a version mismatch, it returns -1.
 */
static int AGA_GetWMInfo(SDL_SysWMinfo *info) {
	if (info && SDL_VERSIONNUM(info->version.major, info->version.minor, info->version.patch)
					>= SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)) {
		return (1);
	} else {
		SDL_SetError("Not compiled with a compatible SDL version %d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
		return (-1);
	}
}

static void AGA_SetCaption(const char *title, const char *icon) {
	// Reset.
    if (_windowTitle) {
        SDL_free(_windowTitle);
        _windowTitle = NULL;
    }
    
    if (_screenTitle) {
        SDL_free(_screenTitle);
        _screenTitle = NULL;
    }

    if (title && SDL_strlen(title) > 0) {
        _windowTitle = (char *)SDL_malloc(SDL_strlen(title) + 1);
        if (_windowTitle) {
            strcpy(_windowTitle, title);
        }
    }
  
	if (icon && SDL_strlen(icon) > 0) {
        _screenTitle = (char *)SDL_malloc(SDL_strlen(icon) + 1);
        if (_screenTitle) {
            strcpy(_screenTitle, icon);
        }
    }

    if (AMIGA_WINDOW) {
        // Note, it's OK to pass NULL.
	   SetWindowTitles(AMIGA_WINDOW, _windowTitle, _screenTitle);
    }
}

static int AGA_Available(void) {
	int available = 0;
	
	ULONG modeId = BestModeID(BIDTAG_NominalWidth, MAX_VIDEO_MODE_WIDTH,
                        BIDTAG_NominalHeight, MAX_VIDEO_MODE_HEIGHT,
            	        BIDTAG_DesiredWidth, MAX_VIDEO_MODE_WIDTH,
            	        BIDTAG_DesiredHeight, MAX_VIDEO_MODE_HEIGHT,
            	        BIDTAG_Depth, AGA_DISPLAY_DEPTH,
            	        BIDTAG_MonitorID, PAL_MONITOR_ID,
            	        TAG_END);

	if (modeId != INVALID_ID) {
		available = 1;
	}

	return available;
}

static void AGA_DeleteDevice(void) {
	if (current_video) {
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[AGA_DeleteDevice]: Current video device is not NULL\n");
#endif
		SDL_free(current_video->hidden);
		SDL_free(current_video);
	}
}

static SDL_VideoDevice* AGA_CreateDevice(int devindex) {
	SDL_VideoDevice *device;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[AGA_CreateDevice]: Called\n");
#endif

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice*) SDL_malloc(sizeof(SDL_VideoDevice));
	if (device) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData*) SDL_malloc((sizeof *device->hidden));
	}

	if ((device == NULL) || (device->hidden == NULL)) {
		SDL_OutOfMemory();
		if (device) {
			SDL_free(device);
		}
		return NULL;
	}

	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = AGA_VideoInit;
	device->ListModes = AGA_ListModes;
	device->ListModesEx = AGA_ListModesEx;
	device->SetVideoMode = AGA_SetVideoMode;
	device->ToggleFullScreen = NULL;
	device->CreateYUVOverlay = NULL;
	device->SetColors = AGA_SetColors;
	device->UpdateRects = AGA_UpdateRects;
	device->VideoQuit = AGA_VideoQuit;

	device->AllocHWSurface = NULL;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = NULL;
	device->UnlockHWSurface = NULL;
	device->FlipHWSurface = NULL;
	device->FreeHWSurface = NULL;

	device->UpdateMouse = Amiga_UpdateMouse;
	device->GrabInput = Amiga_GrabInput;

	device->SetCaption = AGA_SetCaption;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GetWMInfo = AGA_GetWMInfo;

	device->FreeWMCursor = Amiga_FreeWMCursor;
	device->CreateWMCursor = Amiga_CreateWMCursor;
	device->ShowWMCursor = Amiga_ShowWMCursor;
	device->WarpWMCursor = Amiga_WarpWMCursor;
	device->MoveWMCursor = NULL;
	device->CheckMouseMode = Amiga_CheckMouseMode;

	device->InitOSKeymap = Amiga_InitOSKeymap;
	device->QuitOSKeymap = Amiga_QuitOSKeymap;
	device->PumpEvents = Amiga_PumpEvents;
	device->free = AGA_DeleteDevice;

	return device;
}

VideoBootStrap AGA_bootstrap = { "AGA", "AmigaOS AGA", AGA_Available, AGA_CreateDevice };

#endif  // SDL_VIDEO_DRIVER_AMIGA_AGA
