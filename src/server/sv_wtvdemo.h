#ifndef SV_WTVDEMO_H
#define SV_WTVDEMO_H

#define WTV_MAGIC              0x56545744u   // 'W','T','V','D' packed little-endian
#define WTV_VERSION            3   // bumped: tick format gained identity-event and configstring-event blocks
#define WTV_FULL_SNAPSHOT_INTERVAL_MS   8000 // mirrors NDP's FULL_SNAPSHOT_INTERVAL_MS

// Sized for the worst case: every client + every entity, uncompressed, on a
// full-snapshot tick — deliberately generous headroom over MAX_MSGLEN
// (sized for one client's PVS-culled snapshot, not this aggregate).
#define WTV_MAX_TICK_MSGLEN (256 * 1024)

// Cap on real *compressed* output per final .wtv fragment, checked during the
// intermission compression pass (WTV_FeedCompressor) — not the live/raw byte
// count, which is no longer capped during recording itself.
#define WTV_FRAGMENT_MAX_BYTES (10 * 1024 * 1024)

// Max reliable/broadcast commands (chat/obituaries/etc.) queued in a single
// tick. A wire-format-relevant constant: both the live recorder and the
// client-side playback reader (CL_WTV_ReadTick) size fixed arrays against it.
#define WTV_MAX_QUEUED_COMMANDS 64

typedef struct {
	unsigned int magic;
	int version;
	char mapname[64];
} wtvHeader_t;

// Full-snapshot index entry for the final .wtv file. byteOffset is a position
// in the DECOMPRESSED byte-aligned stream (see WTV_DecodeAndWriteTick), not
// an on-disk compressed byte offset.
typedef struct {
	int byteOffset;
	int serverTime;
} wtvIndexEntry_t;

#define WTV_MAX_INDEX_ENTRIES 512

// On-disk header for a final, xz-compressed .wtv or .partN.wtv file.
// Deliberately separate from wtvHeader_t (the temp file's minimal header) —
// this format has fragments and an index, the temp file has neither.
typedef struct {
	unsigned int magic;
	int version;
	int partNumber;
	int hasNextPart;
	char mapname[64];
	int indexOffset; // byte offset, within the DECOMPRESSED stream, of the index trailer
	int indexCount;
} wtvFinalHeader_t;

// Byte-aligned tick format fed to the xz compressor, one block per tick
// (the DECOMPRESSED stream wtvIndexEntry_t.byteOffset points into):
//   wtvIntermediateTickHeader_t
//   commandCount     * { int length;    char text[length]; }  (no null terminator written)
//   identityEventCount     * wtvIdentityEvent_t
//   configstringEventCount * { int index; int length; char value[length]; }  (no null terminator written)
//   playerStateCount * { int clientNum;  playerState_t ps; }  (changed only, memcmp dedup)
//   entityCount      * { int entityNum;  entityState_t es; }  (changed only, memcmp dedup)
// Commands have no dedup baseline — every command is written every tick.
typedef struct {
	int serverTime;
	int commandCount;
	int identityEventCount;
	int configstringEventCount;
	int playerStateCount;
	int entityCount;
} wtvIntermediateTickHeader_t;

// Called when a round transitions into GS_PLAYING (see G_InitGame's GS_PLAYING
// block). roundNum is the game-side round counter (g_currentRound), passed
// through because the engine has no notion of "round" on its own.
void WTV_RecordStart( int roundNum );

// Called either 3 seconds after intermission begins (aborted == 0) or when a
// round is ended abnormally via /ref resetmatch or team swap (aborted == 1).
void WTV_RecordStop( int aborted );

// Called once per server frame from SV_Frame, after SV_SendClientMessages has
// (re)built svs.currFrame for this frame. No-ops unless a recording is active
// and the tick-rate gate (paced by live sv_fps) says this is a recorded tick.
void WTV_RecordTick( void );

// Called from SV_SendServerCommand whenever a command is broadcast to all
// clients (cl == NULL) while a WTV recording is active. Queues the already-
// formatted string for inclusion in the next recorded tick.
void WTV_QueueBroadcastCommand( const char *cmd );

// Lets the game module avoid calling WTV_RecordStart again for a round
// that already has one in progress, without its own tracking state.
qboolean WTV_IsRecording( void );

// Wire-format contract with the client playback reader, hence lives here.
// name uses MAX_NAME_LENGTH, not pers.netname's MAX_NETNAME (defined in a
// QVM-only header this file can't include) — a small accepted truncation risk.
typedef struct {
	int clientNum;
	char guid[GUID_LEN];
	char name[MAX_NAME_LENGTH];
} wtvIdentityEvent_t;

// Called from ClientUserinfoChanged whenever a client's identity is
// established/changes. clientNum is a slot index, not a stable identity —
// lets playback detect a followed slot being reused by someone else.
void WTV_RecordPlayerIdentity( int clientNum, const char *guid, const char *name );

// Called directly from SV_SetConfigstring (native-to-native, no syscall
// needed) whenever a configstring changes while a WTV recording is active.
void WTV_RecordConfigstringChange( int index, const char *value );

// Decodes ".wtvtmp" into "<finalBasePath>.wtv" (+ ".partN.wtv" rollovers),
// byte-aligned and xz-compressed. Callable from a background thread.
// mapBaselines is a private heap snapshot; this function frees it.
void WTV_CompressRound( const char *tempFilePath, const char *finalBasePath, entityState_t *mapBaselines );

#endif // SV_WTVDEMO_H
