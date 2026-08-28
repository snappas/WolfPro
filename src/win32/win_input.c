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

// win_input.c -- win32 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.

#include "../client/client.h"
#include "win_local.h"

// WinUser.h's NEXTRAWINPUTBLOCK/RAWINPUT_ALIGN macros need QWORD on x64, but
// no header this project already includes defines it.
#ifndef QWORD
typedef unsigned __int64 QWORD;
#endif

// RTCWPro - raw input begin - source: quake
// defines
#define MAX_RI_DEVICE_SIZE 128

#define RI_RAWBUTTON_MASK 0x000003E0
#define RI_INVALID_POS    0x80000000

// Under WOW64, GetRawInputBuffer uses the 64-bit (24-byte) RAWINPUTHEADER,
// so native RAWINPUT* misreads dwType/hDevice
#pragma pack(push, 8)
typedef struct {
	DWORD  dwType;
	DWORD  dwSize;
	UINT64 hDevice;
	UINT64 wParam;
} wow64RawInputHeader_t;

typedef struct {
	wow64RawInputHeader_t header;
	union {
		RAWMOUSE    mouse;
		RAWKEYBOARD keyboard;
		RAWHID      hid;
	} data;
} wow64RawInput_t;
#pragma pack(pop)

static qboolean s_isWow64;

// raw input dynamic functions
typedef int 	(WINAPI* pGetRawInputDeviceList)	(OUT PRAWINPUTDEVICELIST pRawInputDeviceList, IN OUT PINT puiNumDevices, IN UINT cbSize);
typedef int 	(WINAPI* pGetRawInputDeviceInfoA)	(IN HANDLE hDevice, IN UINT uiCommand, OUT LPVOID pData, IN OUT PINT pcbSize);
typedef BOOL(WINAPI* pRegisterRawInputDevices)	(IN PCRAWINPUTDEVICE pRawInputDevices, IN UINT uiNumDevices, IN UINT cbSize);

pGetRawInputDeviceList		_GRIDL;
pGetRawInputDeviceInfoA		_GRIDIA;
pRegisterRawInputDevices	_RRID;

typedef struct
{
	HANDLE			rawinputhandle; // raw input, identify particular mice

	int				numbuttons;
	volatile int	buttons;

	volatile int	delta[2];
	int				pos[2];

} rawmouse_t;

rawmouse_t* rawmice;

static int	rawmicecount;

void		IN_DeRegisterRawMouse(void);
// raw input end

// volatile: IN_MouseEvent can touch these from the window thread (only when
// raw-mouse registration failed, so never concurrently with raw-mouse reads).
typedef struct {
	volatile int oldButtonState;

	volatile qboolean mouseActive;
	volatile qboolean mouseInitialized;
} WinMouseVars_t;

static WinMouseVars_t s_wmv;

static int window_center_x, window_center_y;

//
// MIDI definitions
//
static void IN_StartupMIDI( void );
static void IN_ShutdownMIDI( void );

#define MAX_MIDIIN_DEVICES  8

typedef struct {
	int numDevices;
	MIDIINCAPS caps[MAX_MIDIIN_DEVICES];

	HMIDIIN hMidiIn;
} MidiInfo_t;

static MidiInfo_t s_midiInfo;

//
// Joystick definitions
//
#define JOY_MAX_AXES        6               // X, Y, Z, R, U, V

typedef struct {
	qboolean avail;
	int id;                 // joystick number
	JOYCAPS jc;

	int oldbuttonstate;
	int oldpovstate;

	JOYINFOEX ji;
} joystickInfo_t;

static joystickInfo_t joy;

cvar_t  *in_midi;
cvar_t  *in_midiport;
cvar_t  *in_midichannel;
cvar_t  *in_mididevice;

cvar_t  *in_joystick;
cvar_t  *in_joyBallScale;
cvar_t  *in_debugJoystick;
cvar_t  *joy_threshold;

static cvar_t *in_raw; // 0 = legacy Win32 input, 1 = raw input (mouse + keyboard)

qboolean in_appactive;

// forward-referenced functions
void IN_StartupJoystick( void );
void IN_JoyMove( void );

static void MidiInfo_f( void );

// RTCWPro - raw input begin
/*
=========================================================================

Raw Mouse Input

=========================================================================
*/

void IN_ShutdownRawMouse(void) {
	if (rawmicecount < 1)
		return;

	IN_DeRegisterRawMouse();

	Z_Free(rawmice);

	rawmice = NULL;

	rawmicecount = 0;
}

void IN_DeRegisterRawMouse(void)
{
	RAWINPUTDEVICE Rid;

	if (rawmicecount < 1)
		return;

	// deregister raw input
	Rid.usUsagePage = 0x01;
	Rid.usUsage = 0x02;
	Rid.dwFlags = RIDEV_REMOVE;
	Rid.hwndTarget = NULL;

	(*_RRID)(&Rid, 1, sizeof(Rid));
}

