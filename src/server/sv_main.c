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


#include "server.h"

serverStatic_t svs;                 // persistant server info
server_t sv;                        // local server
vm_t            *gvm = NULL;                // game virtual machine // bk001212 init

#ifdef UPDATE_SERVER
versionMapping_t versionMap[MAX_UPDATE_VERSIONS];
int numVersions = 0;
#endif

cvar_t  *sv_fps;                // time rate for running non-clients
cvar_t  *sv_timeout;            // seconds without any message
cvar_t  *sv_zombietime;         // seconds to sink messages after disconnect
cvar_t  *sv_rconPassword;       // password for remote server commands
cvar_t  *sv_privatePassword;    // password for the privateClient slots
cvar_t  *sv_allowDownload;
cvar_t  *sv_maxclients;
cvar_t	*sv_maxclientsPerIP;

cvar_t  *sv_privateClients;     // number of clients reserved for password
cvar_t  *sv_hostname;
cvar_t  *sv_master[MAX_MASTER_SERVERS];     // master server ip address
cvar_t  *sv_reconnectlimit;     // minimum seconds between connect messages
cvar_t  *sv_showloss;           // report when usercmds are lost
cvar_t  *sv_padPackets;         // add nop bytes to messages
cvar_t  *sv_killserver;         // menu system can set to 1 to shut server down
cvar_t  *sv_mapname;
cvar_t  *sv_mapChecksum;
cvar_t  *sv_serverid;
cvar_t  *sv_maxRate;
cvar_t  *sv_minPing;
cvar_t  *sv_maxPing;
cvar_t  *sv_gametype;
cvar_t  *sv_pure;
cvar_t  *sv_floodProtect;
cvar_t  *sv_allowAnonymous;
cvar_t  *sv_lanForceRate; // TTimo - dedicated 1 (LAN) server forces local client rates to 99999 (bug #491)
cvar_t  *sv_onlyVisibleClients; // DHM - Nerve
cvar_t  *sv_friendlyFire;       // NERVE - SMF
cvar_t  *sv_maxlives;           // NERVE - SMF
cvar_t  *sv_tourney;            // NERVE - SMF

cvar_t  *sv_minUserCmdInterval;

cvar_t *sv_dl_maxRate;

// Rafael gameskill
cvar_t  *sv_gameskill;
// done

cvar_t  *sv_showAverageBPS;     // NERVE - SMF - net debugging

cvar_t *sv_serverIP;
cvar_t *sv_serverCountry;

cvar_t* sv_GameConfig;

cvar_t* sv_dlRate;

cvar_t* sv_referencedPakNames;

cvar_t *sv_levelTimeReset;

void SVC_GameCompleteStatus( const netadr_t *from );       // NERVE - SMF

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
===============
SV_ExpandNewlines

Converts newlines to "\n" so a line prints nicer
===============
*/
char    *SV_ExpandNewlines( char *in ) {
	static char string[1024];
	int l;

	l = 0;
	while ( *in && l < sizeof( string ) - 3 ) {
		if ( *in == '\n' ) {
			string[l++] = '\\';
			string[l++] = 'n';
		} else {
			// NERVE - SMF - HACK - strip out localization tokens before string command is displayed in syscon window
			if ( !Q_strncmp( in, "[lon]", 5 ) || !Q_strncmp( in, "[lof]", 5 ) ) {
				in += 5;
				continue;
			}

			string[l++] = *in;
		}
		in++;
	}
	string[l] = 0;

	return string;
}

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd ) {
	int		index, i, n;

	// this is very ugly but it's also a waste to for instance send multiple config string updates
	// for the same config string index in one snapshot
//	if ( SV_ReplacePendingServerCommands( client, cmd ) ) {
//		return;
//	}

	// do not send commands until the gamestate has been sent
	if ( client->state < CS_PRIMED )
		return;

	client->reliableSequence++;
	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 ) {
		Com_Printf( "===== pending server commands =====\n" );
		n = client->reliableSequence - client->reliableAcknowledge;
		for ( i = 0; i < n; i++ ) {
			const int idx = client->reliableAcknowledge + 1 + i;
			Com_Printf( "cmd %5d: %s\n", i, client->reliableCommands[ idx & ( MAX_RELIABLE_COMMANDS - 1 ) ] );
		}
		Com_Printf( "cmd %5d: %s\n", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}
	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}


