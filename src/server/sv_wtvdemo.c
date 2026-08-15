#include "server.h"
#include "sv_wtvdemo.h"
#include "sv_wtvdiscord.h"
#include "../qcommon/threads.h"
#include <lzma.h>

typedef struct {
	qboolean active;
	FILE *file;
	char basePath[MAX_OSPATH];   // "<homepath>/<game>/wtvdemos/<timestamp>_<map>_round<N>"
	int bytesWrittenThisFragment;

	int lastTickTime;              // sv.time of the last recorded tick, for sv_fps pacing
	int lastFullSnapshotTime;       // sv.time of the last full snapshot written

	qboolean hasBaseline[MAX_CLIENTS];
	playerState_t lastPS[MAX_CLIENTS];

	qboolean hasEntityBaseline[MAX_GENTITIES];
	entityState_t lastEntity[MAX_GENTITIES];

	char discordScoreboard[WTV_DISCORD_SCOREBOARD_MAX]; // set via WTV_SetDiscordScoreboard; "" if never set this round
} wtvRecorder_t;

static wtvRecorder_t wtv;

static char wtvPendingCommands[WTV_MAX_QUEUED_COMMANDS][MAX_STRING_CHARS];
static int wtvPendingCommandCount;

void WTV_QueueBroadcastCommand( const char *cmd ) {
	if ( !wtv.active ) {
		return;
	}
	if ( wtvPendingCommandCount >= WTV_MAX_QUEUED_COMMANDS ) {
		return; // drop if a single tick somehow produces more than 64 broadcasts
	}
	Q_strncpyz( wtvPendingCommands[wtvPendingCommandCount], cmd, MAX_STRING_CHARS );
	wtvPendingCommandCount++;
}

void WTV_SetDiscordScoreboard( const char *text ) {
	if ( !wtv.active ) {
		return;
	}
	Q_strncpyz( wtv.discordScoreboard, text, sizeof( wtv.discordScoreboard ) );
}

// 2 * MAX_CLIENTS: a full-server map_restart reconnects every client in one
// tight loop before the next WTV_RecordTick drains this queue — a bare
// MAX_CLIENTS cap leaves no headroom for other userinfo changes in that window.
#define WTV_MAX_QUEUED_IDENTITY_EVENTS ( 2 * MAX_CLIENTS )
static wtvIdentityEvent_t wtvPendingIdentityEvents[WTV_MAX_QUEUED_IDENTITY_EVENTS];
static int wtvPendingIdentityEventCount;

void WTV_RecordPlayerIdentity( int clientNum, const char *guid, const char *name ) {
	if ( !wtv.active ) {
		return;
	}
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return; // shouldn't happen from the game module, guard anyway
	}
	if ( wtvPendingIdentityEventCount >= WTV_MAX_QUEUED_IDENTITY_EVENTS ) {
		Com_Printf( "WTV: dropping identity event for client %i — more than %i queued this tick\n",
			clientNum, WTV_MAX_QUEUED_IDENTITY_EVENTS );
		return;
	}
	// zero the slot first so no stale bytes from a previous occupant (padding,
	// or the tail past each string's NUL) leak into the compressed output.
	Com_Memset( &wtvPendingIdentityEvents[wtvPendingIdentityEventCount], 0, sizeof( wtvIdentityEvent_t ) );
	wtvPendingIdentityEvents[wtvPendingIdentityEventCount].clientNum = clientNum;
	Q_strncpyz( wtvPendingIdentityEvents[wtvPendingIdentityEventCount].guid, guid, GUID_LEN );
	Q_strncpyz( wtvPendingIdentityEvents[wtvPendingIdentityEventCount].name, name, MAX_NAME_LENGTH );
	wtvPendingIdentityEventCount++;
}

// Capped at MAX_STRING_CHARS for the recorded copy only (never the real
// configstring) — accepted tradeoff vs. a 16MB array sized to avoid it.
#define WTV_MAX_QUEUED_CONFIGSTRINGS MAX_CONFIGSTRINGS
typedef struct {
	int index;
	char value[MAX_STRING_CHARS];
} wtvConfigstringEvent_t;
static wtvConfigstringEvent_t wtvPendingConfigstringEvents[WTV_MAX_QUEUED_CONFIGSTRINGS];
static int wtvPendingConfigstringEventCount;

void WTV_RecordConfigstringChange( int index, const char *value ) {
	if ( !wtv.active ) {
		return;
	}
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		return; // shouldn't happen, guard anyway
	}
	if ( wtvPendingConfigstringEventCount >= WTV_MAX_QUEUED_CONFIGSTRINGS ) {
		Com_Printf( "WTV: dropping configstring change for index %i — more than %i queued this tick\n",
			index, WTV_MAX_QUEUED_CONFIGSTRINGS );
		return;
	}
	if ( strlen( value ) > MAX_STRING_CHARS - 1 ) {
		// Known, accepted tradeoff: WTV's own recorded copy of this configstring
		// is truncated here (never the real value clients receive) — see the
		// comment above WTV_MAX_QUEUED_CONFIGSTRINGS.
		Com_Printf( "WTV: configstring %i truncated to %i bytes in recording\n", index, MAX_STRING_CHARS - 1 );
	}
	wtvPendingConfigstringEvents[wtvPendingConfigstringEventCount].index = index;
	Q_strncpyz( wtvPendingConfigstringEvents[wtvPendingConfigstringEventCount].value, value, MAX_STRING_CHARS );
	wtvPendingConfigstringEventCount++;
}

static void WTV_BuildBasePath( int roundNum, char *out, int outSize ) {
	char hpath[MAX_OSPATH];
	char game[64];
	qtime_t now;
	char mapname[64];

	Cvar_VariableStringBuffer( "fs_homepath", hpath, sizeof( hpath ) );
	Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
	Com_RealTime( &now );
	Q_strncpyz( mapname, Cvar_VariableString( "mapname" ), sizeof( mapname ) );

	Com_sprintf( out, outSize, "%s/%s/wtvdemos/%04i%02i%02i-%02i%02i%02i_%s_round%i",
		hpath, game,
		now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
		now.tm_hour, now.tm_min, now.tm_sec,
		mapname, roundNum );
}