int IN_RawInput_IsRDPMouse(char* cDeviceString)
{
	char cRDPString[] = "\\??\\Root#RDP_MOU#";
	int i;

	if (strlen(cDeviceString) < strlen(cRDPString)) {
		return 0;
	}

	for (i = strlen(cRDPString) - 1; i >= 0; i--)
	{
		if (cDeviceString[i] != cRDPString[i])
			return 0;
	}

	return 1; // is RDP mouse
}

void IN_RawMouse(int* mx, int* my) {
	int x;

	*mx = *my = 0;

	for (x = 0; x < rawmicecount; x++)
	{
		*mx += rawmice[x].delta[0];
		*my += rawmice[x].delta[1];

		rawmice[x].delta[0] = rawmice[x].delta[1] = 0;
	}
}

qboolean IN_InitRawMouse(void)
{
	// "0" to exclude, "1" to include
	PRAWINPUTDEVICELIST pRawInputDeviceList;
	int inputdevices, i, j, mtemp;
	char dname[MAX_RI_DEVICE_SIZE];

	// Return 0 if rawinput is not available
	HMODULE user32 = LoadLibrary("user32.dll");
	if (!user32)
	{
		Com_Printf("Raw input: unable to load user32.dll\n");
		return qfalse;
	}
	_RRID = (pRegisterRawInputDevices)GetProcAddress(user32, "RegisterRawInputDevices");
	if (!_RRID)
	{
		Com_Printf("Raw input: function RegisterRawInputDevices could not be registered\n");
		return qfalse;
	}
	_GRIDL = (pGetRawInputDeviceList)GetProcAddress(user32, "GetRawInputDeviceList");
	if (!_GRIDL)
	{
		Com_Printf("Raw input: function GetRawInputDeviceList could not be registered\n");
		return qfalse;
	}
	_GRIDIA = (pGetRawInputDeviceInfoA)GetProcAddress(user32, "GetRawInputDeviceInfoA");
	if (!_GRIDIA)
	{
		Com_Printf("Raw input: function GetRawInputDeviceInfoA could not be registered\n");
		return qfalse;
	}

	// 1st call to GetRawInputDeviceList: Pass NULL to get the number of devices.
	if ((*_GRIDL)(NULL, &inputdevices, sizeof(RAWINPUTDEVICELIST)) != 0)
	{
		Com_Printf("Raw input: unable to count raw input devices\n");
		return qfalse;
	}

	// Allocate the array to hold the DeviceList
	pRawInputDeviceList = Z_Malloc(sizeof(RAWINPUTDEVICELIST) * inputdevices);

	// 2nd call to GetRawInputDeviceList: Pass the pointer to our DeviceList and GetRawInputDeviceList() will fill the array
	if ((*_GRIDL)(pRawInputDeviceList, &inputdevices, sizeof(RAWINPUTDEVICELIST)) == -1)
	{
		Com_Printf("Raw input: unable to get raw input device list\n");
		return qfalse;
	}

	// Loop through all devices and count the mice
	for (i = 0, mtemp = 0; i < inputdevices; i++)
	{
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE)
		{
			j = MAX_RI_DEVICE_SIZE;

			// Get the device name and use it to determine if it's the RDP Terminal Services virtual device.
			if ((*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, dname, &j) < 0)
				dname[0] = 0;

			if (IN_RawInput_IsRDPMouse(dname)) // ignore rdp mouse
				continue;

			// advance temp device count
			mtemp++;
		}
	}

	// exit out if no devices found
	if (!mtemp)
	{
		Com_Printf("Raw input: no usable device found\n");
		return qfalse;
	}

	// Loop again and bind devices
	rawmice = Z_Malloc(sizeof(rawmouse_t) * mtemp);
	for (i = 0; i < inputdevices; i++)
	{
		if (pRawInputDeviceList[i].dwType == RIM_TYPEMOUSE)
		{
			j = MAX_RI_DEVICE_SIZE;

			// Get the device name and use it to determine if it's the RDP Terminal Services virtual device.
			if ((*_GRIDIA)(pRawInputDeviceList[i].hDevice, RIDI_DEVICENAME, dname, &j) < 0)
				dname[0] = 0;

			if (IN_RawInput_IsRDPMouse(dname)) // ignore rdp mouse
				continue;

			// print pretty message about the mouse
			dname[MAX_RI_DEVICE_SIZE - 1] = 0;
			for (mtemp = strlen(dname); mtemp >= 0; mtemp--)
			{
				if (dname[mtemp] == '#')
				{
					dname[mtemp + 1] = 0;
					break;
				}
			}
			Com_Printf("Raw input: [%i] %s\n", i, dname);

			// set handle
			rawmice[rawmicecount].rawinputhandle = pRawInputDeviceList[i].hDevice;
			rawmice[rawmicecount].numbuttons = 10;
			rawmice[rawmicecount].pos[0] = RI_INVALID_POS;
			rawmicecount++;
		}
	}


	// free the RAWINPUTDEVICELIST
	Z_Free(pRawInputDeviceList);

	Com_Printf("Raw input: initialized with %i mice\n", rawmicecount);

	return qtrue; // success
}

//================================
// raw input read functions
//================================

