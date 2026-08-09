#include "client.h"
#include "cl_wtvdemo.h"
#include "../server/sv_wtvdemo.h"
#include <lzma.h>

// Streams one fragment file through liblzma into *buffer (realloc-grown,
// matching cl_demo.c's memoryBuffer_t). Returns qfalse on failure.
static qboolean CL_WTV_DecompressFragment( const char *filePath, int expectedPartNumber, byte **buffer, int *bufferSize, int *bufferCapacity ) {
	FILE *file;
	lzma_stream strm = LZMA_STREAM_INIT;
	byte inBuf[65536];
	byte outBuf[65536];
	lzma_ret ret;
	wtvFinalHeader_t header;

	file = fopen( filePath, "rb" );
	if ( !file ) {
		return qfalse;
	}

	if ( fread( &header, sizeof( header ), 1, file ) != 1 ||
		header.magic != WTV_MAGIC || header.version != WTV_VERSION ||
		header.partNumber != expectedPartNumber ) {
		Com_Printf( "WTV: %s has a bad, missing, or mismatched-part header — stopping\n", filePath );
		fclose( file );
		return qfalse;
	}

	if ( lzma_stream_decoder( &strm, UINT64_MAX, 0 ) != LZMA_OK ) {
		Com_Printf( "WTV: lzma_stream_decoder failed\n" );
		fclose( file );
		return qfalse;
	}

	// Once set to LZMA_FINISH (on a short read), must never revert to
	// LZMA_RUN — liblzma requires the action to stay consistent for the
	// rest of the stream, or lzma_code() rejects it with LZMA_PROG_ERROR.
	lzma_action action = LZMA_RUN;
	strm.avail_in = 0;
	do {
		if ( strm.avail_in == 0 ) {
			size_t bytesRead = fread( inBuf, 1, sizeof( inBuf ), file );
			strm.next_in = inBuf;
			strm.avail_in = bytesRead;
			if ( bytesRead < sizeof( inBuf ) ) {
				action = LZMA_FINISH; // short read: real EOF or a read error either way
			}
		}

		strm.next_out = outBuf;
		strm.avail_out = sizeof( outBuf );
		ret = lzma_code( &strm, action );

		{
			int producedThisCall = (int)( sizeof( outBuf ) - strm.avail_out );
			if ( producedThisCall > 0 ) {
				if ( *bufferSize + producedThisCall > *bufferCapacity ) {
					int newCapacity;
					byte *newBuffer;

					// Checked BEFORE doubling: signed overflow on *= 2 is UB,
					// so bail before the multiply, not after.
					if ( *bufferCapacity > 0 ) {
						if ( *bufferCapacity > INT_MAX / 2 ) {
							Com_Printf( "WTV: out of memory decompressing %s\n", filePath );
							lzma_end( &strm );
							fclose( file );
							return qfalse;
						}
						newCapacity = *bufferCapacity * 2;
					} else {
						newCapacity = ( 1 << 20 );
					}
					while ( newCapacity < *bufferSize + producedThisCall ) {
						if ( newCapacity > INT_MAX / 2 ) {
							Com_Printf( "WTV: out of memory decompressing %s\n", filePath );
							lzma_end( &strm );
							fclose( file );
							return qfalse;
						}
						newCapacity *= 2;
					}
					newBuffer = (byte *)realloc( *buffer, newCapacity );
					if ( !newBuffer ) {
						Com_Printf( "WTV: out of memory decompressing %s\n", filePath );
						lzma_end( &strm );
						fclose( file );
						return qfalse;
					}
					*buffer = newBuffer;
					*bufferCapacity = newCapacity;
				}
				Com_Memcpy( *buffer + *bufferSize, outBuf, producedThisCall );
				*bufferSize += producedThisCall;
			}
		}

		if ( ret == LZMA_STREAM_END ) {
			break;
		}
		if ( ret != LZMA_OK ) {
			Com_Printf( "WTV: lzma_code error %d decompressing %s — stopping at last good point\n", (int)ret, filePath );
			break;
		}
	} while ( qtrue );

	lzma_end( &strm );
	fclose( file );
	return qtrue;
}

