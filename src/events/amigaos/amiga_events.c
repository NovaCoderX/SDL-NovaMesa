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

/* Handle the event stream, converting Amiga events into SDL events */
#include "amiga_events_c.h"
#include "../../video/amigaos/amiga_video_c.h"
#include "../../video/SDL_cursor_c.h"
#include "amiga_keyboard_codes.h"

// Amiga includes.
#include <proto/exec.h>
#include <proto/keymap.h>
#include <proto/lowlevel.h>
#include <clib/alib_protos.h>
#include <devices/input.h>
#include <SDI_interrupt.h>

static struct Interrupt *inputHandler = NULL;
static struct MsgPort *inputPort = NULL;
static struct IOStdReq *inputRequest = NULL;
static char inputHandlerName[] = "SDL Input Handler";

// These are written by the input handler at interrupt time and read by the
// main task, so they must not be cached in a register by the optimiser.
static volatile int mouse_movement_x, mouse_movement_y = 0;
static volatile int inputCaptured = 0;

static SDL_bool relativeMouseMode = SDL_FALSE;
static SDLKey keycode_lookup[SDLK_LAST];

// Set when we leave relative mode, actioned by Amiga_PumpEvents in the main
// task. See Amiga_SetRelativeMouseMode() for why it is deferred.
static SDL_bool reseedMousePosition = SDL_FALSE;

#define MAXIMSGS 64
static struct InputEvent imsgs[MAXIMSGS];
static volatile int imsglow = 0;
static volatile int imsghigh = 0;

static SDLKey CalculateKeySym(int code)
{
    char charbuf;
    struct InputEvent FakedIEvent;
    SDLKey sym = SDLK_UNKNOWN;
    
#ifdef DEBUG_KEYBOARD_EVENTS
    SDL_LogDebugMsgchar("[CalculateKeySym]: Translating code=%d\n", code);
#endif
    
    SDL_memset(&FakedIEvent, 0, sizeof(struct InputEvent));
    FakedIEvent.ie_Class = IECLASS_RAWKEY;
    FakedIEvent.ie_Code = code;

    if (MapRawKey(&FakedIEvent, &charbuf, 1, 0) == 1) {
        sym = charbuf;
    }    
    
    return sym;
}

static Uint16 CalculateUnicode(int code, int qual)
{
    char charbuf;
    struct InputEvent FakedIEvent;
    Uint16 unicode = 0;
    
#ifdef DEBUG_KEYBOARD_EVENTS
    SDL_LogDebugMsgchar("[CalculateUnicode]: Translating code=%d with qualifier=%n\n", code, qual);
#endif

    SDL_memset(&FakedIEvent, 0, sizeof(struct InputEvent));
    FakedIEvent.ie_Class = IECLASS_RAWKEY;
    FakedIEvent.ie_Code = code;
    FakedIEvent.ie_Qualifier = qual;

    if (MapRawKey(&FakedIEvent, &charbuf, 1, 0) == 1) {
        unicode = charbuf;
    }

    return unicode;
}

