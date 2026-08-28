/*
===========================================================================

Return to Castle Wolfenstein multiplayer GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein multiplayer GPL Source Code (RTCW MP Source Code).  

RTCW MP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW MP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW MP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW MP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW MP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/


#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

qbool Sys_IsDebugging(void){
	return IsDebuggerPresent();
}

#ifndef _MSC_VER
void Sys_DebugBreak(void){
	DebugBreak();
}
#endif

void Sys_Sleep( int msec )
{
	PROF_BEGIN( "Sys_Sleep" );

	if ( msec < 0 ) {
		// special case: wait for event or network packet
		DWORD dwResult;
		msec = 300;
		do {
			dwResult = MsgWaitForMultipleObjects( 0, NULL, FALSE, msec, QS_ALLEVENTS );
		}
		while ( dwResult == WAIT_TIMEOUT && NET_Sleep( 10 * 1000 ) );
		//WaitMessage();
		PROF_END();
		return;
	}

	Sleep(msec);

	PROF_END();
}

/*
================
Sys_Milliseconds
================
*/
int sys_timeBase;
int Sys_Milliseconds( void ) {
	int sys_curtime;
	static qboolean initialized = qfalse;

	if ( !initialized ) {
		sys_timeBase = timeGetTime();
		initialized = qtrue;
	}
	sys_curtime = timeGetTime() - sys_timeBase;

	return sys_curtime;
}

int64_t Sys_Microseconds(void){
	static qbool initialized = qfalse;
	static LARGE_INTEGER start;
	static LARGE_INTEGER freq;

	if (!initialized) {
		initialized = qtrue;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&start);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	return ((now.QuadPart - start.QuadPart) * 1000000LL) / freq.QuadPart;
}


void Sys_MicroSleep( int us )
{
	if (us <= 50)
		return;

	PROF_BEGIN( "Sys_MicroSleep" );

	us -= 50;

	LARGE_INTEGER frequency;
	LARGE_INTEGER endTime;
	QueryPerformanceCounter(&endTime);
	QueryPerformanceFrequency(&frequency);
	endTime.QuadPart += ((LONGLONG)us * frequency.QuadPart) / 1000000LL;

	// reminder: we call timeBeginPeriod(1) at init
	// Sleep(1) will generally last 1000-2000 us,
	// but in some cases quite a bit more (I've seen up to 3500 us)
	// because threads can take longer to wake up
	const LONGLONG thresholdUS = (LONGLONG)Cvar_Get("r_sleepThreshold", "2500", CVAR_ARCHIVE)->integer;
	const LONGLONG bigSleepTicks = (thresholdUS * frequency.QuadPart) / 1000000LL;

	for (;;) {
		LARGE_INTEGER currentTime;
		QueryPerformanceCounter(&currentTime);
		const LONGLONG remainingTicks = endTime.QuadPart - currentTime.QuadPart;
		if (remainingTicks <= 0) {
			break;
		}
		if (remainingTicks >= bigSleepTicks) {
			Sleep(1);
		} else {
			YieldProcessor();
		}
	}

	PROF_END();
}


/*
================
Sys_RandomBytes
================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	HCRYPTPROV  prov;

	if( !CryptAcquireContext( &prov, NULL, NULL,
		PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )  {

		return qfalse;
	}

	if( !CryptGenRandom( prov, len, (BYTE *)string ) )  {
		CryptReleaseContext( prov, 0 );
		return qfalse;
	}
	CryptReleaseContext( prov, 0 );
	return qtrue;
}

int Sys_GetHighQualityCPU() {
	return 1;
}

//============================================

char *Sys_GetCurrentUser( void ) {
	static char s_userName[1024];
	unsigned long size = sizeof( s_userName );


	if ( !GetUserName( s_userName, &size ) ) {
		strcpy( s_userName, "player" );
	}

	if ( !s_userName[0] ) {
		strcpy( s_userName, "player" );
	}

	return s_userName;
}

qbool Sys_IsMinimized(void){
	return !!IsIconic(g_wv.hWnd);
}

char* Sys_GetScreenshotPath(char* filename){
	char* basepath = Cvar_VariableString("fs_basepath");
	char* gamepath = Cvar_VariableString("fs_game");

	return va("%s/%s/screenshots/%s.jpg", basepath, gamepath, filename);
}

/*
================
Sys_SetAffinityMask
================
*/
static HANDLE hCurrentProcess = 0;