/*
==================
IT_ProcessRawMouse

Pushes button/wheel events into the ring buffer; movement deltas accumulate
into rawmice[], read back by the main thread each frame in IN_Frame.
==================
*/
static void IT_ProcessRawMouse( HANDLE hDevice, RAWMOUSE *mouse, uint64_t *writeIndex, int timestamp ) {
	int i, j, tbuttons;

	for ( i = 0; i < rawmicecount; i++ ) {
		if ( rawmice[i].rawinputhandle == hDevice )
			break;
	}
	if ( i == rawmicecount ) // we're not tracking this mouse
		return;

	// movement
	if ( mouse->usFlags & MOUSE_MOVE_ABSOLUTE ) {
		if ( rawmice[i].pos[0] != RI_INVALID_POS ) {
			rawmice[i].delta[0] += mouse->lLastX - rawmice[i].pos[0];
			rawmice[i].delta[1] += mouse->lLastY - rawmice[i].pos[1];
		}
		rawmice[i].pos[0] = mouse->lLastX;
		rawmice[i].pos[1] = mouse->lLastY;
	} else { // RELATIVE
		rawmice[i].delta[0] += mouse->lLastX;
		rawmice[i].delta[1] += mouse->lLastY;
		rawmice[i].pos[0] = RI_INVALID_POS;
	}

	// buttons
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_1_DOWN )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE1, qtrue );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_1_UP )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE1, qfalse );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_2_DOWN )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE2, qtrue );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_2_UP )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE2, qfalse );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_3_DOWN )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE3, qtrue );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_3_UP )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE3, qfalse );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_4_DOWN )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE4, qtrue );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_4_UP )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE4, qfalse );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_5_DOWN )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE5, qtrue );
	if ( mouse->usButtonFlags & RI_MOUSE_BUTTON_5_UP )
		WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE5, qfalse );

	// mouse wheel
	if ( mouse->usButtonFlags & RI_MOUSE_WHEEL ) {
		if ( (SHORT)mouse->usButtonData > 0 ) {
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MWHEELUP, qtrue );
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MWHEELUP, qfalse );
		}
		if ( (SHORT)mouse->usButtonData < 0 ) {
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MWHEELDOWN, qtrue );
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MWHEELDOWN, qfalse );
		}
	}

	// extra buttons
	tbuttons = mouse->ulRawButtons & RI_RAWBUTTON_MASK;
	for ( j = 6; j < rawmice[i].numbuttons; j++ ) {
		if ( ( tbuttons & ( 1 << j ) ) && !( rawmice[i].buttons & ( 1 << j ) ) )
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE1 + j, qtrue );
		if ( !( tbuttons & ( 1 << j ) ) && ( rawmice[i].buttons & ( 1 << j ) ) )
			WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, K_MOUSE1 + j, qfalse );
	}

	rawmice[i].buttons &= ~RI_RAWBUTTON_MASK;
	rawmice[i].buttons |= tbuttons;
}


/*
==================
IT_ProcessRawInput

Runs on the input thread. Every event in the batch shares one timeGetTime()
stamp -- the same clock Sys_GetCheapEvent uses.
==================
*/
static void IT_ProcessRawInput( void *inputs, UINT count, uint64_t *writeIndex ) {
	UINT i;
	BYTE *cursor = (BYTE *)inputs;
	int timestamp = (int)timeGetTime();

	for ( i = 0; i < count; i++ ) {
		DWORD dwType, dwSize;
		HANDLE hDevice;
		void *payload;

		if ( s_isWow64 ) {
			wow64RawInput_t *input = (wow64RawInput_t *)cursor;
			dwType = input->header.dwType;
			dwSize = input->header.dwSize;
			hDevice = (HANDLE)(ULONG_PTR)input->header.hDevice;
			payload = &input->data;
			cursor += ( dwSize + 7 ) & ~(DWORD)7; // 8-byte alignment, per MS's WOW64 remark on GetRawInputBuffer
		} else {
			RAWINPUT *input = (RAWINPUT *)cursor;
			dwType = input->header.dwType;
			dwSize = input->header.dwSize;
			hDevice = input->header.hDevice;
			payload = &input->data;
			cursor = (BYTE *)NEXTRAWINPUTBLOCK( input );
		}

		if ( dwType == RIM_TYPEMOUSE ) {
			IT_ProcessRawMouse( hDevice, (RAWMOUSE *)payload, writeIndex, timestamp );
		} else if ( dwType == RIM_TYPEKEYBOARD ) {
			RAWKEYBOARD *kb = (RAWKEYBOARD *)payload;
			qboolean isUp;

			// KEYBOARD_OVERRUN_MAKE_CODE means an invalid/unrecognizable key or a
			// rollover-limit overrun -- there's no real key event to extract.
			if ( kb->MakeCode == KEYBOARD_OVERRUN_MAKE_CODE || kb->VKey >= UCHAR_MAX ) {
				continue;
			}

			isUp = ( kb->Flags & RI_KEY_BREAK ) ? qtrue : qfalse;

			// Alt+Enter (fullscreen toggle) bypasses the window proc under raw input,
			// so drop it here too; GetAsyncKeyState avoids desync from a missed event.
			if ( !isUp && kb->VKey == VK_RETURN && ( GetAsyncKeyState( VK_LMENU ) & 0x8000 ) ) {
				continue;
			}

			{
				qboolean isExtended = ( kb->Flags & ( RI_KEY_E0 | RI_KEY_E1 ) ) ? qtrue : qfalse;
				int key = IN_GetQuakeKey( kb->VKey, kb->MakeCode, isExtended );
				if ( key != 0 ) {
					WIN_PushInputEventB( &g_wv.inputThreadBuffer, writeIndex, timestamp, SE_KEY, key, !isUp );
				}
			}
		}
	}
}


