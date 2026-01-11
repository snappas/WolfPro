#include <assert.h>
#include "../renderer_common/tr_local.h"
#include "../qcommon/qcommon.h"
#include "resource.h"
#include "win_local.h"
#include "glw_win.h"
#include "../client/client.h"

#include "../renderer_vk/volk.h"

vkwstate_t vkw_state;

static BOOL CALLBACK WIN_MonitorEnumCallback( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	if ( lprcMonitor ) {
		g_wv.monitorRects[g_wv.monitorCount] = *lprcMonitor;
		g_wv.hMonitors[g_wv.monitorCount] = hMonitor;
		g_wv.monitorCount++;
	}

	if ( g_wv.monitorCount >= MAX_MONITOR_COUNT )
		return FALSE;
	
	return TRUE;
}

void WIN_InitMonitorList()
{
    g_wv.monitor = 0;
	g_wv.primaryMonitor = 0;
	g_wv.monitorCount = 0;

	EnumDisplayMonitors( NULL, NULL, &WIN_MonitorEnumCallback, 0 );

	const POINT zero = { 0, 0 };
	const HMONITOR hMonitor = MonitorFromPoint( zero, MONITOR_DEFAULTTOPRIMARY );
	for ( int i = 0; i < g_wv.monitorCount; i++ ) {
		if ( hMonitor ==  g_wv.hMonitors[i] ) {
			g_wv.primaryMonitor = i;
			g_wv.monitor = i;
			break;
		}
	}
}

