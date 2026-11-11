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
   
#include <stdio.h>
#include <stdarg.h>




#ifndef NDEBUG
static FILE    *debugLogFile = NULL;
#endif
   
static FILE    *warningLogFile = NULL;

   
 
  
void SDL_LogInit(void) {
    // This Init function can be called multiple times...
    SDL_LogQuit();
    
#ifndef NDEBUG
    debugLogFile = fopen("SDL_debug.txt", "w");
    if (debugLogFile) {
        setbuf(debugLogFile, NULL);
    }
#endif
}

void SDL_LogQuit(void) {
#ifndef NDEBUG    
    if (debugLogFile) {
        fclose (debugLogFile);
        debugLogFile = NULL;
    }
#endif

    if (warningLogFile) {
        fclose (warningLogFile);
        warningLogFile = NULL;
    }
} 
   
#ifndef NDEBUG
void SDL_LogDebugMsgchar(char *message, ...) {
    va_list		argptr;
    char		text[1024];
       
    va_start (argptr, message);
    vsprintf (text, message, argptr);
    va_end (argptr);
    
    if (debugLogFile) {
        fwrite(text, 1, strlen(text), debugLogFile);
    } 
}
#endif 
         
void SDL_LogWarningMsgchar(char *message, ...) {
    va_list		argptr;
    char		text[1024];

    va_start (argptr, message);
    vsprintf (text, message, argptr);
    va_end (argptr);    
    
    if (!warningLogFile) {
        warningLogFile = fopen("SDL_warning.txt", "w");
        if (warningLogFile) {
            setbuf(warningLogFile, NULL);
        }
    }
    
    if (warningLogFile) {
        fwrite(text, 1, strlen(text), warningLogFile);
    }    
} 
  
void SDL_LogErrorMsgchar(char *message, ...) {
    va_list		argptr;
    char		text[1024];
    FILE        *errorLogFile = NULL;

    va_start (argptr, message);
    vsprintf (text, message, argptr);
    va_end (argptr);

    // Always append for errors.
    errorLogFile = fopen("SDL_error.txt", "a");
    if (errorLogFile) {
        setbuf(errorLogFile, NULL);
        fwrite(text, 1, strlen(text), errorLogFile);
        fclose (errorLogFile);
        errorLogFile = NULL;
    }
}
