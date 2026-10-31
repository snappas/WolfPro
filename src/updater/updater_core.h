/*
===========================================================================

Return to Castle Wolfenstein multiplayer GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Return to Castle Wolfenstein multiplayer GPL Source Code (RTCW MP Source Code).

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

===========================================================================
*/

#ifndef UPDATER_CORE_H
#define UPDATER_CORE_H

// File operations for wolfpro_updater: Win32 + libc only, no engine headers.
// All paths are UTF-8 and at most UPDATER_MAX_PATH-1 bytes.

#define UPDATER_MAX_PATH 1024
#define UPDATER_MAX_TAG 64
#define UPDATER_MAX_ARGS 64 /* extra relaunch arguments */
#define UPDATER_MAX_ARG_LEN 512
#define UPDATER_MAX_CMDLINE 4096 /* relaunch command line, exe included */
#define UPDATER_MUTEX_NAME L"Local\\WolfProUpdater" /* held while the updater runs */

typedef struct {
	int pid; /* process to wait for, 0 = none */
	char install[UPDATER_MAX_PATH]; /* install root */
	char staging[UPDATER_MAX_PATH]; /* extracted update tree */
	char tag[UPDATER_MAX_TAG]; /* new version tag */
	char from[UPDATER_MAX_TAG]; /* version being replaced, "" if unknown */
	char relaunch[UPDATER_MAX_PATH]; /* exe to start afterwards, "" = none */
	int numArgs;
	char args[UPDATER_MAX_ARGS][UPDATER_MAX_ARG_LEN]; /* arguments after the exe */
} updaterPending_t;

/* All return 1 on success, 0 on failure (err receives the reason). */
int UpdaterCore_ReadPending(const char *path, updaterPending_t *out);
/* Backs up every file it overwrites to update/backup.new, then swaps the staging
   tree into the install; update/backup is replaced only once that succeeds. */
int UpdaterCore_Apply(const updaterPending_t *p, char *err, int errSize);
int UpdaterCore_Rollback(const char *installDir, char *err, int errSize);
int UpdaterCore_WriteResult(const char *installDir, const char *status, const char *tag, const char *err);
/* Reads one "key value" line from update/backup/manifest.txt. */
int UpdaterCore_ReadManifestValue(const char *installDir, const char *key, char *out, int outSize);

#endif
