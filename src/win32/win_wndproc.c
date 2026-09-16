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


#include "../client/client.h"
#include "win_local.h"
#include "../renderer_common/tr_public.h"

WinVars_t g_wv;


static volatile windowRequest_t s_windowRequest = WINREQ_NONE;
static volatile qboolean        s_windowRequestDone = qfalse;
static volatile qboolean        s_windowRequestResult = qfalse;
static windowCreateParams_t     s_windowCreateParams;


/*
==================
WIN_ProcessWindowRequest

Runs on the window thread. Performs the actual CreateWindowEx/DestroyWindow
call in response to a pending request, then signals completion.
==================
*/
static void WIN_ProcessWindowRequest( void ) {
	if ( s_windowRequest == WINREQ_CREATE ) {
		g_wv.hWnd = CreateWindowEx(
			s_windowCreateParams.exStyle,
			s_windowCreateParams.className,
			s_windowCreateParams.windowName,
			s_windowCreateParams.style,
			s_windowCreateParams.x, s_windowCreateParams.y,
			s_windowCreateParams.w, s_windowCreateParams.h,
			NULL, NULL,
			s_windowCreateParams.hInstance,
			NULL );

		if ( g_wv.hWnd ) {
			ShowWindow( g_wv.hWnd, SW_SHOW );
			UpdateWindow( g_wv.hWnd );
		}

		s_windowRequestResult = ( g_wv.hWnd != NULL );
	} else if ( s_windowRequest == WINREQ_DESTROY ) {
		if ( g_wv.hWnd ) {
			ShowWindow( g_wv.hWnd, SW_HIDE );
			DestroyWindow( g_wv.hWnd );
			g_wv.hWnd = NULL;
		}
		s_windowRequestResult = qtrue;
	}

	s_windowRequest = WINREQ_NONE;
	s_windowRequestDone = qtrue;
}


/*
==================
WIN_RequestWindow

Blocking call from the main thread: asks the window thread to create
g_wv.hWnd, then spin-waits. Only called during vid_restart, so that's fine.
==================
*/
qboolean WIN_RequestWindow( const windowCreateParams_t *params ) {
	s_windowCreateParams = *params;
	s_windowRequestDone = qfalse;
	s_windowRequest = WINREQ_CREATE;

	while ( !s_windowRequestDone ) {
		Sleep( 1 );
	}

	return s_windowRequestResult;
}


/*
==================
WIN_RequestWindowDestroy
==================
*/
void WIN_RequestWindowDestroy( void ) {
	s_windowRequestDone = qfalse;
	s_windowRequest = WINREQ_DESTROY;

	while ( !s_windowRequestDone ) {
		Sleep( 1 );
	}
}


/*
==================
WIN_MainWindowThreadFunc

Pumps g_wv.hWnd's message queue and services create/destroy requests;
signals initDoneEvent immediately since no window exists until requested.
==================
*/
static void WIN_MainWindowThreadFunc( thread_t *thread ) {
	MSG msg;
	qboolean profRegistered = qfalse;

	SetEvent( thread->initDoneEvent );

	while ( !thread->stopRequested ) {
		// PROF_InitThread Z_Mallocs, but mainzone doesn't exist until Com_Init --
		// this thread starts earlier, so poll com_zoneInitialized before calling it
		if ( !profRegistered && com_zoneInitialized ) {
			PROF_InitThread( "Window" );
			profRegistered = qtrue;
		}

		PROF_BEGIN( "PeekMessage" );
		while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
			if ( !GetMessage( &msg, NULL, 0, 0 ) ) {
				break; // WM_QUIT -- not something this codebase posts today, but don't spin forever if it ever is
			}
			g_wv.sysMsgTime = msg.time;
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		PROF_END();

		if ( s_windowRequest != WINREQ_NONE ) {
			WIN_ProcessWindowRequest();
		}

		Sleep( 1 );
	}

	PROF_ShutdownThread();
}