static SDL_keysym *TranslateKey(int code, int qual, SDL_keysym *keysym, int pressed)
{
#ifdef DEBUG_KEYBOARD_EVENTS
    SDL_LogDebugMsgchar("[TranslateKey]: Translating code=%d\n", code);
#endif

	/* Set the raw keyboard scancode */
	keysym->scancode = code;
	keysym->sym = SDLK_UNKNOWN;
	keysym->mod = KMOD_NONE;
	keysym->unicode = 0;
	
	/* Sanity check */
	if (code >= SDLK_LAST) {
		SDL_LogWarningMsgchar("Could not translate code, outside array bounds\n");
		return keysym;
    }
	
    // Calculate the modifier (if any).
	if (qual & IEQUALIFIER_LSHIFT) {
		keysym->mod |= KMOD_LSHIFT;
    }

	if (qual & IEQUALIFIER_RSHIFT) {
		keysym->mod |= KMOD_RSHIFT;
    }

	if (qual & IEQUALIFIER_CAPSLOCK)
		keysym->mod |= KMOD_CAPS;

	if (qual & IEQUALIFIER_CONTROL) {
        keysym->mod |= KMOD_CTRL;
    }

	if (qual & IEQUALIFIER_LALT) {
		keysym->mod |= KMOD_LALT;
    }

	if (qual & IEQUALIFIER_RALT) {
		keysym->mod |= KMOD_RALT;
    }

	if (qual & IEQUALIFIER_NUMERICPAD) {
		keysym->mod |= KMOD_NUM;
    }
	
    if (keycode_lookup[code] == SDLK_TRANSLATE_KEY) {
#ifdef DEBUG_KEYBOARD_EVENTS
        SDL_LogDebugMsgchar("[TranslateKey]: Code %d is marked as SDLK_TRANSLATE_KEY\n", code);
#endif
        keysym->sym = CalculateKeySym(code);
        if (keysym->sym >= SDLK_LAST) {
            SDL_LogWarningMsgchar("Invalid translated key code detected\n");
            keysym->sym = SDLK_UNKNOWN;
        }
    } else {
#ifdef DEBUG_KEYBOARD_EVENTS
        SDL_LogDebugMsgchar("[TranslateKey]: Code %d is not marked as SDLK_TRANSLATE_KEY\n", code);
#endif
        keysym->sym = keycode_lookup[code];
    }

    // If UNICODE is on and key is pressed, get the UNICODE value for the key using the modifier.
    if (pressed && SDL_TranslateUNICODE) {
        if (keysym->mod == KMOD_NONE) {
            // Without a modifier the unicode will just match the sym value.
            keysym->unicode = keysym->sym;
        } else {
            keysym->unicode = CalculateUnicode(code, qual);
        }

        // Unicode must be valid.
        if (keysym->unicode < 32 || keysym->unicode > 254) {
            // Invalid!
            keysym->unicode = 0;
        }
    }
	 
#ifdef DEBUG_KEYBOARD_EVENTS
    SDL_LogDebugMsgchar("[TranslateKey]: Returning - keysym->scancode = %u\n", keysym->scancode);
    SDL_LogDebugMsgchar("[TranslateKey]: Returning - keysym->sym = %u\n", keysym->sym);
    SDL_LogDebugMsgchar("[TranslateKey]: Returning - keysym->mod = %X\n", keysym->mod);
    SDL_LogDebugMsgchar("[TranslateKey]: Returning - keysym->unicode = %u\n", keysym->unicode);
#endif

    return keysym;	
}

static inline int ConvertMouseButton(int code)
{
	switch(code)
	{
		case IECODE_MBUTTON:
			return SDL_BUTTON_MIDDLE;

		case IECODE_RBUTTON:
			return SDL_BUTTON_RIGHT;

		default:
			return SDL_BUTTON_LEFT;
	}
}

static void IN_AddEvent(struct InputEvent *event)
{
	// The buffer is full when advancing the write index would land it on the
	// read index (which is how an empty buffer is spelled).
	int next = (imsghigh + 1) % MAXIMSGS;

	if (next != imsglow) {
		CopyMem(event, &imsgs[imsghigh], sizeof(struct InputEvent));
		imsghigh = next;
	}
}

HANDLERPROTO(IN_InputHandler, struct InputEvent *, struct InputEvent *ielist, APTR id)
{
	struct InputEvent *event;
	int code;

    if (AMIGA_WINDOW && (AMIGA_WINDOW->Flags & WFLG_WINDOWACTIVE))
    {
        for (event = ielist; event; event = event->ie_NextEvent)
    	{
            // We only care about mouse and keyboard events!
            if (event->ie_Class == IECLASS_RAWMOUSE) {
                code = event->ie_Code;

                // mouse buttons 1-3
    			if (code != IECODE_NOBUTTON)
    			{
        			// Store the event.
                    IN_AddEvent(event);

    				if (inputCaptured)
                    {
                        // Eat the event without breaking the linked list.
                        event->ie_Class = IECLASS_NULL;
                        event->ie_Code = 0;
                        event->ie_Qualifier = 0;
                    }
    			}

                // Always store the relative mouse movement.
                mouse_movement_x += event->ie_position.ie_xy.ie_x;
                mouse_movement_y += event->ie_position.ie_xy.ie_y;
            }
            
            if (event->ie_Class == IECLASS_RAWKEY) {
                // Store the event.
                IN_AddEvent(event);
                    
                if (inputCaptured) {
                    // Eat the event without breaking the linked list. Note that
                    // clearing ie_Code alone is NOT enough - raw code 0 is a
                    // real key (RAWKEY_TILDE), so the swallowed keystroke was
                    // being handed to Intuition as a backquote press.
                    event->ie_Class = IECLASS_NULL;
                    event->ie_Code = 0;
                    event->ie_Qualifier = 0;
                }
            }
        }
    }

    return ielist;
}

