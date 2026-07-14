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