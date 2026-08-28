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

// win_local.h: Win32-specific Quake3 header file

#if defined ( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning(disable : 4201)
#pragma warning( push )
#endif
#include <windows.h>
#include <VersionHelpers.h>
#if defined ( _MSC_VER ) && ( _MSC_VER >= 1200 )
#pragma warning( pop )
#endif

// SPSC ring buffers: exactly one writer and one reader per buffer (which
// thread plays which role varies), so no locking is needed beyond the
// memory ordering x64's TSO already guarantees.

typedef void (*threadFunction_t)( struct thread_t *thread );

typedef struct thread_t {
	threadFunction_t function;
	HANDLE           thread;
	HANDLE           initDoneEvent;
	volatile qboolean stopRequested;
	volatile qboolean exitedEarly;
} thread_t;

qboolean WIN_CreateThread( thread_t *thread, threadFunction_t function );
void     WIN_DestroyThread( thread_t *thread, qboolean forceExit );

typedef struct {
	volatile uint64_t writeIndex;
	byte               padding0[56];
	volatile uint64_t readIndex;
	byte               padding1[56];
	uint64_t           size; // array length, in elements
	float              maxUsagePc; // max. percentage fill between write & read
	byte               padding2[64 - 8 - 4];
} spscRingBuffer_t;

typedef struct {
	int timestamp;
	int event;
	int arg1;
	int arg2;
} inputEvent_t;

typedef enum {
	WCMD_WINDOW_MESSAGE,
	WCMD_PROCESS_LINE,
	WCMD_APPEND_LINE,
	WCMD_SET_ERROR_TEXT
} windowCommandId_t;

typedef struct {
	uint64_t          userData; // for WCMD_PROCESS_LINE/APPEND_LINE/SET_ERROR_TEXT: string buffer start offset
	HWND              hWnd;
	WPARAM            wParam;
	LPARAM            lParam;
	UINT              uMsg;
	uint32_t          dataLength; // for the same three: string byte length
	windowCommandId_t type;
} windowCommand_t;

typedef struct {
	spscRingBuffer_t base;
	inputEvent_t      inputs[16 << 10];
} inputBuffer_t;

typedef struct {
	spscRingBuffer_t  base;
	windowCommand_t   commands[4 << 10];
} windowCommandBuffer_t;

typedef struct {
	spscRingBuffer_t base;
	char             data[256 << 10];
} stringBuffer_t;

typedef struct {
	uint64_t          begin;
	uint64_t          end;
	spscRingBuffer_t *buffer;
} ringBufferIter_t;

// C has no function overloading, so the batched (push-then-flush) and
// single-push variants need distinct names.
void     WIN_InitRingBuffer( spscRingBuffer_t *buffer, uint64_t arrayLength );

void     WIN_PushInputEventB( inputBuffer_t *buffer, uint64_t *writeIndex, int timestamp, int event, int arg1, int arg2 ); // batched: caller owns writeIndex, must flush with WIN_FlushInputEvents
void     WIN_FlushInputEvents( inputBuffer_t *buffer, uint64_t writeIndex );
void     WIN_PushInputEvent( inputBuffer_t *buffer, int timestamp, int event, int arg1, int arg2 ); // single push, read-modify-write-back internally
void     WIN_PushWindowEvent( windowCommandBuffer_t *buffer, HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );
void     WIN_PushWindowCommand( windowCommandBuffer_t *buffer, windowCommandId_t command, uint64_t userData, uint32_t dataLength );
uint64_t WIN_PushString( stringBuffer_t *buffer, const char *string, uint32_t stringLength );

void WIN_BeginReading( ringBufferIter_t *iter, spscRingBuffer_t *buffer );
void WIN_EndReading( ringBufferIter_t *iter );

#ifdef DOOMSOUND    ///// (SA) DOOMSOUND
#include "../mssdk/include/dinput.h"
#include "../mssdk/include/dsound.h"
#else
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dsound.h>
#endif  ///// (SA) DOOMSOUND

#include <winsock.h>

