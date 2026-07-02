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

// common.c -- misc functions used in client and server

#include "../game/q_shared.h"
#include "qcommon.h"
#include <setjmp.h>
#include "threads.h"

#define MAX_NUM_ARGVS   50

#define USE_MULTI_SEGMENT // allocate additional zone segments on demand

#ifdef DEDICATED
#define MIN_COMHUNKMEGS		48
#define DEF_COMHUNKMEGS		56
#else
#define MIN_COMHUNKMEGS		64
#define DEF_COMHUNKMEGS		128
#endif

#ifdef USE_MULTI_SEGMENT
#define DEF_COMZONEMEGS		12
#else
#define DEF_COMZONEMEGS		25
#endif


int com_argc;
char    *com_argv[MAX_NUM_ARGVS + 1];

jmp_buf abortframe;     // an ERR_DROP occured, exit the entire frame

int		CPU_Flags = 0;

FILE *debuglogfile;
static fileHandle_t logfile;
fileHandle_t com_journalFile;               // events are written here
fileHandle_t com_journalDataFile;           // config files are written here

cvar_t  *com_viewlog;
cvar_t  *com_speeds;
cvar_t  *com_developer;
cvar_t  *com_dedicated;
cvar_t  *com_timescale;
cvar_t  *com_fixedtime;
cvar_t  *com_dropsim;       // 0.0 to 1.0, simulated packet drops
cvar_t  *com_journal;
cvar_t  *com_maxfps;
cvar_t  *com_timedemo;
cvar_t  *com_sv_running;
cvar_t  *com_cl_running;
cvar_t  *com_logfile;       // 1 = buffer log, 2 = flush after each print
cvar_t  *com_showtrace;
cvar_t  *com_version;
cvar_t  *com_blood;
cvar_t  *com_buildScript;   // for automated data building scripts
cvar_t  *com_introPlayed;
cvar_t  *cl_paused;
cvar_t  *sv_paused;
cvar_t  *sv_packetdelay;
cvar_t	*cl_packetdelay;
cvar_t  *com_cameraMode;
#if defined( _WIN32 ) && defined( _DEBUG )
cvar_t  *com_noErrorInterrupt;
#endif
cvar_t  *com_recommendedSet;

// Rafael Notebook
cvar_t  *cl_notebook;

cvar_t  *com_hunkused;      // Ridah

cvar_t	*com_yieldCPU;
cvar_t	*com_affinityMask;

// com_speeds times
int time_game;
int time_frontend;          // renderer frontend time
int time_backend;           // renderer backend time

static int	lastTime;
int com_frameTime;
int com_frameNumber;
int64_t	com_nextTargetTimeUS = INT64_MIN;

qboolean com_errorEntered = qfalse;
qboolean com_fullyInitialized = qfalse;

// renderer window states
qboolean	gw_minimized = qfalse; // this will be always true for dedicated servers
#ifndef DEDICATED
qboolean	gw_active = qtrue;
#endif

char com_errorMessage[MAXPRINTMSG];

void Com_WriteConfig_f( void );
void CIN_CloseAllVideos();

//============================================================================

static char *rd_buffer;
static int rd_buffersize;
static void ( *rd_flush )( char *buffer );

void Com_BeginRedirect( char *buffer, int buffersize, void ( *flush )( char *) ) {
	if ( !buffer || !buffersize || !flush ) {
		return;
	}
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void Com_EndRedirect( void ) {
	if ( rd_flush ) {
		rd_flush( rd_buffer );
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.

A raw string should NEVER be passed as fmt, because of "%f" type crashers.
=============
*/
void QDECL Com_Printf( const char *fmt, ... ) {
	va_list argptr;
	char msg[MAXPRINTMSG];
	static qboolean opening_qconsole = qfalse;

	va_start( argptr,fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if ( rd_buffer ) {
		if ( ( strlen( msg ) + strlen( rd_buffer ) ) > ( rd_buffersize - 1 ) ) {
			rd_flush( rd_buffer );
			*rd_buffer = 0;
		}
		Q_strcat( rd_buffer, rd_buffersize, msg );
		// show_bug.cgi?id=51
		// only flush the rcon buffer when it's necessary, avoid fragmenting
		//rd_flush(rd_buffer);
		//*rd_buffer = 0;
		return;
	}

	// echo to console if we're not a dedicated server
	if ( com_dedicated && !com_dedicated->integer ) {
		CL_ConsolePrint( msg );
	}

	// echo to dedicated console and early console
	Sys_Print( msg );

	// logfile
	if ( com_logfile && com_logfile->integer ) {
		// TTimo: only open the qconsole.log if the filesystem is in an initialized state
		//   also, avoid recursing in the qconsole.log opening (i.e. if fs_debug is on)
		if ( !logfile && FS_Initialized() && !opening_qconsole ) {
			struct tm *newtime;
			time_t aclock;

			opening_qconsole = qtrue;

			time( &aclock );
			newtime = localtime( &aclock );

#ifdef __MACOS__    //DAJ MacOS file typing
			{
				extern _MSL_IMP_EXP_C long _fcreator, _ftype;
				_ftype = 'TEXT';
				_fcreator = 'R*ch';
			}
#endif
			if ((com_dedicated && com_dedicated->integer) || com_logfile->integer > 2) {
				char buffer[26];
				strftime(buffer, 26, "%Y-%m-%d_%H.%M.%S", newtime);

				char *filename = va("logs\\rtcwconsole_%s.log", buffer);
				logfile = FS_FOpenFileWrite(filename);
			}
			else {
				logfile = FS_FOpenFileWrite("rtcwconsole.log");
			}

			Com_Printf( "logfile opened on %s\n", asctime( newtime ) );
			if ( com_logfile->integer > 1 ) {
				// force it to not buffer so we get valid
				// data even if we are crashing
				FS_ForceFlush( logfile );
			}

			opening_qconsole = qfalse;
		}
		if ( logfile && FS_Initialized() ) {
			FS_Write( msg, strlen( msg ), logfile );
		}
	}
}


/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void QDECL Com_DPrintf( const char *fmt, ... ) {
	va_list argptr;
	char msg[MAXPRINTMSG];

	if ( !com_developer || !com_developer->integer ) {
		return;         // don't confuse non-developers with techie stuff...
	}

	va_start( argptr,fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}

/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/
qbool crashing = qfalse;
void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list argptr;
	static int lastErrorTime;
	static int errorCount;
	int currentTime;

#if 0   //#if defined(_WIN32) && defined(_DEBUG)
	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		if ( !com_noErrorInterrupt->integer ) {
			__asm {
				int 0x03
			}
		}
	}
#endif
	

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( com_buildScript && com_buildScript->integer ) {
		code = ERR_FATAL;
	}

	// make sure we can get at our local stuff
	FS_PureServerSetLoadedPaks( "", "" );

	// if we are getting a solid stream of ERR_DROP, do an ERR_FATAL
	currentTime = Sys_Milliseconds();
	if ( currentTime - lastErrorTime < 100 ) {
		if ( ++errorCount > 3 ) {
			code = ERR_FATAL;
		}
	} else {
		errorCount = 0;
	}
	lastErrorTime = currentTime;

	if ( com_errorEntered ) {
		Sys_Error( "recursive error after: %s", com_errorMessage );
	}
	com_errorEntered = qtrue;

	va_start( argptr,fmt );
	Q_vsnprintf( com_errorMessage, sizeof( com_errorMessage ), fmt, argptr );
	va_end( argptr );

	if(code == ERR_FATAL && Sys_IsDebugging()){
		Sys_DebugBreak();
	}

	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		Cvar_Set( "com_errorMessage", com_errorMessage );
	}

	if ( code == ERR_SERVERDISCONNECT ) {
		CL_Disconnect( qtrue );
		crashing = qtrue;
		CL_FlushMemory();
		com_errorEntered = qfalse;
		longjmp( abortframe, -1 );
	} else if ( code == ERR_DROP || code == ERR_DISCONNECT ) {
		Com_Printf( "********************\nERROR: %s\n********************\n", com_errorMessage );
		SV_Shutdown( va( "Server crashed: %s\n",  com_errorMessage ) );
		CL_Disconnect( qtrue );
		CL_FlushMemory();
		com_errorEntered = qfalse;
		longjmp( abortframe, -1 );
	} else if ( code == ERR_NEED_CD ) {
		SV_Shutdown( "Server didn't have CD\n" );
		if ( com_cl_running && com_cl_running->integer ) {
			CL_Disconnect( qtrue );
			CL_FlushMemory();
			com_errorEntered = qfalse;
			CL_CDDialog();
		} else {
			Com_Printf( "Server didn't have CD\n" );
		}
		longjmp( abortframe, -1 );
	} else {
		CL_Shutdown();
		SV_Shutdown( va( "Server fatal crashed: %s\n", com_errorMessage ) );
	}

	Com_Shutdown();

	Sys_Error( "%s", com_errorMessage );
}


/*
=============
Com_Quit_f

Both client and server can use this, and it will
do the apropriate things.
=============
*/

void Com_Quit(int status)
{
	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		SV_Shutdown("Server quit");
#ifndef DEDICATED
		CL_Shutdown();
#endif
		Com_Shutdown();
		FS_Shutdown( qtrue );
	}

	Sys_Quit(status);
}

void Com_Quit_f( void ) {
	Com_Quit(0);
}



/*
============================================================================

COMMAND LINE FUNCTIONS

+ characters seperate the commandLine string into multiple console
command lines.

All of these are valid:

quake3 +set test blah +map test
quake3 set test blah+map test
quake3 set test blah + map test

============================================================================
*/

#define MAX_CONSOLE_LINES   32
int com_numConsoleLines;
char    *com_consoleLines[MAX_CONSOLE_LINES];

/*
==================
Com_ParseCommandLine

Break it up into multiple console lines
==================
*/
void Com_ParseCommandLine( char *commandLine ) {
	com_consoleLines[0] = commandLine;
	com_numConsoleLines = 1;

	while ( *commandLine ) {
		// look for a + seperating character
		// if commandLine came from a file, we might have real line seperators
		if ( *commandLine == '+' || *commandLine == '\n' ) {
			if ( com_numConsoleLines == MAX_CONSOLE_LINES ) {
				return;
			}
			com_consoleLines[com_numConsoleLines] = commandLine + 1;
			com_numConsoleLines++;
			*commandLine = 0;
		}
		commandLine++;
	}
}


/*
===================
Com_SafeMode

Check for "safe" on the command line, which will
skip loading of wolfconfig.cfg
===================
*/
qboolean Com_SafeMode( void ) {
	int i;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmp( Cmd_Argv( 0 ), "safe" )
			 || !Q_stricmp( Cmd_Argv( 0 ), "cvar_restart" ) ) {
			com_consoleLines[i][0] = 0;
			return qtrue;
		}
	}
	return qfalse;
}


/*
===============
Com_StartupVariable

Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets shouls
be after execing the config and default.
===============
*/
void Com_StartupVariable( const char *match ) {
	int i;
	char    *s;
	cvar_t  *cv;

	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( strcmp( Cmd_Argv( 0 ), "set" ) ) {
			continue;
		}

		s = Cmd_Argv( 1 );
		if ( !match || !strcmp( s, match ) ) {
			Cvar_Set( s, Cmd_Argv( 2 ) );
			cv = Cvar_Get( s, "", 0 );
			cv->flags |= CVAR_USER_CREATED;
//			com_consoleLines[i] = 0;
		}
	}
}


/*
=================
Com_AddStartupCommands

Adds command line parameters as script statements
Commands are seperated by + signs

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting
=================
*/
qboolean Com_AddStartupCommands( void ) {
	int i;
	qboolean added;

	added = qfalse;
	// quote every token, so args with semicolons can work
	for ( i = 0 ; i < com_numConsoleLines ; i++ ) {
		if ( !com_consoleLines[i] || !com_consoleLines[i][0] ) {
			continue;
		}

		// set commands won't override menu startup
		if ( Q_stricmpn( com_consoleLines[i], "set", 3 ) ) {
			added = qtrue;
		}
		Cbuf_AddText( com_consoleLines[i] );
		Cbuf_AddText( "\n" );
	}

	return added;
}


//============================================================================

void Info_Print( const char *s ) {
	char key[512];
	char value[512];
	char    *o;
	int l;

	if ( *s == '\\' ) {
		s++;
	}
	while ( *s )
	{
		o = key;
		while ( *s && *s != '\\' )
			*o++ = *s++;

		l = o - key;
		if ( l < 20 ) {
			memset( o, ' ', 20 - l );
			key[20] = 0;
		} else {
			*o = 0;
		}
		Com_Printf( "%s", key );

		if ( !*s ) {
			Com_Printf( "MISSING VALUE\n" );
			return;
		}

		o = value;
		s++;
		while ( *s && *s != '\\' )
			*o++ = *s++;
		*o = 0;

		if ( *s ) {
			s++;
		}
		Com_Printf( "%s\n", value );
	}
}

/*
============
Com_StringContains
============
*/
char *Com_StringContains( char *str1, char *str2, int casesensitive ) {
	int len, i, j;

	len = strlen( str1 ) - strlen( str2 );
	for ( i = 0; i <= len; i++, str1++ ) {
		for ( j = 0; str2[j]; j++ ) {
			if ( casesensitive ) {
				if ( str1[j] != str2[j] ) {
					break;
				}
			} else {
				if ( toupper( str1[j] ) != toupper( str2[j] ) ) {
					break;
				}
			}
		}
		if ( !str2[j] ) {
			return str1;
		}
	}
	return NULL;
}

