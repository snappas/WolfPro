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

#ifndef CL_UPDATE_LOGIC_H
#define CL_UPDATE_LOGIC_H

// Pure update-decision helpers: libc only (jansson for parsing), no engine
// headers, so the standalone updater can compile the same file.

#define UPDATER_MAX_ASSETS 16

typedef struct {
	char name[128];
	char url[512];
	long long size;
	char sha256[65]; /* lowercase hex, "" if no digest */
} updaterAsset_t;

typedef struct {
	char tag[64];
	char commit[8]; /* 7-char short SHA, "" if unknown */
	long long publishedAt; /* UTC epoch seconds */
	int prerelease, draft;
	int numAssets;
	updaterAsset_t assets[UPDATER_MAX_ASSETS];
} updaterRelease_t;

typedef enum { UPDATER_CHANNEL_LOCAL,
			   UPDATER_CHANNEL_RELEASE,
			   UPDATER_CHANNEL_DEV } updaterChannel_t;

#ifndef UPDATER_NO_JSON
/* Returns 1 on success; notes (optional) receives the raw body. */
int Updater_ParseRelease(const char *json, updaterRelease_t *out, char *notes, int notesSize);
#endif

long long Updater_ParseIso8601(const char *s); /* "2026-10-18T12:34:56Z" -> 1792326896, -1 on error */
updaterChannel_t Updater_ChannelFromString(const char *s);
int Updater_ShouldOffer(const updaterRelease_t *latest, const char *installedCommit,
					updaterChannel_t channel, long long buildTime,
					const char *skippedTag, int force);
const updaterAsset_t *Updater_PickAsset(const updaterRelease_t *rel, int is64);
void Updater_SanitizeNotes(const char *in, char *out, int outSize);
int Updater_IsSafeZipPath(const char *path);
int Updater_BuildCommandLine(char *out, int outSize, const char *const *argv, int argc);

#endif
