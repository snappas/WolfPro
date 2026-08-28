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

// win_syscon.h
#include "../client/client.h"
#include "win_local.h"
#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>

#define SYSCON_DEFAULT_WIDTH    540
#define SYSCON_DEFAULT_HEIGHT   450

// must match Conbuf_AppendTextImpl's CONSOLE_BUFFER_SIZE, or a long line
// gets silently truncated before it gets there
#define SYSCON_MAX_TEXT_LEN     16384

#define COPY_ID         1
#define QUIT_ID         2
#define CLEAR_ID        3

#define ERRORBOX_ID     10
#define ERRORTEXT_ID    11

#define EDIT_ID         100
#define INPUT_ID        101

typedef struct
{
	HWND hWnd;
	HWND hwndBuffer;

	HWND hwndButtonClear;
	HWND hwndButtonCopy;
	HWND hwndButtonQuit;

	HWND hwndErrorBox;
	HWND hwndErrorText;

	HBITMAP hbmLogo;
	HBITMAP hbmClearBitmap;

	HBRUSH hbrEditBackground;
	HBRUSH hbrErrorBackground;

	HFONT hfBufferFont;
	HFONT hfButtonFont;

	HWND hwndInputLine;

	char errorString[80];

	int visLevel;
	qboolean quitOnClose;
	int windowWidth, windowHeight;

	WNDPROC SysInputLineWndProc;

} WinConData;

static WinConData s_wcd;

static void Sys_CreateConsoleImpl( void );
static void Sys_DestroyConsoleImpl( void );
static void Conbuf_AppendTextImpl( const char *pMsg );
static void Sys_SetErrorTextImpl( const char *buf );

// forwards a command line to the main thread instead of calling Cvar_Set
// or Sys_QueEvent directly, which aren't safe to call off the main thread
static void SysCon_QueueCommand( const char *text ) {
	uint32_t len = (uint32_t)strlen( text );
	uint64_t offset = WIN_PushString( &g_wv.conCmdStringBuffer, text, len );
	WIN_PushWindowCommand( &g_wv.conCmdBuffer, WCMD_PROCESS_LINE, offset, len );
}

/*
==================
SysConThreadFunc

Owns the early-console window's whole lifetime: creation, message pump,
draining main-thread commands, and teardown on stop.
==================
*/
static void SysConThreadFunc( thread_t *thread ) {
	MSG msg;
	qboolean profRegistered = qfalse;

	Sys_CreateConsoleImpl();

	if ( s_wcd.hWnd == NULL ) {
		// no window means nothing will ever answer a Sys_Error's WM_QUIT wait --
		// report the failure instead of pumping an empty queue forever
		thread->exitedEarly = qtrue;
		SetEvent( thread->initDoneEvent );
		return;
	}

	SetEvent( thread->initDoneEvent );

	while ( !thread->stopRequested ) {
		// PROF_InitThread Z_Mallocs, but mainzone doesn't exist until Com_Init --
		// this thread starts earlier, so poll com_zoneInitialized before calling it
		if ( !profRegistered && com_zoneInitialized ) {
			PROF_InitThread( "Syscon" );
			profRegistered = qtrue;
		}

		PROF_BEGIN( "PeekMessage" );
		while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
			if ( !GetMessage( &msg, NULL, 0, 0 ) ) {
				// WM_QUIT (Quit button or a forced error dialog closing) --
				// stop the loop ourselves, nothing else will
				thread->stopRequested = qtrue;
				break;
			}
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		PROF_END();

		{
			ringBufferIter_t iter;
			uint64_t i;

			WIN_BeginReading( &iter, &g_wv.mainCmdBuffer.base );
			for ( i = iter.begin; i < iter.end; i++ ) {
				windowCommand_t *cmd = &g_wv.mainCmdBuffer.commands[ i % g_wv.mainCmdBuffer.base.size ];
				char text[SYSCON_MAX_TEXT_LEN];
				uint32_t len = cmd->dataLength < sizeof( text ) - 1 ? cmd->dataLength : sizeof( text ) - 1;
				uint32_t j;

				for ( j = 0; j < len; j++ ) {
					text[j] = g_wv.mainCmdStringBuffer.data[ ( cmd->userData + j ) % g_wv.mainCmdStringBuffer.base.size ];
				}
				text[len] = 0;

				if ( cmd->type == WCMD_APPEND_LINE ) {
					Conbuf_AppendTextImpl( text );
				} else if ( cmd->type == WCMD_SET_ERROR_TEXT ) {
					Sys_SetErrorTextImpl( text );
				}

				g_wv.mainCmdStringBuffer.base.readIndex = cmd->userData + cmd->dataLength;
			}
			WIN_EndReading( &iter );
		}

		Sleep( 1 );
	}

	Sys_DestroyConsoleImpl();

	PROF_ShutdownThread();
}


