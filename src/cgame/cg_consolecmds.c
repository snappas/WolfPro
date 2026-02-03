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

/*
 * name:		cg_consolecmds.c
 *
 * desc:		text commands typed in at the local console, or executed by a key binding
 *
*/


#include "cg_local.h"
#include "../ui/ui_shared.h"



void CG_TargetCommand_f( void ) {
	int targetNum;
	char test[4];

	targetNum = CG_CrosshairPlayer();
	if ( !targetNum ) {
		return;
	}

	trap_Argv( 1, test, 4 );
	trap_SendConsoleCommand( va( "gc %i %i", targetNum, atoi( test ) ) );
}



/*
=================
CG_SizeUp_f

Keybinding command
=================
*/
static void CG_SizeUp_f( void ) {
	trap_Cvar_Set( "cg_viewsize", va( "%i",(int)( cg_viewsize.integer + 10 ) ) );
}


/*
=================
CG_SizeDown_f

Keybinding command
=================
*/
static void CG_SizeDown_f( void ) {
	trap_Cvar_Set( "cg_viewsize", va( "%i",(int)( cg_viewsize.integer - 10 ) ) );
}


/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f( void ) {
	CG_Printf( "(%i %i %i) : %i\n", (int)cg.refdef.vieworg[0],
			   (int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2],
			   (int)cg.refdefViewAngles[YAW] );
}


static void CG_ScoresDown_f( void ) {
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		// the scores are more than two seconds out of data,
		// so request new ones
		cg.scoresRequestTime = cg.time;
		trap_SendClientCommand( "score" );

		// leave the current scores up if they were already
		// displayed, but if this is the first hit, clear them out
		if ( !cg.showScores ) {
			cg.showScores = qtrue;
			cg.numScores = 0;
		}
	} else {
		// show the cached contents even if they just pressed if it
		// is within two seconds
		cg.showScores = qtrue;
	}
}

static void CG_ScoresUp_f( void ) {
	if ( cg.showScores ) {
		cg.showScores = qfalse;
		cg.scoreFadeTime = cg.time;
	}
}


extern menuDef_t *menuScoreboard;
void Menu_Reset();          // FIXME: add to right include file

static void CG_LoadHud_f( void ) {
	char buff[1024];
	const char *hudSet;
	memset( buff, 0, sizeof( buff ) );

	String_Init();
	Menu_Reset();

//	trap_Cvar_VariableStringBuffer("cg_hudFiles", buff, sizeof(buff));
//	hudSet = buff;
//	if (hudSet[0] == '\0') {
	hudSet = "ui_mp/hud.txt";
//	}

	CG_LoadMenus( hudSet );
	menuScoreboard = NULL;
}

/*
// TTimo: defined but not used
static void CG_scrollScoresDown_f( void) {
	if (menuScoreboard && cg.scoreBoardShowing) {
		Menu_ScrollFeeder(menuScoreboard, FEEDER_SCOREBOARD, qtrue);
		Menu_ScrollFeeder(menuScoreboard, FEEDER_REDTEAM_LIST, qtrue);
		Menu_ScrollFeeder(menuScoreboard, FEEDER_BLUETEAM_LIST, qtrue);
	}
}
*/

/*
// TTimo: defined but not used
static void CG_scrollScoresUp_f( void) {
	if (menuScoreboard && cg.scoreBoardShowing) {
		Menu_ScrollFeeder(menuScoreboard, FEEDER_SCOREBOARD, qfalse);
		Menu_ScrollFeeder(menuScoreboard, FEEDER_REDTEAM_LIST, qfalse);
		Menu_ScrollFeeder(menuScoreboard, FEEDER_BLUETEAM_LIST, qfalse);
	}
}
*/

/*
// TTimo: defined but not used
static void CG_spWin_f( void) {
	trap_Cvar_Set("cg_cameraOrbit", "2");
	trap_Cvar_Set("cg_cameraOrbitDelay", "35");
	trap_Cvar_Set("cg_thirdPerson", "1");
	trap_Cvar_Set("cg_thirdPersonAngle", "0");
	trap_Cvar_Set("cg_thirdPersonRange", "100");
//	CG_AddBufferedSound(cgs.media.winnerSound);
	//trap_S_StartLocalSound(cgs.media.winnerSound, CHAN_ANNOUNCER);
	CG_CenterPrint("YOU WIN!", SCREEN_HEIGHT * .30, 0);
}
*/

