/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

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

/* The high-level video driver subsystem */
#include "SDL.h"
#include "SDL_sysvideo.h"
#include "SDL_blit.h"
#include "SDL_pixels_c.h"
#include "SDL_cursor_c.h"
#include "../events/SDL_sysevents.h"
#include "../events/SDL_events_c.h"


/* Available video drivers */
static VideoBootStrap *bootstrap[] = {
#if SDL_VIDEO_DRIVER_QUARTZ
	&QZ_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_X11
	&X11_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DGA
	&DGA_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_NANOX
	&NX_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_IPOD
	&iPod_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_QTOPIA
	&Qtopia_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_WSCONS
	&WSCONS_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_FBCON
	&FBCON_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DIRECTFB
	&DirectFB_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_PS2GS
	&PS2GS_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_PS3
	&PS3_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_GGI
	&GGI_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_VGL
	&VGL_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_SVGALIB
	&SVGALIB_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_GAPI
	&GAPI_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_WINDIB
	&WINDIB_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DDRAW
	&DIRECTX_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_BWINDOW
	&BWINDOW_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_TOOLBOX
	&TOOLBOX_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DRAWSPROCKET
	&DSp_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_PHOTON
	&ph_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_EPOC
	&EPOC_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_XBIOS
	&XBIOS_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_GEM
	&GEM_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_PICOGUI
	&PG_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DC
	&DC_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_NDS
	&NDS_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_RISCOS
	&RISCOS_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_OS2FS
	&OS2FSLib_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_AALIB
	&AALIB_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_CACA
	&CACA_bootstrap,
#endif
#ifdef SDL_VIDEO_DRIVER_AMIGA
	&RTG_bootstrap,
#endif
#if SDL_VIDEO_DRIVER_DUMMY
	&DUMMY_bootstrap,
#endif
	NULL
};

SDL_VideoDevice *current_video = NULL;

/* Various local functions */
int SDL_VideoInit(const char *driver_name, Uint32 flags);
void SDL_VideoQuit(void);

#ifdef SDL_VIDEO_OPENGL
void SDL_GL_UpdateRectsLock(SDL_VideoDevice* this, int numrects, SDL_Rect* rects);
#endif

extern SDL_GrabMode SDL_WM_GrabInputOff(void);


#ifdef SDL_VIDEO_OPENGL
static int lock_count = 0;
#endif


/*
 * Initialize the video and event subsystems -- determine native pixel format
 */
int SDL_VideoInit (const char *driver_name, Uint32 flags)
{
	SDL_VideoDevice *video;
	int index;
	int i;
	SDL_PixelFormat vformat;
	Uint32 video_flags;

	/* Toggle the event thread flags, based on OS requirements */
#if defined(MUST_THREAD_EVENTS)
	flags |= SDL_INIT_EVENTTHREAD;
#elif defined(CANT_THREAD_EVENTS)
	if ( (flags & SDL_INIT_EVENTTHREAD) == SDL_INIT_EVENTTHREAD ) {
		SDL_SetError("OS doesn't support threaded events");
		return(-1);
	}
#endif

	/* Check to make sure we don't overwrite 'current_video' */
	if ( current_video != NULL ) {
		SDL_VideoQuit();
	}

	/* Select the proper video driver */
	index = 0;
	video = NULL;
	if ( driver_name != NULL ) {
#if 0	/* This will be replaced with a better driver selection API */
		if ( SDL_strrchr(driver_name, ':') != NULL ) {
			index = atoi(SDL_strrchr(driver_name, ':')+1);
		}
#endif
		for ( i=0; bootstrap[i]; ++i ) {
			if ( SDL_strcasecmp(bootstrap[i]->name, driver_name) == 0) {
				if ( bootstrap[i]->available() ) {
					video = bootstrap[i]->create(index);
					break;
				}
			}
		}
	} else {
		for ( i=0; bootstrap[i]; ++i ) {
			if ( bootstrap[i]->available() ) {
				video = bootstrap[i]->create(index);
				if ( video != NULL ) {
					break;
				}
			}
		}
	}
	
	if ( video == NULL ) {
		SDL_SetError("No available video device");
		return(-1);
	}
	
	current_video = video;
	current_video->name = bootstrap[i]->name;

	/* Do some basic variable initialization */
	video->screen = NULL;
	video->visible = NULL;
	video->physpal = NULL;
	video->gammacols = NULL;
	video->gamma = NULL;
	video->wm_title = NULL;
	video->wm_icon  = NULL;
	video->offset_x = 0;
	video->offset_y = 0;
	SDL_memset(&video->info, 0, (sizeof video->info));
	
	video->displayformatalphapixel = NULL;
	
	/* Initialize the video subsystem */
	SDL_memset(&vformat, 0, sizeof(vformat));
	if ( video->VideoInit(&vformat) < 0 ) {
		SDL_VideoQuit();
		return(-1);
	}

	/* Create a zero sized video surface of the appropriate format */
	video_flags = SDL_SWSURFACE;
	SDL_VideoSurface = SDL_CreateRGBSurface(video_flags, 0, 0,
				vformat.BitsPerPixel,
				vformat.Rmask, vformat.Gmask, vformat.Bmask, 0);
	if ( SDL_VideoSurface == NULL ) {
		SDL_VideoQuit();
		return(-1);
	}
	SDL_PublicSurface = NULL;	/* Until SDL_SetVideoMode() */

#if 0 /* Don't change the current palette - may be used by other programs.
       * The application can't do anything with the display surface until
       * a video mode has been set anyway. :)
       */
	/* If we have a palettized surface, create a default palette */
	if ( SDL_VideoSurface->format->palette ) {
		SDL_PixelFormat *vf = SDL_VideoSurface->format;
		SDL_DitherColors(vf->palette->colors, vf->BitsPerPixel);
		video->SetColors(video,
				 0, vf->palette->ncolors, vf->palette->colors);
	}
#endif

    // Set info to match the current video mode (surface).
    video->info.vfmt = SDL_VideoSurface->format;

	/* Start the event loop */
	if ( SDL_StartEventLoop(flags) < 0 ) {
		SDL_VideoQuit();
		return(-1);
	} 
	
	SDL_CursorInit();

#ifdef SDL_VIDEO_OPENGL
	// OpenGL is always available (nothing to load).
	current_video->gl_config.driver_loaded = 1;
