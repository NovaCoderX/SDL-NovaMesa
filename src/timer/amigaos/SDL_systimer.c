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
#include "SDL_thread.h" 
#include "../SDL_timer_c.h"

// Amiga includes.
#include <proto/exec.h>
#include <proto/timer.h>

#include <time.h>

extern void SDL_SYS_SetThreadPriority(SDL_Thread *thread, int priority);

static struct MsgPort *msgPort = NULL;

// Used by Threads, can't be static!
struct timerequest *timerReq[2] = { NULL, NULL };

/* Data to handle a single periodic alarm */
static int timerThreadAlive = 0;
static SDL_Thread *timerThread = NULL;
static struct Task *timerParentTask = NULL;

void SDL_StartTicks(void) {
#ifdef DEBUG_TIMERS
    SDL_LogDebugMsgchar("SDL_StartTicks()\n");
#endif

	msgPort = CreateMsgPort();
	if (!msgPort) {
		SDL_SetError("Could not create Message Port for the Timer");
		return;
	}

	timerReq[0] = (struct timerequest*) CreateIORequest(msgPort, sizeof(struct timerequest));
	if (!timerReq[0]) {
		SDL_SetError("Could not create IO Request for the Timer");
		DeleteMsgPort(msgPort);
		return;
	}

	timerReq[1] = (struct timerequest*) CreateIORequest(msgPort, sizeof(struct timerequest));
	if (!timerReq[1]) {
		SDL_SetError("Could not create IO Request for the Timer");
		DeleteIORequest((struct IORequest* )timerReq[0]);
		DeleteMsgPort(msgPort);
		return;
	}

	if (OpenDevice(TIMERNAME, UNIT_VBLANK, (struct IORequest *)timerReq[0], 0) != 0) {
		SDL_SetError("Can't open timer device UNIT_VBLANK");

		DeleteIORequest((struct IORequest* )timerReq[0]);
		DeleteIORequest((struct IORequest* )timerReq[1]);
		DeleteMsgPort(msgPort);
		return;
	}

	if (OpenDevice(TIMERNAME, UNIT_MICROHZ, (struct IORequest *)timerReq[1], 0) != 0) {
		SDL_SetError("Can't open timer device UNIT_MICROHZ");

		if (timerReq[0]->tr_node.io_Device) {
			CloseDevice((struct IORequest* )timerReq[0]);
		}

		DeleteIORequest((struct IORequest* )timerReq[0]);
		DeleteIORequest((struct IORequest* )timerReq[1]);
		DeleteMsgPort(msgPort);
		return;
	}
}

void SDL_FinishTicks(void) {
#ifdef DEBUG_TIMERS
    SDL_LogDebugMsgchar("SDL_FinishTicks()\n");
#endif

	if (timerReq[1]) {
		CloseDevice((struct IORequest* )timerReq[1]);
		DeleteIORequest((struct IORequest* )timerReq[1]);
		timerReq[1] = NULL;
	}

	if (timerReq[0]) {
		CloseDevice((struct IORequest* )timerReq[0]);
		DeleteIORequest((struct IORequest* )timerReq[0]);
		timerReq[0] = NULL;
	}

	if (msgPort) {
		DeleteMsgPort(msgPort);
		msgPort = NULL;
	}
}

Uint32 SDL_GetTicks(void) {
#if (__GNUC__ > 3)
	return clock();
#else
	return clock() * 10;
#endif
}

static void delete_timer(struct timerequest *tr) {
	struct MsgPort *mp;

	if (tr) {
		mp = tr->tr_node.io_Message.mn_ReplyPort;
		if (mp) {
			DeleteMsgPort(mp);
		}

		CloseDevice((struct IORequest* )tr);
		DeleteIORequest((struct IORequest* )tr);
	}
}

static struct timerequest* create_timer(ULONG unit) {
	struct MsgPort *mp;
	struct timerequest *tr;

	mp = CreateMsgPort();
	if (!mp) {
		SDL_SetError("Could not create Message Port for the Timer");
		return (NULL);
	}

	tr = (struct timerequest*) CreateIORequest(mp, sizeof(struct timerequest));
	if (!tr) {
		SDL_SetError("Could not create IO Request for the Timer");
		DeleteMsgPort(mp);
		return NULL;
	}

	if (OpenDevice(TIMERNAME, unit, (struct IORequest *)tr, 0) != 0) {
		SDL_SetError("Can't open timer device");
		delete_timer(tr);
		return NULL;
	}

	return tr;
}