static void WTV_OpenFragment( int partNumber ) {
	char dirPath[MAX_OSPATH];
	char filePath[MAX_OSPATH + 16];
	char hpath[MAX_OSPATH];
	char game[64];
	wtvHeader_t header;

	Cvar_VariableStringBuffer( "fs_homepath", hpath, sizeof( hpath ) );
	Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
	Com_sprintf( dirPath, sizeof( dirPath ), "%s/%s/wtvdemos", hpath, game );
	Sys_Mkdir( dirPath );

	Com_sprintf( filePath, sizeof( filePath ), "%s.wtvtmp", wtv.basePath );

	wtv.file = fopen( filePath, "wb" );
	if ( !wtv.file ) {
		Com_Printf( "WTV: failed to open %s for recording\n", filePath );
		wtv.active = qfalse;
		return;
	}

	header.magic = WTV_MAGIC;
	header.version = WTV_VERSION;
	Q_strncpyz( header.mapname, Cvar_VariableString( "mapname" ), sizeof( header.mapname ) );

	fwrite( &header, sizeof( header ), 1, wtv.file );
	wtv.bytesWrittenThisFragment = (int)sizeof( header );
}

static void WTV_CloseFragment( void ) {
	if ( !wtv.file ) {
		return;
	}
	fclose( wtv.file );
	wtv.file = NULL;
}

static void WTV_DeleteTempFile( void ) {
	char filePath[MAX_OSPATH + 16];
	Com_sprintf( filePath, sizeof( filePath ), "%s.wtvtmp", wtv.basePath );
	remove( filePath );
}

void WTV_RecordStart( int roundNum ) {
	if ( wtv.active ) {
		// a previous round's stop trap should always fire before the next round's
		// start trap; if this ever fires it means the lifecycle guard in g_main.c/
		// g_svcmds.c let a round begin while the last one's file was still open.
		Com_Printf( "WTV: RecordStart called while already recording — closing previous file first\n" );
		WTV_CloseFragment();
	}

	Com_Memset( &wtv, 0, sizeof( wtv ) );
	WTV_BuildBasePath( roundNum, wtv.basePath, sizeof( wtv.basePath ) );
	wtv.active = qtrue;
	WTV_OpenFragment( 1 );

	// wtvPendingCommands/-Count are file-static, outside wtvRecorder_t, so the
	// memset above doesn't reach them; a leftover count from the previous
	// round's queue would otherwise corrupt this round's first recorded tick.
	wtvPendingCommandCount = 0;
	wtvPendingIdentityEventCount = 0;
	wtvPendingConfigstringEventCount = 0;

	// Dump every currently-set configstring once, so the first recorded tick
	// carries full initial state; after that it's just the per-change hook.
	{
		int i;
		for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
			if ( sv.configstrings[i] && sv.configstrings[i][0] ) {
				WTV_RecordConfigstringChange( i, sv.configstrings[i] );
			}
		}
	}
}

// Fire-and-forget dispatch (mirrors stats_submit_curl.c) — args must be a
// heap copy the thread owns/frees itself; Threads_Create never joins.
typedef struct {
	char tempFilePath[MAX_OSPATH];
	char finalBasePath[MAX_OSPATH];
	entityState_t *mapBaselines; // heap copy of sv.svEntities[].baseline; see WTV_RecordStop
	char *discordScoreboard; // heap copy of wtv.discordScoreboard; NULL if never set this round
} wtvCompressThreadArgs_t;

static void *WTV_CompressThreadFn( void *args ) {
	wtvCompressThreadArgs_t *threadArgs = (wtvCompressThreadArgs_t *)args;
	WTV_CompressRound( threadArgs->tempFilePath, threadArgs->finalBasePath, threadArgs->mapBaselines, threadArgs->discordScoreboard );
	free( threadArgs );
	return NULL;
}

void WTV_RecordStop( int aborted ) {
	if ( !wtv.active ) {
		return;
	}
	WTV_CloseFragment();
	if ( aborted ) {
		WTV_DeleteTempFile();
	} else {
		wtvCompressThreadArgs_t *threadArgs = (wtvCompressThreadArgs_t *)malloc( sizeof( wtvCompressThreadArgs_t ) );
		if ( threadArgs ) {
			// sv.svEntities[] gets zeroed by SV_ClearServer shortly after this
			// returns — snapshot the baselines now into a private copy the
			// background thread owns, so it never reads live sv state.
			threadArgs->mapBaselines = (entityState_t *)malloc( sizeof( entityState_t ) * MAX_GENTITIES );
			if ( threadArgs->mapBaselines ) {
				int i;
				for ( i = 0; i < MAX_GENTITIES; i++ ) {
					threadArgs->mapBaselines[i] = sv.svEntities[i].baseline;
				}
				Com_sprintf( threadArgs->tempFilePath, sizeof( threadArgs->tempFilePath ), "%s.wtvtmp", wtv.basePath );
				Q_strncpyz( threadArgs->finalBasePath, wtv.basePath, sizeof( threadArgs->finalBasePath ) );
				// NULL if no scoreboard was ever set this round (not an allocation
				// failure worth aborting the round over) — treated the same as an
				// empty webhook cvar: upload the file(s) with no message text.
				if ( wtv.discordScoreboard[0] ) {
					size_t len = strlen( wtv.discordScoreboard ) + 1;
					threadArgs->discordScoreboard = (char *)malloc( len );
					if ( threadArgs->discordScoreboard ) {
						Com_Memcpy( threadArgs->discordScoreboard, wtv.discordScoreboard, len );
					}
				} else {
					threadArgs->discordScoreboard = NULL;
				}
				Threads_Create( WTV_CompressThreadFn, threadArgs );
			} else {
				Com_Printf( "WTV: failed to allocate baseline snapshot — recording for this round left as an unprocessed temp file\n" );
				free( threadArgs );
			}
		} else {
			Com_Printf( "WTV: failed to allocate compression thread args — recording for this round left as an unprocessed temp file\n" );
		}
	}
	wtv.active = qfalse;
	// defensively drop anything queued but not yet drained
	wtvPendingCommandCount = 0;
	wtvPendingIdentityEventCount = 0;
	wtvPendingConfigstringEventCount = 0;
}

