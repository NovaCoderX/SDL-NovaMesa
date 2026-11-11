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

#ifdef SDL_VIDEO_DRIVER_AMIGA

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
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

// Kept as defines so the library name only has to be changed in one place.
#define P96_LIBRARY_NAME "Picasso96API.library"
#define P96_LIBRARY_VERSION 2

#define WINDOW_PLACEMENT_PADDING 40

#define MIN_VIDEO_MODE_WIDTH 320
#define MIN_VIDEO_MODE_HEIGHT 200

#define MAX_VIDEO_MODE_WIDTH 1280
#define MAX_VIDEO_MODE_HEIGHT 1024

struct Library *P96Base = NULL;

// Custom screen - Only used when running in full screen mode.
static struct Screen *_customScreen = NULL;

SDL_Rect** _screenModeList = NULL;
int _numScreenModes = 0;

static char *_screenTitle = NULL;
static char *_windowTitle = NULL;

// Palette data.
static SDL_Color _currentPalette[256];
static int _paletteDirtyStart = 255;
static int _paletteDirtyEnd = 0;
static ULONG _colorMappingTable[256];

// P96 has no LUT aware equivalent of CyberGraphics' WriteLUTPixelArray(), so an
// 8-bit surface shown in a window is expanded into this ARGB buffer first.
// Only allocated for windowed 8-bit modes.
static ULONG *_lutConvertBuffer = NULL;

static SDL_bool isP96Mode(ULONG modeId) {
	if (modeId == INVALID_ID) {
		return SDL_FALSE;
	}

	return p96GetModeIDAttr(modeId, P96IDA_ISP96) ? SDL_TRUE : SDL_FALSE;
}

static SDL_bool isUsableP96Mode(ULONG modeId) {
	if (!isP96Mode(modeId)) {
		return SDL_FALSE;
	}

	if (ModeNotAvailable(modeId)) {
		return SDL_FALSE;
	}

	return SDL_TRUE;
}

static SDL_bool isPixelFormat32(ULONG pixelFormat) {
	switch (pixelFormat)
	{
		case RGBFB_A8R8G8B8:
		case RGBFB_A8B8G8R8:
		case RGBFB_R8G8B8A8:
		case RGBFB_B8G8R8A8:
			return SDL_TRUE;

		default:
			return SDL_FALSE;
	}
}

static ULONG getP96Mode8(int width, int height) {
	ULONG modeId = INVALID_ID;
	ULONG nextid = NextDisplayInfo(INVALID_ID);

	while (nextid != INVALID_ID) {
		if (isUsableP96Mode(nextid)) {
			if (p96GetModeIDAttr(nextid, P96IDA_RGBFORMAT) == RGBFB_CLUT) {
				if (p96GetModeIDAttr(nextid, P96IDA_WIDTH) == (ULONG)width) {
					if (p96GetModeIDAttr(nextid, P96IDA_HEIGHT) == (ULONG)height) {
						// Success, exact match.
						modeId = nextid;
						break;
					}
				}
			}
		}

		nextid = NextDisplayInfo(nextid);
	}

	return modeId;
}

static ULONG getP96Mode32(int width, int height) {
	ULONG modeId = INVALID_ID;
	ULONG nextid = NextDisplayInfo(INVALID_ID);

	while (nextid != INVALID_ID) {
		if (isUsableP96Mode(nextid)) {
			if (isPixelFormat32(p96GetModeIDAttr(nextid, P96IDA_RGBFORMAT))) {
				if (p96GetModeIDAttr(nextid, P96IDA_WIDTH) == (ULONG)width) {
					if (p96GetModeIDAttr(nextid, P96IDA_HEIGHT) == (ULONG)height) {
						// Success, exact match.
						modeId = nextid;
						break;
					}
				}
			}
		}

		nextid = NextDisplayInfo(nextid);
	}

	return modeId;
}

// Returns the SDL channel mask (0 = red, 1 = green, 2 = blue, 3 = alpha) for a
// given P96 RGBFormat.
//
// P96's RGBFB_#? names describe the BYTE ORDER in memory, whereas an SDL mask
// describes a native 32-bit WORD. On a big endian 68k the two coincide, which
// is why RGBFB_A8R8G8B8 yields exactly the masks a PC SDL port expects.
//
// Only the formats this driver can present as an SDL surface are listed - CLUT
// and the four 32-bit byte orders. The old 15/16 and 24-bit entries have gone;
// they were unreachable, and the "PC" variants of them were mapped incorrectly
// (byte swapping a 16-bit pixel does not reverse its channel order the way
// byte swapping a 32-bit one does).
static Uint32 calculateColorMask(int colorIndex, RGBFTYPE rgbFormat) {
	switch (rgbFormat) {
	case RGBFB_CLUT:
		return 0;

	case RGBFB_A8R8G8B8: // 0xAARRGGBB
		switch (colorIndex) {
		case 0:
			return 0x00FF0000;
		case 1:
			return 0x0000FF00;
		case 2:
			return 0x000000FF;
		default:
			return 0xFF000000;
		}

	case RGBFB_A8B8G8R8: // 0xAABBGGRR
		switch (colorIndex) {
		case 0:
			return 0x000000FF;
		case 1:
			return 0x0000FF00;
		case 2:
			return 0x00FF0000;
		default:
			return 0xFF000000;
		}

	case RGBFB_R8G8B8A8: // 0xRRGGBBAA
		switch (colorIndex) {
		case 0:
			return 0xFF000000;
		case 1:
			return 0x00FF0000;
		case 2:
			return 0x0000FF00;
		default:
			return 0x000000FF;
		}

	case RGBFB_B8G8R8A8: // 0xBBGGRRAA
		switch (colorIndex) {
		case 0:
			return 0x0000FF00;
		case 1:
			return 0x00FF0000;
		case 2:
			return 0xFF000000;
		default:
			return 0x000000FF;
		}

	default:
		return 0;
	}
}