static void wait_for_timer(struct timerequest *tr, struct timeval *tv) {
	/* add a new timer request */
	tr->tr_node.io_Command = TR_ADDREQUEST;

	/* structure assignment */
	tr->tr_time = *tv;

	/* post request to the timer -- will go to sleep till done */
	DoIO((struct IORequest* )tr);
}

static void time_delay(struct timeval *tv) {
	struct timerequest *tr;

	tr = create_timer(UNIT_MICROHZ);
	if (tr) {
		wait_for_timer(tr, tv);
		delete_timer(tr);
	}
}

void SDL_Delay(Uint32 ms) {
	struct timeval tv;

	if (ms != 0) {
		tv.tv_secs = ms / 1000;
		tv.tv_micro = (ms % 1000) * 1000;

		time_delay(&tv);
	}
}

// Note - This executes in a seperate thread.
static int RunTimer(void *unused) {
	struct MsgPort *port = NULL;
	struct timerequest *request = NULL;

	port = CreateMsgPort();
	if (!port) {
		SDL_SetError("Could not create Message Port for the Timer");
		return -1;
	}

	request = (struct timerequest*) CreateIORequest(port, sizeof(struct timerequest));
	if (!request) {
		SDL_SetError("Could not create IO Request for the Timer");
		DeleteMsgPort(port);
		port = NULL;
		return -1;
	}

	if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)request, 0) != 0) {
		SDL_SetError("Can't open timer device UNIT_MICROHZ");
		CloseDevice((struct IORequest* )request);
		DeleteIORequest((struct IORequest* )request);
		request = NULL;

		DeleteMsgPort(port);
		port = NULL;
		return -1;
	}

	request->tr_node.io_Command = TR_ADDREQUEST;

	// Signal parent thread that we are ready to go.
	Signal(timerParentTask, SIGBREAKF_CTRL_F);

	while (timerThreadAlive) {
		ULONG running = SDL_timer_running;

		request->tr_time.tv_secs = 0;
		request->tr_time.tv_micro = running ? 10000 : 30000;

		SendIO((struct IORequest* )request);

		if (running) {
			SDL_ThreadedTimerCheck();
		}

		WaitPort(port);
		GetMsg(port);

	}

	CloseDevice((struct IORequest* )request);
	DeleteIORequest((struct IORequest* )request);
	request = NULL;

	DeleteMsgPort(port);
	port = NULL;

	if (timerThreadAlive) {
		// There was a problem with the Timer thread (did not exit gracefully).
		Forbid();

		timerThreadAlive = 0;

		// Wake up parent.
		Signal(timerParentTask, SIGBREAKF_CTRL_F);
	}

	return 0;
}

/* This is only called if the event thread is not running */
int SDL_SYS_TimerInit(void) {
	const char *env;
	int priority = 1; // Default.

#ifdef DEBUG_TIMERS	
    SDL_LogDebugMsgchar("SDL_SYS_TimerInit: Called\n");
#endif

	timerThreadAlive = 1;

	// Find the task for this process.
	timerParentTask = FindTask(NULL);

	// Clear this signal.
	SetSignal(0L, SIGBREAKF_CTRL_F);

	timerThread = SDL_CreateThread(RunTimer, NULL);
	if (!timerThread) {
		SDL_SetError("Thread creation failed for the Timer");
		return -1;
	}

	/* Waits for the child thread to be ready */
	Wait(SIGBREAKF_CTRL_F);

	if (!timerThreadAlive) /* the thread quit too early */
	{
		timerThread = NULL;
		return -1;
	}

	// Set timer thread priority.
	env = SDL_getenv("SDL_TIMER_PRIORITY");
	if (env) {
		priority = SDL_atoi(env);
	}

	SDL_SYS_SetThreadPriority(timerThread, priority);

	return SDL_SetTimerThreaded(1);
}

void SDL_SYS_TimerQuit(void) {
#ifdef DEBUG_TIMERS	    
	SDL_LogDebugMsgchar("SDL_SYS_TimerQuit: Called\n");
#endif

	timerThreadAlive = 0;
	if (timerThread) {
		// I think this means that the parent (application) thread should wait until the timer thread has finished (no timeout).
		SDL_WaitThread(timerThread, NULL);

		timerThread = NULL;
	}
}

// These should never be called!
int SDL_SYS_StartTimer(void) {
	SDL_SetError("Internal logic error: AmigaOS uses a threaded timer");
	return -1;
}

void SDL_SYS_StopTimer(void) {
	SDL_SetError("Internal logic error: AmigaOS uses a threaded timer");
}