/*
// TTimo: defined but not used
static void CG_spLose_f( void) {
	trap_Cvar_Set("cg_cameraOrbit", "2");
	trap_Cvar_Set("cg_cameraOrbitDelay", "35");
	trap_Cvar_Set("cg_thirdPerson", "1");
	trap_Cvar_Set("cg_thirdPersonAngle", "0");
	trap_Cvar_Set("cg_thirdPersonRange", "100");
//	CG_AddBufferedSound(cgs.media.loserSound);
	//trap_S_StartLocalSound(cgs.media.loserSound, CHAN_ANNOUNCER);
	CG_CenterPrint("YOU LOSE...", SCREEN_HEIGHT * .30, 0);
}
*/

//----(SA)	item (key/pickup) drawing
static void CG_InventoryDown_f( void ) {
	cg.showItems = qtrue;
}

static void CG_InventoryUp_f( void ) {
	cg.showItems = qfalse;
	cg.itemFadeTime = cg.time;
}

//----(SA)	end

static void CG_TellTarget_f( void ) {
	int clientNum;
	char command[128];
	char message[128];

	clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	trap_Args( message, 128 );
	Com_sprintf( command, 128, "tell %i %s", clientNum, message );
	trap_SendClientCommand( command );
}

static void CG_TellAttacker_f( void ) {
	int clientNum;
	char command[128];
	char message[128];

	clientNum = CG_LastAttacker();
	if ( clientNum == -1 ) {
		return;
	}

	trap_Args( message, 128 );
	Com_sprintf( command, 128, "tell %i %s", clientNum, message );
	trap_SendClientCommand( command );
}

/*
// TTimo: defined but not used
static void CG_NextTeamMember_f( void ) {
  CG_SelectNextPlayer();
}
*/

/*
// TTimo: defined but not used
static void CG_PrevTeamMember_f( void ) {
  CG_SelectPrevPlayer();
}
*/

/////////// cameras

#define MAX_CAMERAS 64  // matches define in splines.cpp
qboolean cameraInuse[MAX_CAMERAS];

int CG_LoadCamera( const char *name ) {
	int i;
	for ( i = 1; i < MAX_CAMERAS; i++ ) {    // start at '1' since '0' is always taken by the cutscene camera
		if ( !cameraInuse[i] ) {
			if ( trap_loadCamera( i, name ) ) {
				cameraInuse[i] = qtrue;
				return i;
			}
		}
	}
	return -1;
}

void CG_FreeCamera( int camNum ) {
	cameraInuse[camNum] = qfalse;
}



static void CG_Fade_f( void ) {
	int r, g, b, a;
	float time;

	if ( trap_Argc() < 6 ) {
		return;
	}

	r = atof( CG_Argv( 1 ) );
	g = atof( CG_Argv( 2 ) );
	b = atof( CG_Argv( 3 ) );
	a = atof( CG_Argv( 4 ) );

	time = atof( CG_Argv( 5 ) ) * 1000;

	CG_Fade( r, g, b, a, time );
}

// NERVE - SMF
static void CG_QuickMessage_f( void ) {
	if ( cg_quickMessageAlt.integer ) {
		trap_UI_Popup( "UIMENU_WM_QUICKMESSAGEALT" );
	} else {
		trap_UI_Popup( "UIMENU_WM_QUICKMESSAGE" );
	}
}