uint64_t Sys_GetAffinityMask( void )
{
	DWORD_PTR dwProcessAffinityMask;
	DWORD_PTR dwSystemAffinityMask;

	if ( hCurrentProcess == 0 )	{
		hCurrentProcess = GetCurrentProcess();
	}

	if ( GetProcessAffinityMask( hCurrentProcess, &dwProcessAffinityMask, &dwSystemAffinityMask ) )	{
		return (uint64_t)dwProcessAffinityMask;
	}

	return 0;
}


qboolean Sys_SetAffinityMask( const uint64_t mask )
{
	DWORD_PTR dwProcessAffinityMask = (DWORD_PTR)mask;

	if ( hCurrentProcess == 0 ) {
		hCurrentProcess = GetCurrentProcess();
	}

	if ( SetProcessAffinityMask( hCurrentProcess, dwProcessAffinityMask ) )	{
		//Sleep( 0 );
		return qtrue;
	}

	return qfalse;
}


/*
==================
WIN_CreateThread

Blocks until the new thread signals it has finished startup, so the
caller never proceeds with a half-initialized thread.
==================
*/
qboolean WIN_CreateThread( thread_t *thread, threadFunction_t function ) {
	HANDLE waitHandles[2];

	memset( thread, 0, sizeof( *thread ) );
	thread->function = function;
	thread->initDoneEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
	if ( !thread->initDoneEvent ) {
		return qfalse;
	}

	thread->thread = CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)function, thread, 0, NULL );
	if ( !thread->thread ) {
		CloseHandle( thread->initDoneEvent );
		thread->initDoneEvent = NULL;
		return qfalse;
	}

	waitHandles[0] = thread->initDoneEvent;
	waitHandles[1] = thread->thread;
	WaitForMultipleObjects( 2, waitHandles, FALSE, INFINITE );

	CloseHandle( thread->initDoneEvent );
	thread->initDoneEvent = NULL;

	return qtrue;
}


/*
==================
WIN_DestroyThread

forceExit qfalse blocks without requesting a stop -- used on the Sys_Error
path, where the console must stay open until the user closes it.
==================
*/
void WIN_DestroyThread( thread_t *thread, qboolean forceExit ) {
	if ( !thread->thread ) {
		return;
	}

	if ( forceExit ) {
		thread->stopRequested = qtrue;
	}

	WaitForSingleObject( thread->thread, INFINITE );
	CloseHandle( thread->thread );
	thread->thread = NULL;
}


/*
==================
WIN_InitRingBuffer
==================
*/
void WIN_InitRingBuffer( spscRingBuffer_t *buffer, uint64_t arrayLength ) {
	buffer->writeIndex = 0;
	buffer->readIndex = 0;
	buffer->size = arrayLength;
	buffer->maxUsagePc = 0.0f;
}


/*
==================
WIN_WaitToWrite

Spin-waits until there is at least one free slot to write into. Full
buffers block the producer -- events are never dropped or overwritten.
==================
*/
static void WIN_WaitToWrite( spscRingBuffer_t *buffer, uint64_t writeIndex ) {
	while ( buffer->readIndex + buffer->size <= writeIndex ) {
		YieldProcessor();
	}
}


/*
==================
WIN_PushInputEventB

Batched push: caller tracks writeIndex locally, calls this per event, then
WIN_FlushInputEvents once to publish the whole batch atomically.
==================
*/
void WIN_PushInputEventB( inputBuffer_t *buffer, uint64_t *writeIndex, int timestamp, int event, int arg1, int arg2 ) {
	WIN_WaitToWrite( &buffer->base, *writeIndex );

	inputEvent_t *slot = &buffer->inputs[ *writeIndex % buffer->base.size ];
	slot->timestamp = timestamp;
	slot->event = event;
	slot->arg1 = arg1;
	slot->arg2 = arg2;

	( *writeIndex )++;
}