void WIN_StartSysconThread( void ) {
	if ( !WIN_CreateThread( &g_wv.sysconThread, SysConThreadFunc ) ) {
		// runs before Com_Init, and this thread is what would have displayed
		// a Com_Error/Sys_Error message in the first place
		MessageBox( NULL, "Failed to create the console window thread", "RTCW Error", MB_OK | MB_ICONERROR );
		exit( 1 );
	}
}


void WIN_StopSysconThread( qboolean forceExit ) {
	WIN_DestroyThread( &g_wv.sysconThread, forceExit );
}

static LONG WINAPI ConWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
	static qboolean s_timePolarity;
	int cx, cy;
	float sx;
	float x, y, w, h;

	switch ( uMsg )
	{
	case WM_SIZE:
		// NERVE - SMF
		cx = LOWORD( lParam );
		cy = HIWORD( lParam );

//		if ( cx < SYSCON_DEFAULT_WIDTH )
//			cx = SYSCON_DEFAULT_WIDTH;
//		if ( cy < SYSCON_DEFAULT_HEIGHT )
//			cy = SYSCON_DEFAULT_HEIGHT;

		sx = (float)cx / SYSCON_DEFAULT_WIDTH;


		x = 5;
		y = 40;
		w = cx - 15;
		h = cy - 100;
		SetWindowPos( s_wcd.hwndBuffer, NULL, x, y, w, h, 0 );

		y = y + h + 8;
		h = 20;
		SetWindowPos( s_wcd.hwndInputLine, NULL, x, y, w, h, 0 );

		y = y + h + 4;
		w = 72 * sx;
		h = 24;
		SetWindowPos( s_wcd.hwndButtonCopy, NULL, x, y, w, h, 0 );

		x = x + w + 2;
		SetWindowPos( s_wcd.hwndButtonClear, NULL, x, y, w, h, 0 );

		x = cx - 15 - w;
		SetWindowPos( s_wcd.hwndButtonQuit, NULL, x, y, w, h, 0 );

		s_wcd.windowWidth = cx;
		s_wcd.windowHeight = cy;
		// -NERVE - SMF
		break;
	case WM_ACTIVATE:
		if ( LOWORD( wParam ) != WA_INACTIVE ) {
			SetFocus( s_wcd.hwndInputLine );
		}

		if ( com_viewlog && ( com_dedicated && !com_dedicated->integer ) ) {
			// if the viewlog is open, check to see if it's being minimized
			if ( com_viewlog->integer == 1 ) {
				if ( HIWORD( wParam ) ) {   // minimized flag
					SysCon_QueueCommand( "viewlog 2" );
				}
			} else if ( com_viewlog->integer == 2 )   {
				if ( !HIWORD( wParam ) ) {      // minimized flag
					SysCon_QueueCommand( "viewlog 1" );
				}
			}
		}
		break;

	case WM_CLOSE:
		if ( ( com_dedicated && com_dedicated->integer ) ) {
			SysCon_QueueCommand( "quit" );
		} else if ( s_wcd.quitOnClose )   {
			PostQuitMessage( 0 );
		} else
		{
			Sys_ShowConsole( 0, qfalse );
			SysCon_QueueCommand( "viewlog 0" );
		}
		return 0;
	case WM_CTLCOLORSTATIC:
		if ( ( HWND ) lParam == s_wcd.hwndBuffer ) {
			SetBkColor( ( HDC ) wParam, RGB( 86, 117, 100 ) );
			SetTextColor( ( HDC ) wParam, RGB( 0xff, 0xff, 0xff ) );
			return (INT_PTR) s_wcd.hbrEditBackground;
		} else if ( ( HWND ) lParam == s_wcd.hwndErrorBox )   {
			if ( s_timePolarity & 1 ) {
				SetBkColor( ( HDC ) wParam, RGB( 0x80, 0x80, 0x80 ) );
				SetTextColor( ( HDC ) wParam, RGB( 0xff, 0x0, 0x00 ) );
			} else
			{
				SetBkColor( ( HDC ) wParam, RGB( 0x80, 0x80, 0x80 ) );
				SetTextColor( ( HDC ) wParam, RGB( 0x00, 0x0, 0x00 ) );
			}
			return (INT_PTR) s_wcd.hbrErrorBackground;
		}
		break;

	case WM_COMMAND:
		if ( wParam == COPY_ID ) {
			SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
			SendMessage( s_wcd.hwndBuffer, WM_COPY, 0, 0 );
		} else if ( wParam == QUIT_ID )   {
			if ( s_wcd.quitOnClose ) {
				PostQuitMessage( 0 );
			} else
			{
				SysCon_QueueCommand( "quit" );
			}
		} else if ( wParam == CLEAR_ID )   {
			SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
			SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, ( LPARAM ) "" );
			UpdateWindow( s_wcd.hwndBuffer );
		}
		break;
	case WM_CREATE:
//		s_wcd.hbmLogo = LoadBitmap( g_wv.hInstance, MAKEINTRESOURCE( IDB_BITMAP1 ) );
//		s_wcd.hbmClearBitmap = LoadBitmap( g_wv.hInstance, MAKEINTRESOURCE( IDB_BITMAP2 ) );
		s_wcd.hbrEditBackground = CreateSolidBrush( RGB( 86, 117, 100 ) );
		s_wcd.hbrErrorBackground = CreateSolidBrush( RGB( 0x80, 0x80, 0x80 ) );
		SetTimer( hWnd, 1, 1000, NULL );
		break;
	case WM_ERASEBKGND:
		return DefWindowProc( hWnd, uMsg, wParam, lParam );
	case WM_TIMER:
		if ( wParam == 1 ) {
			s_timePolarity = !s_timePolarity;
			if ( s_wcd.hwndErrorBox ) {
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}
		}
		break;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

LONG WINAPI InputLineWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
	char inputBuffer[1024];

	switch ( uMsg )
	{
	case WM_KILLFOCUS:
		if ( ( HWND ) wParam == s_wcd.hWnd ||
			 ( HWND ) wParam == s_wcd.hwndErrorBox ) {
			SetFocus( hWnd );
			return 0;
		}
		break;

	case WM_CHAR:
		if ( wParam == 13 ) {
			char echo[1100];

			GetWindowText( s_wcd.hwndInputLine, inputBuffer, sizeof( inputBuffer ) );
			SetWindowText( s_wcd.hwndInputLine, "" );

			// hand the line to the main thread the same way the quit/viewlog
			// buttons do -- nothing here is shared with it unsynchronized
			SysCon_QueueCommand( inputBuffer );

			// echoes locally rather than via the cross-thread Conbuf_AppendText;
			// Com_sprintf, since va()'s static buffer isn't thread-safe here
			Com_sprintf( echo, sizeof( echo ), "]%s\n", inputBuffer );
			Conbuf_AppendTextImpl( echo );

			return 0;
		}
	}

	return CallWindowProc( s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam );
}

