#ifndef CL_WTVDEMO_H
#define CL_WTVDEMO_H

// Loads <filename>.wtv plus any .partN.wtv continuations into one retained
// heap buffer. Returns qfalse only if zero bytes decoded; a later fragment
// failing still returns qtrue with whatever decoded so far.
qboolean CL_WTV_LoadRound( const char *filename, byte **outBuffer, int *outSize );

// Peeks the first tick's configstrings so the caller can seed cl.gameState
// and call CL_InitCGame() before CL_WTV_BuildNDPStream (which crashes if
// cgame isn't up yet). Returns qfalse if the first tick can't be read.
qboolean CL_WTV_PeekFirstTickConfigStrings( const byte *buffer, int bufferSize, char *outConfigStrings, int outConfigStringsSize, int outConfigStringOffsets[MAX_CONFIGSTRINGS], int *outNumConfigStringBytes );

// Builds demo.buffer/demo.indices[] for followClientNum from buffer, same
// shape classic demo parsing produces, so the existing NDP/cgame machinery
// plays it with zero changes. followClientNum must already be validated.
qboolean CL_WTV_BuildNDPStream( const byte *buffer, int bufferSize, int followClientNum );

// Console command: playwtv <filename> <clientNum> — loads <fs_game>/wtvdemos/
// <filename>.wtv (plus any continuations), bootstraps cl.gameState/clc/cgame,
// builds the NDP stream, and starts playback following clientNum.
void CL_WTV_PlayDemo_f( void );

// Console command: wtvfollow <clientNum|next|prev> — switches which player
// the retained recording follows, preserving current playback position.
void CL_WTV_Follow_f( void );

// Console command: wtvplayers — lists every clientNum with a resolved
// identity in the currently retained recording.
void CL_WTV_Players_f( void );

#endif // CL_WTVDEMO_H