static int RTG_VideoInit(SDL_PixelFormat *vformat) {
	struct Screen *defaultScreen;
	ULONG width, height;
	int i;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[RTG_VideoInit]: Called\n");
#endif

	// Check args.
	if (!current_video) {
		SDL_SetError("The current video device cannot be NULL");
		return -1;
	}

	if (!P96Base) {
		SDL_SetError("Could not open " P96_LIBRARY_NAME);
		return -1;
	}

	// Before SetVideoMode is called, we use the WB screen as the default.
	defaultScreen = LockPubScreen(NULL);
	if (!defaultScreen) {
		SDL_SetError("Could not lock the default screen");
		return -1;
	}

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("Checking if we are using a P96 native display\n");
#endif

	if (!isP96Mode(GetVPModeID(&defaultScreen->ViewPort))) {
		UnlockPubScreen(NULL, defaultScreen);
		SDL_SetError("Workbench screen is not P96 native");
		return -1;
	}

	// Store a copy of the default screen's P96 attributes before we close it.
	width = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_WIDTH);
	height = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_HEIGHT);
	
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
	vformat->BitsPerPixel = 8;
	vformat->BytesPerPixel = 1;
	vformat->Rmask = 0;
	vformat->Gmask = 0;
	vformat->Bmask = 0;
	vformat->Amask = 0;
	
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[RTG_VideoInit]: Default screen attributes - width=%u, height=%u\n", width, height);
#endif

    // Must init color mapping table with default values (only used for window modes).
    for (i = 0; i < 256; i++) {
        _colorMappingTable[i] = 0;
    }

	// We're done!
	return 0;
}

SDL_Rect** RTG_ListModes(SDL_PixelFormat *format, Uint32 flags) {
    int i;
    SDL_bool allocationError = SDL_FALSE;

	// Reject any unsupported flags.
	if ((flags & SDL_HWSURFACE) == SDL_HWSURFACE) {
		if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
			return NULL;
		}
	}

#ifndef SDL_VIDEO_OPENGL
    if ((flags & SDL_OPENGL) == SDL_OPENGL) {
        return NULL;
    }