qboolean WTV_IsRecording( void ) {
	return wtv.active;
}

static void WTV_WriteEntities( msg_t *msg, qboolean fullSnapshot ) {
	int i;
	entityState_t *ent;
	qboolean presentThisTick[MAX_GENTITIES];

	Com_Memset( presentThisTick, 0, sizeof( presentThisTick ) );

	for ( i = 0; i < svs.currFrame->count; i++ ) {
		ent = svs.currFrame->ents[i];
		presentThisTick[ent->number] = qtrue;
		if ( fullSnapshot || !wtv.hasEntityBaseline[ent->number] ) {
			MSG_WriteDeltaEntity( msg, &sv.svEntities[ent->number].baseline, ent, qtrue );
		} else {
			MSG_WriteDeltaEntity( msg, &wtv.lastEntity[ent->number], ent, qfalse );
		}
		wtv.lastEntity[ent->number] = *ent;
		wtv.hasEntityBaseline[ent->number] = qtrue;
	}

	// An entity present last tick but absent this one needs an explicit
	// removal record (mirrors SV_EmitPacketEntities), or playback would
	// never know it stopped existing and would keep drawing it forever.
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( wtv.hasEntityBaseline[i] && !presentThisTick[i] ) {
			MSG_WriteDeltaEntity( msg, &wtv.lastEntity[i], NULL, qtrue );
			wtv.hasEntityBaseline[i] = qfalse;
		}
	}

	MSG_WriteBits( msg, ( MAX_GENTITIES - 1 ), GENTITYNUM_BITS ); // end of entity list, mirrors SV_EmitPacketEntities
}

static void WTV_WritePlayerStates( msg_t *msg, qboolean fullSnapshot ) {
	int i;
	playerState_t *ps;
	byte activeCount = 0;

	// msg is Huffman bit-packed here, so a byte doesn't land at a fixed
	// offset — the count must be written once, up front, never patched later.
	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		if ( svs.clients[i].state == CS_ACTIVE ) {
			activeCount++;
		}
	}
	MSG_WriteByte( msg, activeCount );

	for ( i = 0; i < sv_maxclients->integer; i++ ) {
		if ( svs.clients[i].state != CS_ACTIVE ) {
			continue;
		}
		ps = SV_GameClientNum( i );
		MSG_WriteByte( msg, i );
		if ( fullSnapshot || !wtv.hasBaseline[i] ) {
			MSG_WriteDeltaPlayerstate( msg, NULL, ps );
		} else {
			MSG_WriteDeltaPlayerstate( msg, &wtv.lastPS[i], ps );
		}
		wtv.lastPS[i] = *ps;
		wtv.hasBaseline[i] = qtrue;
	}
}

static void WTV_WriteReliableCommands( msg_t *msg ) {
	int i;
	MSG_WriteByte( msg, (byte)wtvPendingCommandCount );
	for ( i = 0; i < wtvPendingCommandCount; i++ ) {
		MSG_WriteString( msg, wtvPendingCommands[i] );
		if ( msg->overflowed ) {
			break;
		}
	}
	if ( !msg->overflowed ) {
		wtvPendingCommandCount = 0;
	}
	// else: leave the queue as-is — WTV_RecordTick drops the whole tick on
	// overflow, so every entry (even ones already written) must retry next tick.
}

static void WTV_WriteIdentityEvents( msg_t *msg ) {
	int i;
	MSG_WriteByte( msg, (byte)wtvPendingIdentityEventCount );
	for ( i = 0; i < wtvPendingIdentityEventCount; i++ ) {
		MSG_WriteByte( msg, wtvPendingIdentityEvents[i].clientNum );
		MSG_WriteString( msg, wtvPendingIdentityEvents[i].guid );
		MSG_WriteString( msg, wtvPendingIdentityEvents[i].name );
		if ( msg->overflowed ) {
			break;
		}
	}
	if ( !msg->overflowed ) {
		wtvPendingIdentityEventCount = 0;
	}
	// else: leave the queue as-is — WTV_RecordTick drops the whole tick on
	// overflow, so every entry (even ones already written) must retry next tick.
}

static void WTV_WriteConfigstringChanges( msg_t *msg ) {
	int i;
	// count can exceed a single byte (up to MAX_CONFIGSTRINGS queued), so write
	// it as a short rather than reusing the byte-sized count pattern the
	// commands/identity-events blocks use (both capped well under 256).
	MSG_WriteShort( msg, (short)wtvPendingConfigstringEventCount );
	for ( i = 0; i < wtvPendingConfigstringEventCount; i++ ) {
		MSG_WriteShort( msg, (short)wtvPendingConfigstringEvents[i].index );
		MSG_WriteString( msg, wtvPendingConfigstringEvents[i].value );
		if ( msg->overflowed ) {
			break;
		}
	}
	if ( !msg->overflowed ) {
		wtvPendingConfigstringEventCount = 0;
	}
	// else: same "whole tick is dropped, retry everything" reasoning as
	// WTV_WriteIdentityEvents above.
}