/*
============
Com_Filter
============
*/
int Com_Filter( char *filter, char *name, int casesensitive ) {
	char buf[MAX_TOKEN_CHARS];
	char *ptr;
	int i, found;

	while ( *filter ) {
		if ( *filter == '*' ) {
			filter++;
			for ( i = 0; *filter; i++ ) {
				if ( *filter == '*' || *filter == '?' ) {
					break;
				}
				buf[i] = *filter;
				filter++;
			}
			buf[i] = '\0';
			if ( strlen( buf ) ) {
				ptr = Com_StringContains( name, buf, casesensitive );
				if ( !ptr ) {
					return qfalse;
				}
				name = ptr + strlen( buf );
			}
		} else if ( *filter == '?' )      {
			filter++;
			name++;
		} else if ( *filter == '[' && *( filter + 1 ) == '[' )           {
			filter++;
		} else if ( *filter == '[' )      {
			filter++;
			found = qfalse;
			while ( *filter && !found ) {
				if ( *filter == ']' && *( filter + 1 ) != ']' ) {
					break;
				}
				if ( *( filter + 1 ) == '-' && *( filter + 2 ) && ( *( filter + 2 ) != ']' || *( filter + 3 ) == ']' ) ) {
					if ( casesensitive ) {
						if ( *name >= *filter && *name <= *( filter + 2 ) ) {
							found = qtrue;
						}
					} else {
						if ( toupper( *name ) >= toupper( *filter ) &&
							 toupper( *name ) <= toupper( *( filter + 2 ) ) ) {
							found = qtrue;
						}
					}
					filter += 3;
				} else {
					if ( casesensitive ) {
						if ( *filter == *name ) {
							found = qtrue;
						}
					} else {
						if ( toupper( *filter ) == toupper( *name ) ) {
							found = qtrue;
						}
					}
					filter++;
				}
			}
			if ( !found ) {
				return qfalse;
			}
			while ( *filter ) {
				if ( *filter == ']' && *( filter + 1 ) != ']' ) {
					break;
				}
				filter++;
			}
			filter++;
			name++;
		} else {
			if ( casesensitive ) {
				if ( *filter != *name ) {
					return qfalse;
				}
			} else {
				if ( toupper( *filter ) != toupper( *name ) ) {
					return qfalse;
				}
			}
			filter++;
			name++;
		}
	}
	return qtrue;
}

/*
============
Com_FilterPath
============
*/
int Com_FilterPath( char *filter, char *name, int casesensitive ) {
	int i;
	char new_filter[MAX_QPATH];
	char new_name[MAX_QPATH];

	for ( i = 0; i < MAX_QPATH - 1 && filter[i]; i++ ) {
		if ( filter[i] == '\\' || filter[i] == ':' ) {
			new_filter[i] = '/';
		} else {
			new_filter[i] = filter[i];
		}
	}
	new_filter[i] = '\0';
	for ( i = 0; i < MAX_QPATH - 1 && name[i]; i++ ) {
		if ( name[i] == '\\' || name[i] == ':' ) {
			new_name[i] = '/';
		} else {
			new_name[i] = name[i];
		}
	}
	new_name[i] = '\0';
	return Com_Filter( new_filter, new_name, casesensitive );
}

/*
============
Com_HashKey
============
*/
int Com_HashKey( char *string, int maxlen ) {
	int register hash, i;

	hash = 0;
	for ( i = 0; i < maxlen && string[i] != '\0'; i++ ) {
		hash += string[i] * ( 119 + i );
	}
	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	return hash;
}

/*
================
Com_RealTime
================
*/
int Com_RealTime( qtime_t *qtime ) {
	time_t t;
	struct tm *tms;

	t = time( NULL );
	if ( !qtime ) {
		return t;
	}
	tms = localtime( &t );
	if ( tms ) {
		qtime->tm_sec = tms->tm_sec;
		qtime->tm_min = tms->tm_min;
		qtime->tm_hour = tms->tm_hour;
		qtime->tm_mday = tms->tm_mday;
		qtime->tm_mon = tms->tm_mon;
		qtime->tm_year = tms->tm_year;
		qtime->tm_wday = tms->tm_wday;
		qtime->tm_yday = tms->tm_yday;
		qtime->tm_isdst = tms->tm_isdst;
	}
	return t;
}


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

#define	ZONE_ID		0x1d4a11
#define TRASH_ID	(ZONE_ID + 1)

#define MINFRAGMENT		64

#define BUCKET_COUNT	4
#define BUCKET_SIZE		64

#define USE_STATIC_TAGS
#ifdef _DEBUG
#define USE_TRASH_TEST
#define USE_ZONE_ID
#endif

#ifdef ZONE_DEBUG
typedef struct zonedebug_s {
	const char *label;
	const char *file;
	int line;
	int allocSize;
} zonedebug_t;
#endif

typedef struct memblock_s {
	struct memblock_s	*next, *prev;
	uint32_t	size;	// including the header and possibly tiny fragments, if 0 then it is a zone separator thus can't be released/merged
	memtag_t	tag;	// a tag of 0 is a free block
#ifdef USE_ZONE_ID
	int			id;		// should be ZONE_ID
#endif
#ifdef ZONE_DEBUG
	zonedebug_t d;
#endif
} memblock_t;

typedef struct freeblock_s {
	struct freeblock_s *prev;
	struct freeblock_s *next;
} freeblock_t;

typedef struct memzone_s {
	size_t		size;		// total bytes malloced, including header
	size_t		used;		// total bytes used
	memblock_t	blocklist;	// start / end cap for linked list
#ifdef USE_MULTI_SEGMENT
	struct {
		memblock_t	filler;	// just to allocate some space before freelist
		freeblock_t head;
	} bucket[BUCKET_COUNT];
#else
	memblock_t	*rover;
#endif
	const char *name;
} memzone_t;

static int minfragment = MINFRAGMENT; // may be adjusted at runtime

// main zone for all "dynamic" memory allocation
static memzone_t *mainzone;

// we also have a small zone for small allocations that would only
// fragment the main zone (think of cvar and cmd strings)
static memzone_t *smallzone;


#ifdef USE_MULTI_SEGMENT
static int GetBucketIndex( const memzone_t *zone, uint32_t size ) 
{
	const int index = size / BUCKET_SIZE;
	return index > (BUCKET_COUNT - 1) ? BUCKET_COUNT - 1 : index;
}


static void InsertFree( memzone_t *zone, memblock_t *block )
{
	freeblock_t *fb = (freeblock_t *)(block + 1);
	freeblock_t *prev, *next;
	const int index = GetBucketIndex( zone, block->size );
	prev = &zone->bucket[ index ].head;

	next = prev->next;

#ifdef ZONE_DEBUG
	if ( block->size < sizeof( *fb ) + sizeof( *block ) ) {
		Com_Error( ERR_FATAL, "InsertFree: bad block size: %i\n", block->size );
	}
#endif

	prev->next = fb;
	next->prev = fb;

	fb->prev = prev;
	fb->next = next;
}


static void RemoveFree( memblock_t *block )
{
	freeblock_t *fb = (freeblock_t *)(block + 1);
	freeblock_t *prev;
	freeblock_t *next;

#ifdef ZONE_DEBUG
	if ( fb->next == NULL || fb->prev == NULL || fb->next == fb || fb->prev == fb ) {
		Com_Error( ERR_FATAL, "RemoveFree: bad pointers fb->next: %p, fb->prev: %p\n", fb->next, fb->prev );
	}
#endif

	prev = fb->prev;
	next = fb->next;

	prev->next = next;
	next->prev = prev;
}


static memblock_t *SplitBlock( memblock_t *base, size_t base_size, size_t fragment_size )
{
	memblock_t *fragment = (memblock_t *)((unsigned char *)base + base_size);

	fragment->size = fragment_size;
	fragment->prev = base;
	fragment->next = base->next;
	fragment->next->prev = fragment;

	base->next = fragment;
	base->size = base_size;

	return fragment;
}


/*
================
NewBlock

Allocates new free block within specified memory zone

Separator is needed to avoid additional runtime checks in Z_Free()
to prevent merging it with previous free block
================
*/
static memblock_t *NewBlock( memzone_t *zone, uint32_t size )
{
	memblock_t *prev, *next;
	memblock_t *block, *sep;
	uint32_t alloc_size;

	// zone->prev is pointing on last block in the list
	prev = zone->blocklist.prev;
	next = prev->next;

	size = PAD( size, 1U << 21 ); // round up to 2M blocks
	// allocate separator block before new free block
	alloc_size = size + sizeof( *sep );

	//sep = (memblock_t *)calloc( alloc_size, 1 );
	sep = (memblock_t *)malloc( alloc_size );
	if ( sep == NULL ) {
		Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %u bytes from the %s zone",
			size, zone->name );
		return NULL;
	}
	Com_Memset( sep, 0x0, sizeof( *sep ) + sizeof( *block ) );
	block = sep + 1;

	// link separator with prev
	prev->next = sep;
	sep->prev = prev;

	// link separator with block
	sep->next = block;
	block->prev = sep;

	// link block with next
	block->next = next;
	next->prev = block;

	sep->tag = TAG_GENERAL; // in-use block
	sep->size = 0;			// 0 = segment separator

	block->tag = TAG_FREE;
	block->size = size;

#ifdef USE_ZONE_ID
	sep->id = -ZONE_ID;
	block->id = ZONE_ID;
#endif

	// update zone statistics
	zone->size += alloc_size;
	zone->used += sizeof( *sep );

	InsertFree( zone, block );

	return block;
}


static memblock_t *SearchFree( memzone_t *zone, uint32_t size )
{
	const int index = GetBucketIndex( zone, size );
	const freeblock_t *fb = zone->bucket[ index ].head.next;
	const freeblock_t *fh = &zone->bucket[ 0 ].head;

	for ( ;; ) {
		memblock_t *base;
		if ( fb == fh ) {
			return NewBlock( zone, size );
		}
		base = (memblock_t *)((byte *)fb - sizeof( *base ));
		fb = fb->next;
		if ( base->size >= size ) {
			return base;
		}
	}
	return NULL;
}
#endif // USE_MULTI_SEGMENT


/*
========================
Z_Init
========================
*/
static void Z_Init( memzone_t *zone, uint32_t size, const char *name )
{
	memblock_t *block;
	int i, n, min_fragment;

	Com_Memset( zone, 0x0, sizeof( *zone ) + sizeof( *block ) );

	zone->name = name;

#ifdef USE_MULTI_SEGMENT
	min_fragment = sizeof( memblock_t ) + sizeof( freeblock_t );
#else
	min_fragment = sizeof( memblock_t );
#endif

	if ( minfragment < min_fragment ) {
		// in debug mode size of memblock_t may exceed MINFRAGMENT
		minfragment = PAD( min_fragment, sizeof( intptr_t ) );
	}

	// set the entire zone to one free block
	zone->blocklist.next = zone->blocklist.prev = block = (memblock_t *)(zone + 1);
	zone->blocklist.tag = TAG_GENERAL; // in use block
	// zone->blocklist.size = 0;
	zone->size = size;
	// zone->used = 0;
#ifndef USE_MULTI_SEGMENT
	zone->rover = block;
#endif

	block->prev = block->next = &zone->blocklist;
	block->size = size - sizeof( *zone );
	block->tag = TAG_FREE;

#ifdef USE_ZONE_ID
	zone->blocklist.id = -ZONE_ID;
	block->id = ZONE_ID;
#endif

#ifdef USE_MULTI_SEGMENT
	n = ARRAY_LEN( zone->bucket );

	for ( i = 0; i < n; i++ ) {
		zone->bucket[i].head.next = &zone->bucket[(i + 1) % n].head;
		zone->bucket[i].head.prev = &zone->bucket[(i + n - 1) % n].head;
		// zone->bucket[i].filler.size = 0;
		zone->bucket[i].filler.tag = TAG_GENERAL;
#ifdef USE_ZONE_ID
		zone->bucket[i].filler.id = ZONE_ID;
#endif
	}

	InsertFree( zone, block );
#endif // USE_MULTI_SEGMENT
}


/*
========================
Z_AvailableZoneMemory
========================
*/
static int Z_AvailableZoneMemory( const memzone_t *zone )
{
#ifdef USE_MULTI_SEGMENT
	return (1024*1024*1024); // unlimited
#else
	return zone->size - zone->used;
#endif
}


/*
========================
Z_AvailableMemory
========================
*/
int Z_AvailableMemory( void )
{
	return Z_AvailableZoneMemory( mainzone );
}


static void MergeBlock( memblock_t *curr_free, const memblock_t *next )
{
	curr_free->size += next->size;
	curr_free->next = next->next;
	curr_free->next->prev = curr_free;
}


/*
========================
Z_Free
========================
*/
void Z_Free( void *ptr )
{
	memblock_t *block, *other;
	memzone_t *zone;

	if ( ptr == NULL ) {
#ifdef _DEBUG
		Com_Error( ERR_DROP, "Z_Free: NULL pointer" );
#else
		return;
#endif
	}

	block = (memblock_t *)((byte *)ptr - sizeof( memblock_t ));

#ifdef USE_ZONE_ID
	if ( block->id != ZONE_ID ) {
		Com_Error( ERR_FATAL, "Z_Free: freed a pointer without ZONEID" );
	}
#endif

	if ( block->tag == TAG_FREE ) {
		Com_Error( ERR_FATAL, "Z_Free: freed a freed pointer" );
	}

#ifdef USE_STATIC_TAGS
	if ( block->tag == TAG_STATIC ) {
		return;
	}
#endif

	// check the memory trash tester
#ifdef USE_TRASH_TEST
	if ( *(int *)((byte *)block + block->size - 4) != TRASH_ID ) {
		Com_Error( ERR_FATAL, "Z_Free: memory block wrote past end" );
	}
#endif

	zone = (block->tag == TAG_SMALL) ? smallzone : mainzone;

	zone->used -= block->size;

	// set the block to something that should cause problems
	// if it is referenced...
#ifdef ZONE_DEBUG
	Com_Memset( ptr, 0xaa, block->size - sizeof( *block ) );
#endif

	block->tag = TAG_FREE; // mark as free
#ifdef USE_ZONE_ID
	block->id = ZONE_ID;
#endif

	other = block->prev;
	if ( other->tag == TAG_FREE ) {
#ifdef USE_MULTI_SEGMENT
		RemoveFree( other );
#endif
		// merge with previous free block
		MergeBlock( other, block );
#ifndef USE_MULTI_SEGMENT
		if ( block == zone->rover ) {
			zone->rover = other;
		}
#endif
		block = other;
	}

#ifndef USE_MULTI_SEGMENT
	zone->rover = block;
#endif

	other = block->next;
	if ( other->tag == TAG_FREE ) {
#ifdef USE_MULTI_SEGMENT
		RemoveFree( other );
#endif
		// merge the next free block onto the end
		MergeBlock( block, other );
	}

#ifdef USE_MULTI_SEGMENT
	InsertFree( zone, block );
#endif
}