#endif

	/* We're ready to go! */
	return(0);
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
	if ( current_video != NULL ) {
		SDL_strlcpy(namebuf, current_video->name, maxlen);
		return(namebuf);
	}
	return(NULL);
}

/*
 * Get the current display surface
 */
SDL_Surface *SDL_GetVideoSurface(void)
{
	SDL_Surface *visible;

	visible = NULL;
	if ( current_video ) {
		visible = current_video->visible;
	}
	return(visible);
}

/*
 * Get the current information about the video hardware
 */
const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
	const SDL_VideoInfo *info;

	info = NULL;
	if ( current_video ) {
		info = &current_video->info;
	}
	return(info);
}

/*
 * Return a pointer to an array of available screen dimensions for the
 * given format, sorted largest to smallest.  Returns NULL if there are
 * no dimensions available for a particular format, or (SDL_Rect **)-1
 * if any dimension is okay for the given format.  If 'format' is NULL,
 * the mode list will be for the format given by SDL_GetVideoInfo()->vfmt
 */
SDL_Rect ** SDL_ListModes (SDL_PixelFormat *format, Uint32 flags)
{
	SDL_Rect **modes;
	
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[SDL_ListModes]: Called\n");
#endif

	modes = NULL;
	if ( SDL_VideoSurface ) {
		if ( format == NULL ) {
			format = SDL_VideoSurface->format;
		}
		modes = current_video->ListModes(format, flags);
	}
	return(modes);
}

/*
 * Check to see if a particular video mode is supported.
 * It returns 0 if the requested mode is not supported.
 */
static Uint8 SDL_closest_depths[4][8] = {
	/* 8 bit closest depth ordering */
	{ 0, 8, 8, 8, 8, 8, 0, 0 },
	/* 15,16 bit closest depth ordering */
	{ 0, 16, 32, 32, 32, 32, 0, 0 },
	/* 24 bit closest depth ordering */
	{ 0, 24, 32, 32, 32, 32, 0, 0 },
	/* 32 bit closest depth ordering */
	{ 0, 32, 32, 32, 32, 32, 0, 0 }
};

#ifdef __MACOS__ /* MPW optimization bug? */
#define NEGATIVE_ONE 0xFFFFFFFF
#else
#define NEGATIVE_ONE -1
#endif

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
	int table, b, i;
	int supported;
	SDL_PixelFormat format;
	SDL_Rect **sizes;
	
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: width=%d, height=%d, bpp=%d\n", width, height, bpp);
#endif	

	/* Currently 1 and 4 bpp are not supported */
	if ( bpp < 8 || bpp > 32 ) {
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Unsupported bit-depth\n");
#endif            
		return(0);
	}
	
	if ( (width <= 0) || (height <= 0) ) {
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Unsupported width or height\n");
#endif        
		return(0);
	}

	/* Search through the list valid of modes */
	SDL_memset(&format, 0, sizeof(format));
	supported = 0;
	table = ((bpp+7)/8)-1;
	SDL_closest_depths[table][0] = bpp;
	SDL_closest_depths[table][7] = 0;
	for ( b = 0; !supported && SDL_closest_depths[table][b]; ++b ) {
		format.BitsPerPixel = SDL_closest_depths[table][b];
		sizes = current_video->ListModesEx(&format, flags);
		if ( sizes == (SDL_Rect **)0 ) {
			/* No sizes supported at this bit-depth */
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: No sizes supported at this bit-depth (%dbit)\n", format.BitsPerPixel);
#endif			
			continue;
		} else 
		if (sizes == (SDL_Rect **)NEGATIVE_ONE) {
			/* Any size supported at this bit-depth */
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Any size supported at this bit-depth\n");
#endif			
			supported = 1;
			continue;
		} else if (current_video->handles_any_size) {
			/* Driver can center a smaller surface to simulate fullscreen */
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Driver can center a smaller surface to simulate fullscreen\n");
#endif			
			for ( i=0; sizes[i]; ++i ) {
				if ((sizes[i]->w >= width) && (sizes[i]->h >= height)) {
					supported = 1; /* this mode can fit the centered window. */
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: This mode can fit the centered window\n");
#endif					
					break;
				}
			}
		} else {
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Sizes are supported at this bit-depth (%dbit)\n", format.BitsPerPixel);
#endif
    		// NOVA_CODER
            for ( i=0; sizes[i]; ++i ) {             
                if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
                    if ((sizes[i]->w == width) && (sizes[i]->h == height)) {
                        supported = 1;
    				    break;
    			     }
    		    } else {
                    // For window modes, any new window must be able to fit inside the screen.
                    if ((sizes[i]->w >= width) && (sizes[i]->h >= height)) {                
    				    supported = 1;
    				    break;
    			     }
                }
            }
        }
	}
	if ( supported ) {
#ifdef DEBUG_VIDEO
        SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Mode supported!\n");
#endif
		--b;
		return(SDL_closest_depths[table][b]);
	} else {
#ifdef DEBUG_VIDEO
        SDL_LogDebugMsgchar("[SDL_VideoModeOK]: Mode not supported!\n");
#endif
            
		return(0);
	}
}
 
/*
 * Get the closest non-emulated video mode to the one requested
 */
static int SDL_GetVideoMode (int *w, int *h, int *BitsPerPixel, Uint32 flags)
{
	int table, b, i;
	int supported;
	int native_bpp;
	SDL_PixelFormat format;
	SDL_Rect **sizes;
	
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_GetVideoMode]: width=%d, height=%d, depth=%d\n", *w, *h, *BitsPerPixel);
#endif

#ifdef DEBUG_VIDEO
	if ((flags & SDL_SWSURFACE) != SDL_SWSURFACE) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_SWSURFACE NOT requested\n");
    } else {
         SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_SWSURFACE requested\n");
    }

	if ((flags & SDL_HWSURFACE) != SDL_HWSURFACE) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_HWSURFACE NOT requested\n");
    } else {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_HWSURFACE requested\n");
    }

    if ((flags & SDL_HWPALETTE) != SDL_HWPALETTE) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_HWPALETTE NOT requested\n");
    } else {
         SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_HWPALETTE requested\n");
    }

    if ((flags & SDL_DOUBLEBUF) != SDL_DOUBLEBUF) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_DOUBLEBUF NOT requested\n");
    } else {
         SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_DOUBLEBUF requested\n");
    }

    if ((flags & SDL_FULLSCREEN) != SDL_FULLSCREEN) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_FULLSCREEN NOT requested\n");
    } else {
         SDL_LogDebugMsgchar("[SDL_GetVideoMode]: SDL_FULLSCREEN requested\n");
    }