void WTV_RecordTick( void ) {
	static byte msgBuffer[WTV_MAX_TICK_MSGLEN];
	msg_t msg;
	qboolean fullSnapshot;
	int payloadLength;

	if ( !wtv.active ) {
		return;
	}
	if ( sv.time - wtv.lastTickTime < 1000 / sv_fps->integer ) {
		return; // not yet time for the next recorded tick, paced by live sv_fps
	}
	wtv.lastTickTime = sv.time;

	if ( svs.currFrame == NULL ) {
		return; // not built yet this frame; catch it on a later tick instead of crashing
	}

	fullSnapshot = ( wtv.lastFullSnapshotTime == 0 ) ||
		( sv.time - wtv.lastFullSnapshotTime >= WTV_FULL_SNAPSHOT_INTERVAL_MS );

	MSG_Init( &msg, msgBuffer, sizeof( msgBuffer ) ); // msg.oob defaults to qfalse: Huffman-coded like live snapshots
	MSG_WriteLong( &msg, sv.time );
	MSG_WriteByte( &msg, fullSnapshot ? 1 : 0 );
	WTV_WriteReliableCommands( &msg );
	WTV_WriteIdentityEvents( &msg );
	WTV_WriteConfigstringChanges( &msg );
	WTV_WritePlayerStates( &msg, fullSnapshot );
	WTV_WriteEntities( &msg, fullSnapshot );

	if ( msg.overflowed ) {
		// Warn-and-drop, matching SV_SendClientSnapshot's convention — losing
		// one tick beats crashing the server mid-round.
		Com_Printf( "WARNING: WTV record tick overflowed (%i clients, %i entities) — tick dropped\n",
			sv_maxclients->integer, svs.currFrame->count );
		return;
	}

	payloadLength = msg.cursize;
	fwrite( &payloadLength, sizeof( payloadLength ), 1, wtv.file );
	fwrite( msg.data, 1, payloadLength, wtv.file );
	wtv.bytesWrittenThisFragment += (int)sizeof( payloadLength ) + payloadLength;

	if ( fullSnapshot ) {
		wtv.lastFullSnapshotTime = sv.time;
	}
}

// Round compression: decodes a completed round's .wtvtmp, re-serializes it
// byte-aligned. Own state, separate from wtvRecorder_t/wtv (live recording).

typedef struct {
	// Delta-decode baselines: mirror the bit-stream's own delta chain, i.e.
	// what WTV_WritePlayerStates/WTV_WriteEntities used as "from" on encode.
	playerState_t lastPS[MAX_CLIENTS];
	qboolean hasPSBaseline[MAX_CLIENTS];
	entityState_t lastEntity[MAX_GENTITIES];
	qboolean hasEntityBaseline[MAX_GENTITIES];

	// Separate "last written to output" baselines — a full-snapshot tick
	// force-resends unchanged values in the bit-stream; comparing against
	// these (not the delta-decode baselines above) lets the writer still dedup.
	playerState_t lastWrittenPS[MAX_CLIENTS];
	qboolean hasWrittenPS[MAX_CLIENTS];
	entityState_t lastWrittenEntity[MAX_GENTITIES];
	qboolean hasWrittenEntity[MAX_GENTITIES];
} wtvDecodeState_t;

// fast (0-2); preset 6 (normal mode/bt4) was tried empirically and gave the
// same ratio for far more CPU time — the remaining entropy here is mostly
// continuously-varying floats, which doesn't benefit from deeper match-finding.
#define WTV_XZ_PRESET 1u
#define WTV_XZ_DICT_SIZE (32u * 1024u * 1024u)   // comfortably covers a full fragment's worth of byte-aligned data

// Sets up a fresh LZMA2 .xz stream encoder with a large custom dictionary.
// opt/filters can be plain locals — lzma_stream_encoder copies what it needs.
static qboolean WTV_InitEncoder( lzma_stream *strm ) {
	lzma_options_lzma opt;
	lzma_filter filters[2];
	lzma_stream tmp = LZMA_STREAM_INIT;

	*strm = tmp;

	if ( lzma_lzma_preset( &opt, WTV_XZ_PRESET ) ) {
		Com_Printf( "WTV: lzma_lzma_preset failed\n" );
		return qfalse;
	}
	opt.dict_size = WTV_XZ_DICT_SIZE;

	filters[0].id = LZMA_FILTER_LZMA2;
	filters[0].options = &opt;
	filters[1].id = LZMA_VLI_UNKNOWN;
	filters[1].options = NULL;

	if ( lzma_stream_encoder( strm, filters, LZMA_CHECK_CRC32 ) != LZMA_OK ) {
		Com_Printf( "WTV: lzma_stream_encoder failed\n" );
		return qfalse;
	}
	return qtrue;
}

// Per-round compression state, private to one WTV_CompressRound call and,
// via its background worker thread, private to one round's compression pass —
// never shared with wtvRecorder_t/wtv or with any other round's compression.
typedef struct {
	lzma_stream strm;
	FILE *fragmentFile;
	int partNumber;
	int compressedBytesThisFragment; // real on-disk compressed size; drives the WTV_FRAGMENT_MAX_BYTES rollover decision
	int byteAlignedBytesThisFragment; // decompressed-stream position; drives wtvIndexEntry_t.byteOffset values
	char finalBasePath[MAX_OSPATH];
	const entityState_t *mapBaselines; // private snapshot of sv.svEntities[].baseline; never read live sv here
	wtvIndexEntry_t indexEntries[WTV_MAX_INDEX_ENTRIES];
	int indexEntryCount;
	qboolean failed; // set once a fragment file/lzma_code error makes further output for this round unreliable
	qboolean rolloverPending; // threshold crossed; actual rollover deferred to the next tick boundary
} wtvCompressState_t;

void WTV_BuildFragmentPath( const char *finalBasePath, int partNumber, char *out, int outSize ) {
	if ( partNumber == 1 ) {
		Com_sprintf( out, outSize, "%s.wtv", finalBasePath );
	} else {
		Com_sprintf( out, outSize, "%s.part%i.wtv", finalBasePath, partNumber );
	}
}