#ifdef DOOMSOUND    ///// (SA)DOOMSOUND
#ifdef __cplusplus
extern "C" {
#endif
#endif  ///// (SA) DOOMSOUND

void    IN_MouseEvent( int mstate );
void    IN_LegacyMouseMove( void );

void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

void    Sys_DestroyConsole( qboolean waitForUser );

// Input subsystem

void    IN_Init( void );
void    IN_Shutdown( void );
void    IN_JoystickCommands( void );

void    IN_Move( usercmd_t *cmd );
// add additional non keyboard / non mouse movement on top of the keyboard move cmd

void    IN_DeactivateWin32Mouse( void );

void    IN_Activate( qboolean active );
void    IN_Frame( void );

int  IN_GetQuakeKey( int vkCode, int scanCode, qboolean isExtended );
void WIN_StartInputThread( void );
void WIN_StopInputThread( qboolean forceExit );


// crash handling
void WIN_InstallExceptionHandlers(void);
void WIN_RegisterExceptionCommands(void);
void WIN_EndTimePeriod(void);

// window procedure
LRESULT CALLBACK MWT_MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

// custom messages for calls that only work on the thread owning the window;
// posted infrequently enough that SendMessage's cost doesn't matter.
#define WM_SETCURSORVIS    ( WM_USER + 0 )
#define WM_SETMOUSECAPTURE ( WM_USER + 1 )

typedef enum { WINREQ_NONE, WINREQ_CREATE, WINREQ_DESTROY } windowRequest_t;

typedef struct {
	DWORD       exStyle;
	const char  *className;
	const char  *windowName;
	DWORD       style;
	int         x, y, w, h;
	HINSTANCE   hInstance;
} windowCreateParams_t;

qboolean WIN_RequestWindow( const windowCreateParams_t *params ); // creates g_wv.hWnd on the window thread; returns qtrue on success
void     WIN_RequestWindowDestroy( void );                        // destroys g_wv.hWnd on the window thread, clears it

void WIN_ProcessMainWindowEvents( void );

void WIN_StartWindowThread( void );
void WIN_StopWindowThread( qboolean forceExit );

void WIN_ProcessConsoleWindowEvents( void );
void WIN_StartSysconThread( void );
void WIN_StopSysconThread( qboolean forceExit );

void Conbuf_AppendText( const char *msg );

void SNDDMA_Activate( void );
int  SNDDMA_InitDS();

#define MAX_MONITOR_COUNT 16
typedef struct
{

	HINSTANCE reflib_library;           // Handle to refresh DLL
	qboolean reflib_active;

	HWND hWnd;
	HINSTANCE hInstance;
	qboolean activeApp;
	qboolean isMinimized;
	OSVERSIONINFO osversion;

	// when we get a windows message, we store the time off so keyboard processing
	// can know the exact time of an event
	unsigned sysMsgTime;

	int			monitor;		// 0-based index of the monitor currently used for display
	int			primaryMonitor;	// 0-based index of the primary monitor
	int			monitorCount;
	RECT		monitorRects[MAX_MONITOR_COUNT];
	HMONITOR	hMonitors[MAX_MONITOR_COUNT];

	qbool		inputInitialized;
	qbool		duringCreateWindow;	// qtrue during the call to CreateWindow
	qbool       noborder;

	// window thread -> main thread
	windowCommandBuffer_t mainWndCmdBuffer;
	inputBuffer_t          legacyInputBuffer; // WM_CHAR always; mouse/keyboard fallback only if raw init failed

	qboolean rawInput; // set qtrue once the input thread's raw mouse registration succeeds

	thread_t mainWindowThread;

	// input thread -> main thread
	inputBuffer_t inputThreadBuffer;

	thread_t inputThread;
	volatile qboolean inputThreadReady; // set qtrue once the input thread's RegisterRawInputDevices succeeds

	// syscon thread -> main thread
	windowCommandBuffer_t conCmdBuffer;
	stringBuffer_t         conCmdStringBuffer;

	// main thread -> syscon thread
	windowCommandBuffer_t mainCmdBuffer;
	stringBuffer_t         mainCmdStringBuffer;

	thread_t sysconThread;
} WinVars_t;

extern WinVars_t g_wv;


#ifdef DOOMSOUND    ///// (SA) DOOMSOUND
#ifdef __cplusplus
}
#endif
#endif  ///// (SA) DOOMSOUND