/*
==================
WIN_FlushInputEvents
==================
*/
void WIN_FlushInputEvents( inputBuffer_t *buffer, uint64_t writeIndex ) {
	buffer->base.writeIndex = writeIndex;
}


/*
==================
WIN_PushInputEvent

Convenience wrapper for callers that push a single event without batching.
==================
*/
void WIN_PushInputEvent( inputBuffer_t *buffer, int timestamp, int event, int arg1, int arg2 ) {
	uint64_t writeIndex = buffer->base.writeIndex;
	WIN_PushInputEventB( buffer, &writeIndex, timestamp, event, arg1, arg2 );
	WIN_FlushInputEvents( buffer, writeIndex );
}


/*
==================
WIN_PushWindowEvent
==================
*/
void WIN_PushWindowEvent( windowCommandBuffer_t *buffer, HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
	uint64_t writeIndex = buffer->base.writeIndex;
	windowCommand_t *slot;

	WIN_WaitToWrite( &buffer->base, writeIndex );

	slot = &buffer->commands[ writeIndex % buffer->base.size ];
	slot->type = WCMD_WINDOW_MESSAGE;
	slot->hWnd = hWnd;
	slot->uMsg = uMsg;
	slot->wParam = wParam;
	slot->lParam = lParam;
	slot->userData = 0;
	slot->dataLength = 0;

	buffer->base.writeIndex = writeIndex + 1;
}


/*
==================
WIN_PushWindowCommand
==================
*/
void WIN_PushWindowCommand( windowCommandBuffer_t *buffer, windowCommandId_t command, uint64_t userData, uint32_t dataLength ) {
	uint64_t writeIndex = buffer->base.writeIndex;
	windowCommand_t *slot;

	WIN_WaitToWrite( &buffer->base, writeIndex );

	slot = &buffer->commands[ writeIndex % buffer->base.size ];
	slot->type = command;
	slot->hWnd = NULL;
	slot->uMsg = 0;
	slot->wParam = 0;
	slot->lParam = 0;
	slot->userData = userData;
	slot->dataLength = dataLength;

	buffer->base.writeIndex = writeIndex + 1;
}


/*
==================
WIN_PushString

Handles wraparound by splitting the copy at the array boundary. Returns
the start offset to pass as a windowCommand_t's userData.
==================
*/
uint64_t WIN_PushString( stringBuffer_t *buffer, const char *string, uint32_t stringLength ) {
	uint64_t writeIndex = buffer->base.writeIndex;
	uint64_t startIndex;
	uint64_t i;

	for ( i = 0; i < (uint64_t)stringLength; i++ ) {
		WIN_WaitToWrite( &buffer->base, writeIndex + i );
	}

	startIndex = writeIndex;
	for ( i = 0; i < (uint64_t)stringLength; i++ ) {
		buffer->data[ ( writeIndex + i ) % buffer->base.size ] = string[i];
	}

	buffer->base.writeIndex = writeIndex + stringLength;

	return startIndex;
}


/*
==================
WIN_BeginReading

Snapshots the currently-available range [readIndex, writeIndex) for the
caller to iterate. Also updates maxUsagePc for diagnostic purposes.
==================
*/
void WIN_BeginReading( ringBufferIter_t *iter, spscRingBuffer_t *buffer ) {
	iter->buffer = buffer;
	iter->begin = buffer->readIndex;
	iter->end = buffer->writeIndex;

	if ( iter->end > iter->begin ) {
		float usagePc = 100.0f * (float)( iter->end - iter->begin ) / (float)buffer->size;
		if ( usagePc > buffer->maxUsagePc ) {
			buffer->maxUsagePc = usagePc;
		}
	}
}


/*
==================
WIN_EndReading

Publishes the whole batch [begin, end) as consumed in one write.
==================
*/
void WIN_EndReading( ringBufferIter_t *iter ) {
	iter->buffer->readIndex = iter->end;
}