// Opens fragment partNumber (finalBasePath + ".wtv" for part 1, "+.partN.wtv"
// after) and writes its header with placeholder index fields — those get
// patched by WTV_CloseFinalFragment once the fragment's real values are known.
static qboolean WTV_OpenFinalFragment( wtvCompressState_t *cs, int partNumber ) {
	char filePath[MAX_OSPATH + 16];
	wtvFinalHeader_t header;

	if ( !WTV_InitEncoder( &cs->strm ) ) {
		return qfalse;
	}

	WTV_BuildFragmentPath( cs->finalBasePath, partNumber, filePath, sizeof( filePath ) );
	cs->fragmentFile = fopen( filePath, "wb" );
	if ( !cs->fragmentFile ) {
		Com_Printf( "WTV: failed to open %s for compressed output\n", filePath );
		lzma_end( &cs->strm );
		return qfalse;
	}

	Com_Memset( &header, 0, sizeof( header ) );
	header.magic = WTV_MAGIC;
	header.version = WTV_VERSION;
	header.partNumber = partNumber;
	if ( fwrite( &header, sizeof( header ), 1, cs->fragmentFile ) != 1 ) {
		Com_Printf( "WTV: fwrite failed writing header to %s\n", filePath );
		cs->failed = qtrue;
		fclose( cs->fragmentFile );
		cs->fragmentFile = NULL;
		lzma_end( &cs->strm );
		return qfalse;
	}

	cs->partNumber = partNumber;
	cs->compressedBytesThisFragment = (int)sizeof( header );
	cs->byteAlignedBytesThisFragment = 0;
	cs->indexEntryCount = 0;
	return qtrue;
}

// Finishes cs->strm's xz stream, patches the fragment header's real
// indexOffset/indexCount/hasNextPart, and closes it. Called for both a
// mid-round rollover and the final fragment at round end.
static void WTV_CloseFinalFragment( wtvCompressState_t *cs, qboolean hasNextPart ) {
	byte indexBuf[WTV_MAX_INDEX_ENTRIES * sizeof( wtvIndexEntry_t )];
	int indexBytes;
	int indexOffset;
	int indexCount;
	lzma_ret ret;
	int flag;

	if ( !cs->fragmentFile ) {
		lzma_end( &cs->strm );
		return;
	}

	indexBytes = cs->indexEntryCount * (int)sizeof( wtvIndexEntry_t );
	indexOffset = cs->byteAlignedBytesThisFragment;
	indexCount = cs->indexEntryCount;

	// Fed straight to lzma_code (not WTV_FeedCompressor) so finalizing a
	// fragment can never itself trigger another rollover.
	if ( !cs->failed && indexBytes > 0 ) {
		Com_Memcpy( indexBuf, cs->indexEntries, indexBytes );
		cs->strm.next_in = indexBuf;
		cs->strm.avail_in = (size_t)indexBytes;
		do {
			byte outBuf[65536];
			int producedThisCall;
			cs->strm.next_out = outBuf;
			cs->strm.avail_out = sizeof( outBuf );
			ret = lzma_code( &cs->strm, LZMA_RUN );
			producedThisCall = (int)( sizeof( outBuf ) - cs->strm.avail_out );
			if ( producedThisCall > 0 ) {
				if ( fwrite( outBuf, 1, producedThisCall, cs->fragmentFile ) != (size_t)producedThisCall ) {
					Com_Printf( "WTV: fwrite failed writing index trailer\n" );
					cs->failed = qtrue;
					break;
				}
			}
			if ( ret != LZMA_OK ) {
				Com_Printf( "WTV: lzma_code error %d writing index trailer\n", (int)ret );
				cs->failed = qtrue;
				break;
			}
		} while ( cs->strm.avail_in > 0 );
	}

	// Always finish the stream to LZMA_STREAM_END, even after an index-trailer
	// error above — the fragment must still end with a structurally valid xz
	// stream rather than being truncated mid-flight.
	cs->strm.next_in = NULL;
	cs->strm.avail_in = 0;
	do {
		byte outBuf[65536];
		int producedThisCall;
		cs->strm.next_out = outBuf;
		cs->strm.avail_out = sizeof( outBuf );
		ret = lzma_code( &cs->strm, LZMA_FINISH );
		producedThisCall = (int)( sizeof( outBuf ) - cs->strm.avail_out );
		if ( producedThisCall > 0 ) {
			if ( fwrite( outBuf, 1, producedThisCall, cs->fragmentFile ) != (size_t)producedThisCall ) {
				Com_Printf( "WTV: fwrite failed finishing compressed stream\n" );
				cs->failed = qtrue;
				break;
			}
		}
		if ( ret != LZMA_OK && ret != LZMA_STREAM_END ) {
			Com_Printf( "WTV: lzma_code error %d finishing stream\n", (int)ret );
			cs->failed = qtrue;
			break;
		}
	} while ( ret != LZMA_STREAM_END );
	lzma_end( &cs->strm );

	fseek( cs->fragmentFile, (long)offsetof( wtvFinalHeader_t, indexOffset ), SEEK_SET );
	if ( fwrite( &indexOffset, sizeof( indexOffset ), 1, cs->fragmentFile ) != 1 ) {
		Com_Printf( "WTV: fwrite failed patching indexOffset header field\n" );
		cs->failed = qtrue;
	}
	fseek( cs->fragmentFile, (long)offsetof( wtvFinalHeader_t, indexCount ), SEEK_SET );
	if ( fwrite( &indexCount, sizeof( indexCount ), 1, cs->fragmentFile ) != 1 ) {
		Com_Printf( "WTV: fwrite failed patching indexCount header field\n" );
		cs->failed = qtrue;
	}
	if ( hasNextPart ) {
		flag = 1;
		fseek( cs->fragmentFile, (long)offsetof( wtvFinalHeader_t, hasNextPart ), SEEK_SET );
		if ( fwrite( &flag, sizeof( flag ), 1, cs->fragmentFile ) != 1 ) {
			Com_Printf( "WTV: fwrite failed patching hasNextPart header field\n" );
			cs->failed = qtrue;
		}
	}

	// Checked last so a late buffered-write error still sets cs->failed
	// before WTV_CompressRound might otherwise remove() the recoverable temp.
	if ( fclose( cs->fragmentFile ) != 0 ) {
		Com_Printf( "WTV: fclose failed for fragment part %i\n", cs->partNumber );
		cs->failed = qtrue;
	}
	cs->fragmentFile = NULL;
}