/*
==================
IT_ThreadFunc
==================
*/
static void IT_ThreadFunc( thread_t *thread ) {
	HWND hwnd;
	RAWINPUTDEVICE rid[2];
	// sized for the larger of the two entry shapes (wow64RawInput_t) so the
	// same buffer works whether or not this process is running under WOW64
	BYTE inputBuf[1024 * sizeof( wow64RawInput_t )];
	UINT headerSize;
	BOOL wow64 = FALSE;

	PROF_InitThread( "Input" );

	IsWow64Process( GetCurrentProcess(), &wow64 );
	s_isWow64 = wow64 ? qtrue : qfalse;
	// cbSizeHeader must stay the native size even under WOW64 -- passing 24
	// here makes GetRawInputBuffer fail outright; only interpretation changes.
	headerSize = sizeof( RAWINPUTHEADER );

	hwnd = CreateWindowEx( 0, "Message", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_wv.hInstance, NULL );

	// no RIDEV_INPUTSINK, matching cnq3: raw input only arrives while
	// foreground, same as the legacy WM_KEYDOWN/WM_MOUSEMOVE fallback.
	rid[0].usUsagePage = 1; rid[0].usUsage = 2; // mouse
	rid[0].dwFlags = 0;
	rid[0].hwndTarget = hwnd;

	rid[1].usUsagePage = 1; rid[1].usUsage = 6; // keyboard
	rid[1].dwFlags = 0;
	rid[1].hwndTarget = hwnd;

	if ( !RegisterRawInputDevices( rid, 2, sizeof( rid[0] ) ) ) {
		// Com_Error here would deadlock (IN_Shutdown -> WIN_StopInputThread
		// would wait on this very thread) -- exitedEarly reports it instead.
		DestroyWindow( hwnd );
		PROF_ShutdownThread();
		thread->exitedEarly = qtrue;
		SetEvent( thread->initDoneEvent );
		return;
	}

	g_wv.inputThreadReady = qtrue;
	SetEvent( thread->initDoneEvent );

	while ( !thread->stopRequested ) {
		DWORD wait = MsgWaitForMultipleObjects( 0, NULL, FALSE, 50, QS_RAWINPUT );

		if ( wait == WAIT_OBJECT_0 ) {
			// resets QS_RAWINPUT's "new" bit so the wait blocks again next time;
			// unlike PeekMessage it discards nothing, so it can't race GetRawInputBuffer.
			GetQueueStatus( QS_RAWINPUT );

			PROF_BEGIN( "GetRawInputBuffer" );

			UINT size = sizeof( inputBuf );
			UINT count = GetRawInputBuffer( (PRAWINPUT)inputBuf, &size, headerSize );

			if ( count != (UINT)-1 && count > 0 ) {
				uint64_t writeIndex = g_wv.inputThreadBuffer.base.writeIndex;
				IT_ProcessRawInput( inputBuf, count, &writeIndex );
				WIN_FlushInputEvents( &g_wv.inputThreadBuffer, writeIndex );
			}

			PROF_END();
		}
	}

	DestroyWindow( hwnd );
	PROF_ShutdownThread();
}


void WIN_StartInputThread( void ) {
	if ( !WIN_CreateThread( &g_wv.inputThread, IT_ThreadFunc ) ) {
		Com_Error( ERR_FATAL, "Failed to create the input thread" );
	}
}


void WIN_StopInputThread( qboolean forceExit ) {
	WIN_DestroyThread( &g_wv.inputThread, forceExit );
	// falls back to legacy WM_KEYDOWN/UP until the next WIN_StartInputThread
	g_wv.inputThreadReady = qfalse;
}
// raw input end

/*
============================================================

WIN32 MOUSE CONTROL

============================================================
*/