/*
** Sys_CreateConsoleImpl
*/
static void Sys_CreateConsoleImpl( void ) {
	HDC hDC;
	WNDCLASS wc;
	RECT rect;
	const char *DEDCLASS = "Wolf WinConsole";

#ifdef UPDATE_SERVER        // DHM - Nerve
	const char *WINDOWNAME = "Wolf Update Server";
#else
	const char *WINDOWNAME = "Wolf Console";
#endif

	int nHeight;
	int swidth, sheight;
	int DEDSTYLE = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_SIZEBOX;

	memset( &wc, 0, sizeof( wc ) );

	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC) ConWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = g_wv.hInstance;
	wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE( IDI_ICON1 ) );
	wc.hCursor       = LoadCursor( NULL,IDC_ARROW );
	wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = DEDCLASS;

	if ( !RegisterClass( &wc ) ) {
		return;
	}

	rect.left = 0;
	rect.right = SYSCON_DEFAULT_WIDTH;
	rect.top = 0;
	rect.bottom = SYSCON_DEFAULT_HEIGHT;
	AdjustWindowRect( &rect, DEDSTYLE, FALSE );

	hDC = GetDC( GetDesktopWindow() );
	swidth = GetDeviceCaps( hDC, HORZRES );
	sheight = GetDeviceCaps( hDC, VERTRES );
	ReleaseDC( GetDesktopWindow(), hDC );

	s_wcd.windowWidth = rect.right - rect.left + 1;
	s_wcd.windowHeight = rect.bottom - rect.top + 1;

	s_wcd.hWnd = CreateWindowEx( 0,
								 DEDCLASS,
								 WINDOWNAME,
								 DEDSTYLE,
								 ( swidth - 600 ) / 2, ( sheight - 450 ) / 2, rect.right - rect.left + 1, rect.bottom - rect.top + 1,
								 NULL,
								 NULL,
								 g_wv.hInstance,
								 NULL );

	if ( s_wcd.hWnd == NULL ) {
		return;
	}

	//
	// create fonts
	//
	hDC = GetDC( s_wcd.hWnd );
	nHeight = -MulDiv( 8, GetDeviceCaps( hDC, LOGPIXELSY ), 72 );

	s_wcd.hfBufferFont = CreateFont( nHeight,
									 0,
									 0,
									 0,
									 FW_LIGHT,
									 0,
									 0,
									 0,
									 DEFAULT_CHARSET,
									 OUT_DEFAULT_PRECIS,
									 CLIP_DEFAULT_PRECIS,
									 DEFAULT_QUALITY,
									 FF_MODERN | FIXED_PITCH,
									 "Courier New" );

	ReleaseDC( s_wcd.hWnd, hDC );

	//
	// create the input line
	//
	s_wcd.hwndInputLine = CreateWindow( "edit", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
										ES_LEFT | ES_AUTOHSCROLL,
										6, 400, 528, 20,
										s_wcd.hWnd,
										( HMENU ) INPUT_ID,         // child window ID
										g_wv.hInstance, NULL );

	//
	// create the buttons
	//
	s_wcd.hwndButtonCopy = CreateWindow( "button", NULL, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
										 5, 425, 72, 24,
										 s_wcd.hWnd,
										 ( HMENU ) COPY_ID,         // child window ID
										 g_wv.hInstance, NULL );
	SendMessage( s_wcd.hwndButtonCopy, WM_SETTEXT, 0, ( LPARAM ) "copy" );

	s_wcd.hwndButtonClear = CreateWindow( "button", NULL, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
										  82, 425, 72, 24,
										  s_wcd.hWnd,
										  ( HMENU ) CLEAR_ID,       // child window ID
										  g_wv.hInstance, NULL );
	SendMessage( s_wcd.hwndButtonClear, WM_SETTEXT, 0, ( LPARAM ) "clear" );

	s_wcd.hwndButtonQuit = CreateWindow( "button", NULL, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
										 462, 425, 72, 24,
										 s_wcd.hWnd,
										 ( HMENU ) QUIT_ID,         // child window ID
										 g_wv.hInstance, NULL );
	SendMessage( s_wcd.hwndButtonQuit, WM_SETTEXT, 0, ( LPARAM ) "quit" );


	//
	// create the scrollbuffer
	//
	s_wcd.hwndBuffer = CreateWindow( "edit", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
									 ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
									 6, 40, 526, 354,
									 s_wcd.hWnd,
									 ( HMENU ) EDIT_ID,             // child window ID
									 g_wv.hInstance, NULL );
	SendMessage( s_wcd.hwndBuffer, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	s_wcd.SysInputLineWndProc = ( WNDPROC ) SetWindowLongPtr( s_wcd.hwndInputLine, GWLP_WNDPROC, (LONG_PTR) InputLineWndProc );
	SendMessage( s_wcd.hwndInputLine, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	ShowWindow( s_wcd.hWnd, SW_SHOWDEFAULT );
	UpdateWindow( s_wcd.hWnd );
	SetForegroundWindow( s_wcd.hWnd );
	SetFocus( s_wcd.hwndInputLine );

	s_wcd.visLevel = 1;
}

/*
** Sys_DestroyConsoleImpl
*/
static void Sys_DestroyConsoleImpl( void ) {
	if ( s_wcd.hWnd ) {
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		CloseWindow( s_wcd.hWnd );
		DestroyWindow( s_wcd.hWnd );
		s_wcd.hWnd = 0;
	}
}

/*
** Sys_DestroyConsole
*/
void Sys_DestroyConsole( qboolean waitForUser ) {
	WIN_StopSysconThread( !waitForUser );
}

/*
** Sys_ShowConsole
*/
void Sys_ShowConsole( int visLevel, qboolean quitOnClose ) {
	s_wcd.quitOnClose = quitOnClose;

	if ( visLevel == s_wcd.visLevel ) {
		return;
	}

	s_wcd.visLevel = visLevel;

	if ( !s_wcd.hWnd ) {
		return;
	}

	switch ( visLevel )
	{
	case 0:
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		break;
	case 1:
		ShowWindow( s_wcd.hWnd, SW_SHOWNORMAL );
		SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
		break;
	case 2:
		ShowWindow( s_wcd.hWnd, SW_MINIMIZE );
		break;
	default:
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
		break;
	}
}

/*
==================
WIN_ProcessConsoleWindowEvents

Drains command lines the syscon thread forwarded and queues them as
SE_CONSOLE events; called once per frame, independently of IN_Frame.
==================
*/
void WIN_ProcessConsoleWindowEvents( void ) {
	if ( g_wv.sysconThread.exitedEarly ) {
		Com_Error( ERR_FATAL, "The syscon thread exited early" );
	}

	{
		ringBufferIter_t iter;
		uint64_t i;

		WIN_BeginReading( &iter, &g_wv.conCmdBuffer.base );
		for ( i = iter.begin; i < iter.end; i++ ) {
			windowCommand_t *cmd = &g_wv.conCmdBuffer.commands[ i % g_wv.conCmdBuffer.base.size ];
			char text[SYSCON_MAX_TEXT_LEN];
			uint32_t len = cmd->dataLength < sizeof( text ) - 1 ? cmd->dataLength : sizeof( text ) - 1;
			uint32_t j;

			for ( j = 0; j < len; j++ ) {
				text[j] = g_wv.conCmdStringBuffer.data[ ( cmd->userData + j ) % g_wv.conCmdStringBuffer.base.size ];
			}
			text[len] = 0;

			if ( cmd->type == WCMD_PROCESS_LINE ) {
				char *b = CopyString( text );
				Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( b ) + 1, b );
			}

			g_wv.conCmdStringBuffer.base.readIndex = cmd->userData + cmd->dataLength;
		}
		WIN_EndReading( &iter );
	}
}