static void IN_ShutdownInputHandler(void)
{
#ifdef DEBUG_EVENTS
    SDL_LogDebugMsgchar("[IN_ShutdownInputHandler]: Shutting down the input handler\n");
#endif

    // Stop the handler swallowing anything on the way out.
    inputCaptured = 0;

	if (inputRequest)
	{
		// IND_REMHANDLER finds the handler to remove through io_Data, so make
		// sure it is still pointing at our Interrupt (a warp borrows it).
		inputRequest->io_Data = (APTR)inputHandler;
		inputRequest->io_Command = IND_REMHANDLER;
		DoIO((struct IORequest *)inputRequest);

		CloseDevice((struct IORequest *)inputRequest);
		DeleteStdIO(inputRequest);
		inputRequest = NULL;
	}

    if (inputPort)
    {
        DeleteMsgPort(inputPort);
        inputPort = NULL;
    }

    if (inputHandler)
    {
        FreeVec((APTR)inputHandler);
        inputHandler = NULL;
    }
}

static int IN_InitInputHandler(void)
{
#ifdef DEBUG_EVENTS
    SDL_LogDebugMsgchar("[IN_SetupInputHandler]: Setting up the input handler\n");
#endif
    
    // Reset.
    mouse_movement_x = mouse_movement_y = 0;
    imsglow = imsghigh = 0;

	inputHandler = AllocVec(sizeof(struct Interrupt), MEMF_PUBLIC|MEMF_CLEAR);
    if(!inputHandler) {
        SDL_SetError("Could not allocated memory for the input handler");
        return -1;
    }

    inputPort = CreateMsgPort();
   	if (!inputPort)
	{
		SDL_SetError("Could not create input handler message port");
        return -1;
    }

	inputRequest = CreateStdIO(inputPort);
	if (!inputRequest)
	{
		SDL_SetError("Could not create input request");
        return -1;
    }

	if (OpenDevice("input.device", 0, (struct IORequest *)inputRequest, 0) != 0)
	{
		// Success (zero if sucessful, non zero otherwise)
		SDL_SetError("Could not open input.device");
        return -1;
	}

    inputHandler->is_Node.ln_Type = NT_INTERRUPT;
	inputHandler->is_Node.ln_Pri = 100;
	inputHandler->is_Node.ln_Name = inputHandlerName;
	inputHandler->is_Code = (APTR)IN_InputHandler;
	inputRequest->io_Data = (APTR)inputHandler;

	inputRequest->io_Command = IND_ADDHANDLER;
	DoIO((struct IORequest *)inputRequest);

#ifdef DEBUG_EVENTS
    SDL_LogDebugMsgchar("[IN_SetupInputHandler]: Finished setting up the input handler\n");
#endif

    // Success.
    return 0;
}

// Moves the real Intuition pointer by injecting an absolute position event
// into the input stream.
//
// Note the deliberate use of IECLASS_POINTERPOS rather than a synthetic
// IECLASS_RAWMOUSE delta - our own handler only accumulates movement from
// IECLASS_RAWMOUSE, so a warp can never feed back into mouse_movement_x/y.
// That matters because the classic SDL 1.2 mouse look idiom is warp to the
// centre of the window and then read the delta.
void Amiga_WarpSystemPointer(int x, int y)
{
    struct InputEvent ie;

    if (!inputRequest) {
        return;
    }

#ifdef DEBUG_CURSOR
    SDL_LogDebugMsgchar("[Amiga_WarpSystemPointer]: Warping to x = %d, y = %d\n", x, y);
#endif

    SDL_memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_POINTERPOS;
    ie.ie_Code = IECODE_NOBUTTON;
    ie.ie_position.ie_xy.ie_x = x;
    ie.ie_position.ie_xy.ie_y = y;

    inputRequest->io_Command = IND_WRITEEVENT;
    inputRequest->io_Data = (APTR)&ie;
    inputRequest->io_Length = sizeof(ie);
    DoIO((struct IORequest *)inputRequest);

    // Put io_Data back where IND_ADDHANDLER left it, otherwise IND_REMHANDLER
    // will not find the handler at shutdown and we leave a dangling interrupt.
    inputRequest->io_Data = (APTR)inputHandler;
}