/*
================
IN_ActivateWin32Mouse
================
*/
void IN_ActivateWin32Mouse( void ) {
	int width, height;
	RECT window_rect;

	width = GetSystemMetrics( SM_CXSCREEN );
	height = GetSystemMetrics( SM_CYSCREEN );

	GetWindowRect( g_wv.hWnd, &window_rect );
	if ( window_rect.left < 0 ) {
		window_rect.left = 0;
	}
	if ( window_rect.top < 0 ) {
		window_rect.top = 0;
	}
	if ( window_rect.right >= width ) {
		window_rect.right = width - 1;
	}
	if ( window_rect.bottom >= height - 1 ) {
		window_rect.bottom = height - 1;
	}
	window_center_x = ( window_rect.right + window_rect.left ) / 2;
	window_center_y = ( window_rect.top + window_rect.bottom ) / 2;

	SetCursorPos( window_center_x, window_center_y );

	// SetCapture/ShowCursor only work on the thread that owns the window, so
	// they're bridged to the window thread rather than called from here
	if ( g_wv.hWnd ) {
		SendMessage( g_wv.hWnd, WM_SETMOUSECAPTURE, TRUE, 0 );
	}
	// NERVE - SMF - dont do this in developer mode
	if ( !com_developer->integer ) {
		ClipCursor( &window_rect );
	}
	if ( g_wv.hWnd ) {
		SendMessage( g_wv.hWnd, WM_SETCURSORVIS, FALSE, 0 );
	}
}

/*
================
IN_DeactivateWin32Mouse
================
*/
void IN_DeactivateWin32Mouse( void ) {
	// NERVE - SMF - dont do this in developer mode
	if ( !com_developer->integer ) {
		ClipCursor( NULL );
	}
	if ( g_wv.hWnd ) {
		SendMessage( g_wv.hWnd, WM_SETMOUSECAPTURE, FALSE, 0 );
		SendMessage( g_wv.hWnd, WM_SETCURSORVIS, TRUE, 0 );
	}
}

/*
============================================================

  MOUSE CONTROL

============================================================
*/

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
void IN_ActivateMouse( void ) {
	if ( !s_wmv.mouseInitialized ) {
		return;
	}
	if ( s_wmv.mouseActive ) {
		return;
	}

	s_wmv.mouseActive = qtrue;

	IN_ActivateWin32Mouse();
}


/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
void IN_DeactivateMouse( void ) {
	if ( !s_wmv.mouseInitialized ) {
		return;
	}
	if ( !s_wmv.mouseActive ) {
		return;
	}
	s_wmv.mouseActive = qfalse;

	IN_DeactivateWin32Mouse();
}

/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse( void ) {
	s_wmv.mouseInitialized = qtrue;

	if ( !in_raw->integer ) {
		g_wv.rawInput = qfalse;
		return;
	}

	if ( !IN_InitRawMouse() ) {
		Com_Printf( "Raw input: unable to register raw input\n" );
		IN_ShutdownRawMouse();
	}

	g_wv.rawInput = ( rawmicecount > 0 );
}

/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent( int mstate ) {
	int i;

	if ( !s_wmv.mouseInitialized ) {
		return;
	}

// perform button actions
	//for  ( i = 0 ; i < 3 ; i++ )
	for (i = 0; i < 5; i++) // RTCWPro - increase for extra buttons
	{
		if ( ( mstate & ( 1 << i ) ) &&
			 !( s_wmv.oldButtonState & ( 1 << i ) ) ) {
			WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + i, qtrue );
		}

		if ( !( mstate & ( 1 << i ) ) &&
			 ( s_wmv.oldButtonState & ( 1 << i ) ) ) {
			WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_KEY, K_MOUSE1 + i, qfalse );
		}
	}

	s_wmv.oldButtonState = mstate;
}

/*
===========
IN_LegacyMouseMove

WM_MOUSEMOVE handler for the !in_raw fallback -- the cursor is pinned to
window_center_x/y while active, so its drift from center each message is
the delta; SetCursorPos then pulls it back to center for the next one.
===========
*/
void IN_LegacyMouseMove( void ) {
	POINT pos;
	int mx, my;

	if ( !s_wmv.mouseActive ) {
		return;
	}

	GetCursorPos( &pos );
	mx = pos.x - window_center_x;
	my = pos.y - window_center_y;

	if ( mx || my ) {
		WIN_PushInputEvent( &g_wv.legacyInputBuffer, g_wv.sysMsgTime, SE_MOUSE, mx, my );
		SetCursorPos( window_center_x, window_center_y );
	}
}

/*
=========================================================================

=========================================================================
*/