/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by
the client game module: "cp", "print", "chat", etc
A NULL client will broadcast to all clients
=================
*/
void QDECL SV_SendServerCommand( client_t *cl, const char *fmt, ... ) {
	va_list		argptr;
	char		message[MAX_STRING_CHARS+128]; // slightly larger than allowed, to detect overflows
	client_t	*client;
	int			j, len;
	
	va_start( argptr, fmt );
	len = Q_vsnprintf( message, sizeof( message ), fmt, argptr );
	va_end( argptr );

	if ( cl != NULL ) {
		// outdated clients can't properly decode 1023-chars-long strings
		// http://aluigi.altervista.org/adv/q3msgboom-adv.txt
		if ( len <= 1022 || cl->longstr ) {
			SV_AddServerCommand( cl, message );
		}
		return;
	}

	// hack to echo broadcast prints to console
	if ( com_dedicated->integer && !strncmp( message, "print", 5 ) ) {
		Com_Printf( "broadcast: %s\n", SV_ExpandNewlines( message ) );
	}

	// send the data to all relevant clients
	for ( j = 0, client = svs.clients; j < sv.maxclients; j++, client++ ) {
		// Ridah, don't need to send messages to AI
		if ( client->gentity && client->gentity->r.svFlags & SVF_CASTAI ) {
			continue;
		}
		if ( len <= 1022 || client->longstr ) {
			SV_AddServerCommand( client, message );
		}
	}
}
		

/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
#define HEARTBEAT_MSEC  300 * 1000
#define HEARTBEAT_GAME  "Wolfenstein-1"
#define HEARTBEAT_DEAD  "WolfFlatline-1"         // NERVE - SMF

void SV_MasterHeartbeat( const char *hbname ) {
	static netadr_t adr[MAX_MASTER_SERVERS];
	int i;

	// DHM - Nerve :: Update Server doesn't send heartbeat
#ifdef UPDATE_SERVER
	return;
#endif

	// "dedicated 1" is for lan play, "dedicated 2" is for inet public play
	if ( !com_dedicated || com_dedicated->integer != 2 ) {
		return;     // only dedicated servers send heartbeats
	}

	// if not time yet, don't send anything
	if ( svs.time < svs.nextHeartbeatTime ) {
		return;
	}
	svs.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;


	// send to group masters
	for ( i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		if ( !sv_master[i]->string[0] ) {
			continue;
		}

		// see if we haven't already resolved the name
		// resolving usually causes hitches on win95, so only
		// do it when needed
		if ( sv_master[i]->modified ) {
			sv_master[i]->modified = qfalse;

			Com_Printf( "Resolving %s\n", sv_master[i]->string );
			if ( !NET_StringToAdr( sv_master[i]->string, &adr[i], NA_UNSPEC ) ) {
				// if the address failed to resolve, clear it
				// so we don't take repeated dns hits
				Com_Printf( "Couldn't resolve address: %s\n", sv_master[i]->string );
				Cvar_Set( sv_master[i]->name, "" );
				sv_master[i]->modified = qfalse;
				continue;
			}
			if ( !strstr( ":", sv_master[i]->string ) ) {
				adr[i].port = BigShort( PORT_MASTER );
			}
			Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", sv_master[i]->string,
						adr[i].ipv._4[0], adr[i].ipv._4[1], adr[i].ipv._4[2], adr[i].ipv._4[3],
						BigShort( adr[i].port ) );
		}


		Com_Printf( "Sending heartbeat to %s\n", sv_master[i]->string );
		// this command should be changed if the server info / status format
		// ever incompatably changes
		NET_OutOfBandPrint( NS_SERVER, &adr[i], "heartbeat %s\n", hbname );
	}
}