static void CG_OpenLimbo_f( void ) {
	int currentTeam;
	char buf[32];

	// No limbo menu in demos
	if (cg.demoPlayback) {
		return;
	}

	// set correct team, also set current team to detect if its changed
	if ( cg.snap ) {
		currentTeam = cg.snap->ps.persistant[PERS_TEAM] - 1;
	} else {
		currentTeam = 0;
	}

	if ( currentTeam > 2 ) {
		currentTeam = 2;
	}

	// Arnout - don't set currentteam when following as it won't be the actual currentteam
	if ( currentTeam != mp_team.integer && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		trap_Cvar_Set( "mp_team", va( "%d", currentTeam ) );
	}

	if ( currentTeam != mp_currentTeam.integer && !( cg.snap->ps.pm_flags & PMF_FOLLOW ) ) {
		trap_Cvar_Set( "mp_currentTeam", va( "%d", currentTeam ) );
	}

	// set current player type
	if ( mp_currentPlayerType.integer != cg.snap->ps.stats[ STAT_PLAYER_CLASS ] ) {
		trap_Cvar_Set( "mp_currentPlayerType", va( "%i", cg.snap->ps.stats[ STAT_PLAYER_CLASS ] ) );
	}

	// set isSpectator
	trap_Cvar_VariableStringBuffer( "ui_isSpectator", buf, 32 );

	if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR && cg.snap->ps.pm_type != PM_INTERMISSION ) {
		trap_SendConsoleCommand( "+scores\n" );           // NERVE - SMF - blah

		if ( !atoi( buf ) ) {
			trap_Cvar_Set( "ui_isSpectator", "1" );
		}
	} else {
		if ( atoi( buf ) ) {
			trap_Cvar_Set( "ui_isSpectator", "0" );
		}
	}

	trap_UI_Popup( "UIMENU_WM_LIMBO" );
}

static void CG_CloseLimbo_f( void ) {
	trap_UI_ClosePopup( "UIMENU_WM_LIMBO" );
}

static void CG_LimboMessage_f( void ) {
	char teamStr[80], classStr[80], weapStr[80];

	Q_strncpyz( teamStr, CG_TranslateString( CG_Argv( 1 ) ), 80 );
	Q_strncpyz( classStr, CG_TranslateString( CG_Argv( 2 ) ), 80 );
	Q_strncpyz( weapStr, CG_TranslateString( CG_Argv( 3 ) ), 80 );

	CG_PriorityCenterPrint( va( "%s %s %s %s %s.", CG_TranslateString( "You will spawn as an" ),
								teamStr, classStr, CG_TranslateString( "with a" ), weapStr ), SCREEN_HEIGHT - ( SCREEN_HEIGHT * 0.25 ), SMALLCHAR_WIDTH, -1 );
}

static void CG_VoiceChat_f( void ) {
	char chatCmd[64];

	if ( cgs.gametype < GT_WOLF || trap_Argc() != 2 ) {
		return;
	}

	// NERVE - SMF - don't let spectators voice chat
	// NOTE - This cg.snap will be the person you are following, but its just for intermission test
	if ( cg.snap && ( cg.snap->ps.pm_type != PM_INTERMISSION ) ) {
		if ( cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR || cgs.clientinfo[cg.clientNum].team == TEAM_FREE ) {
			CG_Printf( CG_TranslateString( "Can't voice chat as a spectator.\n" ) );
			return;
		}
	}

	trap_Argv( 1, chatCmd, 64 );

	trap_SendConsoleCommand( va( "cmd vsay %s\n", chatCmd ) );
}

static void CG_TeamVoiceChat_f( void ) {
	char chatCmd[64];

	if ( cgs.gametype < GT_WOLF || trap_Argc() != 2 ) {
		return;
	}

	// NERVE - SMF - don't let spectators voice chat
	// NOTE - This cg.snap will be the person you are following, but its just for intermission test
	if ( cg.snap && ( cg.snap->ps.pm_type != PM_INTERMISSION ) ) {
		if ( cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR || cgs.clientinfo[cg.clientNum].team == TEAM_FREE ) {
			CG_Printf( CG_TranslateString( "Can't team voice chat as a spectator.\n" ) );
			return;
		}
	}

	trap_Argv( 1, chatCmd, 64 );

	trap_SendConsoleCommand( va( "cmd vsay_team %s\n", chatCmd ) );
}

static void CG_SetWeaponCrosshair_f( void ) {
	char crosshair[64];

	trap_Argv( 1, crosshair, 64 );
	cg.newCrosshairIndex = atoi( crosshair ) + 1;
}
// -NERVE - SMF

