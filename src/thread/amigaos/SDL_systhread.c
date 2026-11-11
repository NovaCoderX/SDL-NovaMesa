/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

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
#include "SDL_error.h"
#include "../SDL_thread_c.h"


// Amiga includes.
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>


/* Per thread bookkeeping, hung off SDL_Thread->data.
 *
 * The generic SDL 1.2 code allocates and zeroes SDL_Thread but never touches
 * its 'data' field, so we are free to use it and do not have to keep a side
 * table keyed off the task pointer.
 *
 * The signal bit is what makes SDL_WaitThread() work with more than one
 * thread. Signal bits belong to the task that allocates them, so the PARENT
 * allocates it in SDL_SYS_CreateThread() and the child signals it on the way
 * out. Every thread therefore has its own bit and waiting for one thread can
 * never be satisfied by a different one finishing. */
struct thread_hwdata
{
	struct Task *parent;   /* Task that created the thread and will wait on it. */
	BYTE sigBit;           /* Completion signal bit, allocated in the parent. */
	BYTE sigAllocated;     /* Whether sigBit is valid. */
};


/* Returns the signal mask the child should use to report completion. */
static ULONG doneSignalMask(SDL_Thread *thread)
{
	struct thread_hwdata *hwdata;

	if (thread) {
		hwdata = (struct thread_hwdata *)thread->data;
		if (hwdata && hwdata->sigAllocated) {
			return (1L << hwdata->sigBit);
		}
	}

	/* We could not get a bit of our own, so fall back to the old shared
	 * behaviour rather than never reporting completion at all. */
	return SIGBREAKF_CTRL_F;
}


void SDL_SYS_RunThread(void *data)
{
	thread_args *args;
	int (*userfunc)(void *);
	void *userdata;
	int *statusloc;

	/* Get the thread id */
	args = (thread_args *)data;
	args->info->threadid = SDL_ThreadID();

	/* Figure out what function to run */
	userfunc = args->func;
	userdata = args->data;
	statusloc = &args->info->status;

	/* Wake up the parent thread */
	Signal(args->wait,SIGBREAKF_CTRL_E);

	/* Run the function */
	*statusloc = userfunc(userdata);
}

// Runs in a seperate thread
static Uint32 RunThread(char *args __asm("a0")) {

    struct Task *parent;
    ULONG doneSignal;

    thread_args *data=(thread_args *)atol(args);

	/* Cache everything we need BEFORE running the user function.
	 * SDL_SYS_RunThread() wakes the parent, which then frees the thread_args,
	 * so nothing below may look at 'data' again. */
	parent = data->wait;
	doneSignal = doneSignalMask(data->info);

	SDL_SYS_RunThread(data);

    // Tell the parent that the child thread has finished processing.
    Signal(parent, doneSignal);

    return(0);
}


static int threadId = 0;

int SDL_SYS_CreateThread(SDL_Thread *thread, thread_args *args)
{
	/* Create the thread and go! */
	char bufferArgs[24];
	char bufferName[64];
	struct thread_hwdata *hwdata;

	hwdata = (struct thread_hwdata *)SDL_malloc(sizeof(struct thread_hwdata));
	if (!hwdata)
	{
		SDL_SetError("Not enough memory to create thread");
		return(-1);
	}

	hwdata->parent = FindTask(NULL);
	hwdata->sigBit = AllocSignal(-1);
	hwdata->sigAllocated = (hwdata->sigBit != -1);
	thread->data = hwdata;

	if (hwdata->sigAllocated)
	{
		/* Make sure nothing stale is pending on the bit we just took. */
		SetSignal(0L, (1L << hwdata->sigBit));
	}

#ifdef DEBUG_THREADS
	if (!hwdata->sigAllocated)
	{
		SDL_LogDebugMsgchar("[SDL_SYS_CreateThread]: No free signal bit, falling back to SIGBREAKF_CTRL_F\n");
	}
#endif

	/* Same for the startup handshake - the parent waits on CTRL_E immediately
	 * after we return, and a stale one would let it run on and free the
	 * arguments before the child has read them. */
	SetSignal(0L, SIGBREAKF_CTRL_E);

    SDL_snprintf(bufferArgs, sizeof(bufferArgs), "%ld", (LONG)args);

    threadId++;
    SDL_snprintf(bufferName, sizeof(bufferName), "SDL Task %d", threadId);

	thread->handle=(struct Task *)CreateNewProcTags(
                    NP_Output, Output(),
                    NP_Input, Input(), 
					NP_Name, (ULONG)bufferName,
					NP_CloseOutput, FALSE,
                    NP_CloseInput, FALSE, 
					NP_StackSize, 262144UL,
					NP_Entry, (ULONG)RunThread,
					NP_Arguments, (ULONG)bufferArgs,
					TAG_DONE);


	if (!thread->handle)
	{
		if (hwdata->sigAllocated)
		{
			FreeSignal(hwdata->sigBit);
		}

		SDL_free(hwdata);
		thread->data = NULL;

		SDL_SetError("Not enough resources to create thread");
		return(-1);
	}
	
	return 0;
}


Uint32 SDL_ThreadID(void)
{
    // Will return the current task (Note - this is equivalent to thread->handle).
    return((Uint32)FindTask(NULL));
}

/* Note - this must be called from the same task that created the thread,
 * because the completion signal was allocated in, and is sent to, that task. */
void SDL_SYS_WaitThread(SDL_Thread *thread)
{
	struct thread_hwdata *hwdata = (struct thread_hwdata *)thread->data;

	if (hwdata && hwdata->sigAllocated)
	{
#ifdef DEBUG_THREADS
		if (hwdata->parent != FindTask(NULL))
		{
			SDL_LogDebugMsgchar("[SDL_SYS_WaitThread]: Called from a different task to the one that created the thread, this will not work\n");
		}
#endif

		/* Deliberately NOT clearing the bit first. If the thread has already
		 * finished then its signal is sitting here waiting for us, and
		 * throwing it away would block forever. Any stale signal was cleared
		 * in SDL_SYS_CreateThread() before the child could possibly run. */
		Wait(1L << hwdata->sigBit);

		FreeSignal(hwdata->sigBit);
		hwdata->sigAllocated = 0;
	}
	else
	{
		/* No bit of our own, so the child fell back to the shared signal. */
		SetSignal(0L, SIGBREAKF_CTRL_F);
		Wait(SIGBREAKF_CTRL_F);
	}

	if (hwdata)
	{
		SDL_free(hwdata);
		thread->data = NULL;
	}
}

/* Note - this is cooperative only. There is no safe way to stop a task that is
 * running arbitrary C code, so the child only stops if the thread function
 * itself checks for SIGBREAKF_CTRL_C. SDL_KillThread() calls SDL_WaitThread()
 * straight afterwards, so a thread function that ignores the signal will block
 * the caller until it returns on its own. */
void SDL_SYS_KillThread(SDL_Thread *thread)
{
#ifdef DEBUG_THREADS
    SDL_LogDebugMsgchar("[SDL_SYS_KillThread]: Killing thread Id %u\n", thread->threadid);
#endif
    
    // Send this siginal to the child thread will will case it to exit gracefully.
    Signal((struct Task *)thread->handle,SIGBREAKF_CTRL_C);
}

void SDL_SYS_SetThreadPriority(SDL_Thread *thread, int priority)
{
#ifdef DEBUG_THREADS
    SDL_LogDebugMsgchar("[SDL_SYS_SetThreadPriority]: Setting thread Id %u to priority %d\n", thread->threadid, priority);
#endif

    SetTaskPri(thread->handle, priority);
}