// Streams data through cs->strm to the fragment file. Rollover is only
// flagged (cs->rolloverPending), not acted on, so a tick's several feed
// calls never split across two fragments. No-ops once cs->failed is set.
static void WTV_FeedCompressor( wtvCompressState_t *cs, const byte *data, int len ) {
	byte outBuf[65536];

	if ( cs->failed || !cs->fragmentFile || len <= 0 ) {
		return;
	}

	cs->strm.next_in = data;
	cs->strm.avail_in = (size_t)len;
	do {
		lzma_ret ret;
		int producedThisCall;

		cs->strm.next_out = outBuf;
		cs->strm.avail_out = sizeof( outBuf );
		ret = lzma_code( &cs->strm, LZMA_RUN );
		producedThisCall = (int)( sizeof( outBuf ) - cs->strm.avail_out );
		if ( producedThisCall > 0 ) {
			if ( fwrite( outBuf, 1, producedThisCall, cs->fragmentFile ) != (size_t)producedThisCall ) {
				Com_Printf( "WTV: fwrite failed during compression — round output truncated\n" );
				cs->failed = qtrue;
				return;
			}
			cs->compressedBytesThisFragment += producedThisCall;
		}
		if ( ret != LZMA_OK ) {
			Com_Printf( "WTV: lzma_code error %d during compression — round output truncated\n", (int)ret );
			cs->failed = qtrue;
			return;
		}
	} while ( cs->strm.avail_in > 0 );
	cs->byteAlignedBytesThisFragment += len;

	if ( cs->compressedBytesThisFragment >= WTV_FRAGMENT_MAX_BYTES ) {
		cs->rolloverPending = qtrue;
	}
}

// Reads one length-prefixed record into scratchBuf, sets up msg for reading
// (mirrors MB_InitMessage in cl_demo.c). qfalse means: stop decoding.
static qboolean WTV_ReadOneTick( FILE *tempFile, byte *scratchBuf, int scratchBufSize, msg_t *msg ) {
	int payloadLength;

	if ( fread( &payloadLength, sizeof( payloadLength ), 1, tempFile ) != 1 ) {
		return qfalse; // clean EOF
	}
	if ( payloadLength <= 0 || payloadLength > scratchBufSize ) {
		Com_Printf( "WTV: corrupt tick record (length %i) — stopping decode\n", payloadLength );
		return qfalse;
	}
	if ( fread( scratchBuf, 1, payloadLength, tempFile ) != (size_t)payloadLength ) {
		Com_Printf( "WTV: truncated tick record — stopping decode\n" );
		return qfalse;
	}

	MSG_Init( msg, scratchBuf, scratchBufSize );
	MSG_BeginReading( msg );
	msg->cursize = payloadLength;
	return qtrue;
}

// Mirrors MSG_ReadString but writes into the caller's buffer, not the
// engine's static one — safe from the background thread. dst may be NULL
// to skip without storing (still advances msg's position the same way).
static int WTV_ReadStringSafe( msg_t *msg, char *dst, int dstSize ) {
	int strLen = 0;
	for ( ;; ) {
		int c = MSG_ReadByte( msg );
		if ( c <= 0 || strLen >= dstSize - 1 ) {
			break;
		}
		if ( dst ) {
			dst[strLen] = ( c == '%' || c > 127 ) ? '.' : (char)c;
		}
		strLen++;
	}
	if ( dst ) {
		dst[strLen] = '\0';
	}
	return strLen;
}