#endif

	/* Check parameters */
	if ( *BitsPerPixel < 8 || *BitsPerPixel > 32 ) {
		SDL_SetError("Invalid bits per pixel (range is {8...32})");
		return(0);
	}
	if ((*w <= 0) || (*h <= 0)) {
		SDL_SetError("Invalid width or height");
		return(0);
	}

	/* Try the original video mode, get the closest depth */
	native_bpp = SDL_VideoModeOK(*w, *h, *BitsPerPixel, flags);
	if ( native_bpp == *BitsPerPixel ) {
#ifdef DEBUG_VIDEO
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: Supported mode found with exact size and depth match\n");
#endif        
		return(1);
	}
	
	if ( native_bpp > 0 ) {
		*BitsPerPixel = native_bpp;
#ifdef DEBUG_VIDEO
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: Supported mode found with exact size match only\n");
#endif		
		return(1);
	}
	
#ifdef DEBUG_VIDEO
    SDL_LogDebugMsgchar("[SDL_GetVideoMode]: No exact size match at any depth, look for closest match..\n");
#endif

	/* No exact size match at any depth, look for closest match */
	SDL_memset(&format, 0, sizeof(format));
	supported = 0;
	table = ((*BitsPerPixel+7)/8)-1;
	SDL_closest_depths[table][0] = *BitsPerPixel;
	SDL_closest_depths[table][7] = SDL_VideoSurface->format->BitsPerPixel;
	for ( b = 0; !supported && SDL_closest_depths[table][b]; ++b ) {
		int best;

		format.BitsPerPixel = SDL_closest_depths[table][b];
		sizes = SDL_ListModes(&format, flags);
		if ( sizes == (SDL_Rect **)0 ) {
			/* No sizes supported at this bit-depth */
			continue;
		}
		
		best=0;
		for ( i=0; sizes[i]; ++i ) {
            if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
    			/* Mode with height bigger or equal than asked ? */
    			if ((sizes[i]->w == *w) && (sizes[i]->h >= *h)) {
    				/* Mode with height smaller or equal than current best ? */
    				if ((sizes[i]->w == sizes[best]->w) || (sizes[i]->h <= sizes[best]->h)) {
    					/* Now choose the mode that has less pixels */
    					if ((sizes[i]->w * sizes[i]->h) <= (sizes[best]->w * sizes[best]->h)) {
    						best=i;
    						supported = 1;
    					}
    				}
    			}
            } else {
                /* Mode with both dimensions bigger or equal than asked ? */
                if ((sizes[i]->w >= *w) && (sizes[i]->h >= *h)) {
                	/* Mode with any dimension smaller or equal than current best ? */
                	if ((sizes[i]->w <= sizes[best]->w) || (sizes[i]->h <= sizes[best]->h)) {
                		/* Now choose the mode that has less pixels */
                		if ((sizes[i]->w * sizes[i]->h) <= (sizes[best]->w * sizes[best]->h)) {
                			best=i;
                			supported = 1;
                		}
                	}
                } 
            }               
		}
		
		if (supported) {
			*w=sizes[best]->w;
			*h=sizes[best]->h;
			*BitsPerPixel = SDL_closest_depths[table][b];
		}
	}
	
	if (!supported) {
        if ((flags & SDL_FULLSCREEN) == SDL_FULLSCREEN) {
        	SDL_SetError("No video mode supports the following attributes: width=%d, height=%d, depth=%d",
        					*w, *h, *BitsPerPixel);
        } else {
        	SDL_SetError("Unsupported window attributes: width=%d, height=%d, depth=%d", *w, *h, *BitsPerPixel);
        }
	}
	
#ifdef DEBUG_VIDEO
	if (supported) {
        SDL_LogDebugMsgchar("[SDL_GetVideoMode]: Closest match found - width=%d, height=%d, depth=%d\n", *w, *h, *BitsPerPixel);
    }        
#endif
	
	return(supported);
}


#ifdef __QNXNTO__
    #include <sys/neutrino.h>
#endif /* __QNXNTO__ */

#ifdef WIN32
	extern int sysevents_mouse_pressed;
#endif
 
/*
 * Set the requested video mode, allocating a shadow buffer if necessary.
 */