/*
===========
IN_Startup
===========
*/
void IN_Startup( void ) {

	// TODO: no joystick support yet...


//	Com_Printf ("\n------- Input Initialization -------\n");
	IN_StartupMouse();
	IN_StartupJoystick();
	IN_StartupMIDI();
//	Com_Printf ("------------------------------------\n");

	in_joystick->modified = qfalse;
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown( void ) {
	WIN_StopInputThread( qtrue );
	IN_DeactivateMouse();
	IN_ShutdownRawMouse(); // RTCWPro - raw input
	IN_ShutdownMIDI();
	Cmd_RemoveCommand( "midiinfo" );
}


/*
===========
IN_Init
===========
*/
void IN_Init( void ) {
	// MIDI input controler variables
	in_midi                 = Cvar_Get( "in_midi",                   "0",     CVAR_ARCHIVE );
	in_midiport             = Cvar_Get( "in_midiport",               "1",     CVAR_ARCHIVE );
	in_midichannel          = Cvar_Get( "in_midichannel",            "1",     CVAR_ARCHIVE );
	in_mididevice           = Cvar_Get( "in_mididevice",         "0",     CVAR_ARCHIVE );

	Cmd_AddCommand( "midiinfo", MidiInfo_f );

	// joystick variables
	in_joystick             = Cvar_Get( "in_joystick",               "0",     CVAR_ARCHIVE | CVAR_LATCH );
	in_joyBallScale         = Cvar_Get( "in_joyBallScale",           "0.02",      CVAR_ARCHIVE );
	in_debugJoystick        = Cvar_Get( "in_debugjoystick",          "0",     CVAR_TEMP );

	joy_threshold           = Cvar_Get( "joy_threshold",         "0.15",      CVAR_ARCHIVE );

	in_raw                  = Cvar_Get( "in_raw",                    "1",     CVAR_ARCHIVE | CVAR_LATCH );

	IN_Startup();

	if ( in_raw->integer ) {
		WIN_StartInputThread();
	}

	Com_Printf( g_wv.inputThreadReady ? "Using raw keyboard/mouse input\n" : "Using Win32 keyboard/mouse input\n" );
}


/*
===========
IN_Activate

Called when the main window gains or loses focus.
The window may have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void IN_Activate( qboolean active ) {
	in_appactive = active;

	if ( !active ) {
		IN_DeactivateMouse();
	}
}


/*
==================
IN_MouseSamplingSuspended

True when mouse look/capture shouldn't run this frame (unfocused, or
console/imgui has the mouse) -- shared with IN_DrainInputBuffers' delta reset.
==================
*/
static qboolean IN_MouseSamplingSuspended( void ) {
	qbool isFullscreen = Cvar_VariableIntegerValue( "r_fullscreen" ) != 0;
	qbool releaseFullscreen = isFullscreen && ( cls.keyCatchers & KEYCATCH_IMGUI );
	qbool releaseWindowed = !isFullscreen && ( cls.keyCatchers & ( KEYCATCH_CONSOLE | KEYCATCH_IMGUI ) );
	return !in_appactive || releaseWindowed || releaseFullscreen;
}


/*
==================
IN_DrainInputBuffers

Events are discarded here on plain !in_appactive, guarding against a few
trailing events still sitting in the ring buffer from just before focus was
lost -- deliberately narrower than IN_MouseSamplingSuspended() below, which
would also block console/imgui typing. rawmice[] deltas reset on that
broader gate instead, to avoid a backlog dumping as one SE_MOUSE jump.
==================
*/
static void IN_DrainInputBuffers( void ) {
	ringBufferIter_t iter;
	uint64_t i;

	WIN_BeginReading( &iter, &g_wv.inputThreadBuffer.base );
	for ( i = iter.begin; i < iter.end; i++ ) {
		inputEvent_t *ev = &g_wv.inputThreadBuffer.inputs[ i % g_wv.inputThreadBuffer.base.size ];
		if ( in_appactive ) {
			Sys_QueEvent( ev->timestamp, ev->event, ev->arg1, ev->arg2, 0, NULL );
		}
	}
	WIN_EndReading( &iter );

	WIN_BeginReading( &iter, &g_wv.legacyInputBuffer.base );
	for ( i = iter.begin; i < iter.end; i++ ) {
		inputEvent_t *ev = &g_wv.legacyInputBuffer.inputs[ i % g_wv.legacyInputBuffer.base.size ];
		if ( in_appactive ) {
			Sys_QueEvent( ev->timestamp, ev->event, ev->arg1, ev->arg2, 0, NULL );
		}
	}
	WIN_EndReading( &iter );

	if ( IN_MouseSamplingSuspended() ) {
		int x;
		for ( x = 0; x < rawmicecount; x++ ) {
			rawmice[x].delta[0] = rawmice[x].delta[1] = 0;
		}
	}

	if ( g_wv.inputThread.exitedEarly ) {
		Com_Error( ERR_FATAL, "The input thread exited early" );
	}
}


/*
==================
IN_Frame

Called every frame, even if not generating commands
==================
*/
void IN_Frame( void ) {
	WIN_ProcessMainWindowEvents();
	IN_DrainInputBuffers();

	// post joystick events
	IN_JoyMove();

	if ( !s_wmv.mouseInitialized ) {
		return;
	}

	if ( IN_MouseSamplingSuspended() ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();

	// post events to the system que
	if ( g_wv.rawInput ) {
		int mx, my;
		IN_RawMouse( &mx, &my );
		if ( mx || my ) {
			Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
		}
	}
}


/*
===================
IN_ClearStates
===================
*/
void IN_ClearStates( void ) {
	s_wmv.oldButtonState = 0;
}


/*
=========================================================================

JOYSTICK

=========================================================================
*/

/*
===============
IN_StartupJoystick
===============
*/
void IN_StartupJoystick( void ) {
	int numdevs;
	MMRESULT mmr;

	// assume no joystick
	joy.avail = qfalse;

	if ( !in_joystick->integer ) {
		Com_DPrintf( "Joystick is not active.\n" );
		return;
	}

	// verify joystick driver is present
	if ( ( numdevs = joyGetNumDevs() ) == 0 ) {
		Com_DPrintf( "joystick not found -- driver not present\n" );
		return;
	}

	// cycle through the joystick ids for the first valid one
	mmr = 0;
	for ( joy.id = 0 ; joy.id < numdevs ; joy.id++ )
	{
		memset( &joy.ji, 0, sizeof( joy.ji ) );
		joy.ji.dwSize = sizeof( joy.ji );
		joy.ji.dwFlags = JOY_RETURNCENTERED;

		if ( ( mmr = joyGetPosEx( joy.id, &joy.ji ) ) == JOYERR_NOERROR ) {
			break;
		}
	}

	// abort startup if we didn't find a valid joystick
	if ( mmr != JOYERR_NOERROR ) {
		Com_Printf( "joystick not found -- no valid joysticks (%x)\n", mmr );
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	memset( &joy.jc, 0, sizeof( joy.jc ) );
	if ( ( mmr = joyGetDevCaps( joy.id, &joy.jc, sizeof( joy.jc ) ) ) != JOYERR_NOERROR ) {
		Com_Printf( "joystick not found -- invalid joystick capabilities (%x)\n", mmr );
		return;
	}

	Com_Printf( "Joystick found.\n" );
	Com_Printf( "Pname: %s\n", joy.jc.szPname );
	Com_Printf( "OemVxD: %s\n", joy.jc.szOEMVxD );
	Com_Printf( "RegKey: %s\n", joy.jc.szRegKey );

	Com_Printf( "Numbuttons: %i / %i\n", joy.jc.wNumButtons, joy.jc.wMaxButtons );
	Com_Printf( "Axis: %i / %i\n", joy.jc.wNumAxes, joy.jc.wMaxAxes );
	Com_Printf( "Caps: 0x%x\n", joy.jc.wCaps );
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		Com_Printf( "HASPOV\n" );
	} else {
		Com_Printf( "no POV\n" );
	}

	// old button and POV states default to no buttons pressed
	joy.oldbuttonstate = 0;
	joy.oldpovstate = 0;

	// mark the joystick as available
	joy.avail = qtrue;
}

/*
===========
JoyToF
===========
*/
float JoyToF( int value ) {
	float fValue;

	// move centerpoint to zero
	value -= 32768;

	// convert range from -32768..32767 to -1..1
	fValue = (float)value / 32768.0;

	if ( fValue < -1 ) {
		fValue = -1;
	}
	if ( fValue > 1 ) {
		fValue = 1;
	}
	return fValue;
}

int JoyToI( int value ) {
	// move centerpoint to zero
	value -= 32768;

	return value;
}

int joyDirectionKeys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY16, K_JOY17,
	K_JOY18, K_JOY19,
	K_JOY20, K_JOY21,
	K_JOY22, K_JOY23,

	K_JOY24, K_JOY25,
	K_JOY26, K_JOY27
};

/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove( void ) {
	float fAxisValue;
	int i;
	DWORD buttonstate, povstate;
	int x, y;

	// verify joystick is available and that the user wants to use it
	if ( !joy.avail ) {
		return;
	}

	// collect the joystick data, if possible
	memset( &joy.ji, 0, sizeof( joy.ji ) );
	joy.ji.dwSize = sizeof( joy.ji );
	joy.ji.dwFlags = JOY_RETURNALL;

	if ( joyGetPosEx( joy.id, &joy.ji ) != JOYERR_NOERROR ) {
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,\
		//      // but what should be done?
		// Com_Printf ("IN_ReadJoystick: no response\n");
		// joy.avail = false;
		return;
	}

	if ( in_debugJoystick->integer ) {
		Com_Printf( "%8x %5i %5.2f %5.2f %5.2f %5.2f %6i %6i\n",
					joy.ji.dwButtons,
					joy.ji.dwPOV,
					JoyToF( joy.ji.dwXpos ), JoyToF( joy.ji.dwYpos ),
					JoyToF( joy.ji.dwZpos ), JoyToF( joy.ji.dwRpos ),
					JoyToI( joy.ji.dwUpos ), JoyToI( joy.ji.dwVpos ) );
	}

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = joy.ji.dwButtons;
	for ( i = 0 ; i < joy.jc.wNumButtons ; i++ ) {
		if ( ( buttonstate & ( 1 << i ) ) && !( joy.oldbuttonstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qtrue, 0, NULL );
		}
		if ( !( buttonstate & ( 1 << i ) ) && ( joy.oldbuttonstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qfalse, 0, NULL );
		}
	}
	joy.oldbuttonstate = buttonstate;

	povstate = 0;

	// convert main joystick motion into 6 direction button bits
	for ( i = 0; i < joy.jc.wNumAxes && i < 4 ; i++ ) {
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = JoyToF( ( &joy.ji.dwXpos )[i] );

		if ( fAxisValue < -joy_threshold->value ) {
			povstate |= ( 1 << ( i * 2 ) );
		} else if ( fAxisValue > joy_threshold->value ) {
			povstate |= ( 1 << ( i * 2 + 1 ) );
		}
	}

	// convert POV information from a direction into 4 button bits
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		if ( joy.ji.dwPOV != JOY_POVCENTERED ) {
			if ( joy.ji.dwPOV == JOY_POVFORWARD ) {
				povstate |= 1 << 12;
			}
			if ( joy.ji.dwPOV == JOY_POVBACKWARD ) {
				povstate |= 1 << 13;
			}
			if ( joy.ji.dwPOV == JOY_POVRIGHT ) {
				povstate |= 1 << 14;
			}
			if ( joy.ji.dwPOV == JOY_POVLEFT ) {
				povstate |= 1 << 15;
			}
		}
	}

	// determine which bits have changed and key an auxillary event for each change
	for ( i = 0 ; i < 16 ; i++ ) {
		if ( ( povstate & ( 1 << i ) ) && !( joy.oldpovstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qtrue, 0, NULL );
		}

		if ( !( povstate & ( 1 << i ) ) && ( joy.oldpovstate & ( 1 << i ) ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qfalse, 0, NULL );
		}
	}
	joy.oldpovstate = povstate;

	// if there is a trackball like interface, simulate mouse moves
	if ( joy.jc.wNumAxes >= 6 ) {
		x = JoyToI( joy.ji.dwUpos ) * in_joyBallScale->value;
		y = JoyToI( joy.ji.dwVpos ) * in_joyBallScale->value;
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	}
}