/*
================
Z_FreeTags
================
*/
int Z_FreeTags( memtag_t tag )
{
	int			count;
	memzone_t *zone;
	memblock_t *block, *freed;

	if ( tag == TAG_STATIC ) {
		Com_Error( ERR_FATAL, "Z_FreeTags( TAG_STATIC )" );
		return 0;
	} else {
		zone = (tag == TAG_SMALL) ? smallzone : mainzone;
	}

	count = 0;
	for ( block = zone->blocklist.next; ; ) {
#ifdef USE_ZONE_ID
		if ( block->tag == tag && block->id == ZONE_ID ) {
#else
		if ( block->tag == tag && block->size != 0 ) {
#endif
			if ( block->prev->tag == TAG_FREE )
				freed = block->prev;  // current block will be merged with previous
			else
				freed = block; // will leave in place
			Z_Free( (void *)(block + 1) );
			block = freed;
			count++;
		}
		if ( block->next == &zone->blocklist ) {
			break;	// all blocks have been hit
		}
		block = block->next;
	}

	return count;
}


/*
================
Z_TagMalloc
================
*/
#ifdef ZONE_DEBUG
void *Z_TagMallocDebug( size_t size, memtag_t tag, const char *label, const char *file, int line ) {
	int		allocSize;
#else
void *Z_TagMalloc( size_t size, memtag_t tag ) {
#endif
#ifndef USE_MULTI_SEGMENT
	memblock_t *start, *rover;
#endif
	memblock_t	*base;
	memzone_t	*zone;
	size_t		extra;

	if ( size > INT_MAX ) {
		Com_Error( ERR_FATAL, "Z_TagMalloc: %"PRIz"u > INT_MAX", size );
	}

	if ( tag == TAG_FREE ) {
		Com_Error( ERR_FATAL, "Z_TagMalloc: tried to use with TAG_FREE" );
	}

	zone = (tag == TAG_SMALL) ? smallzone : mainzone;

#ifdef ZONE_DEBUG
	allocSize = size;
#endif

#ifdef USE_MULTI_SEGMENT
	if ( size < (sizeof( freeblock_t )) ) {
		size = (sizeof( freeblock_t ));
	}
#endif

	//
	// scan through the block list looking for the first free block
	// of sufficient size
	//
	size += sizeof( *base );	// account for size of block header
#ifdef USE_TRASH_TEST
	size += 4;					// space for memory trash tester
#endif

	size = PAD( size, sizeof( intptr_t ) );		// align to 32/64 bit boundary

#ifdef USE_MULTI_SEGMENT
	base = SearchFree( zone, size );

	RemoveFree( base );
#else
	base = rover = zone->rover;
	start = base->prev;

	do {
		if ( rover == start ) {
			// scanned all the way around the list
#ifdef ZONE_DEBUG
			//Z_LogHeap();
			Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %u bytes from the %s zone: %s, line: %d (%s)",
				size, zone->name, file, line, label );
#else
			Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %u bytes from the %s zone",
				size, zone->name );
#endif
			return NULL;
		}
		if ( rover->tag != TAG_FREE ) {
			base = rover = rover->next;
		} else {
			rover = rover->next;
		}
	} while ( base->tag != TAG_FREE || base->size < size );
#endif

	//
	// found a block big enough
	//
	extra = base->size - size;
	if ( extra >= minfragment ) {
		memblock_t *fragment = SplitBlock( base, size, extra );
#ifdef USE_MULTI_SEGMENT
		InsertFree( zone, fragment );
#endif
		fragment->tag = TAG_FREE;
#ifdef USE_ZONE_ID
		fragment->id = ZONE_ID;
#endif
	}

#ifndef USE_MULTI_SEGMENT
	zone->rover = base->next;	// next allocation will start looking here
#endif
	zone->used += base->size;

	base->tag = tag;			// no longer a free block
#ifdef USE_ZONE_ID
	base->id = ZONE_ID;
#endif

#ifdef ZONE_DEBUG
	base->d.label = label;
	base->d.file = file;
	base->d.line = line;
	base->d.allocSize = allocSize;
#endif

#ifdef USE_TRASH_TEST
	// marker for memory trash testing
	*(int *)((byte *)base + base->size - 4) = TRASH_ID;
#endif

	return (void *)(base + 1);
}


/*
========================
Z_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *Z_MallocDebug( size_t size, const char *label, const char *file, int line ) {
#else
void *Z_Malloc( size_t size ) {
#endif
	void	*buf;

  //Z_CheckHeap ();	// DEBUG

#ifdef ZONE_DEBUG
	buf = Z_TagMallocDebug( size, TAG_GENERAL, label, file, line );
#else
	buf = Z_TagMalloc( size, TAG_GENERAL );
#endif
	Com_Memset( buf, 0, size );

	return buf;
}


/*
========================
S_Malloc
========================
*/
#ifdef ZONE_DEBUG
void *S_MallocDebug( size_t size, const char *label, const char *file, int line ) {
	return Z_TagMallocDebug( size, TAG_SMALL, label, file, line );
}
#else
void *S_Malloc( size_t size ) {
	return Z_TagMalloc( size, TAG_SMALL );
}
#endif


/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap( void )
{
	const memblock_t *block;
	const memzone_t *zone;

	zone = mainzone;
	for ( block = zone->blocklist.next; ; ) {
		if ( block->next == &zone->blocklist ) {
			break;	// all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next ) {
#ifdef USE_MULTI_SEGMENT
			const memblock_t *next = block->next;
#ifdef USE_ZONE_ID
			if ( next->size == 0 && next->id == -ZONE_ID && next->tag == TAG_GENERAL ) {
#else
			if ( next->size == 0 && next->tag == TAG_GENERAL ) {
#endif
				block = next; // new zone segment
			} else
#endif
			Com_Error( ERR_FATAL, "Z_CheckHeap: block size does not touch the next block" );
		}
		if ( block->next->prev != block ) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: next block doesn't have proper back link" );
		}
		if ( block->tag == TAG_FREE && block->next->tag == TAG_FREE ) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: two consecutive free blocks" );
		}
		block = block->next;
	}
}


/*
========================
Z_LogZoneHeap
========================
*/
static void Z_LogZoneHeap( memzone_t *zone, const char *name )
{
#ifdef ZONE_DEBUG
	char dump[32], *ptr;
	int  i, j;
#endif
	memblock_t	*block;
	char		buf[4096];
	size_t size, allocSize, numBlocks;
	int len;

	if ( logfile == FS_INVALID_HANDLE || !FS_Initialized() )
		return;

	size = numBlocks = 0;
#ifdef ZONE_DEBUG
	allocSize = 0;
#endif
	len = Com_sprintf( buf, sizeof(buf), "\r\n================\r\n%s log\r\n================\r\n", name );
	FS_Write( buf, len, logfile );
	for ( block = zone->blocklist.next ; ; ) {
		if ( block->tag != TAG_FREE ) {
#ifdef ZONE_DEBUG
			ptr = ((char *) block) + sizeof(memblock_t);
			j = 0;
			for (i = 0; i < 20 && i < block->d.allocSize; i++) {
				if (ptr[i] >= 32 && ptr[i] < 127) {
					dump[j++] = ptr[i];
				}
				else {
					dump[j++] = '_';
				}
			}
			dump[j] = '\0';
			len = Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s) [%s]\r\n", block->d.allocSize, block->d.file, block->d.line, block->d.label, dump);
			FS_Write( buf, len, logfile );
			allocSize += block->d.allocSize;
#endif
			size += block->size;
			numBlocks++;
		}
		if ( block->next == &zone->blocklist ) {
			break; // all blocks have been hit
		}
		block = block->next;
	}
#ifdef ZONE_DEBUG
	// subtract debug memory
	size -= numBlocks * sizeof(zonedebug_t);
#else
	allocSize = numBlocks * sizeof(memblock_t); // + 32 bit alignment
#endif
	len = Com_sprintf( buf, sizeof( buf ), "%"PRIz"u %s memory in %"PRIz"u blocks\r\n", size, name, numBlocks );
	FS_Write( buf, len, logfile );
	len = Com_sprintf( buf, sizeof( buf ), "%"PRIz"u %s memory overhead\r\n", size - allocSize, name );
	FS_Write( buf, len, logfile );
	FS_Flush( logfile );
}


/*
========================
Z_LogHeap
========================
*/
void Z_LogHeap( void )
{
	Z_LogZoneHeap( mainzone, "MAIN" );
	Z_LogZoneHeap( smallzone, "SMALL" );
}

#ifdef USE_STATIC_TAGS

// static mem blocks to reduce a lot of small zone overhead
typedef struct memstatic_s {
	memblock_t b;
	byte mem[2];
} memstatic_t;

#ifdef USE_ZONE_ID
#define MEM_STATIC(chr) { { NULL, NULL, sizeof(memstatic_t), TAG_STATIC, ZONE_ID }, {chr,'\0'} }
#else
#define MEM_STATIC(chr) { { NULL, NULL, sizeof(memstatic_t), TAG_STATIC }, {chr,'\0'} }
#endif

static const memstatic_t emptystring =
	MEM_STATIC( '\0' );

static const memstatic_t numberstring[] = {
	MEM_STATIC( '0' ),
	MEM_STATIC( '1' ),
	MEM_STATIC( '2' ),
	MEM_STATIC( '3' ),
	MEM_STATIC( '4' ),
	MEM_STATIC( '5' ),
	MEM_STATIC( '6' ),
	MEM_STATIC( '7' ),
	MEM_STATIC( '8' ),
	MEM_STATIC( '9' )
};
#endif // USE_STATIC_TAGS

/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from a memstatic_t might be returned
========================
*/
char *CopyString( const char *in )
{
	char *out;
#ifdef USE_STATIC_TAGS
	if ( in[0] == '\0' ) {
		return ((char *)&emptystring) + sizeof(memblock_t);
	}
	else if ( in[0] >= '0' && in[0] <= '9' && in[1] == '\0' ) {
		return ((char *)&numberstring[in[0]-'0']) + sizeof(memblock_t);
	}
#endif
	out = S_Malloc( strlen( in ) + 1 );
	strcpy( out, in );
	return out;
}


/*
==============================================================================

Goals:
	reproducible without history effects -- no out of memory errors on weird map to map changes
	allow restarting of the client without fragmentation
	minimize total pages in use at run time
	minimize total pages needed during load time

  Single block of memory with stack allocators coming from both ends towards the middle.

  One side is designated the temporary memory allocator.

  Temporary memory can be allocated and freed in any order.

  A highwater mark is kept of the most in use at any time.

  When there is no temporary memory allocated, the permanent and temp sides
  can be switched, allowing the already touched temp memory to be used for
  permanent storage.

  Temp memory must never be allocated on two ends at once, or fragmentation
  could occur.

  If we have any in-use temp memory, additional temp allocations must come from
  that side.

  If not, we can choose to make either side the new temp side and push future
  permanent allocations to the other side.  Permanent allocations should be
  kept on the side that has the current greatest wasted highwater mark.

==============================================================================
*/


#define	HUNK_MAGIC	0x89537892
#define	HUNK_FREE_MAGIC	0x89537893

typedef struct {
	unsigned int magic;
	unsigned int size;
} hunkHeader_t;

typedef struct {
	int		mark;
	int		permanent;
	int		temp;
	int		tempHighwater;
} hunkUsed_t;

typedef struct hunkblock_s {
	int size;
	byte printed;
	struct hunkblock_s *next;
	const char *label;
	const char *file;
	int line;
} hunkblock_t;

static	hunkblock_t *hunkblocks;

static	hunkUsed_t	hunk_low, hunk_high;
static	hunkUsed_t	*hunk_permanent, *hunk_temp;

static	byte	*s_hunkData = NULL;
static	int		s_hunkTotal;

static const char *tagName[ TAG_COUNT ] = {
	"FREE",
	"GENERAL",
	"PACK",
	"SEARCH-PATH",
	"SEARCH-PACK",
	"SEARCH-DIR",
	"BOTLIB",
	"RENDERER",
	"CLIENTS",
	"SMALL",
	"STATIC"
};

typedef struct zone_stats_s {
	size_t zoneSegments;
	size_t zoneBlocks;
	size_t zoneBytes;
	size_t botlibBytes;
	size_t rendererBytes;
	size_t freeBytes;
	size_t freeBlocks;
	size_t freeSmallest;
	size_t freeLargest;
} zone_stats_t;