/*
=================
SV_MasterGameCompleteStatus

NERVE - SMF - Sends gameCompleteStatus messages to all master servers
=================
*/
void SV_MasterGameCompleteStatus() {
	static netadr_t adr[MAX_MASTER_SERVERS];
	int i;

	// "dedicated 1" is for lan play, "dedicated 2" is for inet public play
	if ( !com_dedicated || com_dedicated->integer != 2 ) {
		return;     // only dedicated servers send master game status
	}

	// send to group masters
	for ( i = 0 ; i < MAX_MASTER_SERVERS ; i++ ) {
		if ( !sv_master[i]->string[0] ) {
			continue;
		}

		// see if we haven't already resolved the name
		// resolving usually causes hitches on win95, so only
		// do it when needed
		if ( sv_master[i]->modified ) {
			sv_master[i]->modified = qfalse;

			Com_Printf( "Resolving %s\n", sv_master[i]->string );
			if ( !NET_StringToAdr( sv_master[i]->string, &adr[i], NA_UNSPEC ) ) {
				// if the address failed to resolve, clear it
				// so we don't take repeated dns hits
				Com_Printf( "Couldn't resolve address: %s\n", sv_master[i]->string );
				Cvar_Set( sv_master[i]->name, "" );
				sv_master[i]->modified = qfalse;
				continue;
			}
			if ( !strstr( ":", sv_master[i]->string ) ) {
				adr[i].port = BigShort( PORT_MASTER );
			}
			Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", sv_master[i]->string,
						adr[i].ipv._4[0], adr[i].ipv._4[1], adr[i].ipv._4[2], adr[i].ipv._4[3],
						BigShort( adr[i].port ) );
		}

		Com_Printf( "Sending gameCompleteStatus to %s\n", sv_master[i]->string );
		// this command should be changed if the server info / status format
		// ever incompatably changes
		SVC_GameCompleteStatus( &adr[i] );
	}
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown( void ) {
	// send a hearbeat right now
	svs.nextHeartbeatTime = -9999;
	SV_MasterHeartbeat( HEARTBEAT_DEAD );               // NERVE - SMF - changed to flatline

	// send it again to minimize chance of drops
//	svs.nextHeartbeatTime = -9999;
//	SV_MasterHeartbeat( HEARTBEAT_DEAD );

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/



/*
=================
SVC_GameCompleteStatus

NERVE - SMF - Send serverinfo cvars, etc to master servers when
game complete. Useful for tracking global player stats.
=================
*/
void SVC_GameCompleteStatus( const netadr_t *from ) {
	char player[1024];
	char status[MAX_MSGLEN];
	int i;
	client_t    *cl;
	playerState_t   *ps;
	int statusLength;
	int playerLength;
	char infostring[MAX_INFO_STRING];


	strcpy( infostring, Cvar_InfoString( CVAR_SERVERINFO, NULL ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv( 1 ) );

	// add "demo" to the sv_keywords if restricted
	if ( Cvar_VariableValue( "fs_restrict" ) ) {
		char keywords[MAX_INFO_STRING];

		Com_sprintf( keywords, sizeof( keywords ), "demo %s",
					 Info_ValueForKey( infostring, "sv_keywords" ) );
		Info_SetValueForKey( infostring, "sv_keywords", keywords );
	}

	status[0] = 0;
	statusLength = 0;

	for ( i = 0 ; i < sv_maxclients->integer ; i++ ) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {
			ps = SV_GameClientNum( i );
			Com_sprintf( player, sizeof( player ), "%i %i \"%s\"\n",
						 ps->persistant[PERS_SCORE], cl->ping, cl->name );
			playerLength = strlen( player );
			if ( statusLength + playerLength >= sizeof( status ) ) {
				break;      // can't hold any more
			}
			strcpy( status + statusLength, player );
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "gameCompleteStatus\n%s\n%s", infostring, status );
}


/*
==============
SV_FlushRedirect

==============
*/
void SV_FlushRedirect( char *outputbuf ) {
	NET_OutOfBandPrint( NS_SERVER, &svs.redirectAddress, "print\n%s", outputbuf );
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
void SVC_RemoteCommand( const netadr_t *from, msg_t *msg ) {
	qboolean valid;
	unsigned int time;
	char remaining[1024];
	// show_bug.cgi?id=376
	// if we send an OOB print message this size, 1.31 clients die in a Com_Printf buffer overflow
	// the buffer overflow will be fixed in > 1.31 clients
	// but we want a server side fix
	// we must NEVER send an OOB message that will be > 1.31 MAXPRINTMSG (4096)
#define SV_OUTPUTBUF_LENGTH ( 256 - 16 )
	char sv_outputbuf[SV_OUTPUTBUF_LENGTH];
	static unsigned int lasttime = 0;
	char *cmd_aux;

	time = Com_Milliseconds();
	//ignore flood from localhost
	if(!Sys_IsLANAddress(from)){
		if ( time < ( lasttime + 500 ) ) {
			return;
		}
	}
	lasttime = time;

	if ( !strlen( sv_rconPassword->string ) ||
		 strcmp( Cmd_Argv( 1 ), sv_rconPassword->string ) ) {
		valid = qfalse;
		Com_Printf( "Bad rcon from %s:\n%s\n", NET_AdrToString( from ), Cmd_Argv( 2 ) );
	} else {
		valid = qtrue;
		Com_Printf( "Rcon from %s:\n%s\n", NET_AdrToString( from ), Cmd_Argv( 2 ) );
	}

	// start redirecting all print outputs to the packet
	svs.redirectAddress = *from;
	// FIXME TTimo our rcon redirection could be improved
	//   big rcon commands such as status lead to sending
	//   out of band packets on every single call to Com_Printf
	//   which leads to client overflows
	//   see show_bug.cgi?id=51
	//     (also a Q3 issue)
	Com_BeginRedirect( sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect );

	if ( !strlen( sv_rconPassword->string ) ) {
		Com_Printf( "No rconpassword set on the server.\n" );
	} else if ( !valid ) {
		Com_Printf( "Bad rconpassword.\n" );
	} else {
		remaining[0] = 0;

		// ATVI Wolfenstein Misc #284
		// get the command directly, "rcon <pass> <command>" to avoid quoting issues
		// extract the command by walking
		// since the cmd formatting can fuckup (amount of spaces), using a dumb step by step parsing
		cmd_aux = Cmd_Cmd();
		cmd_aux += 4;
		while ( cmd_aux[0] == ' ' )
			cmd_aux++;
		while ( cmd_aux[0] && cmd_aux[0] != ' ' ) // password
			cmd_aux++;
		while ( cmd_aux[0] == ' ' )
			cmd_aux++;

		Q_strcat( remaining, sizeof( remaining ), cmd_aux );

		Cmd_ExecuteString( remaining );

	}

	Com_EndRedirect();
}


//============================================================================


static int cmp_int(const void *a, const void *b){
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings( void ) {
	int i, j;
	client_t    *cl;
	int total, count;
	int delta;
	playerState_t   *ps;

	for ( i = 0; i < sv.maxclients; i++ ) {
		cl = &svs.clients[i];

		if ( cl->state != CS_ACTIVE ) {
			cl->ping = 999;
			continue;
		}
		if ( !cl->gentity ) {
			cl->ping = 999;
			continue;
		}
		if ( cl->netchan.remoteAddress.type == NA_BOT ) {
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;
		for ( j = 0 ; j < PACKET_BACKUP ; j++ ) {
			if ( cl->frames[j].messageAcked == 0 ) {
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count) {
			cl->ping = 999;
		} else {
			cl->ping = total/count;
			if ( cl->ping > 999 ) {
				cl->ping = 999;
			}
		}

		// let the game dll know about the ping
		ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer
seconds, drop the conneciton.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts( void ) {
	int i;
	client_t    *cl;
	int droppoint;
	int zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for ( i = 0, cl = svs.clients ; i < sv.maxclients; i++, cl++ ) {
		if ( cl->state == CS_FREE ) {
			continue;
		}

		// message times may be wrong across a changelevel
		if ( cl->lastPacketTime - svs.time > 0 ) {
			cl->lastPacketTime = svs.time;
		}

		if ( cl->state == CS_ZOMBIE && cl->lastPacketTime - zombiepoint < 0 ) {
			// using the client id cause the cl->name is empty at this point
			SV_PrintClientStateChange( cl, CS_FREE );
			cl->state = CS_FREE;	// can now be reused
			continue;
		}
		if ( cl->justConnected && svs.time - cl->lastPacketTime > 4000 ) {
			// for real client 4 seconds is more than enough to respond
			SVC_RateDropAddress( &cl->netchan.remoteAddress, 10, 1000 ); // enforce burst with progressive multiplier
			SV_DropClient( cl, NULL ); // drop silently
			cl->state = CS_FREE;
			continue;
		}
		if ( cl->state >= CS_CONNECTED && cl->lastPacketTime - droppoint < 0 ) {
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 ) {
				SV_DropClient( cl, "timed out" );
				cl->state = CS_FREE;	// don't bother with zombie state
			}
		} else {
			cl->timeoutCount = 0;
		}
	}
}


/*
==================
SV_CheckPaused
==================
*/
qboolean SV_CheckPaused( void ) {
	

#ifdef DEDICATED
	// can't pause on dedicated servers
	return qfalse;
#else
	int count;
	client_t* cl;
	int i;

	if ( !cl_paused->integer ) {
		return qfalse;
	}

	// only pause if there is just a single client connected
	count = 0;
	for ( i = 0, cl = svs.clients ; i < sv.maxclients; i++, cl++ ) {
		if ( cl->state >= CS_CONNECTED && cl->netchan.remoteAddress.type != NA_BOT ) {
			count++;
		}
	}

	if ( count > 1 ) {
		// don't pause
		if ( sv_paused->integer ) {
			Cvar_Set( "sv_paused", "0" );
		}
		return qfalse;
	}

	if ( !sv_paused->integer ) {
		Cvar_Set( "sv_paused", "1" );
	}
	return qtrue;
#endif // !DEDICATED
}

/*
==================
SV_FrameMsec
Return time in millseconds until processing of the next server frame.
==================
*/
int SV_FrameMsec( void )
{
	if ( sv_fps )
	{
		const int frameMsec = 1000 / sv_fps->integer;

		if ( frameMsec < sv.timeResidual )
			return 0;
		else
			return frameMsec - sv.timeResidual;
	}
	else
		return 1;
}

/*
==================
SV_Restart
==================
*/
static void SV_Restart( const char *reason ) {
	qboolean sv_shutdown = qfalse;
	char mapName[ MAX_CVAR_VALUE_STRING ];
	int i;

	if ( svs.clients ) {
		// check if we can reset map time without full server shutdown
		for ( i = 0; i < sv.maxclients; i++ ) {
			if ( svs.clients[i].state >= CS_CONNECTED ) {
				sv_shutdown = qtrue;
				break;
			}
		}
	}

	sv.time = 0; // force level time reset
	sv.restartTime = 0;
	
	Cvar_VariableStringBuffer( "mapname", mapName, sizeof( mapName ) );
	
	if ( sv_shutdown ) {
		SV_Shutdown( reason );
	}

	Cbuf_AddText( va( "map %s\n", mapName ) );
}

/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec ) {
	int frameMsec;
	int startTime;
	int		i;

	// if ( Cvar_CheckGroup( CVG_SERVER ) )
	// 	SV_TrackCvarChanges(); // update rate settings, etc.

	// the menu kills the server with this cvar
	if ( sv_killserver->integer ) {
		SV_Shutdown( "Server was killed" );
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer )
	{
		if ( com_dedicated->integer )
		{
			// Block indefinitely until something interesting happens
			// on STDIN.
			Sys_Sleep( -1 );
		}
		return;
	}

	// allow pause if only the local client is connected
	if ( SV_CheckPaused() ) {
		return;
	}

	// if it isn't time for the next frame, do nothing

	frameMsec = (1000 / sv_fps->integer) * com_timescale->value;
	// don't let it scale below 1ms
	if (frameMsec < 1)
	{
		Cvar_SetValue( "timescale", sv_fps->value / 1000.0f );
		Com_DPrintf( "timescale adjusted to %f\n", com_timescale->value );
		frameMsec = 1;
	}

	sv.timeResidual += msec;

	if ( !com_dedicated->integer )
		SV_BotFrame( sv.time + sv.timeResidual );

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// 2giga-milliseconds = 23 days, so it won't be too often
	if ( sv.time > 0x78000000 ) {
		SV_Restart( "Restarting server due to time wrapping" );
		return;
	}

	// try to do silent restart earlier if possible
	if ( sv.time > (12*3600*1000) && ( sv_levelTimeReset->integer == 0 || sv.time > 0x40000000 ) ) {
		if ( svs.clients ) {
			for ( i = 0; i < sv.maxclients; i++ ) {
				// FIXME: deal with bots (reconnect?)
				if ( svs.clients[i].state != CS_FREE && svs.clients[i].netchan.remoteAddress.type != NA_BOT ) {
					break;
				}
			}
			if ( i == sv.maxclients ) {
				SV_Restart( "Restarting server" );
				return;
			}
		}
	}

	if ( sv.restartTime && sv.time - sv.restartTime >= 0 ) {
		sv.restartTime = 0;
		Cbuf_AddText( "map_restart 0\n" );
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO ) {
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO, NULL ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}
	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO ) {
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString_Big( CVAR_SYSTEMINFO, NULL ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}
	// NERVE - SMF
	if ( cvar_modifiedFlags & CVAR_WOLFINFO ) {
		SV_SetConfigstring( CS_WOLFINFO, Cvar_InfoString( CVAR_WOLFINFO, NULL ) );
		cvar_modifiedFlags &= ~CVAR_WOLFINFO;
	}

	if ( com_speeds->integer ) {
		startTime = Sys_Milliseconds();
	} else {
		startTime = 0;  // quite a compiler warning
	}

	// update ping based on the all received frames
	SV_CalcPings();

	if ( com_dedicated->integer ) {
		SV_BotFrame( sv.time );
	}

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec ) {
		sv.timeResidual -= frameMsec;
		svs.time += frameMsec;
		sv.time += frameMsec;

		// let everything in the world think and move
#ifndef UPDATE_SERVER
		VM_Call( gvm, GAME_RUN_FRAME, sv.time );
#endif
	}

	if ( com_speeds->integer ) {
		time_game = Sys_Milliseconds() - startTime;
	}

	// check timeouts
	SV_CheckTimeouts();

	// reset current and build new snapshot on first query
	SV_IssueNewSnapshot();

	// send messages back to the clients
	SV_SendClientMessages();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat( HEARTBEAT_GAME );
}


int SV_FrameSleepMS(void)
{
	if ( !sv_fps )
		return 1;

	const int sleepMS = 1000 / sv_fps->value;
	if ( sleepMS < sv.timeResidual )
		return 0;

	return sleepMS - sv.timeResidual;
}
//============================================================================

// This is deliberately quite large to make it more of an effort to DoS
#define MAX_BUCKETS        16384
#define MAX_HASHES          1024

static leakyBucket_t buckets[ MAX_BUCKETS ];
static leakyBucket_t *bucketHashes[ MAX_HASHES ];
static rateLimit_t outboundRateLimit;

/*
================
SVC_HashForAddress
================
*/
static int SVC_HashForAddress( const netadr_t *address ) {
	const byte	*ip = NULL;
	int			size = 0;
	int			hash = 0;
	int			i;

	switch ( address->type ) {
		case NA_IP:  ip = address->ipv._4; size = 4;  break;
		default: break;
	}

	for ( i = 0; i < size; i++ ) {
		hash += (int)( ip[ i ] ) * ( i + 119 );
	}

	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	hash &= ( MAX_HASHES - 1 );

	return hash;
}


/*
================
SVC_RelinkToHead
================
*/
static void SVC_RelinkToHead( leakyBucket_t *bucket, int hash ) {

	if ( bucket->prev != NULL ) {
		bucket->prev->next = bucket->next;
	} else {
		return;
	}

	if ( bucket->next != NULL ) {
		bucket->next->prev = bucket->prev;
	}

	bucket->next = bucketHashes[ hash ];
	if ( bucketHashes[ hash ] != NULL ) {
		bucketHashes[ hash ]->prev = bucket;
	}

	bucket->prev = NULL;
	bucketHashes[ hash ] = bucket;
}


/*
================
SVC_BucketForAddress

Find or allocate a bucket for an address
================
*/
static leakyBucket_t *SVC_BucketForAddress( const netadr_t *address, int burst, int period ) {
	static leakyBucket_t dummy = { 0 };
	static int		start = 0;
	const int		hash = SVC_HashForAddress( address );
	const int		now = Sys_Milliseconds();
	leakyBucket_t	*bucket;
	int				i, n;

	for ( bucket = bucketHashes[ hash ], n = 0; bucket; bucket = bucket->next, n++ ) {
		switch ( bucket->type ) {
			case NA_IP:
				//if ( memcmp( bucket->ipv._4, address->ipv._4, 4 ) == 0 ) {
				if ( memcmp( bucket->ipv._4, address->ipv._4, 4 ) == 0 ) {
					if ( n > 8 ) {
						SVC_RelinkToHead( bucket, hash );
					}
					return bucket;
				}
				break;
			default:
				return &dummy;
		}
	}

	for ( i = 0; i < MAX_BUCKETS; i++ ) {
		int interval;

		if ( start >= MAX_BUCKETS )
			start = 0;
		bucket = &buckets[ start++ ];
		interval = now - bucket->rate.lastTime;

		// Reclaim expired buckets
		if ( bucket->type != NA_BAD && (unsigned)interval > ( bucket->rate.burst * period ) ) {
			if ( bucket->prev != NULL ) {
				bucket->prev->next = bucket->next;
			} else {
				bucketHashes[ bucket->hash ] = bucket->next;
			}
			
			if ( bucket->next != NULL ) {
				bucket->next->prev = bucket->prev;
			}

			bucket->type = NA_BAD;
		}

		if ( bucket->type == NA_BAD ) {
			bucket->type = address->type;
			switch ( address->type ) {
				case NA_IP:  Com_Memcpy( bucket->ipv._4, address->ipv._4, 4 );  break;
				default: break;
			}

			bucket->rate.lastTime = now;
			bucket->rate.burst = 0;
			bucket->hash = hash;
			bucket->toxic = 0;

			// Add to the head of the relevant hash chain
			bucket->next = bucketHashes[ hash ];
			if ( bucketHashes[ hash ] != NULL ) {
				bucketHashes[ hash ]->prev = bucket;
			}

			bucket->prev = NULL;
			bucketHashes[ hash ] = bucket;

			return bucket;
		}
	}

	// Couldn't allocate a bucket for this address
	return NULL;
}

/*
================
SVC_RateLimit
================
*/
qboolean SVC_RateLimit( rateLimit_t *bucket, int burst, int period ) {
	int now = Sys_Milliseconds();
	int interval = now - bucket->lastTime;
	int expired = interval / period;
	int expiredRemainder = interval % period;

	if ( expired > bucket->burst || interval < 0 ) {
		bucket->burst = 0;
		bucket->lastTime = now;
	} else {
		bucket->burst -= expired;
		bucket->lastTime = now - expiredRemainder;
	}

	if ( bucket->burst < burst ) {
		bucket->burst++;
		return qfalse;
	}

	return qtrue;
}


/*
================
SVC_RateDrop
================
*/
static void SVC_RateDrop( leakyBucket_t *bucket, int burst ) {
	if ( bucket != NULL ) {
		if ( bucket->toxic < 10000 )
			++bucket->toxic;
		bucket->rate.burst = burst * bucket->toxic;
		bucket->rate.lastTime = Sys_Milliseconds();
	}
}


/*
================
SVC_RateRestoreBurst
================
*/
static void SVC_RateRestoreBurst( leakyBucket_t *bucket ) {
	if ( bucket != NULL ) {
		if ( bucket->rate.burst > 0 ) {
			bucket->rate.burst--;
		}
	}
}


/*
================
SVC_RateRestoreToxic
================
*/
static void SVC_RateRestoreToxic( leakyBucket_t *bucket ) {
	if ( bucket != NULL ) {
		if ( bucket->toxic > 0 ) {
			bucket->toxic--;
		}
	}
}


/*
================
SVC_RateLimitAddress

Rate limit for a particular address
================
*/
qboolean SVC_RateLimitAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	return bucket ? SVC_RateLimit( &bucket->rate, burst, period ) : qtrue;
}


/*
================
SVC_RateRestoreAddress

Decrease burst rate
================
*/
void SVC_RateRestoreBurstAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateRestoreBurst( bucket );
}


/*
================
SVC_RateRestoreToxicAddress

Decrease toxicity
================
*/
void SVC_RateRestoreToxicAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateRestoreToxic( bucket );
}


/*
================
SVC_RateDropAddress
================
*/
void SVC_RateDropAddress( const netadr_t *from, int burst, int period ) {
	leakyBucket_t *bucket = SVC_BucketForAddress( from, burst, period );

	SVC_RateDrop( bucket, burst );
}

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
static void SVC_Status( const netadr_t *from ) {
	char	player[MAX_NAME_LENGTH + 32]; // score + ping + name
	char	status[MAX_PACKETLEN];
	char	*s;
	int		i;
	client_t	*cl;
	playerState_t	*ps;
	int		statusLength;
	int		playerLength;
	char	infostring[MAX_INFO_STRING+160]; // add some space for challenge string

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getstatus as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SVC_Status: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Allow getstatus to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundRateLimit, 10, 100 ) ) {
		Com_DPrintf( "SVC_Status: rate limit exceeded, dropping request\n" );
		return;
	}

	// A maximum challenge length of 128 should be more than plenty.
	if ( strlen( Cmd_Argv( 1 ) ) > 128 )
		return;

	Q_strncpyz( infostring, Cvar_InfoString( CVAR_SERVERINFO, NULL ), sizeof( infostring ) );

	// echo back the parameter to status. so master servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv( 1 ) );

	s = status;
	status[0] = '\0';
	statusLength = strlen( infostring ) + 16; // strlen( "statusResponse\n\n" )

	for ( i = 0; i < sv.maxclients; i++ ) {
		cl = &svs.clients[i];
		if ( cl->state >= CS_CONNECTED ) {

			ps = SV_GameClientNum( i );
			playerLength = Com_sprintf( player, sizeof( player ), "%i %i \"%s\"\n", 
				ps->persistant[ PERS_SCORE ], cl->ping, cl->name );
			
			if ( statusLength + playerLength >= MAX_PACKETLEN-4 )
				break; // can't hold any more
			
			s = Q_stradd( s, player );
			statusLength += playerLength;
		}
	}

	NET_OutOfBandPrint( NS_SERVER, from, "statusResponse\n%s\n%s", infostring, status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
static void SVC_Info( const netadr_t *from ) {
	char    *antilag;
	int		i, count, humans;
	const char	*gamedir;
	char	infostring[MAX_INFO_STRING];

	// ignore if we are in single player
#ifndef DEDICATED
	if ( Cvar_VariableIntegerValue( "g_gametype" ) == GT_SINGLE_PLAYER || Cvar_VariableIntegerValue("ui_singlePlayerActive")) {
		return;
	}
#endif

	// Prevent using getinfo as an amplifier
	if ( SVC_RateLimitAddress( from, 10, 1000 ) ) {
		if ( com_developer->integer ) {
			Com_Printf( "SVC_Info: rate limit from %s exceeded, dropping request\n",
				NET_AdrToString( from ) );
		}
		return;
	}

	// Allow getinfo to be DoSed relatively easily, but prevent
	// excess outbound bandwidth usage when being flooded inbound
	if ( SVC_RateLimit( &outboundRateLimit, 10, 100 ) ) {
		Com_DPrintf( "SVC_Info: rate limit exceeded, dropping request\n" );
		return;
	}

	/*
	 * Check whether Cmd_Argv(1) has a sane length. This was not done in the original Quake3 version which led
	 * to the Infostring bug discovered by Luigi Auriemma. See http://aluigi.altervista.org/ for the advisory.
	 */

	// A maximum challenge length of 128 should be more than plenty.
	if ( strlen( Cmd_Argv( 1 ) ) > 128 )
		return;

	// don't count privateclients
	count = humans = 0;
	for ( i = sv_privateClients->integer; i < sv.maxclients; i++ ) {
		if ( svs.clients[i].state >= CS_CONNECTED ) {
			count++;
			if (svs.clients[i].netchan.remoteAddress.type != NA_BOT) {
				humans++;
			}
		}
	}

	infostring[0] = 0;

	// echo back the parameter to status. so servers can use it as a challenge
	// to prevent timed spoofed reply packets that add ghost servers
	Info_SetValueForKey( infostring, "challenge", Cmd_Argv( 1 ) );

	Info_SetValueForKey( infostring, "protocol", va( "%i", PROTOCOL_VERSION ) );
	Info_SetValueForKey( infostring, "hostname", sv_hostname->string );
	Info_SetValueForKey( infostring, "mapname", sv_mapname->string );
	Info_SetValueForKey( infostring, "clients", va( "%i", count ) );
	Info_SetValueForKey( infostring, "sv_maxclients", va( "%i", sv.maxclients - sv_privateClients->integer ) );
	Info_SetValueForKey( infostring, "gametype", va( "%i", sv_gametype->integer ) );
	Info_SetValueForKey( infostring, "pure", va( "%i", sv.pure ) );

	if ( sv_minPing->integer ) {
		Info_SetValueForKey( infostring, "minPing", va( "%i", sv_minPing->integer ) );
	}
	if ( sv_maxPing->integer ) {
		Info_SetValueForKey( infostring, "maxPing", va( "%i", sv_maxPing->integer ) );
	}
	gamedir = Cvar_VariableString( "fs_game" );
	if ( *gamedir ) {
		Info_SetValueForKey( infostring, "game", gamedir );
	}
	Info_SetValueForKey( infostring, "sv_allowAnonymous", va( "%i", sv_allowAnonymous->integer ) );

	// Rafael gameskill
	Info_SetValueForKey( infostring, "gameskill", va( "%i", sv_gameskill->integer ) );
	// done

	Info_SetValueForKey( infostring, "friendlyFire", va( "%i", sv_friendlyFire->integer ) );        // NERVE - SMF
	Info_SetValueForKey( infostring, "maxlives", va( "%i", sv_maxlives->integer ? 1 : 0 ) );        // NERVE - SMF
	Info_SetValueForKey( infostring, "tourney", va( "%i", sv_tourney->integer ) );              // NERVE - SMF
	Info_SetValueForKey( infostring, "gamename", GAMENAME_STRING );                               // Arnout: to be able to filter out Quake servers

	// TTimo
	antilag = Cvar_VariableString( "g_antilag" );
	if ( antilag ) {
		Info_SetValueForKey( infostring, "g_antilag", antilag );
	}

	NET_OutOfBandPrint( NS_SERVER, from, "infoResponse\n%s", infostring );
}

/*
====================
SV_SendQueuedPackets

Send download messages and queued packets in the time that we're idle, i.e.
not computing a server frame or sending client snapshots.
Return the time in msec until we expect to be called next
====================
*/
int SV_SendQueuedPackets( void )
{
	int numBlocks;
	int dlStart, deltaT, delayT;
	static int dlNextRound = 0;
	int timeVal = INT_MAX;

	// Send out fragmented packets now that we're idle
	delayT = SV_SendQueuedMessages();
	if(delayT >= 0)
		timeVal = delayT;

	if(sv_dlRate->integer)
	{
		// Rate limiting. This is very imprecise for high
		// download rates due to millisecond timedelta resolution
		dlStart = Sys_Milliseconds();
		deltaT = dlNextRound - dlStart;

		if(deltaT > 0)
		{
			if(deltaT < timeVal)
				timeVal = deltaT + 1;
		}
		else
		{
			numBlocks = SV_SendDownloadMessages();

			if(numBlocks)
			{
				// There are active downloads
				deltaT = Sys_Milliseconds() - dlStart;

				delayT = 1000 * numBlocks * MAX_DOWNLOAD_BLKSIZE;
				delayT /= sv_dlRate->integer * 1024;

				if(delayT <= deltaT + 1)
				{
					// Sending the last round of download messages
					// took too long for given rate, don't wait for
					// next round, but always enforce a 1ms delay
					// between DL message rounds so we don't hog
					// all of the bandwidth. This will result in an
					// effective maximum rate of 1MB/s per user, but the
					// low download window size limits this anyways.
					if(timeVal > 2)
						timeVal = 2;

					dlNextRound = dlStart + deltaT + 1;
				}
				else
				{
					dlNextRound = dlStart + delayT;
					delayT -= deltaT;

					if(delayT < timeVal)
						timeVal = delayT;
				}
			}
		}
	}
	else
	{
		if(SV_SendDownloadMessages())
			timeVal = 0;
	}

	return timeVal;
}

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( const netadr_t *from, msg_t *msg ) {
	char    *s;
	char    *c;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );        // skip the -1 marker

	if ( !Q_strncmp( "connect", (char*)&msg->data[4], 7 ) ) {
		DynHuff_Decompress( msg, 12 );
	}

	s = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( s );

	c = Cmd_Argv( 0 );
	Com_DPrintf( "SV packet %s : %s\n", NET_AdrToString( from ), c );

	if ( !Q_stricmp( c,"getstatus" ) ) {
		SVC_Status( from  );
	} else if ( !Q_stricmp( c,"getinfo" ) ) {
		SVC_Info( from );
	} else if ( !Q_stricmp( c,"getchallenge" ) ) {
		SV_GetChallenge( from );
	} else if ( !Q_stricmp( c,"connect" ) ) {
		SV_DirectConnect( from );
	} else if ( !Q_stricmp( c,"ipAuthorize" ) ) {
		// removed for stateless challenges
	} else if ( !Q_stricmp( c, "rcon" ) ) {
		SVC_RemoteCommand( from, msg );
	} else if ( !Q_stricmp( c,"disconnect" ) ) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	} else {
		Com_DPrintf( "bad connectionless packet from %s:\n%s\n"
					 , NET_AdrToString( from ), s );
	}
}