/*
=========================================================================

MIDI

=========================================================================
*/

static void MIDI_NoteOff( int note ) {
	int qkey;

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 ) {
		return;
	}

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qfalse, 0, NULL );
}

static void MIDI_NoteOn( int note, int velocity ) {
	int qkey;

	if ( velocity == 0 ) {
		MIDI_NoteOff( note );
	}

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 ) {
		return;
	}

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qtrue, 0, NULL );
}

static void CALLBACK MidiInProc( HMIDIIN hMidiIn, UINT uMsg, DWORD dwInstance,
								 DWORD dwParam1, DWORD dwParam2 ) {
	int message;

	switch ( uMsg )
	{
	case MIM_OPEN:
		break;
	case MIM_CLOSE:
		break;
	case MIM_DATA:
		message = dwParam1 & 0xff;

		// note on
		if ( ( message & 0xf0 ) == 0x90 ) {
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer ) {
				MIDI_NoteOn( ( dwParam1 & 0xff00 ) >> 8, ( dwParam1 & 0xff0000 ) >> 16 );
			}
		} else if ( ( message & 0xf0 ) == 0x80 )   {
			if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer ) {
				MIDI_NoteOff( ( dwParam1 & 0xff00 ) >> 8 );
			}
		}
		break;
	case MIM_LONGDATA:
		break;
	case MIM_ERROR:
		break;
	case MIM_LONGERROR:
		break;
	}