SDL_bool Amiga_GetRelativeMouseMode(void)
{
    return relativeMouseMode;
}

// Our input handler ignores everything while the window is inactive, so any
// key released elsewhere is never seen and SDL is left believing it is still
// held. Classic symptom - alt tab away while holding shift and every key is
// shifted forever afterwards. Resynchronise from the qualifier Intuition hands
// us when the window is activated again.
static void Amiga_SyncKeyModifier(SDL_bool held, SDLKey sym)
{
    Uint8 *keystate = SDL_GetKeyState(NULL);

    if (held != (keystate[sym] ? SDL_TRUE : SDL_FALSE)) {
        SDL_keysym keysym;

        SDL_memset(&keysym, 0, sizeof(keysym));
        keysym.scancode = 0;
        keysym.sym = sym;
        keysym.mod = KMOD_NONE;
        keysym.unicode = 0;

        // SDL_PrivateKeyboard() updates both the key state array and the
        // KMOD_#? mask when the sym is a modifier.
        SDL_PrivateKeyboard(held ? SDL_PRESSED : SDL_RELEASED, &keysym);
    }
}

static void Amiga_SyncKeyModifiers(UWORD qual)
{
    SDLMod mod;

#ifdef DEBUG_KEYBOARD_EVENTS
    SDL_LogDebugMsgchar("[Amiga_SyncKeyModifiers]: Resyncing from qualifier %X\n", qual);
#endif

    Amiga_SyncKeyModifier((qual & IEQUALIFIER_LSHIFT)   != 0, SDLK_LSHIFT);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_RSHIFT)   != 0, SDLK_RSHIFT);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_CONTROL)  != 0, SDLK_LCTRL);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_LALT)     != 0, SDLK_LALT);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_RALT)     != 0, SDLK_RALT);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_LCOMMAND) != 0, SDLK_LMETA);
    Amiga_SyncKeyModifier((qual & IEQUALIFIER_RCOMMAND) != 0, SDLK_RMETA);

    // Caps Lock is a latched state rather than a held key, so set the modifier
    // bit directly instead of synthesizing a press and release.
    mod = SDL_GetModState();
    if (qual & IEQUALIFIER_CAPSLOCK) {
        mod |= KMOD_CAPS;
    } else {
        mod &= ~KMOD_CAPS;
    }
    SDL_SetModState(mod);
}

static void Amiga_DispatchEvent(struct IntuiMessage *msg)
{
	switch (msg->Class) {
	    case IDCMP_CLOSEWINDOW:
#ifdef DEBUG_EVENTS
        	SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - IDCMP_CLOSEWINDOW\n");
#endif
    		SDL_PrivateQuit();
    		break;
    		
        case IDCMP_ACTIVEWINDOW:
#ifdef DEBUG_EVENTS
        	SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - IDCMP_ACTIVEWINDOW\n");
#endif
            // Update mouse position.
            current_video->UpdateMouse();

            // Recover any modifier that was released while we were inactive.
            Amiga_SyncKeyModifiers(msg->Qualifier);
            
            // Sometimes in windowed mode the new window can be placed behind
            // other windows which is confusing to the user.
            if (AMIGA_WINDOW) {
            	WindowToFront(AMIGA_WINDOW);
            }
            
            // Finally, tell the application we just got focus.
            SDL_PrivateAppActive(1, SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS);

            // Relative mode depends on us being active, so re-evaluate it now
            // that the application state has been updated.
            Amiga_CheckMouseMode();
            break;
            
        case IDCMP_INACTIVEWINDOW:
#ifdef DEBUG_EVENTS
        	SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - IDCMP_INACTIVEWINDOW\n");
#endif
                        
            // Tell the application we just lost focus.
            SDL_PrivateAppActive(0, SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS);

            // Drop out of relative mode while we are in the background.
            Amiga_SetRelativeMouseMode(SDL_FALSE);
            break;
	}
}

static struct InputEvent *IN_GetNextEvent(void)
{
	struct InputEvent *ie = NULL;

	if (imsglow != imsghigh) {
		ie = &imsgs[imsglow];
		imsglow++;
		imsglow %= MAXIMSGS;
	}

	return ie;
}