static void Zone_Stats( const memzone_t *z, qboolean printDetails, zone_stats_t *stats )
{
	const memblock_t *block;
	const memzone_t *zone;
	zone_stats_t st;

	memset( &st, 0, sizeof( st ) );
	zone = z;
	st.zoneSegments = 1;
	st.freeSmallest = SIZE_MAX;

	//if ( printDetails ) {
	//	Com_Printf( "---------- %s zone segment #%i ----------\n", name, zone->segnum );
	//}

	for ( block = zone->blocklist.next ; ; ) {
		if ( printDetails ) {
			int tag = block->tag;
			Com_Printf( "block:%p  size:%8u  tag: %s\n", (void *)block, block->size,
				(unsigned)tag < TAG_COUNT ? tagName[ tag ] : va( "%i", tag ) );
		}
		if ( block->tag != TAG_FREE ) {
			st.zoneBytes += block->size;
			st.zoneBlocks++;
			if ( block->tag == TAG_BOTLIB ) {
				st.botlibBytes += block->size;
			} else if ( block->tag == TAG_RENDERER ) {
				st.rendererBytes += block->size;
			}
		} else {
			st.freeBytes += block->size;
			st.freeBlocks++;
			if ( block->size > st.freeLargest )
				st.freeLargest = block->size;
			if ( block->size < st.freeSmallest )
				st.freeSmallest = block->size;
		}
		if ( block->next == &zone->blocklist ) {
			break; // all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next) {
#ifdef USE_MULTI_SEGMENT
			const memblock_t *next = block->next;
#ifdef USE_ZONE_ID
			if ( next->size == 0 && next->id == -ZONE_ID && next->tag == TAG_GENERAL ) {
#else
			if ( next->size == 0 && next->tag == TAG_GENERAL ) {
#endif
				st.zoneSegments++;
				if ( printDetails ) {
					Com_Printf( "---------- %s zone segment #%"PRIz"u ----------\n", zone->name, st.zoneSegments );
				}
				block = next->next;
				continue;
			} else
#endif
				Com_Printf( "ERROR: block size does not touch the next block\n" );
		}
		if ( block->next->prev != block) {
			Com_Printf( "ERROR: next block doesn't have proper back link\n" );
		}
		if ( block->tag == TAG_FREE && block->next->tag == TAG_FREE ) {
			Com_Printf( "ERROR: two consecutive free blocks\n" );
		}
		block = block->next;
	}

	// export stats
	if ( stats ) {
		memcpy( stats, &st, sizeof( *stats ) );
	}
}


/*
=================
Com_Meminfo_f
=================
*/
static void Com_Meminfo_f( void ) {
	zone_stats_t st;
	int		unused;

	Com_Printf( "%8i bytes total hunk\n", s_hunkTotal );
	Com_Printf( "\n" );
	Com_Printf( "%8i low mark\n", hunk_low.mark );
	Com_Printf( "%8i low permanent\n", hunk_low.permanent );
	if ( hunk_low.temp != hunk_low.permanent ) {
		Com_Printf( "%8i low temp\n", hunk_low.temp );
	}
	Com_Printf( "%8i low tempHighwater\n", hunk_low.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i high mark\n", hunk_high.mark );
	Com_Printf( "%8i high permanent\n", hunk_high.permanent );
	if ( hunk_high.temp != hunk_high.permanent ) {
		Com_Printf( "%8i high temp\n", hunk_high.temp );
	}
	Com_Printf( "%8i high tempHighwater\n", hunk_high.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i total hunk in use\n", hunk_low.permanent + hunk_high.permanent );
	unused = 0;
	if ( hunk_low.tempHighwater > hunk_low.permanent ) {
		unused += hunk_low.tempHighwater - hunk_low.permanent;
	}
	if ( hunk_high.tempHighwater > hunk_high.permanent ) {
		unused += hunk_high.tempHighwater - hunk_high.permanent;
	}
	Com_Printf( "%8i unused highwater\n", unused );
	Com_Printf( "\n" );

	Zone_Stats( mainzone, !Q_stricmp( Cmd_Argv(1), "main" ) || !Q_stricmp( Cmd_Argv(1), "all" ), &st );
	Com_Printf( "%8"PRIz"u bytes total main zone\n\n", mainzone->size );
	Com_Printf( "%8"PRIz"u bytes in %"PRIz"u main zone blocks%s\n", st.zoneBytes, st.zoneBlocks,
		st.zoneSegments > 1 ? va( " and %"PRIz"u segments", st.zoneSegments ) : "" );
	Com_Printf( "        %8"PRIz"u bytes in botlib\n", st.botlibBytes );
	Com_Printf( "        %8"PRIz"u bytes in renderer\n", st.rendererBytes );
	Com_Printf( "        %8"PRIz"u bytes in other\n", st.zoneBytes - ( st.botlibBytes + st.rendererBytes ) );
	Com_Printf( "        %8"PRIz"u bytes in %"PRIz"u free blocks\n", st.freeBytes, st.freeBlocks );
	if ( st.freeBlocks > 1 ) {
		Com_Printf( "        (largest: %"PRIz"u bytes, smallest: %"PRIz"u bytes)\n\n", st.freeLargest, st.freeSmallest );
	}

	Zone_Stats( smallzone, !Q_stricmp( Cmd_Argv(1), "small" ) || !Q_stricmp( Cmd_Argv(1), "all" ), &st );
	Com_Printf( "%8"PRIz"u bytes total small zone\n\n", smallzone->size );
	Com_Printf( "%8"PRIz"u bytes in %"PRIz"u small zone blocks%s\n", st.zoneBytes, st.zoneBlocks,
		st.zoneSegments > 1 ? va( " and %"PRIz"u segments", st.zoneSegments ) : "" );
	Com_Printf( "        %8"PRIz"u bytes in %"PRIz"u free blocks\n", st.freeBytes, st.freeBlocks );
	if ( st.freeBlocks > 1 ) {
		Com_Printf( "        (largest: %"PRIz"u bytes, smallest: %"PRIz"u bytes)\n", st.freeLargest, st.freeSmallest );
	}
}


/*
===============
Com_TouchMemory

Touch all known used data to make sure it is paged in
===============
*/
unsigned int Com_TouchMemory( void ) {
	const memblock_t *block;
	const memzone_t *zone;
	int		start, end;
	int		i, j;
	unsigned int sum;

	Z_CheckHeap();

	start = Sys_Milliseconds();

	sum = 0;

	j = hunk_low.permanent >> 2;
	for ( i = 0 ; i < j ; i+= 1024 ) {			// only need to touch each page
		sum += ((unsigned int *)s_hunkData)[i];
	}

	i = ( s_hunkTotal - hunk_high.permanent ) >> 2;
	j = hunk_high.permanent >> 2;
	for (  ; i < j ; i += 1024 ) {			// only need to touch each page
		sum += ((unsigned int *)s_hunkData)[i];
	}

	zone = mainzone;
	for (block = zone->blocklist.next ; ; block = block->next) {
		if ( block->tag != TAG_FREE ) {
			j = block->size >> 2;
			for ( i = 0 ; i < j ; i += 1024 ) {				// only need to touch each page
				sum += ((unsigned int *)block)[i];
			}
		}
		if ( block->next == &zone->blocklist ) {
			break; // all blocks have been hit
		}
	}

	end = Sys_Milliseconds();

	Com_Printf( "Com_TouchMemory: %i msec\n", end - start );

	return sum; // just to silent compiler warning
}


/*
=================
Com_InitSmallZoneMemory
=================
*/
static void Com_InitSmallZoneMemory( void ) {
	static byte s_buf[ 512 * 1024 ];
	int smallZoneSize;

	smallZoneSize = sizeof( s_buf );
	Com_Memset( s_buf, 0, smallZoneSize );
	smallzone = (memzone_t *)s_buf;
	Z_Init( smallzone, smallZoneSize, "small" );
}


/*
=================
Com_InitZoneMemory
=================
*/
static void Com_InitZoneMemory( void ) {
	int		mainZoneSize;
	cvar_t	*cv;

	// Please note: com_zoneMegs can only be set on the command line, and
	// not in q3config.cfg or Com_StartupVariable, as they haven't been
	// executed by this point. It's a chicken and egg problem. We need the
	// memory manager configured to handle those places where you would
	// configure the memory manager.

	// allocate the random block zone
	cv = Cvar_Get( "com_zoneMegs", XSTRING( DEF_COMZONEMEGS ), CVAR_LATCH | CVAR_ARCHIVE );

#ifndef USE_MULTI_SEGMENT
	if ( cv->integer < DEF_COMZONEMEGS )
		mainZoneSize = 1024 * 1024 * DEF_COMZONEMEGS;
	else
#endif
		mainZoneSize = cv->integer * 1024 * 1024;

	mainzone = malloc( mainZoneSize );
	if ( !mainzone ) {
		Com_Error( ERR_FATAL, "Zone data failed to allocate %i megs", mainZoneSize / (1024*1024) );
	}
	Z_Init( mainzone, mainZoneSize, "main");
}


/*
=================
Hunk_Log
=================
*/
void Hunk_Log( void ) {
	hunkblock_t	*block;
	char		buf[4096];
	int size, numBlocks;

	if ( logfile == FS_INVALID_HANDLE || !FS_Initialized() )
		return;

	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);
	for (block = hunkblocks ; block; block = block->next) {
#ifdef HUNK_DEBUG
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", block->size, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);
#endif
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}


/*
=================
Hunk_SmallLog
=================
*/
#ifdef HUNK_DEBUG
void Hunk_SmallLog( void ) {
	hunkblock_t	*block, *block2;
	char		buf[4096];
	int size, locsize, numBlocks;

	if ( logfile == FS_INVALID_HANDLE || !FS_Initialized() )
		return;

	for (block = hunkblocks ; block; block = block->next) {
		block->printed = qfalse;
	}
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk Small log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);
	for (block = hunkblocks; block; block = block->next) {
		if (block->printed) {
			continue;
		}
		locsize = block->size;
		for (block2 = block->next; block2; block2 = block2->next) {
			if (block->line != block2->line) {
				continue;
			}
			if (Q_stricmp(block->file, block2->file)) {
				continue;
			}
			size += block2->size;
			locsize += block2->size;
			block2->printed = qtrue;
		}
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", locsize, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);
		size += block->size;
		numBlocks++;
	}
	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}
#endif


/*
=================
Com_InitHunkMemory
=================
*/
static void Com_InitHunkMemory( void ) {
	cvar_t	*cv;

	// make sure the file system has allocated and "not" freed any temp blocks
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redundant routines in the file system utilizing different
	// memory systems
	if ( FS_LoadStack() != 0 ) {
		Com_Error( ERR_FATAL, "Hunk initialization failed. File system load stack not zero" );
	}

	// allocate the stack based hunk allocator
	cv = Cvar_Get( "com_hunkMegs", XSTRING( DEF_COMHUNKMEGS ), CVAR_LATCH | CVAR_ARCHIVE );

	s_hunkTotal = cv->integer * 1024 * 1024;

	s_hunkData = calloc( s_hunkTotal + 63, 1 );
	if ( !s_hunkData ) {
		Com_Error( ERR_FATAL, "Hunk data failed to allocate %i megs", s_hunkTotal / (1024*1024) );
	}

	// cacheline align
	s_hunkData = PADP( s_hunkData, 64 );
	Hunk_Clear();

	Cmd_AddCommand( "meminfo", Com_Meminfo_f );
#ifdef ZONE_DEBUG
	Cmd_AddCommand( "zonelog", Z_LogHeap );
#endif
#ifdef HUNK_DEBUG
	Cmd_AddCommand( "hunklog", Hunk_Log );
	Cmd_AddCommand( "hunksmalllog", Hunk_SmallLog );
#endif
}


/*
====================
Hunk_MemoryRemaining
====================
*/
int	Hunk_MemoryRemaining( void ) {
	int		low, high;

	low = hunk_low.permanent > hunk_low.temp ? hunk_low.permanent : hunk_low.temp;
	high = hunk_high.permanent > hunk_high.temp ? hunk_high.permanent : hunk_high.temp;

	return s_hunkTotal - ( low + high );
}


/*
===================
Hunk_SetMark

The server calls this after the level and game VM have been loaded
===================
*/
void Hunk_SetMark( void ) {
	hunk_low.mark = hunk_low.permanent;
	hunk_high.mark = hunk_high.permanent;
}


/*
=================
Hunk_ClearToMark

The client calls this before starting a vid_restart or snd_restart
=================
*/
void Hunk_ClearToMark( void ) {
	hunk_low.permanent = hunk_low.temp = hunk_low.mark;
	hunk_high.permanent = hunk_high.temp = hunk_high.mark;
}


/*
=================
Hunk_CheckMark
=================
*/
qboolean Hunk_CheckMark( void ) {
	if( hunk_low.mark || hunk_high.mark ) {
		return qtrue;
	}
	return qfalse;
}

void CL_ShutdownCGame( void );
void CL_ShutdownUI( void );
void SV_ShutdownGameProgs( void );

/*
=================
Hunk_Clear

The server calls this before shutting down or loading a new map
=================
*/
void Hunk_Clear( void ) {

#ifndef DEDICATED
	CL_ShutdownCGame();
	CL_ShutdownUI();
#endif
	SV_ShutdownGameProgs();
#ifndef DEDICATED
	CIN_CloseAllVideos();
#endif
	hunk_low.mark = 0;
	hunk_low.permanent = 0;
	hunk_low.temp = 0;
	hunk_low.tempHighwater = 0;

	hunk_high.mark = 0;
	hunk_high.permanent = 0;
	hunk_high.temp = 0;
	hunk_high.tempHighwater = 0;

	hunk_permanent = &hunk_low;
	hunk_temp = &hunk_high;

	Com_Printf( "Hunk_Clear: reset the hunk ok\n" );
	VM_Clear();
#ifdef HUNK_DEBUG
	hunkblocks = NULL;
#endif
}


static void Hunk_SwapBanks( void ) {
	hunkUsed_t	*swap;

	// can't swap banks if there is any temp already allocated
	if ( hunk_temp->temp != hunk_temp->permanent ) {
		return;
	}

	// if we have a larger highwater mark on this side, start making
	// our permanent allocations here and use the other side for temp
	if ( hunk_temp->tempHighwater - hunk_temp->permanent >
		hunk_permanent->tempHighwater - hunk_permanent->permanent ) {
		swap = hunk_temp;
		hunk_temp = hunk_permanent;
		hunk_permanent = swap;
	}
}


/*
=================
Hunk_Alloc

Allocate permanent (until the hunk is cleared) memory
=================
*/
#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( size_t size, ha_pref preference, const char *label, const char *file, int line ) {
#else
void *Hunk_Alloc( size_t size, ha_pref preference ) {
#endif
	void	*buf;

	if ( s_hunkData == NULL)
	{
		Com_Error( ERR_FATAL, "Hunk_Alloc: Hunk memory system not initialized" );
	}

	if ( size > INT_MAX ) {
		Com_Error( ERR_FATAL, "Hunk_Alloc: %"PRIz"u > INT_MAX", size );
	}

	// can't do preference if there is any temp allocated
	if (preference == h_dontcare || hunk_temp->temp != hunk_temp->permanent) {
		Hunk_SwapBanks();
	} else {
		if (preference == h_low && hunk_permanent != &hunk_low) {
			Hunk_SwapBanks();
		} else if (preference == h_high && hunk_permanent != &hunk_high) {
			Hunk_SwapBanks();
		}
	}

#ifdef HUNK_DEBUG
	size += sizeof(hunkblock_t);
#endif

	// round to cacheline
	size = PAD( size, 64 );

	if ( hunk_low.temp + hunk_high.temp + size > s_hunkTotal ) {
#ifdef HUNK_DEBUG
		Hunk_Log();
		Hunk_SmallLog();

		Com_Error(ERR_DROP, "Hunk_Alloc failed on %"PRIz"u: %s, line: %d (%s)", size, file, line, label);
#else
		Com_Error(ERR_DROP, "Hunk_Alloc failed on %"PRIz"u", size);
#endif
	}

	if ( hunk_permanent == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_permanent->permanent);
		hunk_permanent->permanent += size;
	} else {
		hunk_permanent->permanent += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}

	hunk_permanent->temp = hunk_permanent->permanent;

	Com_Memset( buf, 0, size );

#ifdef HUNK_DEBUG
	{
		hunkblock_t *block;

		block = (hunkblock_t *) buf;
		block->size = size - sizeof(hunkblock_t);
		block->file = file;
		block->label = label;
		block->line = line;
		block->next = hunkblocks;
		hunkblocks = block;
		buf = ((byte *) buf) + sizeof(hunkblock_t);
	}
#endif
	return buf;
}


/*
=================
Hunk_AllocateTempMemory

This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
=================
*/
void *Hunk_AllocateTempMemory( size_t size ) {
	void		*buf;
	hunkHeader_t	*hdr;

	// return a Z_Malloc'd block if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redundant routines in the file system utilizing different
	// memory systems
	if ( s_hunkData == NULL )
	{
		return Z_Malloc(size);
	}

	if ( size > INT_MAX ) {
		Com_Error( ERR_FATAL, "Hunk_AllocateTempMemory: %"PRIz"u > INT_MAX", size );
	}

	Hunk_SwapBanks();

	size = PAD(size, sizeof(intptr_t)) + sizeof( hunkHeader_t );

	if ( hunk_temp->temp + hunk_permanent->permanent + size > s_hunkTotal ) {
		Com_Error( ERR_DROP, "Hunk_AllocateTempMemory: failed on %"PRIz"u", size );
	}

	if ( hunk_temp == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_temp->temp);
		hunk_temp->temp += size;
	} else {
		hunk_temp->temp += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp );
	}

	if ( hunk_temp->temp > hunk_temp->tempHighwater ) {
		hunk_temp->tempHighwater = hunk_temp->temp;
	}

	hdr = (hunkHeader_t *)buf;
	buf = (void *)(hdr+1);

	hdr->magic = HUNK_MAGIC;
	hdr->size = size;

	// don't bother clearing, because we are going to load a file over it
	return buf;
}