void WIN_StartWindowThread( void ) {
	if ( !WIN_CreateThread( &g_wv.mainWindowThread, WIN_MainWindowThreadFunc ) ) {
		// runs before Com_Init and before the console window exists, so
		// Com_Error/Sys_Error have nowhere to report this yet
		MessageBox( NULL, "Failed to create the window thread", "RTCW Error", MB_OK | MB_ICONERROR );
		exit( 1 );
	}
}


void WIN_StopWindowThread( qboolean forceExit ) {
	WIN_DestroyThread( &g_wv.mainWindowThread, forceExit );
}


#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL ( WM_MOUSELAST + 1 )  // message that will be supported by the OS
#endif

static UINT MSH_MOUSEWHEEL;

#if defined( ENABLE_PROFILER )
static const char *Prof_GetWindowMessageName( int uMsg ) {
	switch ( uMsg ) {
	case WM_INPUT: return "WM_INPUT";
	case WM_MOUSEWHEEL: return "WM_MOUSEWHEEL";
	case WM_CREATE: return "WM_CREATE";
	case WM_DISPLAYCHANGE: return "WM_DISPLAYCHANGE";
	case WM_DESTROY: return "WM_DESTROY";
	case WM_CLOSE: return "WM_CLOSE";
	case WM_ACTIVATE: return "WM_ACTIVATE";
	case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
	case WM_MOVE: return "WM_MOVE";
	case WM_LBUTTONDOWN: return "WM_LBUTTONDOWN";
	case WM_LBUTTONUP: return "WM_LBUTTONUP";
	case WM_RBUTTONDOWN: return "WM_RBUTTONDOWN";
	case WM_RBUTTONUP: return "WM_RBUTTONUP";
	case WM_MBUTTONDOWN: return "WM_MBUTTONDOWN";
	case WM_MBUTTONUP: return "WM_MBUTTONUP";
	case WM_MOUSEMOVE: return "WM_MOUSEMOVE";
	case WM_SYSCOMMAND: return "WM_SYSCOMMAND";
	case WM_SYSKEYDOWN: return "WM_SYSKEYDOWN";
	case WM_KEYDOWN: return "WM_KEYDOWN";
	case WM_SYSKEYUP: return "WM_SYSKEYUP";
	case WM_KEYUP: return "WM_KEYUP";
	case WM_CHAR: return "WM_CHAR";
	case WM_NCHITTEST: return "WM_NCHITTEST";
	default: return "WM_OTHER";
	}
}
#endif

// Console variables that we need to access from this module
cvar_t      *vid_xpos;          // X coordinate of window position
cvar_t      *vid_ypos;          // Y coordinate of window position
#ifdef DEDICATED
cvar_t      *r_fullscreen;
#endif 

#define VID_NUM_MODES ( sizeof( vid_modes ) / sizeof( vid_modes[0] ) )