/*
===================
CG_DumpLocation_f

Dump a target_location definition to a file
===================
*/
static void CG_DumpLocation_f( void ) {
	char locfilename[MAX_QPATH];
	char locname[MAX_STRING_CHARS];
	char *extptr, *buffptr;
	fileHandle_t f;

	// Check for argument
	if ( trap_Argc() < 2 ) {
		CG_Printf( "Usage: dumploc <locationname>\n" );
		return;
	}
	trap_Args( locname, sizeof( locname ) );

	// Open locations file
	Q_strncpyz( locfilename, cgs.mapname, sizeof( locfilename ) );
	extptr = locfilename + strlen( locfilename ) - 4;
	if ( extptr < locfilename || Q_stricmp( extptr, ".bsp" ) ) {
		CG_Printf( "Unable to dump, unknown map name?\n" );
		return;
	}
	Q_strncpyz( extptr, ".loc", 5 );
	trap_FS_FOpenFile( locfilename, &f, FS_APPEND_SYNC );
	if ( !f ) {
		CG_Printf( "Failed to open '%s' for writing.\n", locfilename );
		return;
	}

	// Strip bad characters out
	for ( buffptr = locname; *buffptr; buffptr++ )
	{
		if ( *buffptr == '\n' ) {
			*buffptr = ' ';
		} else if ( *buffptr == '"' ) {
			*buffptr = '\'';
		}
	}
	// Kill any trailing space as well
	if ( *( buffptr - 1 ) == ' ' ) {
		*( buffptr - 1 ) = 0;
	}

	// Build the entity definition
	buffptr = va(   "{\n\"classname\" \"target_location\"\n\"origin\" \"%i %i %i\"\n\"message\" \"%s\"\n}\n\n",
					(int) cg.snap->ps.origin[0], (int) cg.snap->ps.origin[1], (int) cg.snap->ps.origin[2], locname );

	// And write out/acknowledge
	trap_FS_Write( buffptr, strlen( buffptr ), f );
	trap_FS_FCloseFile( f );
	CG_Printf( "Entity dumped to '%s' (%i %i %i).\n", locfilename,
			   (int) cg.snap->ps.origin[0], (int) cg.snap->ps.origin[1], (int) cg.snap->ps.origin[2] );
}

// Force tapout
static void CG_ForceTapOut_f(void) {
	trap_SendClientCommand("forcetapout");
}

/**
 * @brief ETPro style enemy spawntimer
 */
static void CG_TimerSet_f(void) {

	if (!cg_drawEnemyTimer.integer)
	{
		return;
	}

	if (!cg.hudeditor && cgs.gamestate != GS_PLAYING)
	{
		CG_Printf("You may only use this command during the match.\n");
		return;
	}

	if (cg.snap->ps.pm_type == PM_INTERMISSION) {
		return;
	}

	trap_Cvar_Set("cg_spawnTimer_period", "30"); // just set a default value - cg_draw will use cg_red/bluelimbotime
	trap_Cvar_Set("cg_spawnTimer_set", va("%i", (cg.time - cgs.levelStartTime)));
}

/**
 * @brief ETPro style timer resetting
 */
static void CG_TimerReset_f(void)
{
	if (cgs.gamestate != GS_PLAYING)
	{
		CG_Printf("You may only use this command during the match.\n");
		return;
	}

	if (cg.snap->ps.pm_type == PM_INTERMISSION) {
		return;
	}

	trap_Cvar_Set("cg_spawnTimer_set", va("%d", cg.time - cgs.levelStartTime));
}

void CG_vstrDown_f(void) {
	if (trap_Argc() == 5) {
		trap_SendConsoleCommand(va("vstr %s;", CG_Argv(1)));
	}
	else { CG_Printf("[cgnotify]^3Usage: ^7+vstr [down_vstr] [up_vstr]\n"); }
}

void CG_vstrUp_f(void) {
	if (trap_Argc() == 5) {
		trap_SendConsoleCommand(va("vstr %s;", CG_Argv(2)));
	}
	else { CG_Printf("[cgnotify]^3Usage: ^7+vstr [down_vstr] [up_vstr]\n"); }
}


// +wstats
void CG_wStatsDown_f( void ) {
	if ( !cg.demoPlayback ) {
		int i = cg.snap->ps.clientNum;

		if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
			CPri( "You must be a player or following a player to use +wstats\n" );
			return;
		}

		if(cgs.gamestats.requestTime < cg.time) {
			cgs.gamestats.requestTime = cg.time + 500;
			trap_SendClientCommand(va("wstats %d", i));
		}

		cgs.gamestats.show = 1;
	}
}

// -wstats
void CG_wStatsUp_f( void ) {
	cgs.gamestats.show = 0;
}