/*
==================
Hunk_FreeTempMemory
==================
*/
void Hunk_FreeTempMemory( void *buf ) {
	hunkHeader_t	*hdr;

	// free with Z_Free if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redundant routines in the file system utilizing different
	// memory systems
	if ( s_hunkData == NULL )
	{
		Z_Free(buf);
		return;
	}

	hdr = ( (hunkHeader_t *)buf ) - 1;
	if ( hdr->magic != HUNK_MAGIC ) {
		Com_Error( ERR_FATAL, "Hunk_FreeTempMemory: bad magic" );
	}

	hdr->magic = HUNK_FREE_MAGIC;

	// this only works if the files are freed in stack order,
	// otherwise the memory will stay around until Hunk_ClearTempMemory
	if ( hunk_temp == &hunk_low ) {
		if ( hdr == (void *)(s_hunkData + hunk_temp->temp - hdr->size ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	} else {
		if ( hdr == (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	}
}


/*
=================
Hunk_ClearTempMemory

The temp space is no longer needed.  If we have left more
touched but unused memory on this side, have future
permanent allocs use this side.
=================
*/
void Hunk_ClearTempMemory( void ) {
	if ( s_hunkData != NULL ) {
		hunk_temp->temp = hunk_temp->permanent;
	}
}

/*
===================================================================

EVENTS AND JOURNALING

In addition to these events, .cfg files are also copied to the
journaled file
===================================================================
*/

// bk001129 - here we go again: upped from 64
#define MAX_PUSHED_EVENTS               256
// bk001129 - init, also static
static int com_pushedEventsHead = 0;
static int com_pushedEventsTail = 0;
// bk001129 - static
static sysEvent_t com_pushedEvents[MAX_PUSHED_EVENTS];

/*
=================
Com_InitJournaling
=================
*/
void Com_InitJournaling( void ) {
	Com_StartupVariable( "journal" );
	com_journal = Cvar_Get( "journal", "0", CVAR_INIT );
	if ( !com_journal->integer ) {
		return;
	}

	if ( com_journal->integer == 1 ) {
#ifdef __MACOS__    //DAJ MacOS file typing
		{
			extern _MSL_IMP_EXP_C long _fcreator, _ftype;
			_ftype = 'TEXT';
			_fcreator = 'WlfM';
		}
#endif
		Com_Printf( "Journaling events\n" );
		com_journalFile = FS_FOpenFileWrite( "journal.dat" );
		com_journalDataFile = FS_FOpenFileWrite( "journaldata.dat" );
	} else if ( com_journal->integer == 2 ) {
		Com_Printf( "Replaying journaled events\n" );
		FS_FOpenFileRead( "journal.dat", &com_journalFile, qtrue );
		FS_FOpenFileRead( "journaldata.dat", &com_journalDataFile, qtrue );
	}

	if ( !com_journalFile || !com_journalDataFile ) {
		Cvar_Set( "com_journal", "0" );
		com_journalFile = 0;
		com_journalDataFile = 0;
		Com_Printf( "Couldn't open journal files\n" );
	}
}

/*
=================
Com_GetRealEvent
=================
*/
sysEvent_t  Com_GetRealEvent( void ) {
	int r;
	sysEvent_t ev;

	// either get an event from the system or the journal file
	if ( com_journal->integer == 2 ) {
		r = FS_Read( &ev, sizeof( ev ), com_journalFile );
		if ( r != sizeof( ev ) ) {
			Com_Error( ERR_FATAL, "Error reading from journal file" );
		}
		if ( ev.evPtrLength ) {
			ev.evPtr = Z_Malloc( ev.evPtrLength );
			r = FS_Read( ev.evPtr, ev.evPtrLength, com_journalFile );
			if ( r != ev.evPtrLength ) {
				Com_Error( ERR_FATAL, "Error reading from journal file" );
			}
		}
	} else {
		ev = Sys_GetEvent();

		// write the journal value out if needed
		if ( com_journal->integer == 1 ) {
			r = FS_Write( &ev, sizeof( ev ), com_journalFile );
			if ( r != sizeof( ev ) ) {
				Com_Error( ERR_FATAL, "Error writing to journal file" );
			}
			if ( ev.evPtrLength ) {
				r = FS_Write( ev.evPtr, ev.evPtrLength, com_journalFile );
				if ( r != ev.evPtrLength ) {
					Com_Error( ERR_FATAL, "Error writing to journal file" );
				}
			}
		}
	}

	return ev;
}


/*
=================
Com_InitPushEvent
=================
*/
// bk001129 - added
void Com_InitPushEvent( void ) {
	// clear the static buffer array
	// this requires SE_NONE to be accepted as a valid but NOP event
	memset( com_pushedEvents, 0, sizeof( com_pushedEvents ) );
	// reset counters while we are at it
	// beware: GetEvent might still return an SE_NONE from the buffer
	com_pushedEventsHead = 0;
	com_pushedEventsTail = 0;
}


/*
=================
Com_PushEvent
=================
*/
void Com_PushEvent( sysEvent_t *event ) {
	sysEvent_t      *ev;
	static int printedWarning = 0; // bk001129 - init, bk001204 - explicit int

	ev = &com_pushedEvents[ com_pushedEventsHead & ( MAX_PUSHED_EVENTS - 1 ) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = qtrue;
			Com_Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = qfalse;
	}

	*ev = *event;
	com_pushedEventsHead++;
}

/*
=================
Com_GetEvent
=================
*/
sysEvent_t  Com_GetEvent( void ) {
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ ( com_pushedEventsTail - 1 ) & ( MAX_PUSHED_EVENTS - 1 ) ];
	}
	return Com_GetRealEvent();
}

/*
=================
Com_RunAndTimeServerPacket
=================
*/
void Com_RunAndTimeServerPacket( const netadr_t *evFrom, msg_t *buf ) {
	int t1, t2, msec;

	t1 = 0;

	if ( com_speeds->integer ) {
		t1 = Sys_Milliseconds();
	}

	SV_PacketEvent( evFrom, buf );

	if ( com_speeds->integer ) {
		t2 = Sys_Milliseconds();
		msec = t2 - t1;
		if ( com_speeds->integer == 3 ) {
			Com_Printf( "SV_PacketEvent time: %i\n", msec );
		}
	}
}

/*
=================
Com_EventLoop

Returns last event time
=================
*/
int Com_EventLoop( void ) {
	sysEvent_t ev;

#ifndef DEDICATED
	byte		bufData[ MAX_MSGLEN_BUF ];
	msg_t		buf;

	MSG_Init( &buf, bufData, MAX_MSGLEN );
#endif // !DEDICATED

	while ( 1 ) {
		ev = Com_GetEvent();

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
			// manually send packet events for the loopback channel
#ifndef DEDICATED
			netadr_t evFrom;
			while ( NET_GetLoopPacket( NS_CLIENT, &evFrom, &buf ) ) {
				CL_PacketEvent( &evFrom, &buf );
			}
			while ( NET_GetLoopPacket( NS_SERVER, &evFrom, &buf ) ) {
				// if the server just shut down, flush the events
				if ( com_sv_running->integer ) {
					Com_RunAndTimeServerPacket( &evFrom, &buf );
				}
			}
#endif // !DEDICATED
			return ev.evTime;
		}


		switch ( ev.evType ) {
#ifndef DEDICATED
		case SE_KEY:
			CL_KeyEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CHAR:
			CL_CharEvent( ev.evValue );
			break;
		case SE_MOUSE:
			CL_MouseEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_JOYSTICK_AXIS:
			CL_JoystickEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
#endif // !DEDICATED
		case SE_CONSOLE:
			Cbuf_AddText( (char *)ev.evPtr );
			Cbuf_AddText( "\n" );
			break;
		default:
			// bk001129 - was ev.evTime
			Com_Error( ERR_FATAL, "Com_EventLoop: bad event type %i", ev.evType );
			break;
		// case SE_PACKET:
		// 	// this cvar allows simulation of connections that
		// 	// drop a lot of packets.  Note that loopback connections
		// 	// don't go through here at all.
		// 	if ( com_dropsim->value > 0 ) {
		// 		static int seed;

		// 		if ( Q_random( &seed ) < com_dropsim->value ) {
		// 			break;      // drop this packet
		// 		}
		// 	}

		// 	evFrom = *(netadr_t *)ev.evPtr;
		// 	buf.cursize = ev.evPtrLength - sizeof( evFrom );

		// 	// we must copy the contents of the message out, because
		// 	// the event buffers are only large enough to hold the
		// 	// exact payload, but channel messages need to be large
		// 	// enough to hold fragment reassembly
		// 	if ( (unsigned)buf.cursize > buf.maxsize ) {
		// 		Com_Printf( "Com_EventLoop: oversize packet\n" );
		// 		continue;
		// 	}
		// 	memcpy( buf.data, ( byte * )( (netadr_t *)ev.evPtr + 1 ), buf.cursize );
		// 	if ( com_sv_running->integer ) {
		// 		Com_RunAndTimeServerPacket( &evFrom, &buf );
		// 	} else {
		// 		CL_PacketEvent( &evFrom, &buf );
		// 	}
		// 	break;
		}

		// free any block data
		if ( ev.evPtr ) {
			Z_Free( ev.evPtr );
			ev.evPtr = NULL;
		}
	}

	return 0;   // never reached
}

/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds( void ) {
	sysEvent_t ev;

	// get events and push them until we get a null event with the current time
	do {

		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );

	return ev.evTime;
}

//============================================================================

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
static void Com_Error_f( void ) {
	if ( Cmd_Argc() > 1 ) {
		Com_Error( ERR_DROP, "Testing drop error" );
	} else {
		Com_Error( ERR_FATAL, "Testing fatal error" );
	}
}


/*
=============
Com_Freeze_f

Just freeze in place for a given number of seconds to test
error recovery
=============
*/
static void Com_Freeze_f( void ) {
	float s;
	int start, now;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "freeze <seconds>\n" );
		return;
	}
	s = atof( Cmd_Argv( 1 ) );

	start = Com_Milliseconds();

	while ( 1 ) {
		now = Com_Milliseconds();
		if ( ( now - start ) * 0.001 > s ) {
			break;
		}
	}
}

/*
=================
Com_Crash_f

A way to force a bus error for development reasons
=================
*/
static void Com_Crash_f( void ) {
	//*( int * ) 0 = 0x12345678;
}

qboolean CL_CDKeyValidate( const char *key, const char *checksum );

// TTimo: centralizing the cl_cdkey stuff after I discovered a buffer overflow problem with the dedicated server version
//   not sure it's necessary to have different defaults for regular and dedicated, but I don't want to take the risk
#ifndef DEDICATED
char cl_cdkey[34] = "                                ";
#else
char cl_cdkey[34] = "123456789";
#endif

/*
=================
Com_ReadCDKey
=================
*/
#define CDKEY_SALT			"]=q.0xFF^"