/*
==================
VID_AppActivate
==================
*/
static void VID_AppActivate( BOOL fActive, BOOL minimize ) {
	g_wv.isMinimized = minimize;

	Com_DPrintf( "VID_AppActivate: %i\n", fActive );

	Key_ClearStates();  // FIXME!!!

	// we don't want to act like we're active if we're minimized
	if ( fActive && !g_wv.isMinimized ) {
		g_wv.activeApp = qtrue;
	} else
	{
		g_wv.activeApp = qfalse;
	}
	
	if ( r_fullscreen->integer ) {
		// HWND_BOTTOM only reorders the Z-band -- it can't clear the WS_EX_TOPMOST
		// style set at window creation; HWND_NOTOPMOST does.
		SetWindowPos( g_wv.hWnd, fActive ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	}

	// minimize/restore mouse-capture on demand
	if ( !g_wv.activeApp ) {
		IN_Activate( qfalse );
	} else
	{
		IN_Activate( qtrue );
	}
}

//==========================================================================

static byte s_scantokey[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',    13,    K_CTRL,'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
	'\'',    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*',
	K_ALT,' ',   K_CAPSLOCK,    K_F1, K_F2, K_F3, K_F4, K_F5,    // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0, K_HOME,
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11,
	K_F12,0,    0,    0,    0,    0,    0,    0,                    // 5
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,                      // 6
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0                       // 7
};

static byte s_scantokey_german[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '?',    '\'',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'z',    'u',    'i',
	'o',    'p',    '=',    '+',    13,    K_CTRL, 'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    '[',
	']',    '`',    K_SHIFT,'#',  'y',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '-',    K_SHIFT,'*',
	K_ALT,' ',   K_CAPSLOCK,    K_F1, K_F2, K_F3, K_F4, K_F5,    // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0, K_HOME,
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             '<',              K_F11,
	K_F12,0,    0,    0,    0,    0,    0,    0,                    // 5
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,                      // 6
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0                       // 7
};

static byte s_scantokey_french[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    ')',    '=',    K_BACKSPACE, 9, // 0
	'a',    'z',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '^',    '$',    13,    K_CTRL, 'q',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    'm',
	'%',    '`',    K_SHIFT,'*',  'w',    'x',    'c',    'v',      // 2
	'b',    'n',    ',',    ';',    ':',    '!',    K_SHIFT,'*',
	K_ALT,' ',   K_CAPSLOCK,    K_F1, K_F2, K_F3, K_F4, K_F5,    // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0, K_HOME,
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             '<',              K_F11,
	K_F12,0,    0,    0,    0,    0,    0,    0,                    // 5
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,                      // 6
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0                       // 7
};

static byte s_scantokey_spanish[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '\'',    '!',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',    13,    K_CTRL, 'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    '=',
	'{',    '`',    K_SHIFT,'}',  'z',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '-',    K_SHIFT,'*',
	K_ALT,' ',   K_CAPSLOCK,    K_F1, K_F2, K_F3, K_F4, K_F5,    // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0, K_HOME,
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             '<',              K_F11,
	K_F12,0,    0,    0,    0,    0,    0,    0,                    // 5
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,                      // 6
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0                       // 7
};

static byte s_scantokey_italian[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '\'',    '^',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',    13,    K_CTRL, 'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    '@',
	'#',    '`',    K_SHIFT,'=',  'z',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '-',    K_SHIFT,'*',
	K_ALT,' ',   K_CAPSLOCK,    K_F1, K_F2, K_F3, K_F4, K_F5,    // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0, K_HOME,
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             '<',              K_F11,
	K_F12,0,    0,    0,    0,    0,    0,    0,                    // 5
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0,                      // 6
	0,    0,    0,    0,    0,    0,    0,    0,
	0,    0,    0,    0,    0,    0,    0,    0                       // 7
};
/*
=======
MapKeyFromScanCode

Shared core for MapKey (lParam-based) and IN_GetQuakeKey (RAWKEYBOARD-based)
so the language scan-code tables below aren't duplicated.
=======
*/
static int MapKeyFromScanCode( int scanCode, qboolean isExtended ) {
	int result;

	if ( scanCode > 127 ) {
		return 0;
	}

	result = s_scantokey[scanCode];

	if ( cl_language->integer - 1 == LANGUAGE_FRENCH ) {
		result = s_scantokey_french[scanCode];
	} else if ( cl_language->integer - 1 == LANGUAGE_GERMAN ) {
		result = s_scantokey_german[scanCode];
	} else if ( cl_language->integer - 1 == LANGUAGE_ITALIAN ) {
		result = s_scantokey_italian[scanCode];
	} else if ( cl_language->integer - 1 == LANGUAGE_SPANISH ) {
		result = s_scantokey_spanish[scanCode];
	}

	if ( !isExtended ) {
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			return K_KP_UPARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_DOWNARROW:
			return K_KP_DOWNARROW;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
		default:
			return result;
		}
	} else
	{
		switch ( result )
		{
		case K_PAUSE:
			return K_KP_NUMLOCK;
		case 0x0D:
			return K_KP_ENTER;
		case 0x2F:
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		}
		return result;
	}
}

/*
=======
MapKey

Map from windows lParam to quake keynums
=======
*/
static int MapKey( int key ) {
	int modified = ( key >> 16 ) & 255;
	qboolean is_extended = ( key & ( 1 << 24 ) ) ? qtrue : qfalse;
	return MapKeyFromScanCode( modified, is_extended );
}

/*
=======
IN_GetQuakeKey

vkCode is unused -- kept for signature symmetry with cnq3's original and
possible future use; every table lookup here is scan-code-driven.
=======
*/
int IN_GetQuakeKey( int vkCode, int scanCode, qboolean isExtended ) {
	(void)vkCode;
	return MapKeyFromScanCode( scanCode & 0xFF, isExtended );
}


/*
====================
MainWndProc

main window procedure
====================
*/
static LRESULT CALLBACK MainWndProc_Impl(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam ) {

	if ( uMsg == MSH_MOUSEWHEEL ) {
		if ( !g_wv.rawInput ) {
			if ( ( (int)wParam ) > 0 ) {
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue );
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse );
			} else {
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue );
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse );
			}
		}
		return DefWindowProc( hWnd, uMsg, wParam, lParam );
	}

	switch ( uMsg )
	{
	case WM_MOUSEWHEEL:
		if ( !g_wv.rawInput ) {
			if ( ( short ) HIWORD( wParam ) > 0 ) {
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue );
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse );
			} else {
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue );
				WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse );
			}
		}
		break;

	case WM_CREATE:
		g_wv.hWnd = hWnd;

		vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
		vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );
		r_fullscreen = Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );

		MSH_MOUSEWHEEL = RegisterWindowMessage( "MSWHEEL_ROLLMSG" );
		break;

	case WM_DESTROY:
		g_wv.hWnd = NULL;
		break;

	case WM_WINDOWPOSCHANGING:
		if ( g_wv.noborder ) {
			WINDOWPOS* pos = (LPWINDOWPOS)lParam;
			const int threshold = 10;
			HMONITOR hMonitor;
			MONITORINFO mi;
			const RECT* r;
			RECT rr;

			rr.left = pos->x;
			rr.right = pos->x + pos->cx;
			rr.top = pos->y;
			rr.bottom = pos->y + pos->cy;
			hMonitor = MonitorFromRect( &rr, MONITOR_DEFAULTTOPRIMARY );

			if ( hMonitor ) {
				mi.cbSize = sizeof( mi );
				GetMonitorInfo( hMonitor, &mi );
				r = &mi.rcWork;

				if ( pos->x >= ( r->left - threshold ) && pos->x <= ( r->left + threshold ) )
					pos->x = r->left;
				else if ( ( pos->x + pos->cx ) >= ( r->right - threshold ) && ( pos->x + pos->cx ) <= ( r->right + threshold ) )
					pos->x = ( r->right - pos->cx );

				if ( pos->y >= ( r->top - threshold ) && pos->y <= ( r->top + threshold ) )
					pos->y = r->top;
				else if ( ( pos->y + pos->cy ) >= ( r->bottom - threshold ) && ( pos->y + pos->cy ) <= ( r->bottom + threshold ) )
					pos->y = ( r->bottom - pos->cy );

				return 0;
			}
		}
		break;

	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
		if ( !g_wv.rawInput ) {
			int temp = 0;
			if ( wParam & MK_LBUTTON ) temp |= 1;
			if ( wParam & MK_RBUTTON ) temp |= 2;
			if ( wParam & MK_MBUTTON ) temp |= 4;
			if ( wParam & MK_XBUTTON1 ) temp |= 8;
			if ( wParam & MK_XBUTTON2 ) temp |= 16;
			IN_MouseEvent( temp ); // pushes to g_wv.legacyInputBuffer

			if ( uMsg == WM_MOUSEMOVE ) {
				IN_LegacyMouseMove();
			}
		}
		break;

	case WM_SYSCOMMAND:
		if ( wParam == SC_SCREENSAVE ) {
			return 0;
		}
		break;

	case WM_SYSKEYDOWN:
		if ( wParam == 13 ) {
			WIN_PushWindowEvent( &g_wv.mainWndCmdBuffer, hWnd, uMsg, wParam, lParam );
			return 0;
		}
		// fall through
	case WM_KEYDOWN:
		if ( !g_wv.inputThreadReady ) {
			WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qtrue );
		}
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		if ( !g_wv.inputThreadReady ) {
			WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qfalse );
		}
		break;

	case WM_CHAR:
		WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_CHAR, wParam, 0 );
		break;

	case WM_NCHITTEST:
		if ( g_wv.noborder && GetKeyState( VK_CONTROL ) & ( 1 << 15 ) ) {
			return HTCAPTION;
		}
		break;

	case WM_SETCURSORVIS:
		while ( ShowCursor( wParam ? TRUE : FALSE ) < ( wParam ? 0 : -1 ) ) {
			// drain the counter to exactly 0 (visible) or -1 (hidden)
		}
		break;

	case WM_SETMOUSECAPTURE:
		// SetCapture requires the calling thread to own the window, which is
		// why IN_ActivateWin32Mouse/IN_DeactivateWin32Mouse route it here
		if ( wParam ) {
			SetCapture( hWnd );
		} else {
			ReleaseCapture();
		}
		break;

	case WM_CLOSE:
	case WM_ACTIVATE:
	case WM_MOVE:
		WIN_PushWindowEvent( &g_wv.mainWndCmdBuffer, hWnd, uMsg, wParam, lParam );
		break;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