/*
** Conbuf_AppendTextImpl
*/
static void Conbuf_AppendTextImpl( const char *pMsg ) {
#define CONSOLE_BUFFER_SIZE     16384

	char buffer[CONSOLE_BUFFER_SIZE * 2];
	char *b = buffer;
	const char *msg;
	int bufLen;
	int i = 0;
	static unsigned long s_totalChars;

	//
	// if the message is REALLY long, use just the last portion of it
	//
	if ( strlen( pMsg ) > CONSOLE_BUFFER_SIZE - 1 ) {
		msg = pMsg + strlen( pMsg ) - CONSOLE_BUFFER_SIZE + 1;
	} else
	{
		msg = pMsg;
	}

	//
	// copy into an intermediate buffer
	//
	while ( msg[i] && ( ( b - buffer ) < sizeof( buffer ) - 1 ) )
	{
		if ( msg[i] == '\n' && msg[i + 1] == '\r' ) {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
			i++;
		} else if ( msg[i] == '\r' )     {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		} else if ( msg[i] == '\n' )     {
			b[0] = '\r';
			b[1] = '\n';
			b += 2;
		} else if ( Q_IsColorString( &msg[i] ) )   {
			i++;
		} else
		{
			*b = msg[i];
			b++;
		}
		i++;
	}
	*b = 0;
	bufLen = b - buffer;

	s_totalChars += bufLen;

	//
	// replace selection instead of appending if we're overflowing
	//
	if ( s_totalChars > CONSOLE_BUFFER_SIZE ) {
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
		s_totalChars = bufLen;
	} else {
		// NERVE - SMF - always append at the bottom of the textbox
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0xFFFF, 0xFFFF );
	}

	//
	// put this text into the windows console
	//
	SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
	SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM) buffer );
}

/*
** Sys_SetErrorTextImpl
*/
static void Sys_SetErrorTextImpl( const char *buf ) {
	Q_strncpyz( s_wcd.errorString, buf, sizeof( s_wcd.errorString ) );

	if ( !s_wcd.hwndErrorBox ) {
		s_wcd.hwndErrorBox = CreateWindow( "static", NULL, WS_CHILD | WS_VISIBLE | SS_SUNKEN,
										   6, 5, 526, 30,
										   s_wcd.hWnd,
										   ( HMENU ) ERRORBOX_ID,           // child window ID
										   g_wv.hInstance, NULL );
		SendMessage( s_wcd.hwndErrorBox, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
		SetWindowText( s_wcd.hwndErrorBox, s_wcd.errorString );

		DestroyWindow( s_wcd.hwndInputLine );
		s_wcd.hwndInputLine = NULL;
	}
}

/*
** Conbuf_AppendText
*/
void Conbuf_AppendText( const char *pMsg ) {
	uint32_t len = (uint32_t)strlen( pMsg );
	uint64_t offset = WIN_PushString( &g_wv.mainCmdStringBuffer, pMsg, len );
	WIN_PushWindowCommand( &g_wv.mainCmdBuffer, WCMD_APPEND_LINE, offset, len );
}

/*
** Sys_SetErrorText
*/
void Sys_SetErrorText( const char *buf ) {
	uint32_t len = (uint32_t)strlen( buf );
	uint64_t offset = WIN_PushString( &g_wv.mainCmdStringBuffer, buf, len );
	WIN_PushWindowCommand( &g_wv.mainCmdBuffer, WCMD_SET_ERROR_TEXT, offset, len );
}