void WIN_UpdateMonitorIndexFromCvar()
{
	// r_monitor is the 1-based monitor index, 0 means primary monitor
	// use Cvar_Get to enforce the latched change, if any
	const int monitor = Cvar_Get( "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH )->integer;
	if ( monitor <= 0 || monitor > g_wv.monitorCount ) {
		g_wv.monitor = g_wv.primaryMonitor;
		return;
	}

	g_wv.monitor = Com_ClampInt( 0, g_wv.monitorCount - 1, monitor - 1 );
}

static const char* VKW_GetCurrentDisplayDeviceName()
{
	static char deviceName[CCHDEVICENAME + 1];

	const HMONITOR hMonitor = g_wv.hMonitors[g_wv.monitor];
	if ( hMonitor == NULL )
		return NULL;

	MONITORINFOEXA info;
	ZeroMemory( &info, sizeof(info) );
	info.cbSize = sizeof(info);
	if ( GetMonitorInfoA(hMonitor, (LPMONITORINFO)&info) == 0)
		return NULL;

	Q_strncpyz( deviceName, info.szDevice, sizeof(deviceName) );
	
	return deviceName;
}


static qbool VKW_CreateWindow()
{
	static qbool s_classRegistered = qfalse;

	if ( !s_classRegistered )
	{
		WNDCLASS wc;
		memset( &wc, 0, sizeof( wc ) );

		wc.style         = CS_OWNDC;
		wc.lpfnWndProc   = MainWndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = g_wv.hInstance;
		wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
		wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName  = 0;
		wc.lpszClassName = CLIENT_WINDOW_TITLE"-vk";

		if ( !RegisterClass( &wc ) )
			ri.Error( ERR_FATAL, "VKW_CreateWindow: could not register window class" );

		s_classRegistered = qtrue;
		ri.Printf( PRINT_DEVELOPER, "...registered window class\n" );
	}

	//
	// create the HWND if one does not already exist
	//
	if ( !g_wv.hWnd )
	{
		g_wv.inputInitialized = qfalse;

		RECT r;
		r.left = 0;
		r.top = 0;
		r.right  = glInfo.winWidth;
		r.bottom = glInfo.winHeight;

		int style = WS_VISIBLE | WS_CLIPCHILDREN;
		int exstyle = 0;

		if ( glInfo.winFullscreen )
		{
			style |= WS_POPUP;
			exstyle |= WS_EX_TOPMOST;
		}
		else
		{
			g_wv.noborder = r_noborder->integer;
			if (g_wv.noborder){
				style |= WS_POPUP;
			}else{
				style |= WS_BORDER | WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX;
			}
			style |=  WS_VISIBLE | WS_SYSMENU;
			AdjustWindowRect( &r, style, FALSE );
		}

		const int w = r.right - r.left;
		const int h = r.bottom - r.top;

		const RECT monRect = g_wv.monitorRects[g_wv.monitor];

		int dx = 0;
		int dy = 0;

		if ( !glInfo.winFullscreen )
		{
			dx = ri.Cvar_Get( "vid_xpos", "0", 0 )->integer;
			dy = ri.Cvar_Get( "vid_ypos", "0", 0 )->integer;
			dx = Com_ClampInt( 0, max( 0, monRect.right - monRect.left - w ), dx );
			dy = Com_ClampInt( 0, max( 0, monRect.bottom - monRect.top - h ), dy );
		}

		const int x = monRect.left + dx;
		const int y = monRect.top + dy;

		g_wv.duringCreateWindow = qtrue;
		g_wv.hWnd = CreateWindowEx( exstyle, CLIENT_WINDOW_TITLE"-vk", " " CLIENT_WINDOW_TITLE"-vk", style,
				x, y, w, h, NULL, NULL, g_wv.hInstance, NULL );
		g_wv.duringCreateWindow = qfalse;

		if ( !g_wv.hWnd )
			ri.Error( ERR_FATAL, "VKW_CreateWindow() - Couldn't create window" );

		ShowWindow( g_wv.hWnd, SW_SHOW );
		UpdateWindow( g_wv.hWnd );
		ri.Printf( PRINT_DEVELOPER, "...created window@%d,%d (%dx%d)\n", x, y, w, h );
	}
	else
	{
		ri.Printf( PRINT_DEVELOPER, "...window already present, CreateWindowEx skipped\n" );
	}

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;

	SetForegroundWindow( g_wv.hWnd );
	SetFocus( g_wv.hWnd );

	return qtrue;
}

// @TODO: use it somewhere like before??? :p
static qbool VKW_SetMode()
{
	WIN_InitMonitorList();
	WIN_UpdateMonitorIndexFromCvar();

	const RECT monRect = g_wv.monitorRects[g_wv.monitor];
	const int desktopWidth = (int)(monRect.right - monRect.left);
	const int desktopHeight = (int)(monRect.bottom - monRect.top);
	re.ConfigureVideoMode( desktopWidth, desktopHeight );

	if (!VKW_CreateWindow())
		return qfalse;

	DEVMODE dm;
	ZeroMemory( &dm, sizeof( dm ) );
	dm.dmSize = sizeof( dm );

	if (EnumDisplaySettingsA( VKW_GetCurrentDisplayDeviceName(), ENUM_CURRENT_SETTINGS, &dm ))
		glInfo.displayFrequency = dm.dmDisplayFrequency;

	return qtrue;
}


uint64_t Sys_Vulkan_Init( void* vkInstance )
{
	if(!VKW_SetMode())
	{
	    Com_Error(ERR_FATAL, "VKW_SetMode failed\n");
	}

	VkSurfaceKHR surface;

	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = g_wv.hWnd;
	createInfo.hinstance = GetModuleHandle(NULL);
	if(vkCreateWin32SurfaceKHR((VkInstance)vkInstance, &createInfo, NULL, &surface) != VK_SUCCESS)
	{
		ri.Error(ERR_FATAL, "vkCreateWin32SurfaceKHR failed\n");
	}

	return (uint64_t)surface;
}

void Sys_Vulkan_Shutdown(void)
{
	if ( g_wv.hWnd ) {
		ri.Printf( PRINT_ALL, "...destroying window\n" );
		ShowWindow( g_wv.hWnd, SW_HIDE );
		DestroyWindow( g_wv.hWnd );
		g_wv.hWnd = NULL;
		vkw_state.pixelFormatSet = qfalse;
	}

}

qboolean Sys_Vulkan_GetRequiredExtensions(char** pNames, int* pCount) {
	return qtrue;
}