//	Sys_QueEvent( sys_msg_time, SE_KEY, wMsg, qtrue, 0, NULL );
}

static void MidiInfo_f( void ) {
	int i;

	const char *enableStrings[] = { "disabled", "enabled" };

	Com_Printf( "\nMIDI control:       %s\n", enableStrings[in_midi->integer != 0] );
	Com_Printf( "port:               %d\n", in_midiport->integer );
	Com_Printf( "channel:            %d\n", in_midichannel->integer );
	Com_Printf( "current device:     %d\n", in_mididevice->integer );
	Com_Printf( "number of devices:  %d\n", s_midiInfo.numDevices );
	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		if ( i == Cvar_VariableValue( "in_mididevice" ) ) {
			Com_Printf( "***" );
		} else {
			Com_Printf( "..." );
		}
		Com_Printf(    "device %2d:       %s\n", i, s_midiInfo.caps[i].szPname );
		Com_Printf( "...manufacturer ID: 0x%hx\n", s_midiInfo.caps[i].wMid );
		Com_Printf( "...product ID:      0x%hx\n", s_midiInfo.caps[i].wPid );

		Com_Printf( "\n" );
	}
}

static void IN_StartupMIDI( void ) {
	int i;

	if ( !Cvar_VariableValue( "in_midi" ) ) {
		return;
	}

	//
	// enumerate MIDI IN devices
	//
	s_midiInfo.numDevices = midiInGetNumDevs();

	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		midiInGetDevCaps( i, &s_midiInfo.caps[i], sizeof( s_midiInfo.caps[i] ) );
	}

	//
	// open the MIDI IN port
	//
	if ( midiInOpen( &s_midiInfo.hMidiIn,
					 in_mididevice->integer,
					 (DWORD_PTR) MidiInProc,
					 (DWORD_PTR) NULL,
					 CALLBACK_FUNCTION ) != MMSYSERR_NOERROR ) {
		Com_Printf( "WARNING: could not open MIDI device %d: '%s'\n", in_mididevice->integer, s_midiInfo.caps[( int ) in_mididevice->value] );
		return;
	}

	midiInStart( s_midiInfo.hMidiIn );
}

static void IN_ShutdownMIDI( void ) {
	if ( s_midiInfo.hMidiIn ) {
		midiInClose( s_midiInfo.hMidiIn );
	}
	memset( &s_midiInfo, 0, sizeof( s_midiInfo ) );
}