// Decodes one tick's message (WTV_RecordTick's exact write order) and feeds
// whatever changed through cs's xz compressor; full-snapshot ticks also get
// a wtvIndexEntry_t recorded in cs.
static void WTV_DecodeAndWriteTick( msg_t *msg, wtvDecodeState_t *decodeState, wtvCompressState_t *cs ) {
	wtvIntermediateTickHeader_t tickHeader;
	int changedClientNum[MAX_CLIENTS];
	int changedClientCount = 0;
	int changedEntNum[MAX_GENTITIES];
	int changedEntCount = 0;
	char cmdText[WTV_MAX_QUEUED_COMMANDS][MAX_STRING_CHARS];
	int commandCount = 0;
	wtvIdentityEvent_t identityEvents[WTV_MAX_QUEUED_IDENTITY_EVENTS];
	int identityEventCount = 0;
	wtvConfigstringEvent_t configstringEvents[WTV_MAX_QUEUED_CONFIGSTRINGS];
	int configstringEventCount = 0;
	int rawIdentityCount;
	int rawConfigstringCount;
	int serverTime;
	qboolean fullSnapshot;
	int activeCount;
	int cmdCount;
	int tickStartOffset;
	int i;

	serverTime = MSG_ReadLong( msg );
	fullSnapshot = (qboolean)MSG_ReadByte( msg );

	cmdCount = MSG_ReadByte( msg );
	for ( i = 0; i < cmdCount; i++ ) {
		char *dst = ( commandCount < WTV_MAX_QUEUED_COMMANDS ) ? cmdText[commandCount] : NULL;
		WTV_ReadStringSafe( msg, dst, MAX_STRING_CHARS );
		if ( dst ) {
			commandCount++;
		}
		// else: overflow/corrupt record — still consumed to keep the
		// bit-stream position correct, just not stored.
	}

	rawIdentityCount = MSG_ReadByte( msg );
	for ( i = 0; i < rawIdentityCount; i++ ) {
		int clientNum = MSG_ReadByte( msg );
		wtvIdentityEvent_t *dst = ( identityEventCount < WTV_MAX_QUEUED_IDENTITY_EVENTS ) ? &identityEvents[identityEventCount] : NULL;
		char guidBuf[GUID_LEN];
		char nameBuf[MAX_NAME_LENGTH];

		WTV_ReadStringSafe( msg, guidBuf, sizeof( guidBuf ) );
		WTV_ReadStringSafe( msg, nameBuf, sizeof( nameBuf ) );

		if ( dst && clientNum >= 0 && clientNum < MAX_CLIENTS ) {
			// Zero first so no stale stack padding leaks into the output.
			Com_Memset( dst, 0, sizeof( *dst ) );
			dst->clientNum = clientNum;
			Q_strncpyz( dst->guid, guidBuf, sizeof( dst->guid ) );
			Q_strncpyz( dst->name, nameBuf, sizeof( dst->name ) );
			identityEventCount++;
		}
		// else: overflow/corrupt record — still consumed to keep the
		// bit-stream position correct, just not stored.
	}

	rawConfigstringCount = MSG_ReadShort( msg );
	// Check the genuine -1-past-EOF case before the unsigned cast below,
	// which would otherwise turn it into 65535 and spin the loop that long.
	if ( rawConfigstringCount < 0 ) {
		rawConfigstringCount = 0;
	} else {
		rawConfigstringCount = (unsigned short)rawConfigstringCount;
	}
	for ( i = 0; i < rawConfigstringCount; i++ ) {
		int csIndex = (unsigned short)MSG_ReadShort( msg );
		wtvConfigstringEvent_t *dst = ( configstringEventCount < WTV_MAX_QUEUED_CONFIGSTRINGS ) ? &configstringEvents[configstringEventCount] : NULL;
		char valueBuf[MAX_STRING_CHARS];

		WTV_ReadStringSafe( msg, valueBuf, sizeof( valueBuf ) );

		if ( dst && csIndex >= 0 && csIndex < MAX_CONFIGSTRINGS ) {
			dst->index = csIndex;
			Q_strncpyz( dst->value, valueBuf, sizeof( dst->value ) );
			configstringEventCount++;
		}
		// else: overflow/corrupt record — still consumed to keep the
		// bit-stream position correct, just not stored.
	}

	activeCount = MSG_ReadByte( msg );
	for ( i = 0; i < activeCount; i++ ) {
		int clientNum = MSG_ReadByte( msg );
		playerState_t decodedPS;

		if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
			break; // corrupt/truncated record — mirrors the entity loop's own entNum guard
		}

		// Must mirror WTV_WritePlayerStates' baseline choice exactly, or
		// untransmitted fields reconstruct to the wrong (stale) value.
		MSG_ReadDeltaPlayerstate( msg, ( fullSnapshot || !decodeState->hasPSBaseline[clientNum] ) ? NULL : &decodeState->lastPS[clientNum], &decodedPS );
		decodeState->lastPS[clientNum] = decodedPS;
		decodeState->hasPSBaseline[clientNum] = qtrue;

		if ( changedClientCount < MAX_CLIENTS &&
			( !decodeState->hasWrittenPS[clientNum] ||
			memcmp( &decodedPS, &decodeState->lastWrittenPS[clientNum], sizeof( decodedPS ) ) != 0 ) ) {
			changedClientNum[changedClientCount++] = clientNum;
			decodeState->lastWrittenPS[clientNum] = decodedPS;
			decodeState->hasWrittenPS[clientNum] = qtrue;
		}
	}

	for ( ;; ) {
		int entNum = MSG_ReadEntitynum( msg ); // -1 past end of message
		entityState_t decoded;
		const entityState_t *from;

		if ( entNum < 0 || entNum == ( MAX_GENTITIES - 1 ) ) {
			break; // EOF or the end-of-list terminator WTV_WriteEntities writes
		}

		// Must mirror WTV_WriteEntities' baseline choice exactly (see the
		// playerstate comment above). cs->mapBaselines is a private copy.
		from = ( fullSnapshot || !decodeState->hasEntityBaseline[entNum] ) ? &cs->mapBaselines[entNum] : &decodeState->lastEntity[entNum];
		MSG_ReadDeltaEntity( msg, from, &decoded, entNum );

		if ( decoded.number == ( MAX_GENTITIES - 1 ) ) {
			// Removal — reset this slot so a later reuse of entNum is a
			// fresh baseline write, not delta'd against the removed entity.
			decodeState->hasEntityBaseline[entNum] = qfalse;
			decodeState->hasWrittenEntity[entNum] = qfalse;
		} else {
			decodeState->lastEntity[entNum] = decoded;
			decodeState->hasEntityBaseline[entNum] = qtrue;
		}

		if ( changedEntCount < MAX_GENTITIES &&
			( !decodeState->hasWrittenEntity[entNum] ||
			memcmp( &decoded, &decodeState->lastWrittenEntity[entNum], sizeof( decoded ) ) != 0 ) ) {
			changedEntNum[changedEntCount++] = entNum;
			decodeState->lastWrittenEntity[entNum] = decoded;
			decodeState->hasWrittenEntity[entNum] = qtrue;
		}
	}

	// Full-snapshot ticks get an index entry pointing at this tick's start
	// position in the decompressed stream — captured before any of this
	// tick's data below is fed to the compressor.
	tickStartOffset = -1;
	if ( fullSnapshot && cs->indexEntryCount < WTV_MAX_INDEX_ENTRIES ) {
		tickStartOffset = cs->byteAlignedBytesThisFragment;
	}

	tickHeader.serverTime = serverTime;
	tickHeader.commandCount = commandCount;
	tickHeader.identityEventCount = identityEventCount;
	tickHeader.configstringEventCount = configstringEventCount;
	tickHeader.playerStateCount = changedClientCount;
	tickHeader.entityCount = changedEntCount;
	WTV_FeedCompressor( cs, (const byte *)&tickHeader, (int)sizeof( tickHeader ) );

	for ( i = 0; i < commandCount; i++ ) {
		int cmdLen = (int)strlen( cmdText[i] );
		WTV_FeedCompressor( cs, (const byte *)&cmdLen, (int)sizeof( cmdLen ) );
		WTV_FeedCompressor( cs, (const byte *)cmdText[i], cmdLen );
	}

	for ( i = 0; i < identityEventCount; i++ ) {
		WTV_FeedCompressor( cs, (const byte *)&identityEvents[i], (int)sizeof( wtvIdentityEvent_t ) );
	}

	for ( i = 0; i < configstringEventCount; i++ ) {
		int csLen = (int)strlen( configstringEvents[i].value );
		WTV_FeedCompressor( cs, (const byte *)&configstringEvents[i].index, (int)sizeof( int ) );
		WTV_FeedCompressor( cs, (const byte *)&csLen, (int)sizeof( csLen ) );
		WTV_FeedCompressor( cs, (const byte *)configstringEvents[i].value, csLen );
	}

	for ( i = 0; i < changedClientCount; i++ ) {
		int clientNum = changedClientNum[i];
		WTV_FeedCompressor( cs, (const byte *)&clientNum, (int)sizeof( clientNum ) );
		WTV_FeedCompressor( cs, (const byte *)&decodeState->lastWrittenPS[clientNum], (int)sizeof( playerState_t ) );
	}
	for ( i = 0; i < changedEntCount; i++ ) {
		int entNum = changedEntNum[i];
		WTV_FeedCompressor( cs, (const byte *)&entNum, (int)sizeof( entNum ) );
		WTV_FeedCompressor( cs, (const byte *)&decodeState->lastWrittenEntity[entNum], (int)sizeof( entityState_t ) );
	}

	if ( tickStartOffset >= 0 ) {
		cs->indexEntries[cs->indexEntryCount].byteOffset = tickStartOffset;
		cs->indexEntries[cs->indexEntryCount].serverTime = serverTime;
		cs->indexEntryCount++;
	}

	// This tick's data (and its index entry, if any) is now fully written —
	// the only point where a fragment rollover can safely happen without
	// splitting a tick or misattributing tickStartOffset to the wrong fragment.
	if ( cs->rolloverPending && !cs->failed ) {
		int nextPart = cs->partNumber + 1;
		cs->rolloverPending = qfalse;
		WTV_CloseFinalFragment( cs, qtrue );
		if ( !WTV_OpenFinalFragment( cs, nextPart ) ) {
			cs->failed = qtrue;
		}
	}
}