SDL_Surface * SDL_SetVideoMode (int width, int height, int bpp, Uint32 flags)
{
	SDL_VideoDevice *video;
	SDL_Surface *mode;
	int video_w;
	int video_h;
	int video_bpp;
	int is_opengl;
	SDL_GrabMode saved_grab;
	
#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[SDL_SetVideoMode]: width=%d, height=%d, bpp=%d\n", width, height, bpp);
#endif

#ifdef WIN32
	sysevents_mouse_pressed = 0;
#endif

	/* Start up the video driver, if necessary..
	   WARNING: This is the only function protected this way!
	 */
	if ( ! current_video ) {
		if ( SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0 ) {
			return(NULL);
		}
	}
	video = current_video;

	/* Default to the current width and height */
	if ( width == 0 ) {
		width = video->info.current_w;
	}
	
	if ( height == 0 ) {
		height = video->info.current_h;
	}
	
	/* Default to the current video bpp */
	if ( bpp == 0 ) {
		flags |= SDL_ANYFORMAT;
		bpp = SDL_VideoSurface->format->BitsPerPixel;
	}

	/* Get a good video mode, the closest one possible */
	video_w = width;
	video_h = height;
	video_bpp = bpp;
	if ( ! SDL_GetVideoMode(&video_w, &video_h, &video_bpp, flags) ) {
        // NOTE - Error already reported by SDL_GetVideoMode().
		return(NULL);
	}

	/* Check the requested flags */
	/* There's no palette in > 8 bits-per-pixel mode */
	if ( video_bpp > 8 ) {
		flags &= ~SDL_HWPALETTE;
	}
	
#if 0
	if ( (flags&SDL_FULLSCREEN) != SDL_FULLSCREEN ) {
		/* There's no windowed double-buffering */
		flags &= ~SDL_DOUBLEBUF;
	}
#endif

	if ( (flags&SDL_DOUBLEBUF) == SDL_DOUBLEBUF ) {
		/* Use hardware surfaces when double-buffering */
		flags |= SDL_HWSURFACE;
	}

	is_opengl = ( ( flags & SDL_OPENGL ) == SDL_OPENGL );
	if ( is_opengl ) {
		/* These flags are for 2D video modes only */
		flags &= ~(SDL_HWSURFACE|SDL_DOUBLEBUF);
	}

	/* Reset the keyboard here so event callbacks can run */
	SDL_ResetKeyboard();
	SDL_ResetMouse();
	SDL_SetMouseRange(width, height);
	SDL_cursorstate &= ~CURSOR_USINGSW;

	/* Clean up any previous video mode */
	if ( SDL_PublicSurface != NULL ) {
		SDL_PublicSurface = NULL;
	}
	
	if ( video->physpal ) {
		SDL_free(video->physpal->colors);
		SDL_free(video->physpal);
		video->physpal = NULL;
	}
	
	if( video->gammacols) {
		SDL_free(video->gammacols);
		video->gammacols = NULL;
	}

	/* Save the previous grab state and turn off grab for mode switch */
	saved_grab = SDL_WM_GrabInputOff();

	/* Try to set the video mode, along with offset and clipping */
	mode = SDL_VideoSurface;
	SDL_LockCursor();
	SDL_VideoSurface = NULL;	/* In case it's freed by driver */
	mode = video->SetVideoMode(mode,video_w,video_h,video_bpp,flags);
	if ( mode ) { /* Prevent resize events from mode change */
          /* But not on OS/2 */
#ifndef __OS2__
	    SDL_PrivateResize(mode->w, mode->h);
#endif

	    /* Sam - If we asked for OpenGL mode, and didn't get it, fail */
	    if ( is_opengl && !(mode->flags & SDL_OPENGL) ) {
    		mode = NULL;
    		SDL_SetError("OpenGL not available");
	    }
	}

    // Note - We cannot swap back to the previous mode if it was destroyed by the call to SetVideoMode()....Doh!
	SDL_VideoSurface = mode;

	if ( (mode != NULL) && (!is_opengl) ) {
		/* Sanity check */
		if ( (mode->w < width) || (mode->h < height) ) {
			SDL_SetError("Video mode smaller than requested");
			return(NULL);
		}

		/* If we have a palettized surface, create a default palette */
		if ( mode->format->palette ) {
			SDL_PixelFormat *vf = mode->format;
			SDL_DitherColors(vf->palette->colors, vf->BitsPerPixel);
            video->SetColors(0, vf->palette->ncolors, vf->palette->colors);
		}

		/* Clear the surface to black */
		video->offset_x = 0;
		video->offset_y = 0;
		mode->offset = 0;
		SDL_SetClipRect(mode, NULL);
		SDL_ClearSurface(mode);

		/* Now adjust the offsets to match the desired mode */
		video->offset_x = (mode->w-width)/2;
		video->offset_y = (mode->h-height)/2;

		mode->offset = video->offset_y*mode->pitch + video->offset_x*mode->format->BytesPerPixel;

#ifdef DEBUG_VIDEO
        SDL_LogDebugMsgchar("Requested mode: %dx%dx%d, obtained mode %dx%dx%d (offset %d)\n",
        width, height, bpp, mode->w, mode->h, mode->format->BitsPerPixel, mode->offset);
#endif

		mode->w = width;
		mode->h = height;
		SDL_SetClipRect(mode, NULL);
	}
	
	SDL_ResetCursor();
	SDL_UnlockCursor();

	/* If we failed setting a video mode, return NULL... (Uh Oh!) */
	if ( mode == NULL ) {
		return(NULL);
	}

	/* If there is no window manager, set the SDL_NOFRAME flag */
	if ( ! video->info.wm_available ) {
		mode->flags |= SDL_NOFRAME;
	}

	/* Reset the mouse cursor and grab for new video mode */
	SDL_SetCursor(NULL);
#ifndef __AMIGA__
	if ( video->UpdateMouse ) {
		video->UpdateMouse();
	}
#endif

	SDL_WM_GrabInput(saved_grab);
	
#ifndef __AMIGA__
	SDL_GetRelativeMouseState(NULL, NULL); /* Clear first large delta */
#endif
	
#ifdef SDL_VIDEO_OPENGL
	/* Load GL symbols (before MakeCurrent, where we need glGetString). */
	if (flags & (SDL_OPENGL)) {
		/* If we're running OpenGL, make the context current */
		if ((video->screen->flags & SDL_OPENGL) && video->GL_MakeCurrent) {
			if (video->GL_MakeCurrent() < 0) {
				return(NULL);
			}
		}

		// Hack!
		video->is_32bit = 1; // Currently we only support 32-bit.
	}
#endif

	SDL_PublicSurface = SDL_VideoSurface;
	
	// Update info to match the new video mode (surface).   
	video->info.vfmt = SDL_VideoSurface->format;
	video->info.current_w = SDL_VideoSurface->w;
	video->info.current_h = SDL_VideoSurface->h;
	
	/* We're done! */
	return(SDL_PublicSurface);
}

/* 
 * Convert a surface into the video pixel format.
 */
SDL_Surface * SDL_DisplayFormat (SDL_Surface *surface)
{
	Uint32 flags;

	if ( ! SDL_PublicSurface ) {
		SDL_SetError("No video mode has been set");
		return(NULL);
	}
	
	/* Set the flags appropriate for copying to display surface */
	if (((SDL_PublicSurface->flags&SDL_HWSURFACE) == SDL_HWSURFACE) && current_video->info.blit_hw)
		flags = SDL_HWSURFACE;
	else 
		flags = SDL_SWSURFACE;
#ifdef AUTORLE_DISPLAYFORMAT
	flags |= (surface->flags & (SDL_SRCCOLORKEY|SDL_SRCALPHA));
	flags |= SDL_RLEACCELOK;
#else
	flags |= surface->flags & (SDL_SRCCOLORKEY|SDL_SRCALPHA|SDL_RLEACCELOK);
#endif
	return(SDL_ConvertSurface(surface, SDL_PublicSurface->format, flags));
}

/*
 * Convert a surface into a format that's suitable for blitting to
 * the screen, but including an alpha channel.
 */