#endif

	// Reset.
	if (_screenModeList) {
		for (i = 0; i < _numScreenModes; ++i) {
			SDL_free(_screenModeList[i]);
		}

		SDL_free(_screenModeList);
		_screenModeList = NULL;
		_numScreenModes = 0;
	}

	if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
		ULONG width, height, pixelFormat;
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_ListModes]: Generating a list of screen modes with a depth of %u bits\n", format->BitsPerPixel);
#endif

        ULONG nextid = NextDisplayInfo(INVALID_ID);
            
		while ((nextid != INVALID_ID) && (!allocationError)) {
			if (isUsableP96Mode(nextid)) {
				SDL_bool validMode = SDL_FALSE;

				width = p96GetModeIDAttr(nextid, P96IDA_WIDTH);
    			height = p96GetModeIDAttr(nextid, P96IDA_HEIGHT);
    			pixelFormat = p96GetModeIDAttr(nextid, P96IDA_RGBFORMAT);

				// We only support 8 and 32-bit modes (from an SDL perspective).
				if ((format->BitsPerPixel == 8) && (pixelFormat == RGBFB_CLUT)) {
					validMode = SDL_TRUE;
				}

				// A 32-bit request needs a 32-bit mode
				if ((format->BitsPerPixel == 32) && (isPixelFormat32(pixelFormat))) {
					validMode = SDL_TRUE;
				}
        		
        		// We do not support modes below this size.
        		if (width < MIN_VIDEO_MODE_WIDTH || height < MIN_VIDEO_MODE_HEIGHT) {
                    validMode = SDL_FALSE;
                }
                
        		// Or above this size.
        		if (width > MAX_VIDEO_MODE_WIDTH || height > MAX_VIDEO_MODE_HEIGHT) {
                    validMode = SDL_FALSE;
                }
                
                // Or anything silly.
                if (validMode && (width <= 640)) {
                    // Cross-multiplied equivalent of: height / width < 200 / 320
                    if ((height * 8) < (width * 5)) {
                        validMode = SDL_FALSE;
                    }
                }                      
                    
				if (validMode) {
                    int found = 0;

    				// Check to see if this mode is already stored.
    				for (i = 0; i < _numScreenModes; i++) {
    					if (_screenModeList[i]->w == width && _screenModeList[i]->h == height) {
    						found = 1;
    					}
    				}

    				if (!found) {
    					// Mode not stored already, store it.
    					_numScreenModes++;
    					_screenModeList = (SDL_Rect**) SDL_realloc(_screenModeList, (_numScreenModes + 1) * sizeof(SDL_Rect*));
        				if (!_screenModeList) {
                            allocationError = SDL_TRUE;
                            break;
                        }
    					
						_screenModeList[_numScreenModes] = NULL;
						_screenModeList[_numScreenModes - 1] = (SDL_Rect*) SDL_malloc(sizeof(SDL_Rect));
    					if (!_screenModeList[_numScreenModes - 1]) {
                            allocationError = SDL_TRUE;
                            break;
                        }	
	
						_screenModeList[_numScreenModes - 1]->x = 0;
						_screenModeList[_numScreenModes - 1]->y = 0;
						_screenModeList[_numScreenModes - 1]->w = width;
						_screenModeList[_numScreenModes - 1]->h = height;
    				}
                }
			}

			nextid = NextDisplayInfo(nextid);
		}
	} else {
		struct Screen *defaultScreen;
		ULONG screenWidth, screenHeight, screenFormat, screenDepth;
		SDL_bool screenValid = SDL_FALSE;

#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_ListModes]: Generating a list of virtual window modes that support a depth of %u bits\n", format->BitsPerPixel);
#endif

		defaultScreen = LockPubScreen(NULL);
		if (defaultScreen == NULL) {
			SDL_LogWarningMsgchar("Could not lock the default screen\n");
			return NULL;
		}

		screenWidth = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_WIDTH);
		screenHeight = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_HEIGHT);
		screenFormat = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_RGBFORMAT);
		screenDepth = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_DEPTH);

        // Now finished with the screen.
    	UnlockPubScreen(NULL, defaultScreen);

  		// We only support 8 and 32-bit modes (from an SDL perspective).
    	// An 8-bit surface is expanded to ARGB by us before it is displayed,
		// so the screen itself has to be deeper than 8 bits.
		if ((format->BitsPerPixel == 8) && (screenDepth > 8)) {
			screenValid = SDL_TRUE;
		}

		// A 32-bit request needs a 32-bit screen.
		if ((format->BitsPerPixel == 32) && (isPixelFormat32(screenFormat))) {
			screenValid = SDL_TRUE;
		}

   	    if (screenValid) {
   	    	ULONG modeWidth, modeHeight;
            ULONG nextid = NextDisplayInfo(INVALID_ID);

			while ((nextid != INVALID_ID) && (!allocationError)) {
				if (isUsableP96Mode(nextid)) {
					SDL_bool validMode = SDL_TRUE;

					modeWidth = p96GetModeIDAttr(nextid, P96IDA_WIDTH);
					modeHeight = p96GetModeIDAttr(nextid, P96IDA_HEIGHT);

					// We need to filter our window sizes by the current screen size.
					if (modeWidth > (screenWidth - WINDOW_PLACEMENT_PADDING)) {
						// This mode will fit on the screen.
						validMode = SDL_FALSE;
					}

					if (modeHeight > (screenHeight - WINDOW_PLACEMENT_PADDING)) {
						// This mode will fit on the screen.
						validMode = SDL_FALSE;
					}

					// We do not support modes below this size.
					if (modeWidth < MIN_VIDEO_MODE_WIDTH || modeHeight < MIN_VIDEO_MODE_HEIGHT) {
						validMode = SDL_FALSE;
					}

					// Or above this size.
					if (modeWidth > MAX_VIDEO_MODE_WIDTH || modeHeight > MAX_VIDEO_MODE_HEIGHT) {
						validMode = SDL_FALSE;
					}

					// Or anything silly.
					if (validMode && (modeWidth <= 640)) {
						// Cross-multiplied equivalent of: height / width < 200 / 320
						if ((modeHeight * 8) < (modeWidth * 5)) {
							validMode = SDL_FALSE;
						}
					}

					if (validMode) {
						int found = 0;

						// Check to see if this mode is already stored.
						for (i = 0; i < _numScreenModes; i++) {
							if (_screenModeList[i]->w == modeWidth && _screenModeList[i]->h == modeHeight) {
								found = 1;
							}
						}

						if (!found) {
							_numScreenModes++;
							_screenModeList = (SDL_Rect**) SDL_realloc(_screenModeList, (_numScreenModes + 1) * sizeof(SDL_Rect*));
							if (!_screenModeList) {
								allocationError = SDL_TRUE;
								break;
							}

							_screenModeList[_numScreenModes] = NULL;
							_screenModeList[_numScreenModes - 1] = (SDL_Rect*) SDL_malloc(sizeof(SDL_Rect));
							if (!_screenModeList[_numScreenModes - 1]) {
								allocationError = SDL_TRUE;
								break;
							}

							_screenModeList[_numScreenModes - 1]->x = 0;
							_screenModeList[_numScreenModes - 1]->y = 0;
							_screenModeList[_numScreenModes - 1]->w = modeWidth;
							_screenModeList[_numScreenModes - 1]->h = modeHeight;
						}
					}
				}

				nextid = NextDisplayInfo(nextid);
			}
		}
	}
	
	if (allocationError) {
        SDL_LogWarningMsgchar("Could not allocate the screen mode data\n");

        // Reset.
        _screenModeList = NULL;
        _numScreenModes = 0;
    }

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

SDL_Rect** RTG_ListModesEx(SDL_PixelFormat *format, Uint32 flags) {
	// Reject any unsupported flags.
	if ((flags & SDL_HWSURFACE) == SDL_HWSURFACE) {
		if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
			return NULL;
		}
	}