void Amiga_PumpEvents(void)
{
    struct InputEvent *event;
    int code;
    int qual;
    
    if (AMIGA_WINDOW) {
        struct IntuiMessage *imsg;

        // Action a pending reseed left by a relative mode change.
        if (reseedMousePosition) {
            reseedMousePosition = SDL_FALSE;

            if (current_video && current_video->UpdateMouse) {
                current_video->UpdateMouse();
            }
        }

        while ((imsg = (struct IntuiMessage*)GetMsg(AMIGA_WINDOW->UserPort))) {
            Amiga_DispatchEvent(imsg);
            ReplyMsg((struct Message *)imsg);
        }
        
        if (mouse_movement_x != 0 || mouse_movement_y != 0) {
#ifdef DEBUG_CURSOR
            // The mouse has been moved...
            SDL_LogDebugMsgchar("[Amiga_PumpEvents]: mouse has moved by x = %d, y = %d\n", mouse_movement_x, mouse_movement_y);
#endif    
            if (relativeMouseMode) {
                SDL_PrivateMouseMotion(0, 1, mouse_movement_x, mouse_movement_y);
            } else {
                SDL_PrivateMouseMotion(0, 0, AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
            }

            // Reset.
            mouse_movement_x = mouse_movement_y = 0;
        }
        
    	while ((event = IN_GetNextEvent())) {
    		code = event->ie_Code;
    		qual = event->ie_Qualifier;
    
            // Process any mouse button events.
            if (event->ie_Class == IECLASS_RAWMOUSE) {
    			if (!(code&IECODE_UP_PREFIX)) {
                    SDL_PrivateMouseButton(SDL_PRESSED, ConvertMouseButton(code), AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
    			} else {
    				code&=~IECODE_UP_PREFIX;
                    SDL_PrivateMouseButton(SDL_RELEASED, ConvertMouseButton(code), AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
    			}
    		}
    		
    		// Process any keyboard events.
    		if (event->ie_Class == IECLASS_RAWKEY) {
                code = event->ie_Code;
                qual = event->ie_Qualifier;
                
                switch (code)
                {
      				case RAWKEY_NM_WHEEL_UP:
#ifdef DEBUG_CURSOR
                        SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_BUTTON_WHEELUP\n");
#endif
                        SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_WHEELUP, 0, 0);
                        SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_WHEELUP, 0, 0);
                        break;
    
    				case RAWKEY_NM_WHEEL_DOWN:
#ifdef DEBUG_CURSOR
                        SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_BUTTON_WHEELDOWN\n");
#endif
    				    SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_WHEELDOWN, 0, 0);
    				    SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_WHEELDOWN, 0, 0);
    					break;

                    // The wheel emits a press and a release together above, so
                    // swallow the matching up codes rather than letting them
                    // fall through and be reported as unknown key releases.
                    case (RAWKEY_NM_WHEEL_UP | IECODE_UP_PREFIX):
                    case (RAWKEY_NM_WHEEL_DOWN | IECODE_UP_PREFIX):
                        break;

                    // A horizontal wheel has no meaning in SDL 1.2, so those
                    // codes are simply ignored.
                    case RAWKEY_NM_WHEEL_LEFT:
                    case RAWKEY_NM_WHEEL_RIGHT:
                    case (RAWKEY_NM_WHEEL_LEFT | IECODE_UP_PREFIX):
                    case (RAWKEY_NM_WHEEL_RIGHT | IECODE_UP_PREFIX):
                        break;

                    // The fourth button is a real button, so unlike the wheel
                    // it gets a proper press and release pair.
                    case RAWKEY_NM_BUTTON_FOURTH:
#ifdef DEBUG_CURSOR
                        SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_BUTTON_X1 pressed\n");
#endif
                        SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_X1, AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
                        break;

                    case (RAWKEY_NM_BUTTON_FOURTH | IECODE_UP_PREFIX):
#ifdef DEBUG_CURSOR
                        SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_BUTTON_X1 released\n");
#endif
                        SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_X1, AMIGA_WINDOW->GZZMouseX, AMIGA_WINDOW->GZZMouseY);
                        break;
                
                    default:
                        if (!(code&IECODE_UP_PREFIX) )
            		    {
            				SDL_keysym keysym;
#ifdef DEBUG_KEYBOARD_EVENTS
            				SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_PRESSED\n");
#endif
                            SDL_PrivateKeyboard(SDL_PRESSED, TranslateKey(code, qual, &keysym, SDL_PRESSED));
            		    } else {
            				SDL_keysym keysym;
#ifdef DEBUG_KEYBOARD_EVENTS
                            SDL_LogDebugMsgchar("[Amiga_DispatchEvent] - SDL_RELEASED\n");
#endif
            				code&=~IECODE_UP_PREFIX;
                            SDL_PrivateKeyboard(SDL_RELEASED, TranslateKey(code, qual, &keysym, SDL_RELEASED));
            		    }
                }
            }
    	}
    }
}