SDL_Surface *SDL_DisplayFormatAlpha(SDL_Surface *surface)
{
	SDL_PixelFormat *vf;
	SDL_PixelFormat *format;
	SDL_Surface *converted;
	Uint32 flags;
	
	/* default to ARGB8888 */
	Uint32 amask = 0xff000000;
	Uint32 rmask = 0x00ff0000;
	Uint32 gmask = 0x0000ff00;
	Uint32 bmask = 0x000000ff;

	if ( ! SDL_PublicSurface ) {
		SDL_SetError("No video mode has been set");
		return(NULL);
	}
	vf = SDL_PublicSurface->format;

	switch(vf->BytesPerPixel) {
	    case 2:
		/* For XGY5[56]5, use, AXGY8888, where {X, Y} = {R, B}.
		   For anything else (like ARGB4444) it doesn't matter
		   since we have no special code for it anyway */
		if ( (vf->Rmask == 0x1f) &&
		     (vf->Bmask == 0xf800 || vf->Bmask == 0x7c00)) {
			rmask = 0xff;
			bmask = 0xff0000;
		}
		break;

	    case 3:
	    case 4:
		/* Keep the video format, as long as the high 8 bits are
		   unused or alpha */
		if ( (vf->Rmask == 0xff) && (vf->Bmask == 0xff0000) ) {
			rmask = 0xff;
			bmask = 0xff0000;
		} else if ( vf->Rmask == 0xFF00 && (vf->Bmask == 0xFF000000) ) {
			amask = 0x000000FF;
			rmask = 0x0000FF00;
			gmask = 0x00FF0000;
			bmask = 0xFF000000;
		}
		break;

	    default:
		/* We have no other optimised formats right now. When/if a new
		   optimised alpha format is written, add the converter here */
		break;
	}
	format = SDL_AllocFormat(32, rmask, gmask, bmask, amask);
	flags = SDL_PublicSurface->flags & SDL_HWSURFACE;
	flags |= surface->flags & (SDL_SRCALPHA | SDL_RLEACCELOK);
	converted = SDL_ConvertSurface(surface, format, flags);
	SDL_FreeFormat(format);
	return(converted);
}

/*
 * Update a specific portion of the physical screen
 */
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h)
{
	if ( screen ) {
		SDL_Rect rect;

		/* Perform some checking */
		if ( w == 0 )
			w = screen->w;
			
		if ( h == 0 )
			h = screen->h;
			
		if ( (int)(x+w) > screen->w )
			return;
			
		if ( (int)(y+h) > screen->h )
			return;
 

		/* Fill the rectangle */
		rect.x = (Sint16)x;
		rect.y = (Sint16)y;
		rect.w = (Uint16)w;
		rect.h = (Uint16)h;
		
		SDL_UpdateRects(screen, 1, &rect);
	}
}

void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	int i;

	if ( screen == SDL_VideoSurface ) {
		/* Update the video surface */
		if ( screen->offset ) {
			for ( i=0; i<numrects; ++i ) {
				rects[i].x += current_video->offset_x;
				rects[i].y += current_video->offset_y;
			}
			current_video->UpdateRects(numrects, rects);
			for ( i=0; i<numrects; ++i ) {
				rects[i].x -= current_video->offset_x;
				rects[i].y -= current_video->offset_y;
			}
		} else {
            current_video->UpdateRects(numrects, rects);
		}
	}
}

// NOVA_CODER - Hacked to death.
int SDL_Flip(SDL_Surface *screen)
{
	SDL_UpdateRect(screen, 0, 0, 0, 0);
            
	return(0);
}

// NOVA_CODER
void SDL_ClearSurface(SDL_Surface *surface)
{
	Uint32 black;

#ifdef DEBUG_VIDEO
	SDL_LogDebugMsgchar("[SDL_ClearSurface]: Called\n");
#endif

	black = SDL_MapRGB(surface->format, 0, 0, 0);
	SDL_FillRect(surface, NULL, black);
}

static void SetPalette_logical(SDL_Surface *screen, SDL_Color *colors,
			       int firstcolor, int ncolors)
{
	SDL_Palette *pal = screen->format->palette;

	if ( colors != (pal->colors + firstcolor) ) {
		SDL_memcpy(pal->colors + firstcolor, colors,
		       ncolors * sizeof(*colors));
	}

	SDL_FormatChanged(screen);
}

static int SetPalette_physical(SDL_Surface *screen,
                               SDL_Color *colors, int firstcolor, int ncolors)
{
	int gotall = 1;

	if ( current_video->physpal ) {
		/* We need to copy the new colors, since we haven't
		 * already done the copy in the logical set above.
		 */
		SDL_memcpy(current_video->physpal->colors + firstcolor,
		       colors, ncolors * sizeof(*colors));
	}
	

	if ( screen == SDL_VideoSurface ) {
		SDL_Color gcolors[256];

		if ( current_video->gamma ) {
			SDL_ApplyGamma(current_video->gamma, colors, gcolors, ncolors);
			colors = gcolors;
		}
		gotall = current_video->SetColors(firstcolor, ncolors, colors);
		if ( ! gotall ) {
			/* The video flags shouldn't have SDL_HWPALETTE, and
			   the video driver is responsible for copying back the
			   correct colors into the video surface palette.
			*/
			;
		}
		SDL_CursorPaletteChanged();
	}
	return gotall;
}

/*
 * Set the physical and/or logical colormap of a surface:
 * Only the screen has a physical colormap. It determines what is actually
 * sent to the display.
 * The logical colormap is used to map blits to/from the surface.
 * 'which' is one or both of SDL_LOGPAL, SDL_PHYSPAL
 *
 * Return nonzero if all colours were set as requested, or 0 otherwise.
 */
int SDL_SetPalette(SDL_Surface *screen, int which,
		   SDL_Color *colors, int firstcolor, int ncolors)
{
	SDL_Palette *pal;
	int gotall;
	int palsize;

	if ( !screen ) {
		return 0;
	}
	if ( !current_video || screen != SDL_PublicSurface ) {
		/* only screens have physical palettes */
		which &= ~SDL_PHYSPAL;
	} else if ( (screen->flags & SDL_HWPALETTE) != SDL_HWPALETTE ) {
		/* hardware palettes required for split colormaps */
		which |= SDL_PHYSPAL | SDL_LOGPAL;
	}

	/* Verify the parameters */
	pal = screen->format->palette;
	if( !pal ) {
		return 0;	/* not a palettized surface */
	}
	gotall = 1;
	palsize = 1 << screen->format->BitsPerPixel;
	if ( ncolors > (palsize - firstcolor) ) {
		ncolors = (palsize - firstcolor);
		gotall = 0;
	}

	if ( which & SDL_LOGPAL ) {
		/*
		 * Logical palette change: The actual screen isn't affected,
		 * but the internal colormap is altered so that the
		 * interpretation of the pixel values (for blits etc) is
		 * changed.
		 */
		SetPalette_logical(screen, colors, firstcolor, ncolors);
	}
	if ( which & SDL_PHYSPAL ) {
		SDL_VideoDevice *video = current_video;
		/*
		 * Physical palette change: This doesn't affect the
		 * program's idea of what the screen looks like, but changes
		 * its actual appearance.
		 */
		if ( !video->physpal && !(which & SDL_LOGPAL) ) {
			/* Lazy physical palette allocation */
			int size;
			SDL_Palette *pp = SDL_malloc(sizeof(*pp));
			if ( !pp ) {
				return 0;
			}
			video->physpal = pp;
			pp->ncolors = pal->ncolors;
			size = pp->ncolors * sizeof(SDL_Color);
			pp->colors = SDL_malloc(size);
			if ( !pp->colors ) {
				return 0;
			}
			SDL_memcpy(pp->colors, pal->colors, size);
		}
		if ( ! SetPalette_physical(screen,
		                           colors, firstcolor, ncolors) ) {
			gotall = 0;
		}
	}
	return gotall;
}