#ifndef SDL_VIDEO_OPENGL
    if ((flags & SDL_OPENGL) == SDL_OPENGL) {
        return NULL;
    }
#endif

	if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
		RTG_ListModes(format, flags);
	} else {
		int i;
		struct Screen *defaultScreen;
		ULONG screenWidth, screenHeight, screenFormat, screenDepth;
		SDL_bool screenValid = SDL_FALSE;
		
    	// Reset.
    	if (_screenModeList) {
    		for (i = 0; i < _numScreenModes; ++i) {
    			SDL_free(_screenModeList[i]);
    		}
    
    		SDL_free(_screenModeList);
    		_screenModeList = NULL;
    		_numScreenModes = 0;
    	}

#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_ListModesEx]: Generating a virtual window mode that supports a depth of %u bits\n", format->BitsPerPixel);
#endif

		defaultScreen = LockPubScreen(NULL);
		if (defaultScreen == NULL) {
			SDL_LogWarningMsgchar("Could not lock the default screen\n");
			return NULL;
		}

		screenWidth = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_WIDTH);
		screenHeight = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_HEIGHT);
		screenFormat = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_RGBFORMAT);
		screenDepth = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_DEPTH);

        // Now finished with the screen.
    	UnlockPubScreen(NULL, defaultScreen);

  		// We only support 8 and 32-bit modes (from an SDL perspective).
    	// An 8-bit surface is expanded to ARGB by us before it is displayed,
		// so the screen itself has to be deeper than 8 bits.
		if ((format->BitsPerPixel == 8) && (screenDepth > 8)) {
			screenValid = SDL_TRUE;
		}

		// A 32-bit request needs a 32-bit screen.
		if ((format->BitsPerPixel == 32) && (isPixelFormat32(screenFormat))) {
			screenValid = SDL_TRUE;
		}
               
   	    if (screenValid) {
            // Just return a single virtual mode that supports the largest possible window size.
        	SDL_bool allocationError = SDL_FALSE;

        	_numScreenModes++;
        	_screenModeList = (SDL_Rect**) SDL_realloc(_screenModeList, (_numScreenModes + 1) * sizeof(SDL_Rect*));
        	if (!_screenModeList) {
                allocationError = SDL_TRUE;
            }
            
            if (!allocationError) {
                _screenModeList[_numScreenModes] = NULL;
        		_screenModeList[_numScreenModes - 1] = (SDL_Rect*) SDL_malloc(sizeof(SDL_Rect));
        		if (!_screenModeList[_numScreenModes - 1]) {
                    allocationError = SDL_TRUE;
                }
            }
            
            if (allocationError) {
                 SDL_LogWarningMsgchar("Could not allocate screen mode data\n");
    
                // Reset.
                _screenModeList = NULL;
                _numScreenModes = 0;               
            } else {
                _screenModeList[_numScreenModes - 1]->x = 0;
        		_screenModeList[_numScreenModes - 1]->y = 0;
        		_screenModeList[_numScreenModes - 1]->w = (screenWidth - WINDOW_PLACEMENT_PADDING);
        		_screenModeList[_numScreenModes - 1]->h = (screenHeight - WINDOW_PLACEMENT_PADDING);
            }
        }
	}

	return _screenModeList;
}

void RTG_ShutdownMode(void) {
#ifdef SDL_VIDEO_OPENGL
	// Free any existing OpenGL context.
	NovaMesa_Quit();
#endif

    if (AMIGA_WINDOW) {
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_ShutdownMode]: Closing the window\n");
#endif
		CloseWindow(AMIGA_WINDOW);
		AMIGA_WINDOW = NULL;
	}

	if (_customScreen) {
		// We opened the screen and therefore must close it.
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_ShutdownMode]: Closing the custom screen\n");
#endif
		CloseScreen(_customScreen);
		_customScreen = NULL;
	}

	// Free the 8-bit expansion buffer if we have one.
	if (_lutConvertBuffer) {
		SDL_free(_lutConvertBuffer);
		_lutConvertBuffer = NULL;
	}

    // Reset.
    _paletteDirtyStart = 255;
    _paletteDirtyEnd = 0;
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

	return screen;
}

static struct Window* createWindow(int width, int height, struct Screen *screen, SDL_bool fullscreen) {
#ifdef DEBUG_VIDEO
	if (fullscreen) {
		SDL_LogDebugMsgchar("Trying to create a window (fullscreen mode) - width=%d, height=%d\n", width, height);
	} else {
		SDL_LogDebugMsgchar("Trying to create a window (windowed mode) - width=%d, height=%d\n", width, height);
	}
#endif