qboolean CL_WTV_LoadRound( const char *filename, byte **outBuffer, int *outSize ) {
	byte *buffer = NULL;
	int bufferSize = 0;
	int bufferCapacity = 0;
	char filePath[MAX_OSPATH + 16];
	int partNumber = 1;

	for ( ;; ) {
		qboolean ok;
		int fragmentStart = bufferSize;

		if ( partNumber == 1 ) {
			Com_sprintf( filePath, sizeof( filePath ), "%s.wtv", filename );
		} else {
			Com_sprintf( filePath, sizeof( filePath ), "%s.part%i.wtv", filename, partNumber );
		}

		ok = CL_WTV_DecompressFragment( filePath, partNumber, &buffer, &bufferSize, &bufferCapacity );
		if ( !ok ) {
			// Missing/corrupt continuation is treated the same as reaching the
			// natural end of the recording — but a missing FIRST fragment means
			// there's nothing to play at all.
			if ( partNumber == 1 ) {
				Com_Printf( "WTV: couldn't load %s\n", filePath );
				free( buffer ); // in case a partial decompress grew it before failing (e.g. OOM mid-fragment)
				return qfalse;
			}
			break;
		}

		{
			wtvFinalHeader_t header;
			FILE *f = fopen( filePath, "rb" );
			int hasNextPart = 0;
			if ( f ) {
				if ( fread( &header, sizeof( header ), 1, f ) == 1 ) {
					hasNextPart = header.hasNextPart;
					// Strip the trailing index block (indexOffset==0 means
					// interrupted-before-patched; keep everything then).
					if ( header.indexOffset > 0 && header.indexOffset <= bufferSize - fragmentStart ) {
						bufferSize = fragmentStart + header.indexOffset;
					}
				}
				fclose( f );
			}
			if ( !hasNextPart ) {
				break;
			}
		}
		partNumber++;
	}

	if ( bufferSize <= 0 ) {
		free( buffer );
		return qfalse;
	}

	*outBuffer = buffer;
	*outSize = bufferSize;
	return qtrue;
}

// Client-side "current known state of every player/entity" table. Caller
// must set numConfigStringBytes to 1 and configStrings[0] to '\0' first.
typedef struct {
	playerState_t lastPS[MAX_CLIENTS];
	qboolean hasPS[MAX_CLIENTS];
	entityState_t lastEntity[MAX_GENTITIES];
	qboolean hasEntity[MAX_GENTITIES];

	char clientGuid[MAX_CLIENTS][GUID_LEN];
	char clientName[MAX_CLIENTS][MAX_NAME_LENGTH];
	qboolean hasIdentity[MAX_CLIENTS];

	char configStrings[MAX_GAMESTATE_CHARS];
	int configStringOffsets[MAX_CONFIGSTRINGS];
	int numConfigStringBytes;
} wtvPlaybackState_t;

// Ports cl_demo.c's SaveConfigString compaction algorithm (append when
// there's room, else rebuild the whole buffer) onto wtvPlaybackState_t.
static void CL_WTV_SaveConfigString( wtvPlaybackState_t *state, int index, const char *string ) {
	int numStringBytes;

	if ( string[0] == '\0' ) {
		state->configStringOffsets[index] = 0;
		return;
	}

	numStringBytes = (int)strlen( string ) + 1;
	if ( state->numConfigStringBytes + numStringBytes <= (int)sizeof( state->configStrings ) ) {
		// we have enough space
		Com_Memcpy( state->configStrings + state->numConfigStringBytes, string, numStringBytes );
		state->configStringOffsets[index] = state->numConfigStringBytes;
		state->numConfigStringBytes += numStringBytes;
		return;
	}

	// we ran out of memory, it's time to compact the buffer
	// we first create a new one locally then copy it into the state table
	{
		char cs[sizeof( state->configStrings )];
		int numTotalBytes = 1;
		int i;
		cs[0] = '\0';
		for ( i = 0; i < ARRAY_LEN( state->configStringOffsets ); ++i ) {
			const char *s = ( i == index ) ? string : state->configStrings + state->configStringOffsets[i];
			if ( s[0] == '\0' ) {
				state->configStringOffsets[i] = 0;
				continue;
			}

			numStringBytes = (int)strlen( s ) + 1;
			if ( numTotalBytes + numStringBytes > (int)sizeof( state->configStrings ) ) {
				Com_Printf( "WTV: ran out of config string memory\n" );
				break;
			}

			Com_Memcpy( cs + numTotalBytes, s, numStringBytes );
			state->configStringOffsets[i] = numTotalBytes;
			numTotalBytes += numStringBytes;
		}
		Com_Memcpy( state->configStrings, cs, numTotalBytes );
		state->numConfigStringBytes = numTotalBytes;
	}
}