int SDL_SetColors(SDL_Surface *screen, SDL_Color *colors, int firstcolor,
		  int ncolors)
{
	return SDL_SetPalette(screen, SDL_LOGPAL | SDL_PHYSPAL,
			      colors, firstcolor, ncolors);
}

/*
 * Clean up the video subsystem
 */
void SDL_VideoQuit (void)
{
	SDL_Surface *ready_to_go;

	if ( current_video ) {
		SDL_VideoDevice *video = current_video;
		//SDL_VideoDevice *this  = current_video;

		/* Halt event processing before doing anything else */
		SDL_StopEventLoop();

		/* Clean up allocated window manager items */
		if ( SDL_PublicSurface ) {
			SDL_PublicSurface = NULL;
		}
		
		SDL_CursorQuit();

		/* Just in case... */
		SDL_WM_GrabInputOff();

		/* Clean up the system video */
		video->VideoQuit();

		/* Free any lingering surfaces */
		if ( SDL_VideoSurface != NULL ) {
			ready_to_go = SDL_VideoSurface;
			SDL_VideoSurface = NULL;
			SDL_FreeSurface(ready_to_go);
		}

		/* Clean up miscellaneous memory */
		if ( video->physpal ) {
			SDL_free(video->physpal->colors);
			SDL_free(video->physpal);
			video->physpal = NULL;
		}
		
		if ( video->gammacols ) {
			SDL_free(video->gammacols);
			video->gammacols = NULL;
		}
		
		if ( video->gamma ) {
			SDL_free(video->gamma);
			video->gamma = NULL;
		}
		
		if ( video->wm_title != NULL ) {
			SDL_free(video->wm_title);
			video->wm_title = NULL;
		}
		
		if ( video->wm_icon != NULL ) {
			SDL_free(video->wm_icon);
			video->wm_icon = NULL;
		}

		/* Finish cleaning up video subsystem */
		video->free();
        current_video = NULL;
	}
}

int SDL_GL_LoadLibrary(const char *path)
{
	int retval;

	retval = -1;
	if (current_video == NULL ) {
		SDL_SetError("Video subsystem has not been initialized");
	} else {
		if (current_video->GL_LoadLibrary ) {
			retval = current_video->GL_LoadLibrary(path);
		} else {
			SDL_SetError("No dynamic GL support in video driver");
		}
	}
	return(retval);
}

void *SDL_GL_GetProcAddress(const char* proc)
{
	void *func;

	func = NULL;
	if ( current_video->GL_GetProcAddress ) {
		if ( current_video->gl_config.driver_loaded ) {
			func = current_video->GL_GetProcAddress(proc);
		} else {
			SDL_SetError("No GL driver has been loaded");
		}
	} else {
		SDL_SetError("No dynamic GL support in video driver");
	}
	return func;
}

int SDL_GL_SetAttribute( SDL_GLattr attr, int value )
{
	int retval;

	retval = 0;
	switch (attr) {
		case SDL_GL_RED_SIZE:
			current_video->gl_config.red_size = value;
			break;
		case SDL_GL_GREEN_SIZE:
			current_video->gl_config.green_size = value;
			break;
		case SDL_GL_BLUE_SIZE:
			current_video->gl_config.blue_size = value;
			break;
		case SDL_GL_ALPHA_SIZE:
			current_video->gl_config.alpha_size = value;
			break;
		case SDL_GL_DOUBLEBUFFER:
			current_video->gl_config.double_buffer = value;
			break;
		case SDL_GL_BUFFER_SIZE:
			current_video->gl_config.buffer_size = value;
			break;
		case SDL_GL_DEPTH_SIZE:
			current_video->gl_config.depth_size = value;
			break;
		case SDL_GL_STENCIL_SIZE:
			current_video->gl_config.stencil_size = value;
			break;
		case SDL_GL_ACCUM_RED_SIZE:
			current_video->gl_config.accum_red_size = value;
			break;
		case SDL_GL_ACCUM_GREEN_SIZE:
			current_video->gl_config.accum_green_size = value;
			break;
		case SDL_GL_ACCUM_BLUE_SIZE:
			current_video->gl_config.accum_blue_size = value;
			break;
		case SDL_GL_ACCUM_ALPHA_SIZE:
			current_video->gl_config.accum_alpha_size = value;
			break;
		case SDL_GL_STEREO:
			current_video->gl_config.stereo = value;
			break;
		case SDL_GL_MULTISAMPLEBUFFERS:
			current_video->gl_config.multisamplebuffers = value;
			break;
		case SDL_GL_MULTISAMPLESAMPLES:
			current_video->gl_config.multisamplesamples = value;
			break;
		case SDL_GL_ACCELERATED_VISUAL:
			current_video->gl_config.accelerated = value;
			break;
		case SDL_GL_SWAP_CONTROL:
			current_video->gl_config.swap_control = value;
			break;
		default:
			SDL_SetError("Unknown OpenGL attribute");
			retval = -1;
			break;
	}

	return(retval);
}

int SDL_GL_GetAttribute(SDL_GLattr attr, int* value)
{
	int retval = -1;

	if ( current_video->GL_GetAttribute ) {
		retval = current_video->GL_GetAttribute(attr, value);
	} else {
		*value = 0;
		SDL_SetError("GL_GetAttribute not supported");
	}
	return retval;
}