	if (fullscreen) {
	    return OpenWindowTags(NULL,
	    					WA_CustomScreen, (ULONG)screen,
							WA_Left, 0,
	            			WA_Top, 0,
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
	} else {
	    LONG left = (screen->Width - width)  / 2;
	    LONG top = (screen->Height - height) / 2;

		return OpenWindowTags(NULL,
						WA_PubScreen, (ULONG)screen,
						WA_Left, left,
						WA_Top, top,
						WA_InnerWidth, width,
						WA_InnerHeight, height,
						WA_Backdrop, FALSE,
						WA_Borderless, FALSE,
						WA_DragBar, TRUE,
						WA_DepthGadget, TRUE,
						WA_CloseGadget, TRUE,
						WA_Activate, TRUE,
						WA_SimpleRefresh, TRUE,
						WA_NoCareRefresh, TRUE,
						WA_ReportMouse, FALSE,
						WA_RMBTrap, TRUE,
						WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,
						TAG_END);
	}
}

static SDL_Surface* createSurface(int width, int height, int depth, Uint32 flags) {
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("Creating a surface - width=%d, height=%d, depth=%d\n", width, height, depth);
#endif

    RGBFTYPE rgbFormat;
    Uint32 rmask;
    Uint32 gmask;
    Uint32 bmask;
    Uint32 amask;

	// We only support 8 and 32-bit modes (from an SDL perspective).
	//
	// Note that the 32-bit surface is deliberately forced to ARGB no matter what
	// the display is actually using - p96WritePixelArray() converts on the way
	// out. ARGB is the byte order that corresponds to the 32-bit word a PC SDL
	// port expects (0xAARRGGBB), so unmodified PC code works unchanged.
	if (depth == 8) {
		rgbFormat = RGBFB_CLUT;
	} else {
		rgbFormat = RGBFB_A8R8G8B8;
	}

    rmask = calculateColorMask(0, rgbFormat);
    gmask = calculateColorMask(1, rgbFormat);
    bmask = calculateColorMask(2, rgbFormat);
    amask = 0;

   	return SDL_CreateRGBSurface(flags, width, height, depth, rmask, gmask, bmask, amask);
}

SDL_Surface* RTG_SetVideoMode(SDL_Surface *currentSurface, int width, int height, int bpp, Uint32 flags) {
    ULONG blackPen;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[RTG_SetVideoMode]: width=%d, height=%d, depth=%d\n", width, height, bpp);

	if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_SWSURFACE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_SWSURFACE requested\n");
	}

	if ((flags & SDL_HWSURFACE) != SDL_HWSURFACE) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_HWSURFACE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_HWSURFACE requested\n");
	}

	if ((flags & SDL_HWPALETTE) != SDL_HWPALETTE) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_HWPALETTE NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_HWPALETTE requested\n");
	}

	if ((flags & SDL_DOUBLEBUF) != SDL_DOUBLEBUF) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_DOUBLEBUF NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_DOUBLEBUF requested\n");
	}

	if ((flags & SDL_FULLSCREEN) != SDL_FULLSCREEN) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_FULLSCREEN NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_FULLSCREEN requested\n");
	}

	if ((flags & SDL_OPENGL) != SDL_OPENGL) {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_OPENGL NOT requested\n");
	} else {
		SDL_LogDebugMsgchar("[RTG_SetVideoMode]: SDL_OPENGL requested\n");
	}
