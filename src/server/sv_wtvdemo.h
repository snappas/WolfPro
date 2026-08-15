#ifndef SV_WTVDEMO_H
#define SV_WTVDEMO_H

#define WTV_MAGIC              0x56545744u   // 'W','T','V','D' packed little-endian
#define WTV_VERSION            3   // bumped: tick format gained identity-event and configstring-event blocks
#define WTV_FULL_SNAPSHOT_INTERVAL_MS   8000 // mirrors NDP's FULL_SNAPSHOT_INTERVAL_MS

// Worst case: every client + every entity, uncompressed, on a full-snapshot
// tick — generous headroom over MAX_MSGLEN (one client's PVS-culled snapshot).
#define WTV_MAX_TICK_MSGLEN (256 * 1024)

// Cap on real *compressed* output per final .wtv fragment (WTV_FeedCompressor).
// Decimal MB with ~1MB headroom for deferred-rollover/index-trailer overshoot.
#define WTV_FRAGMENT_MAX_BYTES (9 * 1000 * 1000)

// Native copy buffer for the Discord scoreboard text; must match
// WTV_DISCORD_SCOREBOARD_MAX in g_stats.c (Discord's 2000-char content limit).
#define WTV_DISCORD_SCOREBOARD_MAX 1990

// Max reliable/broadcast commands queued in a single tick — wire-format-
// relevant, sizes fixed arrays in both the recorder and CL_WTV_ReadTick.
#define WTV_MAX_QUEUED_COMMANDS 64

typedef struct {
	unsigned int magic;
	int version;
	char mapname[64];
} wtvHeader_t;

// Full-snapshot index entry. byteOffset is a position in the DECOMPRESSED
// byte-aligned stream (WTV_DecodeAndWriteTick), not an on-disk offset.
typedef struct {
	int byteOffset;
	int serverTime;
} wtvIndexEntry_t;

#define WTV_MAX_INDEX_ENTRIES 512

// On-disk header for a final, xz-compressed .wtv/.partN.wtv file — separate
// from wtvHeader_t (the temp file's minimal header, no fragments/index).
typedef struct {
	unsigned int magic;
	int version;
	int partNumber;
	int hasNextPart;
	char mapname[64];
	int indexOffset; // byte offset, within the DECOMPRESSED stream, of the index trailer
	int indexCount;
} wtvFinalHeader_t;

// Byte-aligned tick format fed to the xz compressor (wtvIndexEntry_t.byteOffset
// points into this stream) — see WTV_DecodeAndWriteTick for the exact field layout.
typedef struct {
	int serverTime;
	int commandCount;
	int identityEventCount;
	int configstringEventCount;
	int playerStateCount;
	int entityCount;
} wtvIntermediateTickHeader_t;

// Called when a round transitions into GS_PLAYING. roundNum is the game-side
// round counter (g_currentRound) — the engine has no notion of "round" itself.
void WTV_RecordStart( int roundNum );

// Called either 3 seconds after intermission begins (aborted == 0) or when a
// round is ended abnormally via /ref resetmatch or team swap (aborted == 1).
void WTV_RecordStop( int aborted );

// Called once per server frame from SV_Frame, after svs.currFrame is rebuilt.
// No-ops unless a recording is active and the sv_fps-paced tick gate allows it.
void WTV_RecordTick( void );

// Called from SV_SendServerCommand for broadcasts (cl == NULL) while a WTV
// recording is active — queues the string for the next recorded tick.
void WTV_QueueBroadcastCommand( const char *cmd );

// Lets the game module avoid calling WTV_RecordStart again for a round
// that already has one in progress, without its own tracking state.
qboolean WTV_IsRecording( void );

// Wire-format contract with the client playback reader. name uses
// MAX_NAME_LENGTH, not MAX_NETNAME (QVM-only header) — small truncation risk.
typedef struct {
	int clientNum;
	char guid[GUID_LEN];
	char name[MAX_NAME_LENGTH];
} wtvIdentityEvent_t;

// Called from ClientUserinfoChanged when a client's identity is established/
// changes. clientNum is a slot index — lets playback detect it being reused.
void WTV_RecordPlayerIdentity( int clientNum, const char *guid, const char *name );

// Called directly from SV_SetConfigstring (native-to-native, no syscall
// needed) whenever a configstring changes while a WTV recording is active.
void WTV_RecordConfigstringChange( int index, const char *value );

// Stores the scoreboard text the game module builds at intermission
// (WTV_BuildDiscordScoreboardText) for the Discord upload; no-op if inactive.
void WTV_SetDiscordScoreboard( const char *text );

// Decodes ".wtvtmp" into "<finalBasePath>.wtv" (+ ".partN.wtv" rollovers).
// mapBaselines and discordScoreboard (may be NULL) are heap snapshots this frees.
void WTV_CompressRound( const char *tempFilePath, const char *finalBasePath, entityState_t *mapBaselines, char *discordScoreboard );

// Builds fragment partNumber's path (".wtv" for part 1, ".partN.wtv" after).
// Shared by WTV_OpenFinalFragment and WTV_DiscordUploadRound.
void WTV_BuildFragmentPath( const char *finalBasePath, int partNumber, char *out, int outSize );

#endif // SV_WTVDEMO_H