void SDL_GL_SwapBuffers(void)
{
	if ( current_video->screen->flags & SDL_OPENGL ) {
		current_video->GL_SwapBuffers();
	} else {
		SDL_SetError("OpenGL video mode has not been set");
	}
}

/* Update rects with locking */
void SDL_GL_UpdateRectsLock(SDL_VideoDevice* this, int numrects, SDL_Rect *rects)
{
	SDL_GL_Lock();
 	SDL_GL_UpdateRects(numrects, rects);
	SDL_GL_Unlock();
}

/* Update rects without state setting and changing (the caller is responsible for it) */
void SDL_GL_UpdateRects(int numrects, SDL_Rect *rects)
{
#ifdef SDL_VIDEO_OPENGL
	SDL_VideoDevice *this = current_video;
	SDL_Rect update, tmp;
	int x, y, i;

	for ( i = 0; i < numrects; i++ )
	{
		tmp.y = rects[i].y;
		tmp.h = rects[i].h;
		for ( y = 0; y <= rects[i].h / 256; y++ )
		{
			tmp.x = rects[i].x;
			tmp.w = rects[i].w;
			for ( x = 0; x <= rects[i].w / 256; x++ )
			{
				update.x = tmp.x;
				update.y = tmp.y;
				update.w = tmp.w;
				update.h = tmp.h;

				if ( update.w > 256 )
					update.w = 256;

				if ( update.h > 256 )
					update.h = 256;

				glFlush();
				glTexSubImage2D(
					GL_TEXTURE_2D,
					0,
					0,
					0,
					update.w,
					update.h,
					this->is_32bit? GL_RGBA : GL_RGB,
#ifdef GL_VERSION_1_2
					this->is_32bit ? GL_UNSIGNED_BYTE : GL_UNSIGNED_SHORT_5_6_5,
#else
					GL_UNSIGNED_BYTE,
#endif
					(Uint8 *)this->screen->pixels +
						this->screen->format->BytesPerPixel * update.x +
						update.y * this->screen->pitch );

				glFlush();
				/*
				* Note the parens around the function name:
				* This is because some OpenGL implementations define glTexCoord etc
				* as macros, and we don't want them expanded here.
				*/
				glBegin(GL_TRIANGLE_STRIP);
				glTexCoord2f( 0.0, 0.0 );
				glVertex2i( update.x, update.y );
				glTexCoord2f( (float)(update.w / 256.0), 0.0 );
				glVertex2i( update.x + update.w, update.y );
				glTexCoord2f( 0.0, (float)(update.h / 256.0) );
				glVertex2i( update.x, update.y + update.h );
				glTexCoord2f( (float)(update.w / 256.0), (float)(update.h / 256.0) );
				glVertex2i( update.x + update.w	, update.y + update.h );
				glEnd();

				tmp.x += 256;
				tmp.w -= 256;
			}
			tmp.y += 256;
			tmp.h -= 256;
		}
	}
#endif
}

/* Lock == save current state */
void SDL_GL_Lock()
{
#ifdef SDL_VIDEO_OPENGL
	lock_count--;
	if (lock_count==-1)
	{
		glPushAttrib( GL_ALL_ATTRIB_BITS );	/* TODO: narrow range of what is saved */

#ifdef GL_CLIENT_PIXEL_STORE_BIT
		glPushClientAttrib( GL_CLIENT_PIXEL_STORE_BIT );
#endif

		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glDisable(GL_FOG);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_CULL_FACE);

		glBindTexture( GL_TEXTURE_2D, current_video->texture );
		glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );

		glPixelStorei( GL_UNPACK_ROW_LENGTH, current_video->screen->pitch / current_video->screen->format->BytesPerPixel );
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1.0, 1.0, 1.0, 1.0);		/* Solaris workaround */

		glViewport(0, 0, current_video->screen->w, current_video->screen->h);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();

		glOrtho(0.0, (GLdouble) current_video->screen->w, (GLdouble) current_video->screen->h, 0.0, 0.0, 1.0);

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
	}
#endif
}

/* Unlock == restore saved state */
void SDL_GL_Unlock()
{
#ifdef SDL_VIDEO_OPENGL
	lock_count++;
	if (lock_count==0)
	{
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();

		glPopClientAttrib();
		glPopAttrib();
	}
#endif
}


extern void SDL_Audio_SetCaption(const char *caption);

/*
 * Sets/Gets the title and icon text of the display window, if any.
 */
void SDL_WM_SetCaption (const char *title, const char *icon)
{
	if ( current_video ) {
		if ( title ) {
			if ( current_video->wm_title ) {
				SDL_free(current_video->wm_title);
			}
			current_video->wm_title = SDL_strdup(title);
		}
		if ( icon ) {
			if ( current_video->wm_icon ) {
				SDL_free(current_video->wm_icon);
			}
			current_video->wm_icon = SDL_strdup(icon);
		}
		if ( (title || icon) && (current_video->SetCaption != NULL) ) {
			current_video->SetCaption(current_video->wm_title,current_video->wm_icon);
		}
	}

	/* PulseAudio can make use of this information. */
	SDL_Audio_SetCaption(title);
}

void SDL_WM_GetCaption (char **title, char **icon)
{
	if ( current_video ) {
		if ( title ) {
			*title = current_video->wm_title;
		}
		if ( icon ) {
			*icon = current_video->wm_icon;
		}
	}
}

/* Utility function used by SDL_WM_SetIcon();
 * flags & 1 for color key, flags & 2 for alpha channel. */
static void CreateMaskFromColorKeyOrAlpha(SDL_Surface *icon, Uint8 *mask, int flags)
{
	int x, y;
	Uint32 colorkey;
#define SET_MASKBIT(icon, x, y, mask) \
	mask[(y*((icon->w+7)/8))+(x/8)] &= ~(0x01<<(7-(x%8)))

	colorkey = icon->format->colorkey;
	switch (icon->format->BytesPerPixel) {
		case 1: { Uint8 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint8 *)icon->pixels + y*icon->pitch;
				for ( x=0; x<icon->w; ++x ) {
					if ( *pixels++ == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					}
				}
			}
		}
		break;

		case 2: { Uint16 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint16 *)icon->pixels +
				                   y*icon->pitch/2;
				for ( x=0; x<icon->w; ++x ) {
					if ( (flags & 1) && *pixels == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					} else if((flags & 2) && (*pixels & icon->format->Amask) == 0) {
						SET_MASKBIT(icon, x, y, mask);
					}
					pixels++;
				}
			}
		}
		break;

		case 4: { Uint32 *pixels;
			for ( y=0; y<icon->h; ++y ) {
				pixels = (Uint32 *)icon->pixels +
				                   y*icon->pitch/4;
				for ( x=0; x<icon->w; ++x ) {
					if ( (flags & 1) && *pixels == colorkey ) {
						SET_MASKBIT(icon, x, y, mask);
					} else if((flags & 2) && (*pixels & icon->format->Amask) == 0) {
						SET_MASKBIT(icon, x, y, mask);
					}
					pixels++;
				}
			}
		}
		break;
	}
}