/*
=================
Com_ReadCDKey
=================
*/
int Com_ReadCDKey( const char *filename ) {
	fileHandle_t f;
	char buffer[33];
	char fbuffer[MAX_OSPATH];

	sprintf( fbuffer, "%s/rtcwkey", filename );

	FS_SV_FOpenFileRead( fbuffer, &f );
	if ( !f ) {
		//Com_WriteNewKey(filename);
		//Q_strncpyz( cl_cdkey, "                ", 17 );
		return 0;
	}

	Com_Memset( buffer, 0, sizeof( buffer ) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if ( CL_CDKeyValidate( buffer, NULL ) ) {
		Q_strncpyz(cl_cdkey, buffer, 17);
	} else {
		Q_strncpyz( cl_cdkey, "                ", 17 );
	}

	#ifndef DEDICATED
        Cvar_Set("cl_guid", Com_MD5(buffer, CDKEY_LEN, CDKEY_SALT, sizeof(CDKEY_SALT) - 1, 0));
    #endif
    return 1;
}

/*
=================
Com_ReadCDKey
=================
*/
void Com_AppendCDKey( const char *filename ) {
	fileHandle_t f;
	char buffer[33];
	char fbuffer[MAX_OSPATH];

	sprintf( fbuffer, "%s/rtcwkey", filename );

	FS_SV_FOpenFileRead( fbuffer, &f );
	if ( !f ) {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
		return;
	}

	Com_Memset( buffer, 0, sizeof( buffer ) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if ( CL_CDKeyValidate( buffer, NULL ) ) {
		strcat( &cl_cdkey[16], buffer );
	} else {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
	}
}

#ifndef DEDICATED // bk001204
/*
=================
Com_WriteCDKey
=================
*/
static void Com_WriteCDKey( const char *filename, const char *ikey ) {
	fileHandle_t f;
	char fbuffer[MAX_OSPATH];
	char key[17];


	sprintf( fbuffer, "%s/rtcwkey", filename );


	Q_strncpyz( key, ikey, 17 );

	if ( !CL_CDKeyValidate( key, NULL ) ) {
		return;
	}

#ifdef __MACOS__    //DAJ MacOS file typing
	{
		extern _MSL_IMP_EXP_C long _fcreator, _ftype;
		_ftype = 'TEXT';
		_fcreator = 'WlfM';
	}
#endif

	f = FS_SV_FOpenFileWrite( fbuffer );
	if ( !f ) {
		Com_Printf( "Couldn't write %s.\n", filename );
		return;
	}

	FS_Write( key, 16, f );

	FS_Printf( f, "\n// generated by RTCW, do not modify\r\n" );
	FS_Printf( f, "// Do not give this file to ANYONE.\r\n" );
#ifdef __MACOS__
	FS_Printf( f, "// Aspyr will NOT ask you to send this file to them.\r\n" );
#else
	FS_Printf( f, "// id Software and Activision will NOT ask you to send this file to them.\r\n" );
#endif
	FS_FCloseFile( f );
}
#endif

void Com_SetRecommended() {
	cvar_t *cv;
	qboolean goodVideo;
	qboolean goodCPU;
	// will use this for recommended settings as well.. do i outside the lower check so it gets done even with command line stuff
	cv = Cvar_Get( "r_highQualityVideo", "1", CVAR_ARCHIVE );
	goodVideo = ( cv && cv->integer );
	goodCPU = Sys_GetHighQualityCPU();

	if ( goodVideo && goodCPU ) {
		Com_Printf( "Found high quality video and CPU\n" );
		Cbuf_AddText( "exec highVidhighCPU.cfg\n" );
	} else if ( goodVideo && !goodCPU ) {
		Cbuf_AddText( "exec highVidlowCPU.cfg\n" );
		Com_Printf( "Found high quality video and low quality CPU\n" );
	} else if ( !goodVideo && goodCPU ) {
		Cbuf_AddText( "exec lowVidhighCPU.cfg\n" );
		Com_Printf( "Found low quality video and high quality CPU\n" );
	} else {
		Cbuf_AddText( "exec lowVidlowCPU.cfg\n" );
		Com_Printf( "Found low quality video and low quality CPU\n" );
	}

// (SA) set the cvar so the menu will reflect this on first run
//	Cvar_Set("ui_glCustom", "999");	// 'recommended'
}


/*
** --------------------------------------------------------------------------------
**
** PROCESSOR STUFF
**
** --------------------------------------------------------------------------------
*/

#ifdef USE_AFFINITY_MASK
static uint64_t eCoreMask;
static uint64_t pCoreMask;
static uint64_t affinityMask; // saved at startup
#endif

#if (idx64 || id386)

#if defined _MSC_VER
#include <intrin.h>
static void CPUID( int func, unsigned int *regs )
{
	__cpuid( (int*)regs, func );
}

#ifdef USE_AFFINITY_MASK
#if idx64
void CPUID_EX( int func, int param, unsigned int *regs )
{
#if defined(_MSC_VER)
    int cpu_info[4] = {-1};
    __cpuidex(cpu_info, (int)func, (int)param);
    regs[0] = cpu_info[0];
    regs[0] = cpu_info[1];
    regs[0] = cpu_info[2];
    regs[0] = cpu_info[3];
#endif
}
#else
void CPUID_EX( int func, int param, unsigned int *regs )
{

	__asm {
		push edi
		mov eax, func
		mov ecx, param
		cpuid
		mov edi, regs
		mov [edi +0], eax
		mov [edi +4], ebx
		mov [edi +8], ecx
		mov [edi+12], edx
		pop edi
	}
}
#endif // !idx64
#endif // USE_AFFINITY_MASK

#else // clang/gcc/mingw

static void CPUID( int func, unsigned int *regs )
{
	__asm__ __volatile__( "cpuid" :
		"=a"(regs[0]),
		"=b"(regs[1]),
		"=c"(regs[2]),
		"=d"(regs[3]) :
		"a"(func) );
}

#ifdef USE_AFFINITY_MASK
static void CPUID_EX( int func, int param, unsigned int *regs )
{
	__asm__ __volatile__( "cpuid" :
		"=a"(regs[0]),
		"=b"(regs[1]),
		"=c"(regs[2]),
		"=d"(regs[3]) :
		"a"(func),
		"c"(param) );
}
#endif // USE_AFFINITY_MASK

#endif  // clang/gcc/mingw

static void Sys_GetProcessorId( char *vendor )
{
	uint32_t regs[4]; // EAX, EBX, ECX, EDX
	uint32_t cpuid_level_ex;
	char vendor_str[12 + 1]; // short CPU vendor string

	// setup initial features
#if idx64
	CPU_Flags |= CPU_SSE | CPU_SSE2 | CPU_FCOM;
#else
	CPU_Flags = 0;
#endif
	vendor[0] = '\0';

	CPUID( 0x80000000, regs );
	cpuid_level_ex = regs[0];

	// get CPUID level & short CPU vendor string
	CPUID( 0x0, regs );
	memcpy(vendor_str + 0, (char*)&regs[1], 4);
	memcpy(vendor_str + 4, (char*)&regs[3], 4);
	memcpy(vendor_str + 8, (char*)&regs[2], 4);
	vendor_str[12] = '\0';

	// get CPU feature bits
	CPUID( 0x1, regs );

	// bit 15 of EDX denotes CMOV/FCMOV/FCOMI existence
	if ( regs[3] & ( 1 << 15 ) )
		CPU_Flags |= CPU_FCOM;

	// bit 23 of EDX denotes MMX existence
	if ( regs[3] & ( 1 << 23 ) )
		CPU_Flags |= CPU_MMX;

	// bit 25 of EDX denotes SSE existence
	if ( regs[3] & ( 1 << 25 ) )
		CPU_Flags |= CPU_SSE;

	// bit 26 of EDX denotes SSE2 existence
	if ( regs[3] & ( 1 << 26 ) )
		CPU_Flags |= CPU_SSE2;

	// bit 0 of ECX denotes SSE3 existence
	//if ( regs[2] & ( 1 << 0 ) )
	//	CPU_Flags |= CPU_SSE3;

	// bit 19 of ECX denotes SSE41 existence
	if ( regs[ 2 ] & ( 1 << 19 ) )
		CPU_Flags |= CPU_SSE41;

	if ( vendor ) {
		if ( cpuid_level_ex >= 0x80000004 ) {
			// read CPU Brand string
			uint32_t i;
			for ( i = 0x80000002; i <= 0x80000004; i++) {
				CPUID( i, regs );
				memcpy( vendor+0, (char*)&regs[0], 4 );
				memcpy( vendor+4, (char*)&regs[1], 4 );
				memcpy( vendor+8, (char*)&regs[2], 4 );
				memcpy( vendor+12, (char*)&regs[3], 4 );
				vendor[16] = '\0';
				vendor += strlen( vendor );
			}
		} else {
			const int print_flags = CPU_Flags;
			vendor = Q_stradd( vendor, vendor_str );
			if (print_flags) {
				// print features
				strcat(vendor, " w/");
				if (print_flags & CPU_FCOM)
					strcat(vendor, " CMOV");
				if (print_flags & CPU_MMX)
					strcat(vendor, " MMX");
				if (print_flags & CPU_SSE)
					strcat(vendor, " SSE");
				if (print_flags & CPU_SSE2)
					strcat(vendor, " SSE2");
				//if ( CPU_Flags & CPU_SSE3 )
				//	strcat( vendor, " SSE3" );
				if (print_flags & CPU_SSE41)
					strcat(vendor, " SSE4.1");
			}
		}
	}
}


#ifdef USE_AFFINITY_MASK
static void DetectCPUCoresConfig( void )
{
	uint32_t regs[4];
	uint32_t i;

	// get highest function parameter and vendor id
	CPUID( 0x0, regs );
	if ( regs[1] != 0x756E6547 || regs[2] != 0x6C65746E || regs[3] != 0x49656E69 || regs[0] < 0x1A ) {
		// non-intel signature or too low cpuid level - unsupported
		eCoreMask = pCoreMask = affinityMask;
		return;
	}

	eCoreMask = 0;
	pCoreMask = 0;

	for ( i = 0; i < sizeof( affinityMask ) * 8; i++ ) {
		const uint64_t mask = 1ULL << i;
		if ( (mask & affinityMask) && Sys_SetAffinityMask( mask ) ) {
			CPUID_EX( 0x1A, 0x0, regs );
			switch ( (regs[0] >> 24) & 0xFF ) {
				case 0x20: eCoreMask |= mask; break;
				case 0x40: pCoreMask |= mask; break;
				default: // non-existing leaf
					eCoreMask = pCoreMask = 0;
					break;
			}
		}
	}

	// restore original affinity
	Sys_SetAffinityMask( affinityMask );

	if ( pCoreMask == 0 || eCoreMask == 0 ) {
		// if either mask is empty - assume non-hybrid configuration
		eCoreMask = pCoreMask = affinityMask;
	}
}
#endif // USE_AFFINITY_MASK

#else // non-x86

#ifndef __linux__

static void Sys_GetProcessorId( char *vendor )
{
	Com_sprintf( vendor, 100, "%s", ARCH_STRING );
#ifdef _WIN32
	CPU_Flags |= CPU_ARMv7 | CPU_IDIVA | CPU_VFPv3;
#endif
}

#else // __linux__

#include <sys/auxv.h>

#if arm32
#include <asm/hwcap.h>
#endif

static void Sys_GetProcessorId( char *vendor )
{
#if arm32
	const char *platform;
	long hwcaps;
	CPU_Flags = 0;

	platform = (const char*)getauxval( AT_PLATFORM );

	if ( !platform || *platform == '\0' ) {
		platform = "(unknown)";
	}

	if ( platform[0] == 'v' || platform[0] == 'V' ) {
		if ( atoi( platform + 1 ) >= 7 ) {
			CPU_Flags |= CPU_ARMv7;
		}
	}

	Com_sprintf( vendor, 100, "ARM %s", platform );
	hwcaps = getauxval( AT_HWCAP );
	if ( hwcaps & ( HWCAP_IDIVA | HWCAP_VFPv3 ) ) {
		strcat( vendor, " /w" );

		if ( hwcaps & HWCAP_IDIVA ) {
			CPU_Flags |= CPU_IDIVA;
			strcat( vendor, " IDIVA" );
		}

		if ( hwcaps & HWCAP_VFPv3 ) {
			CPU_Flags |= CPU_VFPv3;
			strcat( vendor, " VFPv3" );
		}

		if ( ( CPU_Flags & ( CPU_ARMv7 | CPU_VFPv3 ) ) == ( CPU_ARMv7 | CPU_VFPv3 ) ) {
			strcat( vendor, " QVM-bytecode" );
		}
	}
#else // !arm32
	CPU_Flags = 0;
#if arm64
	Com_sprintf( vendor, 100, "%s", ARCH_STRING );
#else
	Com_sprintf( vendor, 128, "%s %s", ARCH_STRING, (const char*)getauxval( AT_PLATFORM ) );
#endif
#endif // !arm32
}

#endif // __linux__

#endif // non-x86

static int hex_code( const int code ) {
	if ( code >= '0' && code <= '9' ) {
		return code - '0';
	}
	if ( code >= 'A' && code <= 'F' ) {
		return code - 'A' + 10;
	}
	if ( code >= 'a' && code <= 'f' ) {
		return code - 'a' + 10;
	}
	return -1;
}


static const char *parseAffinityMask( const char *str, uint64_t *outv, int level ) {
	uint64_t v, mask = 0;

	while ( *str != '\0' ) {
		if ( *str == 'A' || *str == 'a' ) {
			mask = affinityMask;
			++str;
			continue;
		}
		else if ( *str == 'P' || *str == 'p' ) {
			mask = pCoreMask;
			++str;
			continue;
		}
		else if ( *str == 'E' || *str == 'e' ) {
			mask = eCoreMask;
			++str;
			continue;
		}
		else if ( *str == '0' && (str[1] == 'x' || str[1] == 'X') && (v = hex_code( str[2] )) >= 0 ) {
			int hex;
			str += 3; // 0xH
			while ( (hex = hex_code( *str )) >= 0 ) {
				v = v * 16 + hex;
				str++;
			}
			mask = v;
			continue;
		}
		else if ( *str >= '0' && *str <= '9' ) {
			mask = *str++ - '0';
			while ( *str >= '0' && *str <= '9' ) {
				mask = mask * 10 + *str - '0';
				++str;
			}
			continue;
		}

		if ( level == 0 ) {
			while ( *str == '+' || *str == '-' ) {
				str = parseAffinityMask( str + 1, &v, level + 1 );
				switch ( *str ) {
					case '+': mask |= v; break;
					case '-': mask &= ~v; break;
					default: str = ""; break;
				}
			}
			if ( *str != '\0' ) {
				++str; // skip unknown characters
			}
		} else {
			break;
		}
	}

	*outv = mask;
	return str;
}


// parse and set affinity mask
static void Com_SetAffinityMask( const char *str )
{
	uint64_t mask = 0;

	parseAffinityMask( str, &mask, 0 );

	if ( ( mask & affinityMask ) == 0 ) {
		mask = affinityMask; // reset to default
	}

	if ( mask != 0 ) {
		Sys_SetAffinityMask( mask );
	}
}

/*
=================
Com_Init
=================
*/
void Com_Init( char *commandLine ) {
	char    *s;
	int	qport;

	// get the initial time base
	Sys_Milliseconds();

	// TTimo gcc warning: variable `safeMode' might be clobbered by `longjmp' or `vfork'
	volatile qboolean safeMode = qtrue;

	Com_Printf( "%s %s %s\n", Q3_VERSION, CPUSTRING, __DATE__ );

	if ( setjmp( abortframe ) ) {
		Sys_Error( "Error during initialization" );
	}

	// bk001129 - do this before anything else decides to push events
	Com_InitPushEvent();

	Com_InitSmallZoneMemory();
	Cvar_Init();

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	Com_ParseCommandLine( commandLine );

	Cbuf_Init();

	Com_InitZoneMemory();
	Cmd_Init();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get the developer cvar set as early as possible
	Com_StartupVariable( "developer" );

	// done early so bind command exists
	CL_InitKeyCommands();

	FS_InitFilesystem();

	Com_InitJournaling();

	// DHM - Nerve
#ifndef UPDATE_SERVER
	Cbuf_AddText( "exec default.cfg\n" );
	Cbuf_AddText( "exec language.cfg\n" );     // NERVE - SMF

	// skip the q3config.cfg if "safe" is on the command line
	if ( !Com_SafeMode() ) {
		safeMode = qfalse;
		Cbuf_AddText( "exec wolfconfig_mp.cfg\n" );
	}

	Cbuf_AddText( "exec autoexec.cfg\n" );
#endif

	Cbuf_Execute();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get dedicated here for proper hunk megs initialization
#ifdef UPDATE_SERVER
	com_dedicated = Cvar_Get( "dedicated", "1", CVAR_LATCH );
#elif DEDICATED
	// TTimo: default to internet dedicated, not LAN dedicated
	com_dedicated = Cvar_Get( "dedicated", "2", CVAR_ROM );
#else
	com_dedicated = Cvar_Get( "dedicated", "0", CVAR_LATCH );
#endif
	// allocate the stack based hunk allocator
	Com_InitHunkMemory();

	// if any archived cvars are modified after this, we will trigger a writing
	// of the config file
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	//
	// init commands and vars
	//
	com_maxfps = Cvar_Get( "com_maxfps", "85", CVAR_ARCHIVE );
	com_blood = Cvar_Get( "com_blood", "1", CVAR_ARCHIVE );

	com_developer = Cvar_Get( "developer", "0", CVAR_TEMP );
	com_logfile = Cvar_Get( "logfile", "0", CVAR_TEMP );

	com_yieldCPU = Cvar_Get( "com_yieldCPU", "1", CVAR_ARCHIVE );
	com_affinityMask = Cvar_Get( "com_affinityMask", "", CVAR_ARCHIVE );
	com_affinityMask->modified = qfalse;

	com_timescale = Cvar_Get( "timescale", "1", CVAR_CHEAT | CVAR_SYSTEMINFO );
	com_fixedtime = Cvar_Get( "fixedtime", "0", CVAR_CHEAT );
	com_showtrace = Cvar_Get( "com_showtrace", "0", CVAR_CHEAT );
	com_dropsim = Cvar_Get( "com_dropsim", "0", CVAR_CHEAT );
	com_viewlog = Cvar_Get( "viewlog", "0", CVAR_CHEAT );
	com_speeds = Cvar_Get( "com_speeds", "0", 0 );
	com_timedemo = Cvar_Get( "timedemo", "0", CVAR_CHEAT );
	com_cameraMode = Cvar_Get( "com_cameraMode", "0", CVAR_CHEAT );

	cl_paused = Cvar_Get( "cl_paused", "0", CVAR_ROM );
	sv_paused = Cvar_Get( "sv_paused", "0", CVAR_ROM );
	sv_packetdelay = Cvar_Get( "sv_packetdelay", "0", CVAR_CHEAT );
	cl_packetdelay = Cvar_Get( "cl_packetdelay", "0", CVAR_CHEAT );
	com_sv_running = Cvar_Get( "sv_running", "0", CVAR_ROM );
	com_cl_running = Cvar_Get( "cl_running", "0", CVAR_ROM );
	com_buildScript = Cvar_Get( "com_buildScript", "0", 0 );

	com_introPlayed = Cvar_Get( "com_introplayed", "0", CVAR_ARCHIVE );
	com_recommendedSet = Cvar_Get( "com_recommendedSet", "0", CVAR_ARCHIVE );

#if defined( _WIN32 ) && defined( _DEBUG )
	com_noErrorInterrupt = Cvar_Get( "com_noErrorInterrupt", "0", 0 );
#endif

	com_hunkused = Cvar_Get( "com_hunkused", "0", 0 );

	if ( com_dedicated->integer ) {
		if ( !com_viewlog->integer ) {
			Cvar_Set( "viewlog", "1" );
		}
	}

	if ( com_developer && com_developer->integer ) {
		Cmd_AddCommand( "error", Com_Error_f );
		Cmd_AddCommand( "crash", Com_Crash_f );
		Cmd_AddCommand( "freeze", Com_Freeze_f );
	}
	Cmd_AddCommand( "quit", Com_Quit_f );
	Cmd_AddCommand( "writeconfig", Com_WriteConfig_f );

	s = va( "%s %s %s", Q3_VERSION, CPUSTRING, __DATE__ );
	com_version = Cvar_Get( "version", s, CVAR_ROM | CVAR_SERVERINFO );

	Sys_Init();

	// CPU detection
	Cvar_Get( "sys_cpustring", "detect", CVAR_ROM | CVAR_NORESTART );
	if ( !Q_stricmp( Cvar_VariableString( "sys_cpustring" ), "detect" ) ) {
		char vendor[128];
		Com_Printf( "...detecting CPU, found " );
		Sys_GetProcessorId( vendor );
		Cvar_Set( "sys_cpustring", vendor );
	}
	Com_Printf( "%s\n", Cvar_VariableString( "sys_cpustring" ) );

#ifdef USE_AFFINITY_MASK
	// get initial process affinity - we will respect it when setting custom affinity masks
	eCoreMask = pCoreMask = affinityMask = Sys_GetAffinityMask();
#if (idx64 || id386)
	DetectCPUCoresConfig();
#endif
	if ( com_affinityMask->string[0] != '\0' ) {
		Com_SetAffinityMask( com_affinityMask->string );
		com_affinityMask->modified = qfalse;
	}
#endif
	// Pick a random port value
	Com_RandomBytes( (byte*)&qport, sizeof( qport ) );
	Netchan_Init( qport & 0xffff );
	VM_Init();
	SV_Init();

	com_dedicated->modified = qfalse;
	if ( !com_dedicated->integer ) {
		CL_Init();
		Sys_ShowConsole( com_viewlog->integer, qfalse );
	}


	// add + commands from command line
	if ( !Com_AddStartupCommands() ) {
		// if the user didn't give any commands, run default action
	}

	// start in full screen ui mode
	Cvar_Set( "r_uiFullScreen", "1" );

#ifndef DEDICATED
	CL_StartHunkUsers();
#endif
	
	// set com_frameTime so that if a map is started on the
	// command line it will still be able to count on com_frameTime
	// being random enough for a serverid
	// lastTime = com_frameTime = Com_Milliseconds();
	Com_FrameInit();

	// delay this so potential wicked3d dll can find a wolf window
	if ( !com_dedicated->integer ) {
		Sys_ShowConsole( com_viewlog->integer, qfalse );
	}

	// NERVE - SMF - force recommendedSet and don't do vid_restart if in safe mode
	if ( !com_recommendedSet->integer && !safeMode ) {
		Com_SetRecommended();
		Cbuf_ExecuteText( EXEC_APPEND, "vid_restart\n" );
	}
	Cvar_Set( "com_recommendedSet", "1" );

	if ( !com_dedicated->integer ) {
		Cbuf_AddText( "cinematic gmlogo.RoQ\n" );
		if ( !com_introPlayed->integer ) {
			Cvar_Set( com_introPlayed->name, "1" );
			Cvar_Set( "nextmap", "cinematic wolfintro.RoQ" );
		}
	}

	Threads_Init();

	com_fullyInitialized = qtrue;
	Com_Printf( "--- Common Initialization Complete ---\n" );

	NET_Init();
}

//==================================================================

void Com_WriteConfigToFile( const char *filename ) {
	fileHandle_t f;

#ifdef __MACOS__    //DAJ MacOS file typing
	{
		extern _MSL_IMP_EXP_C long _fcreator, _ftype;
		_ftype = 'TEXT';
		_fcreator = 'R*ch';
	}
#endif
	f = FS_FOpenFileWrite( filename );
	if ( !f ) {
		Com_Printf( "Couldn't write %s.\n", filename );
		return;
	}

	FS_Printf( f, "// generated by RTCW, do not modify\n" );
	Key_WriteBindings( f );
	Cvar_WriteVariables( f );
	FS_FCloseFile( f );
}


/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config file if modified
===============
*/
void Com_WriteConfiguration( void ) {
#ifndef DEDICATED // bk001204
	cvar_t  *fs;
#endif
	// if we are quiting without fully initializing, make sure
	// we don't write out anything
	if ( !com_fullyInitialized ) {
		return;
	}

	if ( !( cvar_modifiedFlags & CVAR_ARCHIVE ) ) {
		return;
	}
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	Com_WriteConfigToFile( "wolfconfig_mp.cfg" );

	// bk001119 - tentative "not needed for dedicated"
#ifndef DEDICATED
	fs = Cvar_Get( "fs_game", "wolfpro", CVAR_INIT | CVAR_SYSTEMINFO );
	if ( UI_usesUniqueCDKey() && fs && fs->string[0] != 0 ) {
		Com_WriteCDKey( fs->string, &cl_cdkey[16] );
	} else {
		Com_WriteCDKey( "main", cl_cdkey );
	}
#endif
}


/*
===============
Com_WriteConfig_f

Write the config file to a specific name
===============
*/
void Com_WriteConfig_f( void ) {
	char filename[MAX_QPATH];

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Com_Printf( "Writing %s.\n", filename );
	Com_WriteConfigToFile( filename );
}

/*
================
Com_ModifyMsec
================
*/
int Com_ModifyMsec( int msec ) {
	int clampTime;

	//
	// modify time for debugging values
	//
	if ( com_fixedtime->integer ) {
		msec = com_fixedtime->integer;
	} else if ( com_timescale->value ) {
		msec *= com_timescale->value;
//	} else if (com_cameraMode->integer) {
//		msec *= com_timescale->value;
	}

	// don't let it scale below 1 msec
	if ( msec < 1 && com_timescale->value ) {
		msec = 1;
	}

	if ( com_dedicated->integer ) {
		// dedicated servers don't want to clamp for a much longer
		// period, because it would mess up all the client's views
		// of time.
		if ( msec > 500 && msec < 500000 ) {
			Com_Printf( "Hitch warning: %i msec frame time\n", msec );
		}
		clampTime = 5000;
	} else
	if ( !com_sv_running->integer ) {
		// clients of remote servers do not want to clamp time, because
		// it would skew their view of the server's time temporarily
		clampTime = 5000;
	} else {
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime ) {
		msec = clampTime;
	}

	return msec;
}


static void Com_FrameSleep( qbool demoPlayback )
{
	// "timedemo" playback means we run at full speed
	if ( demoPlayback && com_timedemo->integer )
		return;

	// decide how much sleep we need
	qbool preciseCap = qfalse;
	int64_t sleepUS = 0;
	if ( com_dedicated->integer ) {
		sleepUS = 1000 * SV_FrameSleepMS();
	} else {
		preciseCap = qtrue;
		sleepUS = 1000000 / (com_maxfps->integer > 0? com_maxfps->integer : 999);
#ifndef DEDICATED
		if ( Sys_IsMinimized() ) {
			sleepUS = 20 * 1000;
			preciseCap = qfalse;
		} else if ( !CL_IsFrameSleepEnabled() ) {
			return;
		}
#endif
	}

	// decide when we should stop sleeping
	static int64_t targetTimeUS = INT64_MIN;
	if ( Sys_Microseconds() > targetTimeUS + 3 * sleepUS )
		targetTimeUS = Sys_Microseconds() + sleepUS;
	else
		targetTimeUS += sleepUS;
	com_nextTargetTimeUS = targetTimeUS + 1000000 / (com_maxfps->integer > 0 ? com_maxfps->integer : 999);

	// sleep if needed
	if ( com_dedicated->integer ) {
		while ( targetTimeUS - Sys_Microseconds() > 1000 ) {
			NET_Sleep( 1 );
			Com_EventLoop();
		}
	} else {
		int runEventLoop = 0;
		if ( preciseCap ) {
			for ( ;; ) {
				runEventLoop ^= 1;
				const int64_t remainingUS = targetTimeUS - Sys_Microseconds();
				if ( remainingUS > 3000 && runEventLoop )
					Com_EventLoop();
				else if ( remainingUS > 0 )
					Sys_MicroSleep( remainingUS );
				else
					break;
			}
		} else {
			while ( targetTimeUS - Sys_Microseconds() > 1000 ) {
				Sys_Sleep( 1 );
			}
		}
	}
}

/*
=================
Com_TimeVal
=================
*/
static int Com_TimeVal( int minMsec )
{
	int timeVal;

	timeVal = Com_Milliseconds() - com_frameTime;

	if ( timeVal >= minMsec )
		timeVal = 0;
	else
		timeVal = minMsec - timeVal;

	return timeVal;
}

/*
=================
Com_FrameInit
=================
*/
void Com_FrameInit( void )
{
	lastTime = com_frameTime = Com_Milliseconds();
}

/*
=================
Com_Frame
=================
*/

void Com_Frame( void ) {

#ifndef DEDICATED
	static int bias = 0;
#endif
	int	msec, realMsec, minMsec;
	int	sleepMsec;
	int	timeVal;
	int	timeValSV;

	int timeBeforeFirstEvents;
	int timeBeforeServer;
	int timeBeforeEvents;
	int timeBeforeClient;
	int timeAfter;

	if ( setjmp( abortframe ) ) {
#ifndef DEDICATED
		scr_recursiveUpdate = 0;
#endif
		return;         // an ERR_DROP was thrown
	}

	minMsec = 0; // silent compiler warning

	// bk001204 - init to zero.
	//  also:  might be clobbered by `longjmp' or `vfork'
	timeBeforeFirstEvents = 0;
	timeBeforeServer = 0;
	timeBeforeEvents = 0;
	timeBeforeClient = 0;
	timeAfter = 0;

	// DHM - Nerve :: Don't write config on Update Server
#ifndef UPDATE_SERVER
	// write config file if anything changed
	Com_WriteConfiguration();
#endif

	// if "viewlog" has been modified, show or hide the log console
	if ( com_viewlog->modified ) {
		if ( !com_dedicated->value ) {
			Sys_ShowConsole( com_viewlog->integer, qfalse );
		}
		com_viewlog->modified = qfalse;
	}

	if ( com_affinityMask->modified ) {
		Com_SetAffinityMask( com_affinityMask->string );
		com_affinityMask->modified = qfalse;
	}

	//
	// main event loop
	//
	if ( com_speeds->integer ) {
		timeBeforeFirstEvents = Sys_Milliseconds();
	}

	
	// we may want to spin here if things are going too fast
	if ( com_dedicated->integer ) {
		minMsec = SV_FrameMsec();
#ifndef DEDICATED
		bias = 0;
#endif
	} else {
#ifndef DEDICATED
		if ( 0 ) { // noDelay ) {
			minMsec = 0;
			bias = 0;
		} else {
			// if ( !gw_active && com_maxfpsUnfocused->integer > 0 )
			// 	minMsec = 1000 / com_maxfpsUnfocused->integer;
			// else
			if ( com_maxfps->integer > 0 )
				minMsec = 1000 / com_maxfps->integer;
			else
				minMsec = 1;

			timeVal = com_frameTime - lastTime;
			bias += timeVal - minMsec;

			if ( bias > minMsec )
				bias = minMsec;

			// Adjust minMsec if previous frame took too long to render so
			// that framerate is stable at the requested value.
			minMsec -= bias;
		}
#endif
	}

	// waiting for incoming packets
	//if ( noDelay == qfalse )
	do {
		if ( com_sv_running->integer ) {
			timeValSV = SV_SendQueuedPackets();
			timeVal = Com_TimeVal( minMsec );
			if ( timeValSV < timeVal )
				timeVal = timeValSV;
		} else {
			timeVal = Com_TimeVal( minMsec );
		}
		sleepMsec = timeVal;
#ifndef DEDICATED
		if ( !gw_minimized && timeVal > com_yieldCPU->integer )
			sleepMsec = com_yieldCPU->integer;
		if ( timeVal > sleepMsec )
			Com_EventLoop();
#endif
		NET_Sleep( sleepMsec * 1000 - 500 );
	} while( Com_TimeVal( minMsec ) );

	lastTime = com_frameTime;
	com_frameTime = Com_EventLoop();
	realMsec = com_frameTime - lastTime;

	Cbuf_Execute();

	// mess with msec if needed
	msec = Com_ModifyMsec( realMsec );

	//
	// server side
	//
	if ( com_speeds->integer ) {
		timeBeforeServer = Sys_Milliseconds();
	}

	SV_Frame( msec );

	// if "dedicated" has been modified, start up
	// or shut down the client system.
	// Do this after the server may have started,
	// but before the client tries to auto-connect
	if ( com_dedicated->modified ) {
		// get the latched value
		Cvar_Get( "dedicated", "0", 0 );
		com_dedicated->modified = qfalse;
		if ( !com_dedicated->integer ) {
			CL_Init();
			Sys_ShowConsole( com_viewlog->integer, qfalse );
		} else {
			CL_Shutdown();
			Sys_ShowConsole( 1, qtrue );
		}
	}

	//
	// client system
	//
	if ( !com_dedicated->integer ) {
		//
		// run event loop a second time to get server to client packets
		// without a frame of latency
		//
		if ( com_speeds->integer ) {
			timeBeforeEvents = Sys_Milliseconds();
		}
		Com_EventLoop();
		Cbuf_Execute();

		//
		// client side
		//
		if ( com_speeds->integer ) {
			timeBeforeClient = Sys_Milliseconds();
		}

		CL_Frame( msec );

		if ( com_speeds->integer ) {
			timeAfter = Sys_Milliseconds();
		}
	}

	NET_FlushPacketQueue( 0 );

	Cbuf_Wait();

	//
	// report timing information
	//
	if ( com_speeds->integer ) {
		int all, sv, ev, cl;

		all = timeAfter - timeBeforeServer;
		sv = timeBeforeEvents - timeBeforeServer;
		ev = timeBeforeServer - timeBeforeFirstEvents + timeBeforeClient - timeBeforeEvents;
		cl = timeAfter - timeBeforeClient;
		sv -= time_game;
		cl -= time_frontend + time_backend;

		Com_Printf( "frame:%i all:%3i sv:%3i ev:%3i cl:%3i gm:%3i rf:%3i bk:%3i\n",
					com_frameNumber, all, sv, ev, cl, time_game, time_frontend, time_backend );
	}

	//
	// trace optimization tracking
	//
	if ( com_showtrace->integer ) {

		extern int c_traces, c_brush_traces, c_patch_traces;
		extern int c_pointcontents;

		Com_Printf( "%4i traces  (%ib %ip) %4i points\n", c_traces,
					c_brush_traces, c_patch_traces, c_pointcontents );
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_pointcontents = 0;
	}

	com_frameNumber++;
}

/*
=================
Com_Shutdown
=================
*/
void Com_Shutdown( void ) {
	if ( logfile ) {
		FS_FCloseFile( logfile );
		logfile = 0;
	}

	if ( com_journalFile ) {
		FS_FCloseFile( com_journalFile );
		com_journalFile = 0;
	}

}


void Sys_SnapVector( float *v ) {
	v[0] = rintf( v[0] );
	v[1] = rintf( v[1] );
	v[2] = rintf( v[2] );
}


//------------------------------------------------------------------------


/*
=====================
Q_acos

the msvc acos doesn't always return a value between -PI and PI:

int i;
i = 1065353246;
acos(*(float*) &i) == -1.#IND0

	This should go in q_math but it is too late to add new traps
	to game and ui
=====================
*/
float Q_acos( float c ) {
	float angle;

	angle = acos( c );

	if ( angle > M_PI ) {
		return (float)M_PI;
	}
	if ( angle < -M_PI ) {
		return (float)M_PI;
	}
	return angle;
}

/*
===========================================
command line completion
===========================================
*/

/*
==================
Field_Clear
==================
*/
void Field_Clear( field_t *edit ) {
	memset( edit->buffer, 0, MAX_EDIT_LINE );
	edit->cursor = 0;
	edit->scroll = 0;
}

static const char *completionString;
static char shortestMatch[MAX_TOKEN_CHARS];
static int matchCount;
// field we are working on, passed to Field_CompleteCommand (&g_consoleCommand for instance)
static field_t *completionField;

/*
===============
FindMatches

===============
*/
static void FindMatches( const char *s ) {
	int i;

	if ( Q_stricmpn( s, completionString, strlen( completionString ) ) ) {
		return;
	}
	matchCount++;
	if ( matchCount == 1 ) {
		Q_strncpyz( shortestMatch, s, sizeof( shortestMatch ) );
		return;
	}

	// cut shortestMatch to the amount common with s
	for ( i = 0 ; s[i] ; i++ ) {
		if ( tolower( shortestMatch[i] ) != tolower( s[i] ) ) {
			shortestMatch[i] = 0;
		}
	}
}

/*
===============
PrintMatches

===============
*/
static void PrintMatches( const char *s ) {
	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_Printf( "    %s\n", s );
	}
}

static void keyConcatArgs( void ) {
	int i;
	char    *arg;

	for ( i = 1 ; i < Cmd_Argc() ; i++ ) {
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		arg = Cmd_Argv( i );
		while ( *arg ) {
			if ( *arg == ' ' ) {
				Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  "\"" );
				break;
			}
			arg++;
		}
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  Cmd_Argv( i ) );
		if ( *arg == ' ' ) {
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  "\"" );
		}
	}
}