SDL_GrabMode Amiga_GrabInput(SDL_GrabMode mode)
{
#ifdef DEBUG_GRAB
    SDL_LogDebugMsgchar("[Amiga_GrabInput] - Grab mode %d set\n", mode);
#endif

	if (mode == SDL_GRAB_OFF) {
        inputCaptured = 0;
    } else {
        inputCaptured = 1;
    }
    
    return mode;
}

void Amiga_SetRelativeMouseMode(SDL_bool toggle)
{
    if (toggle == relativeMouseMode) {
        // Nothing to do.
        return;
    }

#ifdef DEBUG_EVENTS
    if (toggle) {
        SDL_LogDebugMsgchar("[Amiga_SetRelativeMouseMode]: Enabling relative mouse movement\n");
    } else {
        SDL_LogDebugMsgchar("[Amiga_SetRelativeMouseMode]: Disabling relative mouse movement\n");
    }
#endif

    relativeMouseMode = toggle;

    // Throw away whatever the handler accumulated under the old mode, otherwise
    // the first report after the switch is one large bogus jump.
    mouse_movement_x = 0;
    mouse_movement_y = 0;

    if (!toggle) {
        // Going back to absolute. Nothing has been updating SDL's idea of where
        // the pointer is while we were in relative mode, so it has to be
        // reseeded from the real Intuition position.
        //
        // Deferred rather than done here, because this can be reached from
        // inside SDL_ShowCursor() or SDL_WM_GrabInput() and Amiga_UpdateMouse()
        // takes the event thread lock. Amiga_PumpEvents() actions it in the
        // main task where the locking is unambiguous.
        reseedMousePosition = SDL_TRUE;
    }
}

void Amiga_CheckMouseMode(void)
{
    const char *env;

    env = SDL_getenv("SDL_RELATIVE_MOUSE_MODE");
    if (env && SDL_atoi(env)) {
    	Amiga_SetRelativeMouseMode(SDL_TRUE);
    } else {
    	Amiga_SetRelativeMouseMode(SDL_FALSE);
    }
}