#endif

	// Free any existing surface (note, this will be our SDL_VideoSurface).
	if (currentSurface) {
		SDL_FreeSurface(currentSurface);
		currentSurface = NULL;
	}

	// Free any existing mode.
	RTG_ShutdownMode();
	
    if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
    	ULONG modeId = INVALID_ID;
    	
    	// Find the P96 mode (already validated by ListModes).
    	if (bpp == 8) {
    		modeId = getP96Mode8(width, height);
    	} else {
    		modeId = getP96Mode32(width, height);
    	}

		if (modeId == INVALID_ID) {
			SDL_SetError("Could not find a suitable P96 mode for the attributes: width=%d, height=%d, depth=%d", width, height, bpp);
			return NULL;
		}
        		
		_customScreen = createCustomScreen(modeId, width, height, p96GetModeIDAttr(modeId, P96IDA_BITSPERPIXEL));
        if (!_customScreen) {
            SDL_SetError("Could not create a screen for the mode attributes: width=%d, height=%d, depth=%d", width, height, bpp);
            return NULL;
        }
		
		// Create the (full screen) window.
		AMIGA_WINDOW = createWindow(width, height, _customScreen, SDL_TRUE);
		if (!AMIGA_WINDOW) {
			SDL_SetError("Could not create a window for the mode attributes: width=%d, height=%d", width, height);
			return NULL;
		}
		
        // Need to set the custom screen black because it could be bigger than what was asked for.
        blackPen = ObtainBestPen(_customScreen->ViewPort.ColorMap, 0, 0, 0, TAG_DONE);
        if (blackPen) {
        	SetAPen(AMIGA_WINDOW->RPort, blackPen);
            RectFill(AMIGA_WINDOW->RPort, 0, 0, AMIGA_WINDOW->Width - 1, AMIGA_WINDOW->Height - 1);
            ReleasePen(_customScreen->ViewPort.ColorMap, blackPen);
        }
    } else {
    	// In windowed mode on a public screen.
    	struct Screen* defaultScreen;
    	ULONG screenFormat, screenDepth;
    	
    	defaultScreen = LockPubScreen(NULL);
		if (!defaultScreen) {
			SDL_SetError("Could not lock the default screen");
			return NULL;
		}

		// Sanity check, make sure the default public screen is large enough.
		if (defaultScreen->Width < (width + WINDOW_PLACEMENT_PADDING)) {
			UnlockPubScreen(NULL, defaultScreen);
			SDL_SetError("Cannot create a window for the requested width: %d", width);
			return NULL;
		}

		if (defaultScreen->Height < (height + WINDOW_PLACEMENT_PADDING)) {
			UnlockPubScreen(NULL, defaultScreen);
			SDL_SetError("Cannot create a window for the requested height: %d", height);
			return NULL;
		}
		
		// Create the window.
		AMIGA_WINDOW = createWindow(width, height, defaultScreen, SDL_FALSE);
		if (!AMIGA_WINDOW) {
			UnlockPubScreen(NULL, defaultScreen);
			SDL_SetError("Could not create a window with the attributes: width=%d, height=%d", width, height);
			return NULL;
		}
		
		// Check the pixel format of the screen we will be rendering into.
		screenFormat = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_RGBFORMAT);
		screenDepth = p96GetBitMapAttr(defaultScreen->RastPort.BitMap, P96BMA_DEPTH);

		// Now finished with the default screen.
		UnlockPubScreen(NULL, defaultScreen);

        if (bpp == 8) {
        	// Sanity check.
			if (screenDepth <= 8) {
				RTG_ShutdownMode();
				SDL_SetError("An 8-bit windowed mode needs a screen deeper than 8 bits");
				return NULL;
			}

			// P96 has no LUT aware equivalent of WriteLUTPixelArray(), so an 8-bit
			// surface is expanded to ARGB by us first. That means the screen has
			// to be deeper than 8 bits, and we need a buffer to expand into.
			_lutConvertBuffer = (ULONG*)SDL_malloc((size_t)width * (size_t)height * sizeof(ULONG));
			if (!_lutConvertBuffer) {
				RTG_ShutdownMode();
				SDL_OutOfMemory();
				return NULL;
			}
        } else {
        	// Sanity check.
        	if (!isPixelFormat32(screenFormat)) {
				RTG_ShutdownMode();
				SDL_SetError("A 32-bit windowed mode needs a 32-bit screen");
				return NULL;
			}
        }
		
        // Reset the title.
        SetWindowTitles(AMIGA_WINDOW, _windowTitle, _screenTitle);

        // Set the window to black.
		blackPen = ObtainBestPen(AMIGA_WINDOW->WScreen->ViewPort.ColorMap, 0, 0, 0, TAG_DONE);
		if (blackPen) {
			SetAPen(AMIGA_WINDOW->RPort, blackPen);

			// Fill only the inner window area to preserve window borders and gadgets.
			RectFill(AMIGA_WINDOW->RPort,
					AMIGA_WINDOW->BorderLeft,
					AMIGA_WINDOW->BorderTop,
					AMIGA_WINDOW->Width - AMIGA_WINDOW->BorderRight - 1,
					AMIGA_WINDOW->Height - AMIGA_WINDOW->BorderBottom - 1);

			ReleasePen(AMIGA_WINDOW->WScreen->ViewPort.ColorMap, blackPen);
		}
    }
    
  	// Create a new software surface, this new surface will NOT contain the passed surface flags!
   	currentSurface = createSurface(width, height, bpp, flags);
    if (!currentSurface) {
        SDL_SetError("Could not create a surface with the following attributes: width=%d, height=%d, depth=%d", width, height, bpp);
		return NULL;
    }
			
	// Add back any missing flags (that we care about).
	if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
		currentSurface->flags |= SDL_FULLSCREEN;
	}

#ifdef SDL_VIDEO_OPENGL
	// Create OpenGL Context if needed.
	if ((flags & SDL_OPENGL) == SDL_OPENGL) {
		if (NovaMesa_Init() < 0) {
			// Error already reported.
			return NULL;
		} else {
			// Our new surface now supports OpenGL.
			currentSurface->flags |= SDL_OPENGL;
		}
	}
#endif

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

static int RTG_SetColors(int firstcolor, int ncolors, SDL_Color *colors) {
#ifdef DEBUG_PALETTE
	SDL_LogDebugMsgchar("[RTG_SetColors]: first color = %d\n", firstcolor);
	SDL_LogDebugMsgchar("[RTG_SetColors]: number of colors = %d\n", ncolors);
#endif

   	SDL_memcpy(_currentPalette + firstcolor, colors, ncolors * sizeof(SDL_Color));

    if (_customScreen) {
        int i;
        ULONG palette[256 * 3 + 2];	
    
        // When running in full screen mode, immediately apply the changes.
    	for (i = 0; i < ncolors; i++) {
    		palette[i * 3 + 1] = _currentPalette[firstcolor + i].r << 24;
    		palette[i * 3 + 2] = _currentPalette[firstcolor + i].g << 24;
    		palette[i * 3 + 3] = _currentPalette[firstcolor + i].b << 24;
    	}
    
    	palette[0] = (ncolors << 16) + firstcolor;
    	palette[((ncolors * 3) + 1)] = 0x00000000;    
    	LoadRGB32(&_customScreen->ViewPort, palette);
    } else {
        // For windowed mode, store for later (because it doesn't really have a palette).
        if (firstcolor < _paletteDirtyStart) {
            _paletteDirtyStart = firstcolor;
        }

        if (firstcolor + ncolors > _paletteDirtyEnd) {
            _paletteDirtyEnd = firstcolor + ncolors;
        }
    }

	return 1;
}