/*
 * Sets the window manager icon for the display window.
 */
void SDL_WM_SetIcon (SDL_Surface *icon, Uint8 *mask)
{
	if ( icon && current_video->SetIcon ) {
		/* Generate a mask if necessary, and create the icon! */
		if ( mask == NULL ) {
			int mask_len = icon->h*(icon->w+7)/8;
			int flags = 0;
			mask = (Uint8 *)SDL_malloc(mask_len);
			if ( mask == NULL ) {
				return;
			}
			SDL_memset(mask, ~0, mask_len);
			if ( icon->flags & SDL_SRCCOLORKEY ) flags |= 1;
			if ( icon->flags & SDL_SRCALPHA ) flags |= 2;
			if( flags ) {
				CreateMaskFromColorKeyOrAlpha(icon, mask, flags);
			}
			current_video->SetIcon(icon, mask);
			SDL_free(mask);
		} else {
			current_video->SetIcon(icon, mask);
		}
	}
}

/*
 * Grab or ungrab the keyboard and mouse input.
 */
static SDL_GrabMode SDL_WM_GrabInputRaw(SDL_GrabMode mode)
{
#ifdef DEBUG_GRAB
    SDL_LogDebugMsgchar("[SDL_WM_GrabInputRaw] - Grab mode %d requested\n", mode);
#endif

	/* If the final grab mode if off, only then do we actually grab */
	if ( mode == SDL_GRAB_OFF ) {
		if ( current_video->input_grab != SDL_GRAB_OFF ) {
			mode = current_video->GrabInput(mode);
		}
	} else {
		if ( current_video->input_grab == SDL_GRAB_OFF ) {
			mode = current_video->GrabInput(mode);
		}
	}	
	
	if ( mode != current_video->input_grab ) {
		current_video->input_grab = mode;
		if ( current_video->CheckMouseMode ) {
			current_video->CheckMouseMode();
		}
	}

	/* Return the final grab state */
	if ( mode >= SDL_GRAB_FULLSCREEN ) {
		mode -= SDL_GRAB_FULLSCREEN;
	}
	
	return mode;
}

SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode)
{
#ifdef DEBUG_GRAB
    SDL_LogDebugMsgchar("[SDL_WM_GrabInput] - Grab mode %d requested\n", mode);
#endif
    
	/* If the video isn't initialized yet, we can't do anything */
	if (!current_video) {
		return SDL_GRAB_OFF;
	}
	
	/* Only do something if we have support for grabs */
	if (current_video->GrabInput == NULL) {
        current_video->input_grab = SDL_GRAB_OFF;
        return current_video->input_grab;
	}

	/* Return the current mode on query */
	if ( mode == SDL_GRAB_QUERY ) {
		mode = current_video->input_grab;
        if ( mode >= SDL_GRAB_FULLSCREEN ) {
			mode -= SDL_GRAB_FULLSCREEN;
		}

		return(mode);
	}
	
	if ( mode >= SDL_GRAB_FULLSCREEN ) {
		mode -= SDL_GRAB_FULLSCREEN;
	}
	
	/* If the video surface is fullscreen, we always grab */
    if (SDL_VideoSurface && (SDL_VideoSurface->flags & SDL_FULLSCREEN)) {
#ifdef DEBUG_GRAB
        SDL_LogDebugMsgchar("[SDL_WM_GrabInput] - Forcing grab mode on because we are in fullscreen mode\n");
#endif
        mode += SDL_GRAB_FULLSCREEN;
    }
	
	return(SDL_WM_GrabInputRaw(mode));
}

SDL_GrabMode SDL_WM_GrabInputOff(void)
{
	SDL_GrabMode mode;

#ifdef DEBUG_GRAB
    SDL_LogDebugMsgchar("[SDL_WM_GrabInputOff] - called\n");
#endif

	/* First query the current grab state */
	mode = SDL_WM_GrabInput(SDL_GRAB_QUERY);

	/* Now explicitly turn off input grab */
	SDL_WM_GrabInputRaw(SDL_GRAB_OFF);

	/* Return the old state */
	return(mode);
}

/*
 * Iconify the window in window managed environments.
 * A successful iconification will result in an SDL_APPACTIVE loss event.
 */
int SDL_WM_IconifyWindow(void)
{
	int retval;

	retval = 0;
	if ( current_video->IconifyWindow ) {
		retval = current_video->IconifyWindow();
	}
	
	return(retval);
}

/*
 * Toggle fullscreen mode
 */
int SDL_WM_ToggleFullScreen(SDL_Surface *surface)
{
	int toggled;

	toggled = 0;
	if ( SDL_PublicSurface && (surface == SDL_PublicSurface) &&
	     current_video->ToggleFullScreen ) {
		if ( surface->flags & SDL_FULLSCREEN ) {
			toggled = current_video->ToggleFullScreen(0);
			if ( toggled ) {
				SDL_VideoSurface->flags &= ~SDL_FULLSCREEN;
				SDL_PublicSurface->flags &= ~SDL_FULLSCREEN;
			}
		} else {
			toggled = current_video->ToggleFullScreen(1);
			if ( toggled ) {
				SDL_VideoSurface->flags |= SDL_FULLSCREEN;
				SDL_PublicSurface->flags |= SDL_FULLSCREEN;
			}
		}
		
		/* Double-check the grab state inside SDL_WM_GrabInput() */
		if ( toggled ) {
			SDL_WM_GrabInput(current_video->input_grab);
		}
	}
	
	return(toggled);
}

/*
 * Get some platform dependent window manager information
 */
int SDL_GetWMInfo (SDL_SysWMinfo *info)
{
	if ( current_video && current_video->GetWMInfo ) {
		return(current_video->GetWMInfo(info));
	} else {
		return(0);
	}
}