static void ConcatRemaining( const char *src, const char *start ) {
	char *str;

	str = strstr( src, start );
	if ( !str ) {
		keyConcatArgs();
		return;
	}

	str += strlen( start );
	Q_strcat( completionField->buffer, sizeof( completionField->buffer ), str );
}

/*
===============
Field_CompleteCommand

perform Tab expansion
NOTE TTimo this was originally client code only
  moved to common code when writing tty console for *nix dedicated server
===============
*/
void Field_CompleteCommand( field_t *field ) {
	field_t temp;
	completionField = field;

	// only look at the first token for completion purposes
	Cmd_TokenizeString( completionField->buffer );

	completionString = Cmd_Argv( 0 );
	if ( completionString[0] == '\\' || completionString[0] == '/' ) {
		completionString++;
	}
	matchCount = 0;
	shortestMatch[0] = 0;

	if ( strlen( completionString ) == 0 ) {
		return;
	}

	Cmd_CommandCompletion( FindMatches );
	Cvar_CommandCompletion( FindMatches );

	if ( matchCount == 0 ) {
		return; // no matches
	}

	Com_Memcpy( &temp, completionField, sizeof( field_t ) );

	if ( matchCount == 1 ) {
		Com_sprintf( completionField->buffer, sizeof( completionField->buffer ), "\\%s", shortestMatch );
		if ( Cmd_Argc() == 1 ) {
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		} else {
			ConcatRemaining( temp.buffer, completionString );
		}
		completionField->cursor = strlen( completionField->buffer );
		return;
	}

	// multiple matches, complete to shortest
	Com_sprintf( completionField->buffer, sizeof( completionField->buffer ), "\\%s", shortestMatch );
	completionField->cursor = strlen( completionField->buffer );
	ConcatRemaining( temp.buffer, completionString );

	Com_Printf( "]%s\n", completionField->buffer );

	// run through again, printing matches
	Cmd_CommandCompletion( PrintMatches );
	Cvar_CommandCompletion( PrintMatches );
}