// Reads one tick's records starting at *readPos, applying changes to state.
// Returns qfalse at a clean end or a record whose size would run past
// bufferSize (corrupt/truncated — stop, keep whatever was already applied).
static qboolean CL_WTV_ReadTick( const byte *buffer, int bufferSize, int *readPos, wtvPlaybackState_t *state, int *outServerTime, int *outIdentityCount, int identityClientNums[MAX_CLIENTS], int *outCommandCount, char commandText[WTV_MAX_QUEUED_COMMANDS][MAX_STRING_CHARS], int *outChangedConfigstringCount, int changedConfigstringIndices[MAX_CONFIGSTRINGS] ) {
	wtvIntermediateTickHeader_t header;
	int i;

	if ( *readPos + (int)sizeof( header ) > bufferSize ) {
		return qfalse;
	}
	Com_Memcpy( &header, buffer + *readPos, sizeof( header ) );
	*readPos += (int)sizeof( header );

	*outServerTime = header.serverTime;
	*outIdentityCount = 0;

	// Commands are captured (not just skipped) so the caller can feed them
	// into SaveCommandString, since reliable commands (chat/obituaries/etc.)
	// must be replayed during playback, not just skipped.
	*outCommandCount = 0;
	for ( i = 0; i < header.commandCount; i++ ) {
		int len;
		if ( *readPos + (int)sizeof( len ) > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( &len, buffer + *readPos, sizeof( len ) );
		*readPos += (int)sizeof( len );
		// bufferSize - *readPos can't underflow (readPos <= bufferSize already
		// guaranteed); *readPos + len could overflow for a corrupt len instead.
		if ( len < 0 || len > bufferSize - *readPos ) {
			return qfalse;
		}
		if ( *outCommandCount < WTV_MAX_QUEUED_COMMANDS && len < MAX_STRING_CHARS ) {
			Com_Memcpy( commandText[*outCommandCount], buffer + *readPos, len );
			commandText[*outCommandCount][len] = '\0';
			(*outCommandCount)++;
		}
		// else: overflow/oversized command — still consumed to keep the
		// bit-stream position correct, just not captured.
		*readPos += len;
	}

	for ( i = 0; i < header.identityEventCount; i++ ) {
		wtvIdentityEvent_t event;
		if ( *readPos + (int)sizeof( event ) > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( &event, buffer + *readPos, sizeof( event ) );
		*readPos += (int)sizeof( event );

		if ( event.clientNum >= 0 && event.clientNum < MAX_CLIENTS ) {
			Q_strncpyz( state->clientGuid[event.clientNum], event.guid, sizeof( state->clientGuid[0] ) );
			Q_strncpyz( state->clientName[event.clientNum], event.name, sizeof( state->clientName[0] ) );
			state->hasIdentity[event.clientNum] = qtrue;
			if ( *outIdentityCount < MAX_CLIENTS ) {
				identityClientNums[(*outIdentityCount)++] = event.clientNum;
			}
		}
	}

	*outChangedConfigstringCount = 0;
	for ( i = 0; i < header.configstringEventCount; i++ ) {
		int csIndex;
		int len;
		char csValue[MAX_STRING_CHARS];

		if ( *readPos + (int)sizeof( csIndex ) + (int)sizeof( len ) > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( &csIndex, buffer + *readPos, sizeof( csIndex ) );
		*readPos += (int)sizeof( csIndex );
		Com_Memcpy( &len, buffer + *readPos, sizeof( len ) );
		*readPos += (int)sizeof( len );
		if ( len < 0 || len >= MAX_STRING_CHARS || *readPos + len > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( csValue, buffer + *readPos, len );
		csValue[len] = '\0';
		*readPos += len;

		if ( csIndex >= 0 && csIndex < MAX_CONFIGSTRINGS ) {
			CL_WTV_SaveConfigString( state, csIndex, csValue );
			if ( *outChangedConfigstringCount < MAX_CONFIGSTRINGS ) {
				changedConfigstringIndices[(*outChangedConfigstringCount)++] = csIndex;
			}
		}
	}

	for ( i = 0; i < header.playerStateCount; i++ ) {
		int clientNum;
		playerState_t ps;
		if ( *readPos + (int)sizeof( clientNum ) + (int)sizeof( ps ) > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( &clientNum, buffer + *readPos, sizeof( clientNum ) );
		*readPos += (int)sizeof( clientNum );
		Com_Memcpy( &ps, buffer + *readPos, sizeof( ps ) );
		*readPos += (int)sizeof( ps );

		if ( clientNum >= 0 && clientNum < MAX_CLIENTS ) {
			state->lastPS[clientNum] = ps;
			state->hasPS[clientNum] = qtrue;
		}
	}

	for ( i = 0; i < header.entityCount; i++ ) {
		int entNum;
		entityState_t es;
		if ( *readPos + (int)sizeof( entNum ) + (int)sizeof( es ) > bufferSize ) {
			return qfalse;
		}
		Com_Memcpy( &entNum, buffer + *readPos, sizeof( entNum ) );
		*readPos += (int)sizeof( entNum );
		Com_Memcpy( &es, buffer + *readPos, sizeof( es ) );
		*readPos += (int)sizeof( es );

		if ( entNum >= 0 && entNum < MAX_GENTITIES ) {
			if ( es.number == MAX_GENTITIES - 1 ) {
				// Removal sentinel — entity no longer exists, stop drawing it.
				state->hasEntity[entNum] = qfalse;
			} else {
				state->lastEntity[entNum] = es;
				state->hasEntity[entNum] = qtrue;
			}
		}
	}

	return qtrue;
}

// Peeks the first tick's identity/configstrings via a throwaway state, so
// the caller can init cgame before CL_WTV_BuildNDPStream's per-tick
// FinalizeNDPSnapshot calls into cgame (which crashes if not init'd yet).
qboolean CL_WTV_PeekFirstTickConfigStrings( const byte *buffer, int bufferSize, char *outConfigStrings, int outConfigStringsSize, int outConfigStringOffsets[MAX_CONFIGSTRINGS], int *outNumConfigStringBytes ) {
	wtvPlaybackState_t *state;
	int readPos;
	int serverTime, identityCount, commandCount, changedConfigstringCount;
	int identityClientNums[MAX_CLIENTS];
	int changedConfigstringIndices[MAX_CONFIGSTRINGS];
	char commandText[WTV_MAX_QUEUED_COMMANDS][MAX_STRING_CHARS];
	qboolean ok;

	state = (wtvPlaybackState_t *)calloc( 1, sizeof( *state ) );
	if ( !state ) {
		return qfalse;
	}
	state->configStrings[0] = '\0';
	state->numConfigStringBytes = 1;

	readPos = 0;
	ok = CL_WTV_ReadTick( buffer, bufferSize, &readPos, state, &serverTime, &identityCount, identityClientNums, &commandCount, commandText, &changedConfigstringCount, changedConfigstringIndices );
	if ( ok ) {
		Com_Memcpy( outConfigStrings, state->configStrings, outConfigStringsSize );
		Com_Memcpy( outConfigStringOffsets, state->configStringOffsets, sizeof( state->configStringOffsets ) );
		*outNumConfigStringBytes = state->numConfigStringBytes;
	}
	free( state );
	return ok;
}

// Zero-valued player state, kept local rather than reaching into cl_demo.c's
// file-static nullPlayerState.
static const playerState_t wtvNullPlayerState = {0};

// Tracks the followed clientNum's guid across ticks, purely so a change can
// be detected and warned about — nothing outside this file needs to query
// it, so this stays file-static rather than exposed through cl_wtvdemo.h.
static char wtvFollowedGuid[GUID_LEN];
static qboolean wtvHasFollowedGuid;

// Every clientNum seen with a resolved identity by the most recent
// CL_WTV_BuildNDPStream call — refreshed on every rebuild (initial load and
// every follow-target switch), so /wtvplayers and /wtvfollow next|prev
// always reflect the currently retained recording without a separate scan.
static char wtvKnownClientGuid[MAX_CLIENTS][GUID_LEN];
static char wtvKnownClientName[MAX_CLIENTS][MAX_NAME_LENGTH];
static qboolean wtvKnownHasIdentity[MAX_CLIENTS];

qboolean CL_WTV_BuildNDPStream( const byte *buffer, int bufferSize, int followClientNum ) {
	wtvPlaybackState_t *state;
	int readPos;
	int nextFullSnapshotTime;
	qboolean isFirstTick;

	state = (wtvPlaybackState_t *)calloc( 1, sizeof( *state ) );
	if ( !state ) {
		Com_Printf( "WTV: out of memory building playback state\n" );
		return qfalse;
	}
	state->configStrings[0] = '\0';
	state->numConfigStringBytes = 1;

	ResetNDPPlaybackState();
	// the stream below re-runs cgame's analysis from scratch, so its
	// accumulators have to start empty or the previous target's kills,
	// pickups and round markers bleed into this one
	CL_CGNDP_ResetAnalysis();

	readPos = 0;
	nextFullSnapshotTime = 0;
	isFirstTick = qtrue;
	wtvHasFollowedGuid = qfalse;

	for ( ;; ) {
		int serverTime;
		int identityCount;
		int identityClientNums[MAX_CLIENTS];
		int commandCount;
		char commandText[WTV_MAX_QUEUED_COMMANDS][MAX_STRING_CHARS];
		int changedConfigstringCount;
		int changedConfigstringIndices[MAX_CONFIGSTRINGS];
		int byteOffset;
		qboolean isFullSnap;
		ndpSnapshot_t *currNDPSnap;
		int i;

		byteOffset = demo.buffer.position;
		if ( !CL_WTV_ReadTick( buffer, bufferSize, &readPos, state, &serverTime, &identityCount, identityClientNums, &commandCount, commandText, &changedConfigstringCount, changedConfigstringIndices ) ) {
			break;
		}

		for ( i = 0; i < identityCount; i++ ) {
			if ( identityClientNums[i] == followClientNum ) {
				if ( wtvHasFollowedGuid && strcmp( wtvFollowedGuid, state->clientGuid[followClientNum] ) != 0 ) {
					Com_Printf( "WTV: the player you're following (clientNum %d) has changed — was \"%s\", now \"%s\" (%s)\n",
						followClientNum, wtvFollowedGuid, state->clientGuid[followClientNum], state->clientName[followClientNum] );
				}
				Q_strncpyz( wtvFollowedGuid, state->clientGuid[followClientNum], sizeof( wtvFollowedGuid ) );
				wtvHasFollowedGuid = qtrue;
			}
		}

		isFullSnap = isFirstTick || ( serverTime > nextFullSnapshotTime );
		if ( isFullSnap ) {
			nextFullSnapshotTime = serverTime + WTV_FULL_SNAPSHOT_INTERVAL_MS;
		}
		isFirstTick = qfalse;

		currNDPSnap = parser.currSnap;
		// Reliable commands replay straight onto this snapshot (not
		// parser.currSnap again — FinalizeNDPSnapshot below reassigns it).
		for ( i = 0; i < commandCount; i++ ) {
			int prefixLen = 0;
			const char *finalCommand = commandText[i];

			if ( !strncmp( commandText[i], "wtvscores ", 10 ) ) {
				prefixLen = 10;
			} else if ( !strncmp( commandText[i], "wtvtinfo ", 9 ) ) {
				prefixLen = 9;
			}

			if ( prefixLen ) {
				int cmdTeam = atoi( commandText[i] + prefixLen );
				int rawTeam = state->hasPS[followClientNum] ? state->lastPS[followClientNum].persistant[PERS_TEAM] : -999;
				int followedTeam = state->hasPS[followClientNum] ? state->lastPS[followClientNum].persistant[PERS_TEAM] : TEAM_RED;
				const char *payload;

				// No omniscient/spectator variant — default to TEAM_RED's view.
				if ( followedTeam != TEAM_RED && followedTeam != TEAM_BLUE ) {
					followedTeam = TEAM_RED;
				}
				if ( cmdTeam != followedTeam ) {
					continue; // drop the non-matching variant entirely
				}
				payload = strchr( commandText[i] + prefixLen, ' ' );
				if ( !payload ) {
					continue;
				}
				finalCommand = payload + 1;
			}

			// Mirrors ParseServerCommand's tokenize-then-analyze order
			// (cl_demo.c) — cgame's command-driven timeline data (docs
			// held, round wins, etc.) reads CG_Argv, which reflects
			// whatever's currently tokenized.
			Cmd_TokenizeString( finalCommand );
			CL_CGNDP_AnalyzeCommand( serverTime );
			SaveCommandString( &currNDPSnap->serverCommands, finalCommand );
		}
		// Configstring changes replay immediately too, same reasoning as
		// commands above (else a change waits up to a full snapshot to show).
		for ( i = 0; i < changedConfigstringCount; i++ ) {
			int idx = changedConfigstringIndices[i];
			const char *csValue = state->configStrings + state->configStringOffsets[idx];
			// a single configstring can be as large as BIG_INFO_STRING (e.g.
			// CS_SYSTEMINFO) — matches client.h's bigConfigString sizing
			char csCommand[BIG_INFO_STRING + 32];

			Com_sprintf( csCommand, sizeof( csCommand ), "cs %d \"%s\"", idx, csValue );
			Cmd_TokenizeString( csCommand );
			CL_CGNDP_AnalyzeCommand( serverTime );
			SaveConfigString( currNDPSnap, idx, csValue, serverTime );
		}
		currNDPSnap->ps = state->hasPS[followClientNum] ? state->lastPS[followClientNum] : wtvNullPlayerState;
		// Mirrors SV_BuildClientSnapshot's own self-omission rule: cgame always
		// draws "my own body" from a synthetic playerstate-derived entity, so
		// including the followed player's real entity too would duplicate it.
		for ( i = 0; i < MAX_GENTITIES; i++ ) {
			if ( state->hasEntity[i] && i != followClientNum ) {
				currNDPSnap->entities[i] = state->lastEntity[i];

				currNDPSnap->entities[i].number = i;
			} else {
				currNDPSnap->entities[i].number = MAX_GENTITIES - 1;
			}
		}
		currNDPSnap->numAreaMaskBytes = 0;
		Com_Memset( currNDPSnap->areaMask, 0, sizeof( currNDPSnap->areaMask ) );

		if ( isFullSnap ) {
			// on a full snapshot, hand cgame every configstring currently known
			// (mirrors ParseGamestate's own full-dump-on-full-snapshot shape)
			for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
				if ( state->configStringOffsets[i] > 0 ) {
					SaveConfigString( currNDPSnap, i, state->configStrings + state->configStringOffsets[i], serverTime );
				}
			}
		}

		// Cast before the multiply — plain-int `100 * readPos` overflows past
		// ~21.4M. Capped at 99 since AnalyzeSnapshot asserts progress < 100.
		parser.progress = ( bufferSize > 0 ) ? (int)( ( (int64_t)readPos * 100 ) / bufferSize ) : 0;
		if ( parser.progress > 99 ) {
			parser.progress = 99;
		}

		// Stop cleanly — FinalizeNDPSnapshot's overflow check longjmps into
		// ndp.abortLoad, which this path never sets up (unlike classic playback).
		if ( isFullSnap && demo.numIndices >= ARRAY_LEN( demo.indices ) ) {
			Com_Printf( "WTV: recording has too many full snapshots to index — stopping playback build early\n" );
			break;
		}
		FinalizeNDPSnapshot( currNDPSnap, isFullSnap, serverTime, byteOffset );
	}

	if ( !wtvHasFollowedGuid && !state->hasPS[followClientNum] ) {
		Com_Printf( "WTV: clientNum %d never appeared as an active player in this recording — playing anyway with an empty player state\n", followClientNum );
	}

	Com_Memcpy( wtvKnownClientGuid, state->clientGuid, sizeof( wtvKnownClientGuid ) );
	Com_Memcpy( wtvKnownClientName, state->clientName, sizeof( wtvKnownClientName ) );
	Com_Memcpy( wtvKnownHasIdentity, state->hasIdentity, sizeof( wtvKnownHasIdentity ) );

	free( state );
	return qtrue;
}

static byte *wtvRetainedBuffer;
static int wtvRetainedBufferSize;
static int wtvRetainedClientNum;
static char wtvRetainedFilename[MAX_OSPATH * 2];

// Frees any previously retained round's decompressed buffer, called at the
// start of a new /playwtv. Not yet wired to disconnect cleanup.
static void CL_WTV_FreeRetainedBuffer( void ) {
	if ( wtvRetainedBuffer ) {
		free( wtvRetainedBuffer );
		wtvRetainedBuffer = NULL;
		wtvRetainedBufferSize = 0;
	}
}

// Shared by /wtvfollow <clientNum> and /wtvfollow next|prev: re-runs the
// per-clientNum projection against the retained buffer and restores playback
// position via the same session-cvar path a real video restart uses, without
// the cost of an actual renderer/cgame reload.
static void CL_WTV_SwitchFollowTarget( int newClientNum ) {
	int currentTime;
	float currentSpeed;

	if ( !wtvKnownHasIdentity[newClientNum] ) {
		Com_Printf( "WTV: clientNum %d never appeared as an active player in this recording — switching anyway\n", newClientNum );
	}

	// demo.currSnap->serverTime tracks live playback position every frame
	// (CL_NDP_ReadUntil advances it) — unlike cl.serverTime, which NDP
	// playback never updates. Captured before CL_WTV_BuildNDPStream wipes
	// `demo` via ResetNDPPlaybackState.
	currentTime = demo.currSnap->serverTime;
	currentSpeed = Cvar_VariableValue( "timescale" );

	if ( !CL_WTV_BuildNDPStream( wtvRetainedBuffer, wtvRetainedBufferSize, newClientNum ) ) {
		Com_Printf( "WTV: couldn't switch to clientNum %d\n", newClientNum );
		return;
	}
	wtvRetainedClientNum = newClientNum;

	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];
	MB_InitRead( &demo.buffer );
	demo.commands[0].command[0] = '\0';
	demo.commands[1].command[0] = '\0';
	demo.numCommands = 1;

	if ( demo.lastServerTime <= demo.firstServerTime ) {
		demo.lastServerTime = demo.firstServerTime + 1;
	}

	// Pre-seed the same cvar CG_Shutdown normally writes, then ask for a
	// video-restart-style analysis so cgame restores that position itself and
	// drives the engine-side seek — the only way to keep cgame's playback
	// time and the engine's in sync across a stream rebuild.
	Cvar_Set( "demo_SessionData", va( "%d %f", currentTime, currentSpeed ) );
	CL_CGNDP_EndAnalysis( wtvRetainedFilename, demo.firstServerTime, demo.lastServerTime, qtrue );

	Cvar_Set( "cg_wtvFreecam", "0" );
	Com_Printf( "WTV: now following clientNum %d (%s)\n", newClientNum,
		wtvKnownHasIdentity[newClientNum] ? wtvKnownClientName[newClientNum] : "unknown" );
}

void CL_WTV_Follow_f( void ) {
	const char *arg;
	int targetClientNum;

	if ( !wtvRetainedBuffer ) {
		Com_Printf( "wtvfollow: not currently playing a WTV recording\n" );
		return;
	}
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "wtvfollow <clientNum|next|prev>\n" );
		return;
	}

	arg = Cmd_Argv( 1 );
	if ( !Q_stricmp( arg, "next" ) || !Q_stricmp( arg, "prev" ) ) {
		int step = !Q_stricmp( arg, "next" ) ? 1 : -1;
		int i;

		targetClientNum = wtvRetainedClientNum;
		for ( i = 0; i < MAX_CLIENTS; i++ ) {
			targetClientNum = ( targetClientNum + step + MAX_CLIENTS ) % MAX_CLIENTS;
			if ( wtvKnownHasIdentity[targetClientNum] ) {
				break;
			}
		}
		if ( !wtvKnownHasIdentity[targetClientNum] ) {
			Com_Printf( "wtvfollow: no other active players found in this recording\n" );
			return;
		}
	} else {
		targetClientNum = atoi( arg );
		if ( targetClientNum < 0 || targetClientNum >= MAX_CLIENTS ) {
			Com_Printf( "wtvfollow: clientNum must be between 0 and %d\n", MAX_CLIENTS - 1 );
			return;
		}
	}

	CL_WTV_SwitchFollowTarget( targetClientNum );
}

void CL_WTV_Players_f( void ) {
	int i;
	qboolean any = qfalse;

	if ( !wtvRetainedBuffer ) {
		Com_Printf( "wtvplayers: not currently playing a WTV recording\n" );
		return;
	}

	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( wtvKnownHasIdentity[i] ) {
			Com_Printf( "%2d: %s%s\n", i, wtvKnownClientName[i], i == wtvRetainedClientNum ? " (following)" : "" );
			any = qtrue;
		}
	}
	if ( !any ) {
		Com_Printf( "wtvplayers: no identified players in this recording\n" );
	}
}