// +stats
void CG_StatsDown_f( void ) {
    if ( !cg.demoPlayback ) {
		int i = cg.snap->ps.clientNum;

		if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
			CPri( "You must be a player or following a player to use +stats\n" );
			return;
		}

        if(cgs.clientGameStats.requestTime < cg.time) {
            cgs.clientGameStats.requestTime = cg.time + 500;
            trap_SendClientCommand( va( "cstats %d", i ) );
        }

        cgs.clientGameStats.show = 1;
    }

}

// -stats
void CG_StatsUp_f( void ) {
    cgs.clientGameStats.show = 0;
}

// +topshots
void CG_topshotsDown_f( void ) {
	if ( !cg.demoPlayback ) {
		cgs.topshots.show = 1;

		if ( cgs.topshots.requestTime < cg.time ) {
			cgs.topshots.requestTime = cg.time + 2000;
			trap_SendClientCommand( "stshots" );
		}
	}
}

// -topshots
void CG_topshotsUp_f( void ) {
	cgs.topshots.show = 0;
}

static void CG_TimerShare_f (void) {
	int secondsThen;
	int spawnTimerSet = cg.time - cgs.levelStartTime;
	int timeLimit     = cgs.timelimit * 60.f * 1000.f;
	int seconds       = (timeLimit - spawnTimerSet) / 1000;
	int period        = (cgs.clientinfo[cg.snap->ps.clientNum].team == TEAM_RED ? cg_bluelimbotime.integer : cg_redlimbotime.integer) / 1000;

	if (cgs.gamestate != GS_PLAYING) {
		CG_Printf("You may only use this command during the match.\n");
		return;
	}

	if (cg.snap->ps.pm_type == PM_INTERMISSION || cgs.clientinfo[cg.clientNum].team == TEAM_SPECTATOR || period == 0) {
		return;
	}

	if (cg_spawnTimer_set.integer == -1) {
		trap_Cvar_Set("cg_spawnTimer_period", "30"); // just set a default value - cg_draw will use cg_red/bluelimbotime
		trap_Cvar_Set("cg_spawnTimer_set", va("%i", spawnTimerSet));
	} else {
		spawnTimerSet = cg_spawnTimer_set.integer;
	}

	secondsThen = (timeLimit - spawnTimerSet) / 1000;
	trap_SendConsoleCommand(va("cmd say_teamnl ^3Enemy spawns in ^1%02d^3s, every ^1%02d^3s\n", (period + (seconds - secondsThen) % period), period));

}

/*
====================
OSPx - Zoom FOV
CG_zoomViewSet_f

Toggles zoom effect (based upon cg_zoomFOV value - can be in or out)
====================
*/
void CG_zoomViewSet_f(void) {
	float value;

	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ||
		cg.snap->ps.pm_flags & PMF_FOLLOW)
		return;

	if (cg_zoomedFOV.value > 120)
		value = 120;
	else if (cg_zoomedFOV.value < 90)
		value = 90;
	else
		value = cg_zoomedFOV.value;

	if (!cg.zoomedFOV) {
		cg.zoomedVal = value;
		cg.zoomedTime = cg.time;
		cg.zoomedFOV = qtrue;
	}
}

/*
====================
OSPx - Zoom FOV
CG_zoomViewRevert_f

Reverts back to default (when player releases the key)
====================
*/
void CG_zoomViewRevert_f(void) {
	cg.zoomedVal = cg_fov.value;
	cg.zoomedTime = cg.time;
	cg.zoomedFOV = qfalse;
	// Need to reset this..
	cg.zoomed = qfalse;
	cg.zoomedBinoc = qfalse;
	cg.zoomedScope = qfalse;
	cg.zoomTime = 0;
	//cg.zoomval = 0; // don't reset sniper zoom while scoped
}


static void CG_ResetMaxSpeed_f(void)
{
	cg.resetmaxspeed = qtrue;
}

static void CG_EditHud_f(void){
	if(!cg.hudeditor){
		trap_Cvar_Set("cg_spawnTimer_period", "30"); // just set a default value - cg_draw will use cg_red/bluelimbotime
		trap_Cvar_Set("cg_spawnTimer_set", va("%i", (cg.time - cgs.levelStartTime)));
		cg.prevHudGenTime = cg.time;
	}
	cg.hudeditor = !cg.hudeditor;
	
}