/*
=================
SV_ReadPackets
=================
*/
void SV_PacketEvent( const netadr_t *from, msg_t *msg ) {
	int i;
	client_t    *cl;
	int qport;

	if ( msg->cursize < 6 ) // too short for anything
		return;

	// check for connectionless packet (0xffffffff) first
	if ( *(int32_t *)msg->data == -1 ) {
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	if ( sv.state == SS_DEAD ) {
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );                // sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for ( i = 0, cl = svs.clients; i < sv.maxclients; i++, cl++ ) {
		if ( cl->state == CS_FREE ) {
			continue;
		}
		if ( !NET_CompareBaseAdr( from, &cl->netchan.remoteAddress ) ) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if ( cl->netchan.qport != qport ) {
			continue;
		}

		// make sure it is a valid, in sequence packet
		if ( SV_Netchan_Process( cl, msg ) ) {
			// the IP port can't be used to differentiate clients, because
			// some address translating routers periodically change UDP
			// port assignments
			if ( cl->netchan.remoteAddress.port != from->port ) {
				Com_Printf( "SV_PacketEvent: fixing up a translated port\n" );
				cl->netchan.remoteAddress.port = from->port;
			}
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if ( cl->state != CS_ZOMBIE ) {
				cl->lastPacketTime = svs.time;	// don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
			return;
		}
	}

	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it
	NET_OutOfBandPrint( NS_SERVER, from, "disconnect" );
}