int Amiga_InitOSKeymap(void)
{
	int i;
    	
#ifdef DEBUG_EVENTS
    SDL_LogDebugMsgchar("[Amiga_InitOSKeymap]: Called\n");
#endif

	/* Map the miscellaneous keys */
	for ( i=0; i < SDLK_LAST; ++i ) {
		keycode_lookup[i] = SDLK_UNKNOWN;
    }
    
    // Top row
    keycode_lookup[RAWKEY_ESCAPE] = SDLK_ESCAPE;
    keycode_lookup[RAWKEY_F1] = SDLK_F1;
    keycode_lookup[RAWKEY_F2] = SDLK_F2;
    keycode_lookup[RAWKEY_F3] = SDLK_F3;
    keycode_lookup[RAWKEY_F4] = SDLK_F4;
    keycode_lookup[RAWKEY_F5] = SDLK_F5;
    keycode_lookup[RAWKEY_F6] = SDLK_F6;
    keycode_lookup[RAWKEY_F7] = SDLK_F7;
    keycode_lookup[RAWKEY_F8] = SDLK_F8;
    keycode_lookup[RAWKEY_F9] = SDLK_F9;
    keycode_lookup[RAWKEY_F10] = SDLK_F10;
    keycode_lookup[RAWKEY_F11] = SDLK_F11;
    keycode_lookup[RAWKEY_F12] = SDLK_F12;
    
    // Second row 
    keycode_lookup[RAWKEY_TILDE] = SDLK_BACKQUOTE;
    keycode_lookup[RAWKEY_1] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_2] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_3] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_4] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_5] = SDLK_TRANSLATE_KEY;    
    keycode_lookup[RAWKEY_6] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_7] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_8] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_9] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_0] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_MINUS] = SDLK_MINUS;
    keycode_lookup[RAWKEY_EQUAL] = SDLK_EQUALS;
    keycode_lookup[RAWKEY_BACKSPACE] = SDLK_BACKSPACE;
    
    // Third row.
    keycode_lookup[RAWKEY_TAB] = SDLK_TAB;
    keycode_lookup[RAWKEY_Q] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_W] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_E] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_R] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_T] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_Y] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_U] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_I] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_O] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_P] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_LBRACKET] = SDLK_LEFTBRACKET;
    keycode_lookup[RAWKEY_RBRACKET] = SDLK_RIGHTBRACKET;
    keycode_lookup[RAWKEY_BACKSLASH] = SDLK_BACKSLASH;

    // Fourth row.
    keycode_lookup[RAWKEY_LCONTROL] = SDLK_LCTRL;
    keycode_lookup[RAWKEY_CAPSLOCK] = SDLK_CAPSLOCK;
    keycode_lookup[RAWKEY_A] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_S] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_D] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_F] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_G] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_H] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_J] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_K] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_L] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_SEMICOLON] = SDLK_SEMICOLON;
    keycode_lookup[RAWKEY_QUOTE] = SDLK_QUOTE;
    keycode_lookup[RAWKEY_RETURN] = SDLK_RETURN;
    
    // Fifth row.
    keycode_lookup[RAWKEY_LSHIFT] = SDLK_LSHIFT;
    keycode_lookup[RAWKEY_Z] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_X] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_C] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_V] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_B] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_N] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_M] = SDLK_TRANSLATE_KEY;
    keycode_lookup[RAWKEY_COMMA] = SDLK_COMMA;
    keycode_lookup[RAWKEY_PERIOD] = SDLK_PERIOD;
    keycode_lookup[RAWKEY_SLASH] = SDLK_SLASH;
    keycode_lookup[RAWKEY_RSHIFT] = SDLK_RSHIFT;
        
    // Sixth row.
    keycode_lookup[RAWKEY_LALT] = SDLK_LALT;
    keycode_lookup[RAWKEY_SPACE] = SDLK_SPACE;
    keycode_lookup[RAWKEY_RALT] = SDLK_RALT;
    
    // Middle column.
    keycode_lookup[RAWKEY_DELETE] = SDLK_DELETE;
    keycode_lookup[RAWKEY_UP] = SDLK_UP;
    keycode_lookup[RAWKEY_LEFT] = SDLK_LEFT;
    keycode_lookup[RAWKEY_DOWN] = SDLK_DOWN;
    keycode_lookup[RAWKEY_RIGHT] = SDLK_RIGHT;
    
    // Keypad 0-9
    keycode_lookup[RAWKEY_KP_7] = SDLK_KP7;
    keycode_lookup[RAWKEY_KP_8] = SDLK_KP8;
    keycode_lookup[RAWKEY_KP_9] = SDLK_KP9;
    keycode_lookup[RAWKEY_KP_4] = SDLK_KP4;
    keycode_lookup[RAWKEY_KP_5] = SDLK_KP5;
    keycode_lookup[RAWKEY_KP_6] = SDLK_KP6;
    keycode_lookup[RAWKEY_KP_1] = SDLK_KP1;
    keycode_lookup[RAWKEY_KP_2] = SDLK_KP2;
    keycode_lookup[RAWKEY_KP_3] = SDLK_KP3;
    keycode_lookup[RAWKEY_KP_0] = SDLK_KP0;
    keycode_lookup[RAWKEY_KP_DECIMAL] = SDLK_KP_PERIOD;
   	keycode_lookup[93] = SDLK_KP_MULTIPLY;
	keycode_lookup[74] = SDLK_KP_MINUS;
	keycode_lookup[RAWKEY_KP_PLUS] = SDLK_KP_PLUS;
    keycode_lookup[RAWKEY_KP_ENTER] = SDLK_KP_ENTER;
    
    inputCaptured = 0;
    relativeMouseMode = SDL_FALSE;
    
    IN_InitInputHandler();
    
    // All is cool...
    return 0;
}

void Amiga_QuitOSKeymap(void)
{
#ifdef DEBUG_EVENTS
    SDL_LogDebugMsgchar("[Amiga_QuitOSKeymap]: Called\n");
#endif

   IN_ShutdownInputHandler();
}