void CL_WTV_PlayDemo_f( void ) {
	char filename[MAX_OSPATH * 2];
	char hpath[MAX_OSPATH];
	char game[64];
	int clientNum;
	char firstTickConfigStrings[MAX_GAMESTATE_CHARS];
	int firstTickConfigStringOffsets[MAX_CONFIGSTRINGS];
	int firstTickNumConfigStringBytes;

	if ( Cmd_Argc() != 3 ) {
		Com_Printf( "playwtv <filename> <clientNum>\n" );
		return;
	}

	clientNum = atoi( Cmd_Argv( 2 ) );
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		Com_Printf( "WTV: clientNum must be between 0 and %d\n", MAX_CLIENTS - 1 );
		return;
	}

	// mirrors WTV_BuildBasePath's composition (sv_wtvdemo.c) — recordings live
	// under <fs_homepath>/<fs_game>/wtvdemos/, not the process's working
	// directory, so a bare relative path never finds a real recording.
	Cvar_VariableStringBuffer( "fs_homepath", hpath, sizeof( hpath ) );
	Cvar_VariableStringBuffer( "fs_game", game, sizeof( game ) );
	Com_sprintf( filename, sizeof( filename ), "%s/%s/wtvdemos/%s", hpath, game, Cmd_Argv( 1 ) );

	// make sure a local server is killed, mirroring CL_PlayDemo's own ordering
	Cvar_Set( "sv_killserver", "1" );

	// no stale state from a previous playback/connection carried into this one
	CL_Disconnect( qtrue );
	CL_WTV_FreeRetainedBuffer();

	if ( !CL_WTV_LoadRound( filename, &wtvRetainedBuffer, &wtvRetainedBufferSize ) ) {
		Com_Printf( "WTV: couldn't load anything from %s\n", filename );
		return;
	}
	wtvRetainedClientNum = clientNum;
	Q_strncpyz( wtvRetainedFilename, filename, sizeof( wtvRetainedFilename ) );
	// tells cgame this is a WTV recording, not a classic demo — follow
	// switching only makes sense here
	Cvar_Set( "cg_wtvActive", "1" );

	// Peek before touching cgame/clc/cls state — doubles as "is there
	// anything usable" check (a successful peek guarantees the real build
	// below processes at least one tick).
	if ( !CL_WTV_PeekFirstTickConfigStrings( wtvRetainedBuffer, wtvRetainedBufferSize, firstTickConfigStrings, sizeof( firstTickConfigStrings ), firstTickConfigStringOffsets, &firstTickNumConfigStringBytes ) ) {
		Com_Printf( "WTV: got nothing usable out of %s\n", filename );
		// wtvRetainedBuffer was already set by CL_WTV_LoadRound above; free it
		// here too so a corrupt/zero-tick recording doesn't leave /wtvplayers
		// or /wtvfollow acting on stale, non-NULL state.
		CL_WTV_FreeRetainedBuffer();
		return;
	}

	// Must run before CL_InitCGame() — its loading-screen render calls into
	// cgame, which dereferences demo.currSnap with no NULL guard otherwise.
	ResetNDPPlaybackState();

	// Bootstrap cl.gameState + clc.* + cgame, mirroring ParseGamestate's
	// tail — CL_InitCGame() must run before CL_WTV_BuildNDPStream, whose
	// per-tick FinalizeNDPSnapshot calls into cgame via VM_Call.
	clc.clientNum = clientNum;
	clc.checksumFeed = 0;
	clc.demoplaying = qtrue;
	clc.newDemoPlayer = qtrue;
	clc.serverMessageSequence = 0;
	clc.lastExecutedServerCommand = 0;
	// clc.demoName intentionally left unset — /vid_restart isn't supported
	// during WTV playback (no .dm_<protocol> file for it to reopen).
	Con_Close();
	CL_ClearState();
	cls.state = CA_LOADING;
	Com_EventLoop();
	Cvar_Set( "r_uiFullScreen", "0" );
	CL_FlushMemory();
	Com_Memcpy( cl.gameState.stringOffsets, firstTickConfigStringOffsets, sizeof( cl.gameState.stringOffsets ) );
	Com_Memcpy( cl.gameState.stringData, firstTickConfigStrings, firstTickNumConfigStringBytes );
	cl.gameState.dataCount = firstTickNumConfigStringBytes;

	CL_InitCGame();
	if ( !cls.cgameNewDemoPlayer ) {
		// Com_Error, not a plain return — cgame is already loaded and
		// cls.state is CA_LOADING, so a return would strand the loading screen.
		Com_Error( ERR_DROP, "WTV: this mod's cgame doesn't support the new demo player — can't play WTV recordings\n" );
	}
	cls.cgameStarted = qtrue;
	cls.state = CA_ACTIVE;

	// Safe now that cgame is up — re-reads the first tick from scratch
	// (harmless, same result) plus every tick after it.
	if ( !CL_WTV_BuildNDPStream( wtvRetainedBuffer, wtvRetainedBufferSize, clientNum ) ) {
		// Com_Error — cls.state is already CA_ACTIVE and demo.currSnap can
		// still be NULL here, so a plain return risks a NULL deref next frame.
		Com_Error( ERR_DROP, "WTV: couldn't build playback data for %s\n", filename );
	}

	// prepare to read from memory (mirrors CL_NDP_PlayDemo's own post-parse setup)
	demo.currSnap = &demo.snapshots[0];
	demo.nextSnap = &demo.snapshots[1];
	MB_InitRead( &demo.buffer );
	demo.commands[0].command[0] = '\0';
	demo.commands[1].command[0] = '\0';
	demo.numCommands = 1;

	// EndAnalysis asserts lastServerTime > firstServerTime; harmless 1ms pad
	// for the single-tick-recording edge case.
	if ( demo.lastServerTime <= demo.firstServerTime ) {
		demo.lastServerTime = demo.firstServerTime + 1;
	}
	CL_CGNDP_EndAnalysis( filename, demo.firstServerTime, demo.lastServerTime, qfalse );
	CL_NDP_Seek( demo.firstServerTime );

	// see CL_PlayDemo's matching exec pair for why this is here
	Cbuf_AddText( "exec demo_binds_default.cfg\n" );
	Cbuf_AddText( "exec demo_binds.cfg\n" );

	Com_Printf( "WTV: playing %s, following clientNum %d\n", filename, clientNum );
}