char *CG_generateFilename(void){
	static char fullFilename[MAX_OSPATH];
	char        prefix[MAX_QPATH];
	qtime_t     ct;
	const char  *serverInfo = CG_ConfigString(CS_SERVERINFO);

	trap_RealTime(&ct);
	fullFilename[0] = '\0';
	prefix[0]       = '\0';


	Com_sprintf(prefix, sizeof(prefix), "%d-%02d/", 1900 + ct.tm_year, ct.tm_mon + 1);


	Com_sprintf(fullFilename, sizeof(fullFilename), "%s%d-%02d-%02d-%02d%02d%02d-%s%s", prefix,
	            1900 + ct.tm_year, ct.tm_mon + 1, ct.tm_mday,
	            ct.tm_hour, ct.tm_min, ct.tm_sec,
	            Info_ValueForKey(serverInfo, "mapname"),
	            ""
	            );

	return fullFilename;
}

void CG_autoRecord_f(void){
	trap_SendConsoleCommand(va("g_synchronousclients 0;g_synchronousclients 1;record %s;g_synchronousclients 0\n", CG_generateFilename()));
}


typedef struct {
	char    *cmd;
	void ( *function )( void );
} consoleCommand_t;

static consoleCommand_t commands[] = {
	{ "testgun", CG_TestGun_f },
	{ "testmodel", CG_TestModel_f },
	{ "nextframe", CG_TestModelNextFrame_f },
	{ "prevframe", CG_TestModelPrevFrame_f },
	{ "nextskin", CG_TestModelNextSkin_f },
	{ "prevskin", CG_TestModelPrevSkin_f },
	{ "viewpos", CG_Viewpos_f },
	{ "+scores", CG_ScoresDown_f },
	{ "-scores", CG_ScoresUp_f },
	{ "+inventory", CG_InventoryDown_f },
	{ "-inventory", CG_InventoryUp_f },
//	{ "+zoom", CG_ZoomDown_f },		// (SA) zoom moved to a wbutton so server can determine weapon firing based on zoom status
//	{ "-zoom", CG_ZoomUp_f },
	{ "zoomin", CG_ZoomIn_f },
	{ "zoomout", CG_ZoomOut_f },
	{ "sizeup", CG_SizeUp_f },
	{ "sizedown", CG_SizeDown_f },
	{ "weaplastused", CG_LastWeaponUsed_f },
	{ "weapnextinbank", CG_NextWeaponInBank_f },
	{ "weapprevinbank", CG_PrevWeaponInBank_f },
	{ "weapnext", CG_NextWeapon_f },
	{ "weapprev", CG_PrevWeapon_f },
	{ "weapalt", CG_AltWeapon_f },
	{ "weapon", CG_Weapon_f },
	{ "weaponbank", CG_WeaponBank_f },
	{ "itemnext", CG_NextItem_f },
	{ "itemprev", CG_PrevItem_f },
	{ "item", CG_Item_f },
	{ "tell_target", CG_TellTarget_f },
	{ "tell_attacker", CG_TellAttacker_f },
	{ "tcmd", CG_TargetCommand_f },
	{ "loadhud", CG_LoadHud_f },
	{ "loaddeferred", CG_LoadDeferredPlayers },  // spelling fixed (SA)
//	{ "camera", CG_Camera_f },	// duffy
	{ "fade", CG_Fade_f },   // duffy
	{ "loadhud", CG_LoadHud_f },

	// NERVE - SMF
	{ "mp_QuickMessage", CG_QuickMessage_f },
	{ "OpenLimboMenu", CG_OpenLimbo_f },
	{ "CloseLimboMenu", CG_CloseLimbo_f },
	{ "LimboMessage", CG_LimboMessage_f },
	{ "VoiceChat", CG_VoiceChat_f },
	{ "VoiceTeamChat", CG_TeamVoiceChat_f },
	{ "SetWeaponCrosshair", CG_SetWeaponCrosshair_f },
	// -NERVE - SMF

	// Arnout
	{ "dumploc", CG_DumpLocation_f },
	{ "timerSet", CG_TimerSet_f },
	{ "timerReset", CG_TimerReset_f },
	{ "resetTimer", CG_TimerReset_f }, // keep ETPro compatibility
	{ "forcetapout", CG_ForceTapOut_f }, 

	{ "+vstr", CG_vstrDown_f },
	{ "-vstr", CG_vstrUp_f },
	{ "+zoomView", CG_zoomViewSet_f },
	{ "-zoomView", CG_zoomViewRevert_f },

	{ "timerShare", CG_TimerShare_f },
	
	{ "resetmaxspeed", CG_ResetMaxSpeed_f },
	{ "edithud", CG_EditHud_f },
	
	{ "restrictions", CG_PrintRestrictions },

	{ "+wstats", CG_wStatsDown_f },
	{ "-wstats", CG_wStatsUp_f },
	{ "+stats", CG_StatsDown_f },
	{ "-stats", CG_StatsUp_f },
	{ "+wtopshots", CG_topshotsDown_f },
	{ "-wtopshots", CG_topshotsUp_f },
};


