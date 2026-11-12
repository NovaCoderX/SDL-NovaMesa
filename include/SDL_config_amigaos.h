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

#ifndef _SDL_config_amigaos_h
#define _SDL_config_amigaos_h

/* This is a set of defines to configure the SDL features */

#include "inttypes.h"

#ifndef PATH_MAX
#define PATH_MAX	4096
#endif

#define SDL_HAS_64BIT_TYPE	1
#define SDL_BYTEORDER	SDL_BIG_ENDIAN

/* Good default values for an Amiga */
#define MIX_DEFAULT_FREQUENCY	11025
#define MIX_DEFAULT_FORMAT	AUDIO_S16MSB
#define MIX_DEFAULT_CHANNELS	2

/* Useful headers */
#define HAVE_SYS_TYPES_H	1
#define HAVE_STDIO_H	1
#define STDC_HEADERS	1
#define HAVE_STRING_H	1
#define HAVE_CTYPE_H	1

/* C library functions */
#define HAVE_MALLOC	1
#define HAVE_CALLOC	1
#define HAVE_REALLOC	1
#define HAVE_FREE	1
#define HAVE_ALLOCA	1
//#define HAVE_GETENV 1
//#define HAVE_PUTENV	1
#define HAVE_QSORT	1
#define HAVE_ABS	1
#define HAVE_BCOPY	1
#define HAVE_MEMSET	1
#define HAVE_MEMCPY	1
#define HAVE_MEMMOVE	1
#define HAVE_MEMCMP	1
#define HAVE_STRLEN	1
#define HAVE_STRDUP	1
#define HAVE_INDEX	1
#define HAVE_RINDEX	1
#define HAVE_STRCHR	1
#define HAVE_STRRCHR	1
#define HAVE_STRSTR	1
#define HAVE_STRTOL	1
#define HAVE_STRTOD	1
#define HAVE_ATOI	1
#define HAVE_ATOF	1
#define HAVE_STRCMP	1
#define HAVE_STRNCMP	1
#define HAVE_STRCASECMP	1
#define HAVE_SSCANF	1
#define HAVE_SNPRINTF	1
#define HAVE_VSNPRINTF	1

#ifndef NDEBUG
	#define DEBUG_VIDEO 1
	#define DEBUG_PALETTE   1
	//#define DEBUG_UPDATE_RECTS   1
	//#define DEBUG_AUDIO 1
	//#define DEBUG_CONVERT   1
	//#define DEBUG_MIXER 1
	//#define DEBUG_TIMERS   1
	//#define DEBUG_THREADS   1
	//#define DEBUG_EVENTS   1
	//#define DEBUG_KEYBOARD_EVENTS 1
	//#define DEBUG_CURSOR   1
	//#define DEBUG_GRAB   1
	//#define DEBUG_JOYSTICK  1
	#define DEBUG_BUILD 1
	#define CHECK_LEAKS 1
#endif

#endif /* _SDL_config_amigaos_h */