static unsigned int CRC32_table[256];
static qbool CRC32_tableCreated = qfalse;


void CRC32_Begin( unsigned int* crc )
{
	if ( !CRC32_tableCreated )
	{
		for ( int i = 0; i < 256; i++ )
		{
			unsigned int c = i;
			for ( int j = 0; j < 8; j++ )
				c = c & 1 ? (c >> 1) ^ 0xEDB88320UL : c >> 1;
			CRC32_table[i] = c;
		}
		CRC32_tableCreated = qtrue;
	}

	*crc = 0xFFFFFFFFUL;
}


void CRC32_ProcessBlock( unsigned int* crc, const void* buffer, unsigned int length )
{
	unsigned int hash = *crc;
	const unsigned char* buf = (const unsigned char*)buffer;
	while ( length-- )
	{
		hash = CRC32_table[(hash ^ *buf++) & 0xFF] ^ (hash >> 8);
	}
	*crc = hash;
}


void CRC32_End( unsigned int* crc )
{
	*crc ^= 0xFFFFFFFFUL;
}

const char* Q_itohex(uint64_t number, qbool uppercase, qbool prefix)
{
	static const char* luts[2] = { "0123456789abcdef", "0123456789ABCDEF" };
	static char buffer[19];
	const int maxLength = 16;

	const char* const lut = luts[uppercase == 0 ? 0 : 1];
	uint64_t x = number;
	int i = maxLength + 2;
	buffer[i] = '\0';
	while (i--) {
		buffer[i] = lut[x & 15];
		x >>= 4;
	}

	int startOffset = 2;
	for (i = 2; i < maxLength + 1; i++, startOffset++) {
		if (buffer[i] != '0')
			break;
	}

	if (prefix) {
		startOffset -= 2;
		buffer[startOffset + 0] = '0';
		buffer[startOffset + 1] = 'x';
	}

	return buffer + startOffset;
}

const char* Com_FormatBytes(uint64_t numBytes)
{
	const char* units[] = { "bytes", "KB", "MB", "GB" };
	const float dividers[] = { 1.0f, (float)(1 << 10), (float)(1 << 20), (float)(1 << 30) };

	int unit = 0;
	for (uint64_t vi = numBytes; vi >= 1024; vi >>= 10) {
		unit++;
	}

	const float vf = (float)numBytes / dividers[unit];

	return va("%.3f %s", vf, units[unit]);
}


/*
=================
RTCWPro
Com_WriteNewKey  ( temporary as this will change in the future)
=================
*/
void Com_WriteNewKey(const char* filename) {
	fileHandle_t f;
	char buffer[16] = { '\0' };
	char fbuffer[MAX_OSPATH];
    static char charset[] = "abcdefghijklmnopqrstuvwxyz123456789";
	srand(time(NULL));

    for (int n = 0; n < 16; n++) {
		int val = rand() % (int) (sizeof(charset) -1);
		buffer[n] = charset[val];
    }

	sprintf(fbuffer, "%s/rtcwkey", filename);

    f = FS_SV_FOpenFileWrite(fbuffer);

	if (!f) {
		Com_Printf( "Couldn't write %s.\n", filename );
		return;
	}

    //FS_Printf(f, "%s", buffer);
	FS_Write(buffer, 16, f);
	FS_FCloseFile(f);

}

/*
==================
Com_RandomBytes

fills string array with len random bytes, preferably from the OS randomizer
==================
*/
void Com_RandomBytes( byte *string, int len )
{
	int i;

	if ( Sys_RandomBytes( string, len ) )
		return;

	Com_Printf( S_COLOR_YELLOW "Com_RandomBytes: using weak randomization\n" );
	srand( time( NULL ) );
	for( i = 0; i < len; i++ )
		string[i] = (unsigned char)( rand() % 256 );
}