static void updateColorMappingTable(void) {
    int i;

    // These are plain 32-bit ARGB values (0x00RRGGBB). That is a VALUE, not a
    // byte order, so it never needs any endian adjustment - the old
    // _targetDisplayLittleEndian branch produced a bit for bit identical result
    // and has been removed.
    for (i = _paletteDirtyStart; i < _paletteDirtyEnd; i++) {
        _colorMappingTable[i] = ((ULONG)_currentPalette[i].r << 16)
                              | ((ULONG)_currentPalette[i].g << 8)
                              |  (ULONG)_currentPalette[i].b;
    }

    // Reset.
    _paletteDirtyStart = 255;
    _paletteDirtyEnd = 0;
}

static void RTG_UpdateRects(int numrects, SDL_Rect *rects) {
#ifdef DEBUG_UPDATE_RECTS
	SDL_LogDebugMsgchar("[RTG_UpdateRects]: Called\n");
#endif

	// Note - UpdateRects can be called before SetVideoMode!
	if (AMIGA_WINDOW) {
		int i;
		Sint16 dirtyStart, dirtyEnd;
		struct RenderInfo renderInfo;

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
    	SDL_LogDebugMsgchar("[RTG_UpdateRects]: dirtyStart = %d\n", dirtyStart);
    	SDL_LogDebugMsgchar("[RTG_UpdateRects]: dirtyEnd = %d\n", dirtyEnd);
#endif

		if (_customScreen && SDL_VideoSurface->format->palette) {
			LONG bitMapLock;

#ifdef DEBUG_UPDATE_RECTS
			SDL_LogDebugMsgchar("[RTG_UpdateRects]: Performing a direct blit because we are using a fullscreen mode with an 8-bit source surface\n");
#endif

			// Source and destination are both LUT8, so blit the dirty strip
			// straight into the screen's bitmap.
			bitMapLock = p96LockBitMap(AMIGA_WINDOW->RPort->BitMap, (UBYTE*)&renderInfo, sizeof(renderInfo));
			if (bitMapLock) {
			    ULONG srcPitch  = (ULONG)SDL_VideoSurface->pitch;
			    ULONG destPitch = (ULONG)renderInfo.BytesPerRow;
			    UBYTE *src  = ((UBYTE*)SDL_VideoSurface->pixels) + (dirtyStart * srcPitch);
			    UBYTE *dest = ((UBYTE*)renderInfo.Memory) + (dirtyStart * destPitch);

			    if (srcPitch == destPitch) {
			        /* Strides match, so the whole strip is one contiguous block. */
			        CopyMemQuick(src, dest, ((dirtyEnd - dirtyStart) * srcPitch));
			    } else {
			        int y;

			        /* CopyMem() rather than CopyMemQuick() - the row length is the
			         * surface width, which is not necessarily a multiple of 4, and
			         * the board's stride can leave dest unaligned. */
			        for (y = dirtyStart; y < dirtyEnd; y++) {
			            CopyMem(src, dest, SDL_VideoSurface->w);
			            src  += srcPitch;
			            dest += destPitch;
			        }
			    }

			    p96UnlockBitMap(AMIGA_WINDOW->RPort->BitMap, bitMapLock);
			}
		} else {
			if (SDL_VideoSurface->format->palette) {
#ifdef DEBUG_UPDATE_RECTS
				SDL_LogDebugMsgchar("[RTG_UpdateRects]: Expanding the 8-bit source surface to ARGB\n");
#endif

				// P96 has no LUT aware equivalent of CyberGraphics'
				// WriteLUTPixelArray(), so expand the dirty strip ourselves and
				// then hand P96 a normal ARGB array.
				if (_lutConvertBuffer) {
					int y;
					int x;
					int w = SDL_VideoSurface->w;
					UBYTE *src = ((UBYTE*)SDL_VideoSurface->pixels) + (dirtyStart * SDL_VideoSurface->pitch);
					ULONG *dest = _lutConvertBuffer + (dirtyStart * w);

					// Check whether the palette was changed.
					if (_paletteDirtyEnd) {
						updateColorMappingTable();
					}

					for (y = dirtyStart; y < dirtyEnd; y++) {
						for (x = 0; x < w; x++) {
							dest[x] = _colorMappingTable[src[x]];
						}

						src += SDL_VideoSurface->pitch;
						dest += w;
					}

					renderInfo.Memory = (APTR)_lutConvertBuffer;
					renderInfo.BytesPerRow = (WORD)(w * sizeof(ULONG));
					renderInfo.pad = 0;
					renderInfo.RGBFormat = RGBFB_A8R8G8B8;

					p96WritePixelArray(&renderInfo, //RenderInfo
						0, //SrcX
						dirtyStart, //SrcY
						AMIGA_WINDOW->RPort, //RastPort
						AMIGA_WINDOW->BorderLeft, //DestX
						AMIGA_WINDOW->BorderTop + dirtyStart, //DestY
						w, //SizeX
						(dirtyEnd - dirtyStart)); //SizeY
				}
			} else {
#ifdef DEBUG_UPDATE_RECTS
				SDL_LogDebugMsgchar("[RTG_UpdateRects]: Using p96WritePixelArray() because the source surface is 32-bits\n");
#endif

				// The surface is ARGB, P96 converts to whatever the screen is
				// really using on the way out.
				renderInfo.Memory = (APTR)SDL_VideoSurface->pixels;
				renderInfo.BytesPerRow = (WORD)SDL_VideoSurface->pitch;
				renderInfo.pad = 0;
				renderInfo.RGBFormat = RGBFB_A8R8G8B8;

				p96WritePixelArray(&renderInfo, //RenderInfo
					0, //SrcX
					dirtyStart, //SrcY
					AMIGA_WINDOW->RPort, //RastPort
					AMIGA_WINDOW->BorderLeft, //DestX
					AMIGA_WINDOW->BorderTop + dirtyStart, //DestY
					SDL_VideoSurface->w, //SizeX
					(dirtyEnd - dirtyStart)); //SizeY
			}
		}
	}
}