// mapBaselines: heap array of MAX_GENTITIES entityState_t, ownership passed
// in — this function frees it on every return path (see WTV_RecordStop).
// discordScoreboard: heap string (or NULL), same ownership rule.
void WTV_CompressRound( const char *tempFilePath, const char *finalBasePath, entityState_t *mapBaselines, char *discordScoreboard ) {
	FILE *tempFile;
	wtvDecodeState_t *decodeState;
	wtvCompressState_t cs;
	byte scratchBuf[WTV_MAX_TICK_MSGLEN];
	msg_t msg;

	tempFile = fopen( tempFilePath, "rb" );
	if ( !tempFile ) {
		Com_Printf( "WTV: WTV_CompressRound: couldn't open %s\n", tempFilePath );
		free( mapBaselines );
		free( discordScoreboard );
		return;
	}

	// Plain malloc (not Z_Malloc, not main-thread-only) and heap, not stack —
	// ~600KB is too large to risk on a background thread's stack.
	decodeState = malloc( sizeof( *decodeState ) );
	if ( !decodeState ) {
		Com_Printf( "WTV: WTV_CompressRound: out of memory\n" );
		fclose( tempFile );
		free( mapBaselines );
		free( discordScoreboard );
		return;
	}
	Com_Memset( decodeState, 0, sizeof( *decodeState ) );

	Com_Memset( &cs, 0, sizeof( cs ) );
	Q_strncpyz( cs.finalBasePath, finalBasePath, sizeof( cs.finalBasePath ) );
	cs.mapBaselines = mapBaselines;

	if ( !WTV_OpenFinalFragment( &cs, 1 ) ) {
		free( decodeState );
		fclose( tempFile );
		free( mapBaselines );
		free( discordScoreboard );
		return;
	}

	// (skip past the temp file's wtvHeader_t — already validated its magic/version if desired)
	{
		wtvHeader_t header;
		fread( &header, sizeof( header ), 1, tempFile );
	}

	while ( WTV_ReadOneTick( tempFile, scratchBuf, sizeof( scratchBuf ), &msg ) ) {
		WTV_DecodeAndWriteTick( &msg, decodeState, &cs );
	}

	// Never remove the temp file until the final fragment is confirmed closed
	// and its compression didn't fail — a crash or failure mid-compression
	// must leave the recoverable temp file behind, not lose everything.
	WTV_CloseFinalFragment( &cs, qfalse );
	fclose( tempFile );
	free( decodeState );
	free( mapBaselines );

	if ( !cs.failed ) {
		remove( tempFilePath );
		WTV_DiscordUploadRound( finalBasePath, cs.partNumber, discordScoreboard );
	} else {
		Com_Printf( "WTV: compression failed for %s — leaving %s in place for recovery\n", finalBasePath, tempFilePath );
	}
	free( discordScoreboard );
}