/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	const char* cmd = CG_Argv(0);

	if (!Q_stricmp(cmd, "drawgui")) {
		return qtrue;
	}

	// Arnout - don't allow console commands until a snapshot is present
	if ( !cg.snap) {
		return qfalse;
	}

	for (int i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		if ( !Q_stricmp( cmd, commands[i].cmd ) ) {
			commands[i].function();
			return qtrue;
		}
	}

	return qfalse;
}


/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	int i;

	for ( i = 0 ; i < sizeof( commands ) / sizeof( commands[0] ) ; i++ ) {
		trap_AddCommand( commands[i].cmd );
	}

	//
	// the game server will interpret these commands, which will be automatically
	// forwarded to the server after they are not recognized locally
	//
	trap_AddCommand( "kill" );
	trap_AddCommand( "say" );
	trap_AddCommand( "say_team" );
	trap_AddCommand( "say_teamnl" );
	trap_AddCommand( "say_limbo" );           // NERVE - SMF
	trap_AddCommand( "tell" );
//	trap_AddCommand ("vsay");
//	trap_AddCommand ("vsay_team");
//	trap_AddCommand ("vtell");
//	trap_AddCommand ("vtaunt");
//	trap_AddCommand ("vosay");
//	trap_AddCommand ("vosay_team");
//	trap_AddCommand ("votell");
	trap_AddCommand( "give" );
	trap_AddCommand( "god" );
	trap_AddCommand( "notarget" );
	trap_AddCommand( "noclip" );
	trap_AddCommand( "team" );
	trap_AddCommand( "follow" );
	trap_AddCommand( "levelshot" );
	trap_AddCommand( "addbot" );
	trap_AddCommand( "setviewpos" );
	trap_AddCommand( "callvote" );
	trap_AddCommand( "vote" );
//	trap_AddCommand ("callteamvote");
//	trap_AddCommand ("teamvote");
	trap_AddCommand( "stats" );
//	trap_AddCommand ("teamtask");
	trap_AddCommand( "loaddeferred" );        // spelling fixed (SA)

//	trap_AddCommand ("startCamera");
//	trap_AddCommand ("stopCamera");
//	trap_AddCommand ("setCameraOrigin");

	// Rafael
	trap_AddCommand( "nofatigue" );

	// NERVE - SMF
	trap_AddCommand( "setspawnpt" );
	trap_AddCommand( "follownext" );
	trap_AddCommand( "followprev" );

	trap_AddCommand( "start_match" );
	trap_AddCommand( "reset_match" );
	trap_AddCommand( "swap_teams" );
	// -NERVE - SMF
	
	// Speclock
	trap_AddCommand( "speclock" );		// Locks team from specs
	trap_AddCommand( "specunlock" );	// Opens team for specs
	trap_AddCommand( "specinvite" );		// Invites player to spec team
	trap_AddCommand( "specuninvite" );		// Takes players ability to spec the team
	trap_AddCommand( "specuninviteall" );	// Removes all spectators from that team
	// Ready
	trap_AddCommand("readyteam");
	trap_AddCommand("ready");
	trap_AddCommand("notready");
	trap_AddCommand("lock");		// Locks team
	trap_AddCommand("unlock");		// Unlocks team

	trap_AddCommand("pause");
	trap_AddCommand("unpause");
	trap_AddCommand("players");

	trap_AddCommand("wstats");
	trap_AddCommand("cstats");
	trap_AddCommand("stats");
	trap_AddCommand("gamestats");
	trap_AddCommand("sgstats");
	trap_AddCommand("stshots");
	trap_AddCommand("scores");
	trap_AddCommand("bottomshots");
	trap_AddCommand("statsall");
	trap_AddCommand("topshots");
	trap_AddCommand("weaponstats");
}