/*
====================
MWT_MainWndProc

thin wrapper around MainWndProc_Impl so the profiler can bracket
every message without touching the switch's many case-block returns
====================
*/
LRESULT CALLBACK MWT_MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
#if defined( ENABLE_PROFILER )
	LRESULT result;

	PROF_BEGIN_I( Prof_GetWindowMessageName( (int)uMsg ), (int32_t)uMsg );
	result = MainWndProc_Impl( hWnd, uMsg, wParam, lParam );
	PROF_END();
	return result;
#else
	return MainWndProc_Impl( hWnd, uMsg, wParam, lParam );
#endif
}


/*
==================
WIN_ProcessMainWindowEvents

Drains window commands the window thread forwarded and re-runs their
original handling here, where it's safe to touch cvars/Cbuf/sound state.
==================
*/
void WIN_ProcessMainWindowEvents( void ) {
	ringBufferIter_t iter;
	uint64_t i;

	WIN_BeginReading( &iter, &g_wv.mainWndCmdBuffer.base );
	for ( i = iter.begin; i < iter.end; i++ ) {
		windowCommand_t *cmd = &g_wv.mainWndCmdBuffer.commands[ i % g_wv.mainWndCmdBuffer.base.size ];

		switch ( cmd->uMsg ) {
		case WM_CLOSE:
			Cbuf_ExecuteText( EXEC_APPEND, "quit" );
			break;

		case WM_ACTIVATE:
		{
			int fActive = LOWORD( cmd->wParam );
			int fMinimized = (BOOL)HIWORD( cmd->wParam );
			VID_AppActivate( fActive != WA_INACTIVE, fMinimized );
#ifndef DOOMSOUND
			SNDDMA_Activate();
#endif
		}
		break;

		case WM_MOVE:
			if ( !r_fullscreen->integer ) {
				int xPos = (short)LOWORD( cmd->lParam );
				int yPos = (short)HIWORD( cmd->lParam );
				RECT r = { 0, 0, 1, 1 };
				int style = GetWindowLongPtr( cmd->hWnd, GWL_STYLE );
				AdjustWindowRect( &r, style, FALSE );

				Cvar_SetValue( "vid_xpos", xPos + r.left );
				Cvar_SetValue( "vid_ypos", yPos + r.top );
				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
				if ( g_wv.activeApp ) {
					IN_Activate( qtrue );
				}
			}
			break;

		case WM_SYSKEYDOWN: // Alt+Enter fullscreen toggle -- only forwarded case for this uMsg
			if ( r_fullscreen ) {
				Cvar_SetValue( "r_fullscreen", !r_fullscreen->integer );
				Cbuf_AddText( "vid_restart\n" );
			}
			break;
		}
	}
	WIN_EndReading( &iter );

	if ( g_wv.mainWindowThread.exitedEarly ) {
		Com_Error( ERR_FATAL, "The window thread exited early" );
	}
}