/* Note:  If we are terminated, this could be called in the middle of
 another SDL video routine -- notably UpdateRects.
 */
static void RTG_VideoQuit(void) {
	int i;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[RTG_VideoQuit]: Called\n");
#endif

	// Free any existing mode.
	RTG_ShutdownMode();

	if (current_video) {
		if (current_video->screen->pixels) {
			SDL_free(current_video->screen->pixels);
			current_video->screen->pixels = NULL;
		}
	}

	if (_screenModeList) {
		for (i = 0; i < _numScreenModes; ++i) {
			SDL_free(_screenModeList[i]);
		}

		SDL_free(_screenModeList);
		_screenModeList = NULL;
		_numScreenModes = 0;
	}
	
    if (_screenTitle) {
        SDL_free(_screenTitle);
        _screenTitle = NULL;
    }
    
    if (_windowTitle) {
        SDL_free(_windowTitle);
        _windowTitle = NULL;
    }	

	if (P96Base) {
		CloseLibrary(P96Base);
		P96Base = NULL;
	}
}

/* If 'info' is the right version, this function fills it and returns 1.
 Otherwise, in case of a version mismatch, it returns -1.
 */
static int RTG_GetWMInfo(SDL_SysWMinfo *info) {
	if (info && SDL_VERSIONNUM(info->version.major, info->version.minor, info->version.patch)
					>= SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)) {
		return (1);
	} else {
		SDL_SetError("Not compiled with a compatible SDL version %d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
		return (-1);
	}
}

static void RTG_SetCaption(const char *title, const char *icon) {
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

static int RTG_Available(void) {
	int available = 0;
	
	P96Base = OpenLibrary(P96_LIBRARY_NAME, P96_LIBRARY_VERSION);
	if (P96Base) {
        struct Screen *defaultScreen = LockPubScreen(NULL);
        if (defaultScreen) {
            if (isP96Mode(GetVPModeID(&defaultScreen->ViewPort))) {
            	available = 1;
            }  
            
            // Finished with the screen for now.
            UnlockPubScreen(NULL, defaultScreen);
        }
	}

	return available;
}

static void RTG_DeleteDevice(void) {
	if (current_video) {
#ifdef DEBUG_VIDEO
		SDL_LogDebugMsgchar("[RTG_DeleteDevice]: Current video device is not NULL\n");
#endif
		SDL_free(current_video->hidden);
		SDL_free(current_video);
	}
}

static SDL_VideoDevice* RTG_CreateDevice(int devindex) {
	SDL_VideoDevice *device;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[RTG_CreateDevice]: Called\n");
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
	device->VideoInit = RTG_VideoInit;
	device->ListModes = RTG_ListModes;
	device->ListModesEx = RTG_ListModesEx;
	device->SetVideoMode = RTG_SetVideoMode;
	device->ToggleFullScreen = NULL;
	device->CreateYUVOverlay = NULL;
	device->SetColors = RTG_SetColors;
	device->UpdateRects = RTG_UpdateRects;
	device->VideoQuit = RTG_VideoQuit;

	device->AllocHWSurface = NULL;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = NULL;
	device->UnlockHWSurface = NULL;
	device->FlipHWSurface = NULL;
	device->FreeHWSurface = NULL;

#ifdef SDL_VIDEO_OPENGL
	device->GL_LoadLibrary = NovaMesa_LoadLibrary;
	device->GL_GetProcAddress = NovaMesa_GetProcAddress;
	device->GL_GetAttribute = NULL;
	device->GL_MakeCurrent = NovaMesa_MakeCurrent;
	device->GL_SwapBuffers = NovaMesa_SwapBuffers;
#endif

	device->UpdateMouse = Amiga_UpdateMouse;
	device->GrabInput = Amiga_GrabInput;

	device->SetCaption = RTG_SetCaption;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GetWMInfo = RTG_GetWMInfo;

	device->FreeWMCursor = Amiga_FreeWMCursor;
	device->CreateWMCursor = Amiga_CreateWMCursor;
	device->ShowWMCursor = Amiga_ShowWMCursor;
	device->WarpWMCursor = Amiga_WarpWMCursor;
	device->MoveWMCursor = NULL;
	device->CheckMouseMode = Amiga_CheckMouseMode;

	device->InitOSKeymap = Amiga_InitOSKeymap;
	device->QuitOSKeymap = Amiga_QuitOSKeymap;
	device->PumpEvents = Amiga_PumpEvents;
	device->free = RTG_DeleteDevice;

	return device;
}

VideoBootStrap RTG_bootstrap = { "RTG", "AmigaOS RTG", RTG_Available, RTG_CreateDevice };

#endif  // SDL_VIDEO_DRIVER_AMIGA
