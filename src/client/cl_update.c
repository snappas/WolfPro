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

// Client auto-update: checks GitHub for the latest release and drives the
// console/ui flow. Windows only; other platforms get a stub command.

#include "client.h"

#ifdef _WIN32

#include "cl_update_logic.h"
#include "../updater/updater_core.h"
#include "../qcommon/unzip.h"

#define UPDATER_MAX_BODY (1024 * 1024)
#define UPDATER_NOTES_SIZE 16384
#define UPDATER_PATH_SIZE UPDATER_MAX_PATH
#define UPDATER_ZIP_NAME 512
#define UPDATER_ZIP_CHUNK 65536
#define UPDATER_ZIP_FRAME_BUDGET (8 * 1024 * 1024) // bytes extracted per frame
#define UPDATER_RELAUNCH_CMD UPDATER_MAX_CMDLINE // updater's relaunch command-line buffer
#define UPDATER_EXE_NAME "wolfpro_updater.exe"

// Windows CA store; revocation checks that can't reach a CRL/OCSP server
// don't fail the transfer
#if CURL_AT_LEAST_VERSION(7, 71, 0)
#define UPDATER_SSL_OPTIONS (CURLSSLOPT_NATIVE_CA | CURLSSLOPT_REVOKE_BEST_EFFORT)
#elif CURL_AT_LEAST_VERSION(7, 70, 0)
#define UPDATER_SSL_OPTIONS CURLSSLOPT_REVOKE_BEST_EFFORT
#endif
#define UPDATER_ADMIN_MSG "This install folder needs administrator rights to update. Run WolfPro as administrator once, or install it outside Program Files."
#define UPDATER_INTERRUPTED_MSG "The last update was interrupted. Type /update rollback to restore the previous version."

typedef struct {
	updaterState_t state;
	qboolean initialized;

	// release check transfer, separate from map downloads
	CURLM *checkM;
	CURL *check;
	struct curl_slist *headers;
	char *body;
	size_t bodyLen;
	qboolean bodyOverflow;
	char curlError[CURL_ERROR_SIZE];
	qboolean forced;

	// last successful check
	qboolean haveRelease;
	updaterRelease_t release;
	char notes[UPDATER_NOTES_SIZE]; // raw release body

	char installDir[UPDATER_PATH_SIZE];

	// package download, on its own handles
	CURLM *dlM;
	CURL *dl;
	struct curl_slist *dlHeaders;
	FILE *dlFile;
	char dlError[CURL_ERROR_SIZE];
	qboolean dlWriteFailed, dlTooLarge;
	long long dlWritten, dlNow, dlTotal, dlReported;
	int dlTenth;
	updaterAsset_t asset;
	char tag[UPDATER_MAX_TAG];

	// <install>\update
	char baseDir[UPDATER_PATH_SIZE];
	char stagingDir[UPDATER_PATH_SIZE];
	char zipPath[UPDATER_PATH_SIZE];

	// incremental extraction into stagingDir
	unzFile zip;
	unsigned char *zipBuf;
	unsigned long zipIndex, zipCount;
	FILE *outFile;
	char outName[UPDATER_ZIP_NAME];
	unsigned long outExpected, outWritten;
} updaterCtx_t;

static updaterCtx_t updater;

static cvar_t *cl_autoUpdateCheck;
static cvar_t *cl_updateSkip;
static cvar_t *cl_updateRepo;

static void CL_Update_SetState(updaterState_t state) {
	updater.state = state;
	Cvar_SetIntegerValue("cl_updateState", (int)state);
}

static qboolean CL_Update_Busy(void) {
	return updater.state == UPDATER_STATE_DOWNLOADING || updater.state == UPDATER_STATE_STAGING || updater.state == UPDATER_STATE_LAUNCHING;
}

static void CL_Update_StopCheck(void) {
	if(updater.checkM) {
		if(updater.check) {
			qcurl_multi_remove_handle(updater.checkM, updater.check);
		}
		qcurl_multi_cleanup(updater.checkM);
		updater.checkM = NULL;
	}
	if(updater.check) {
		qcurl_easy_cleanup(updater.check);
		updater.check = NULL;
	}
	if(updater.headers) {
		curl_slist_free_all(updater.headers);
		updater.headers = NULL;
	}
	if(updater.body) {
		free(updater.body);
		updater.body = NULL;
	}
	updater.bodyLen = 0;
	updater.bodyOverflow = qfalse;
}

static void CL_Update_CheckFailed(const char *why) {
	CL_Update_StopCheck();
	CL_Update_SetState(UPDATER_STATE_CHECKFAILED);
	Cvar_Set("cl_updateError", why);
	Com_Printf("Update check failed: %s\n", why);
}

static size_t CL_Update_CheckWrite(void *data, size_t size, size_t nmemb, void *user) {
	size_t n = size * nmemb;

	if(updater.bodyLen + n > UPDATER_MAX_BODY) {
		updater.bodyOverflow = qtrue;
		return 0;
	}
	memcpy(updater.body + updater.bodyLen, data, n);
	updater.bodyLen += n;
	updater.body[updater.bodyLen] = '\0';
	return n;
}

static qboolean CL_Update_ValidRepo(const char *repo) {
	const char *p;
	int slashes = 0;

	if(!repo[0] || repo[0] == '/') {
		return qfalse;
	}
	for(p = repo; *p; p++) {
		if(*p == '/') {
			slashes++;
		} else if(!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') {
			return qfalse;
		}
	}
	return slashes == 1 && p[-1] != '/';
}

static void CL_Update_StartCheck(qboolean forced) {
	char url[256];

	if(updater.state == UPDATER_STATE_CHECKING) {
		updater.forced = updater.forced || forced;
		if(forced) {
			Com_Printf("Checking for updates...\n");
		}
		return;
	}
	if(!CL_Update_ValidRepo(cl_updateRepo->string)) {
		CL_Update_CheckFailed(va("invalid cl_updateRepo \"%s\"", cl_updateRepo->string));
		return;
	}
	Com_sprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", cl_updateRepo->string);

	CL_Update_StopCheck();
	updater.forced = forced;
	updater.curlError[0] = '\0';
	updater.body = (char *)malloc(UPDATER_MAX_BODY + 1);
	if(!updater.body) {
		CL_Update_CheckFailed("out of memory");
		return;
	}
	updater.body[0] = '\0';

	updater.check = qcurl_easy_init();
	updater.checkM = qcurl_multi_init();
	if(!updater.check || !updater.checkM) {
		CL_Update_CheckFailed("curl init failed");
		return;
	}
	updater.headers = curl_slist_append(updater.headers, "Accept: application/vnd.github+json");
	updater.headers = curl_slist_append(updater.headers, "User-Agent: WolfPro/" WOLFPRO_COMMIT);

	qcurl_easy_setopt(updater.check, CURLOPT_URL, url);
	qcurl_easy_setopt(updater.check, CURLOPT_HTTPHEADER, updater.headers);
	qcurl_easy_setopt(updater.check, CURLOPT_WRITEFUNCTION, CL_Update_CheckWrite);
	qcurl_easy_setopt(updater.check, CURLOPT_WRITEDATA, NULL);
	qcurl_easy_setopt(updater.check, CURLOPT_ERRORBUFFER, updater.curlError);
	qcurl_easy_setopt(updater.check, CURLOPT_FAILONERROR, 1L);
	qcurl_easy_setopt(updater.check, CURLOPT_FOLLOWLOCATION, 1L);
	qcurl_easy_setopt(updater.check, CURLOPT_MAXREDIRS, 3L);
	qcurl_easy_setopt(updater.check, CURLOPT_TIMEOUT, 15L);
	qcurl_easy_setopt(updater.check, CURLOPT_NOSIGNAL, 1L);
	qcurl_easy_setopt(updater.check, CURLOPT_SSL_VERIFYPEER, 1L);
	qcurl_easy_setopt(updater.check, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef UPDATER_SSL_OPTIONS
	qcurl_easy_setopt(updater.check, CURLOPT_SSL_OPTIONS, (long)(UPDATER_SSL_OPTIONS));
#endif
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	qcurl_easy_setopt(updater.check, CURLOPT_PROTOCOLS_STR, "https");
	qcurl_easy_setopt(updater.check, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	qcurl_easy_setopt(updater.check, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
	qcurl_easy_setopt(updater.check, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif

	if(qcurl_multi_add_handle(updater.checkM, updater.check) != CURLM_OK) {
		CL_Update_CheckFailed("curl_multi_add_handle failed");
		return;
	}
	CL_Update_SetState(UPDATER_STATE_CHECKING);
	if(forced) {
		Cvar_Set("cl_updateError", "");
		Com_Printf("Checking for updates...\n");
	}
}

static void CL_Update_PrintNotes(void) {
	char *notes, *line, *next;

	notes = (char *)malloc(UPDATER_NOTES_SIZE);
	if(!notes) {
		return;
	}
	Updater_SanitizeNotes(updater.notes, notes, UPDATER_NOTES_SIZE);
	for(line = notes; line; line = next) {
		next = strchr(line, '\n');
		if(next) {
			*next++ = '\0';
		}
		Com_Printf("%s\n", line);
	}
	free(notes);
}

static void CL_Update_HandleRelease(void) {
	static updaterRelease_t rel;
	static char notes[UPDATER_NOTES_SIZE];
	updaterChannel_t channel = Updater_ChannelFromString(WOLFPRO_CHANNEL);
	qboolean forced = updater.forced;

	// parse into temporaries so a bad response keeps the previous release
	if(!Updater_ParseRelease(updater.body, &rel, notes, sizeof(notes))) {
		CL_Update_CheckFailed("could not parse release info");
		return;
	}
	CL_Update_StopCheck();
	updater.release = rel;
	memcpy(updater.notes, notes, sizeof(updater.notes));
	updater.haveRelease = qtrue;
	Cvar_Set("cl_updateLatest", updater.release.tag);

	if(Updater_ShouldOffer(&updater.release, WOLFPRO_COMMIT, channel, WOLFPRO_BUILD_TIME,
					   cl_updateSkip->string, forced)) {
		CL_Update_SetState(UPDATER_STATE_AVAILABLE);
		Cvar_Set("cl_updatePrompt", "1");
		if(forced) {
			Com_Printf("Update available: %s (installed %s)\n", updater.release.tag, WOLFPRO_COMMIT);
			CL_Update_PrintNotes();
			Com_Printf("Type /update confirm to install.\n");
		} else {
			Com_Printf("Update available: %s (installed %s). Type /update for details.\n",
					   updater.release.tag, WOLFPRO_COMMIT);
		}
		return;
	}

	CL_Update_SetState(UPDATER_STATE_UPTODATE);
	if(forced) {
		Com_Printf("You're on the latest version (%s).\n", WOLFPRO_COMMIT);
	}
}

static void CL_Update_PollCheck(void) {
	CURLMsg *msg;
	CURLcode result;
	long code = 0;
	CURLMcode mres;
	int running, queued;

	mres = qcurl_multi_perform(updater.checkM, &running);
	if(mres != CURLM_OK) {
		CL_Update_CheckFailed(va("curl_multi_perform: %s", qcurl_multi_strerror(mres)));
		return;
	}
	msg = qcurl_multi_info_read(updater.checkM, &queued);
	if(!msg || msg->msg != CURLMSG_DONE) {
		return;
	}
	result = msg->data.result;
	qcurl_easy_getinfo(updater.check, CURLINFO_RESPONSE_CODE, &code);

	if(result != CURLE_OK) {
		if(updater.bodyOverflow) {
			CL_Update_CheckFailed(va("release info exceeds %d bytes", UPDATER_MAX_BODY));
		} else {
			CL_Update_CheckFailed(va("%s (HTTP %ld)",
									 updater.curlError[0] ? updater.curlError : qcurl_easy_strerror(result), code));
		}
		return;
	}
	CL_Update_HandleRelease();
}

// The updater leaves update/result.txt behind after an install or rollback.
static void CL_Update_ConsumeResult(void) {
	char path[UPDATER_PATH_SIZE], line[1024];
	char status[32] = "", tag[64] = "", error[256] = "";
	FILE *f;

	if(!updater.installDir[0]) {
		return;
	}
	Com_sprintf(path, sizeof(path), "%s\\update\\result.txt", updater.installDir);
	f = Sys_UpdateFOpen(path, "rb");
	if(!f) {
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		char *val;

		line[strcspn(line, "\r\n")] = '\0';
		val = strchr(line, ' ');
		if(!val) {
			continue;
		}
		*val++ = '\0';
		if(!strcmp(line, "status")) {
			Q_strncpyz(status, val, sizeof(status));
		} else if(!strcmp(line, "tag")) {
			Q_strncpyz(tag, val, sizeof(tag));
		} else if(!strcmp(line, "error")) {
			Q_strncpyz(error, val, sizeof(error));
		}
	}
	fclose(f);
	if(!Sys_UpdateRemoveFile(path)) {
		Com_Printf("Couldn't delete %s\n", path);
	}

	if(!strcmp(status, "ok")) {
		if(tag[0] && strcmp(tag, "unknown")) {
			Com_Printf("Updated to %s.\n", tag);
		} else {
			Com_Printf("Updated to the new version.\n");
		}
	} else if(!strcmp(status, "failed")) {
		if(!error[0]) {
			Q_strncpyz(error, "unknown error", sizeof(error));
		}
		Com_Printf("Last update (%s) failed: %s\n", tag, error);
		CL_Update_SetState(UPDATER_STATE_FAILED);
		Cvar_Set("cl_updateError", va("Last update failed: %s", error));
		Cvar_Set("cl_updatePrompt", "1");
	} else if(!strcmp(status, "rolledback")) {
		if(tag[0] && strcmp(tag, "unknown")) {
			Cvar_Set("cl_updateSkip", tag);
			Com_Printf("Rolled back from %s; it won't be offered again.\n", tag);
		} else {
			Com_Printf("Rolled back to the previous version.\n");
		}
	} else {
		Com_Printf("Ignoring update result with status \"%s\"\n", status);
	}
}

static qboolean CL_Update_IsLocalBuild(void) {
	return !strcmp(WOLFPRO_COMMIT, "local") || Updater_ChannelFromString(WOLFPRO_CHANNEL) == UPDATER_CHANNEL_LOCAL;
}

// Connecting, playing a demo or running a local server all count.
static qboolean CL_Update_GameActive(void) {
	return cls.state != CA_UNINITIALIZED && cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC;
}

static qboolean CL_Update_RefuseIfActive(void) {
	if(!CL_Update_GameActive()) {
		return qfalse;
	}
	Cvar_Set("cl_updateError", "Disconnect before updating.");
	Com_Printf("Disconnect before updating.\n");
	return qtrue;
}

static qboolean CL_Update_Join(char *out, int size, const char *dir, const char *name) {
	size_t a = strlen(dir), b = strlen(name);

	if(a + 1 + b >= (size_t)size) {
		return qfalse;
	}
	memmove(out, dir, a);
	out[a] = '\\';
	memcpy(out + a + 1, name, b + 1);
	return qtrue;
}

// The updater never runs elevated, so an unwritable install can't be updated.
static qboolean CL_Update_ChooseDirs(char *err, int errSize) {
	char base[UPDATER_PATH_SIZE];

	updater.baseDir[0] = updater.stagingDir[0] = updater.zipPath[0] = '\0';
	if(!updater.installDir[0]) {
		Q_strncpyz(err, "couldn't resolve the install directory", errSize);
		return qfalse;
	}
	if(!Sys_UpdateDirWritable(updater.installDir)) {
		Q_strncpyz(err, UPDATER_ADMIN_MSG, errSize);
		return qfalse;
	}
	if(!CL_Update_Join(base, sizeof(base), updater.installDir, "update")) {
		Com_sprintf(err, errSize, "install path too long: %s", updater.installDir);
		return qfalse;
	}
	if(!CL_Update_Join(updater.stagingDir, sizeof(updater.stagingDir), base, "staging") ||
	   !CL_Update_Join(updater.zipPath, sizeof(updater.zipPath), base, "download.zip")) {
		updater.stagingDir[0] = updater.zipPath[0] = '\0';
		Com_sprintf(err, errSize, "path too long: %s", base);
		return qfalse;
	}
	Q_strncpyz(updater.baseDir, base, sizeof(updater.baseDir));
	return qtrue;
}

static void CL_Update_StopDownload(void) {
	if(updater.dlM) {
		if(updater.dl) {
			qcurl_multi_remove_handle(updater.dlM, updater.dl);
		}
		qcurl_multi_cleanup(updater.dlM);
		updater.dlM = NULL;
	}
	if(updater.dl) {
		qcurl_easy_cleanup(updater.dl);
		updater.dl = NULL;
	}
	if(updater.dlHeaders) {
		curl_slist_free_all(updater.dlHeaders);
		updater.dlHeaders = NULL;
	}
	if(updater.dlFile) {
		fclose(updater.dlFile);
		updater.dlFile = NULL;
	}
}

static void CL_Update_StopExtract(void) {
	if(updater.outFile) {
		fclose(updater.outFile);
		updater.outFile = NULL;
	}
	if(updater.zip) {
		unzClose(updater.zip);
		updater.zip = NULL;
	}
	if(updater.zipBuf) {
		free(updater.zipBuf);
		updater.zipBuf = NULL;
	}
}

static void CL_Update_DiscardFiles(qboolean quiet) {
	if(updater.zipPath[0]) {
		Sys_UpdateRemoveFile(updater.zipPath);
	}
	if(updater.stagingDir[0] && !Sys_UpdateRemoveTree(updater.stagingDir)) {
		if(quiet) {
			Com_DPrintf("Couldn't delete %s\n", updater.stagingDir);
		} else {
			Com_Printf("Couldn't delete %s\n", updater.stagingDir);
		}
	}
}

static void CL_Update_Abort(void) {
	CL_Update_StopDownload();
	CL_Update_StopExtract();
	CL_Update_DiscardFiles(qfalse);
	Cvar_Set("cl_updateDlCount", "0");
	Cvar_Set("cl_updateDlTotal", "0");
}

static void CL_Update_Cancel(void) {
	CL_Update_Abort();
	CL_Update_SetState(updater.haveRelease ? UPDATER_STATE_AVAILABLE : UPDATER_STATE_IDLE);
	Com_Printf("Update cancelled.\n");
}

static void CL_Update_Fail(const char *why) {
	char msg[UPDATER_PATH_SIZE + 256];

	Q_strncpyz(msg, why, sizeof(msg)); // why is often a va() buffer
	CL_Update_Abort();
	CL_Update_SetState(UPDATER_STATE_FAILED);
	Cvar_Set("cl_updateError", msg);
	Cvar_Set("cl_updatePrompt", "1");
	Com_Printf("Update failed: %s\n", msg);
}

static size_t CL_Update_DlWrite(void *data, size_t size, size_t nmemb, void *user) {
	size_t n = size * nmemb;

	if(updater.asset.size > 0 && updater.dlWritten + (long long)n > updater.asset.size) {
		updater.dlTooLarge = qtrue;
		return 0;
	}
	if(fwrite(data, 1, n, updater.dlFile) != n) {
		updater.dlWriteFailed = qtrue;
		return 0;
	}
	updater.dlWritten += (long long)n;
	return n;
}

#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int CL_Update_DlProgress(void *user, curl_off_t dltotal, curl_off_t dlnow,
								curl_off_t ultotal, curl_off_t ulnow) {
	updater.dlNow = (long long)dlnow;
	if(dltotal > 0) {
		updater.dlTotal = (long long)dltotal;
	}
	return 0;
}
#endif

static void CL_Update_DlReport(void) {
	long long now = updater.dlNow > updater.dlWritten ? updater.dlNow : updater.dlWritten;
	long long total = updater.dlTotal > 0 ? updater.dlTotal : updater.asset.size;
	int tenth;

	if(now == updater.dlReported) {
		return;
	}
	updater.dlReported = now;
	Cvar_Set("cl_updateDlCount", va("%lld", now));
	Cvar_Set("cl_updateDlTotal", va("%lld", total > 0 ? total : 0));
	if(total <= 0) {
		return;
	}
	tenth = (int)(now * 10 / total);
	if(tenth > 10) {
		tenth = 10;
	}
	if(tenth > updater.dlTenth) {
		updater.dlTenth = tenth;
		Com_Printf("Downloading update: %d%% (%lld / %lld KB)\n", tenth * 10, now / 1024, total / 1024);
	}
}

static qboolean CL_Update_StartDownload(void) {
	updater.dl = qcurl_easy_init();
	updater.dlM = qcurl_multi_init();
	if(!updater.dl || !updater.dlM) {
		CL_Update_Fail("curl init failed");
		return qfalse;
	}
	updater.dlHeaders = curl_slist_append(updater.dlHeaders, "Accept: application/octet-stream");
	updater.dlHeaders = curl_slist_append(updater.dlHeaders, "User-Agent: WolfPro/" WOLFPRO_COMMIT);

	qcurl_easy_setopt(updater.dl, CURLOPT_URL, updater.asset.url);
	qcurl_easy_setopt(updater.dl, CURLOPT_HTTPHEADER, updater.dlHeaders);
	qcurl_easy_setopt(updater.dl, CURLOPT_WRITEFUNCTION, CL_Update_DlWrite);
	qcurl_easy_setopt(updater.dl, CURLOPT_WRITEDATA, NULL);
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	qcurl_easy_setopt(updater.dl, CURLOPT_NOPROGRESS, 0L);
	qcurl_easy_setopt(updater.dl, CURLOPT_XFERINFOFUNCTION, CL_Update_DlProgress);
	qcurl_easy_setopt(updater.dl, CURLOPT_XFERINFODATA, NULL);
#endif
	qcurl_easy_setopt(updater.dl, CURLOPT_ERRORBUFFER, updater.dlError);
	qcurl_easy_setopt(updater.dl, CURLOPT_FAILONERROR, 1L);
	qcurl_easy_setopt(updater.dl, CURLOPT_FOLLOWLOCATION, 1L);
	qcurl_easy_setopt(updater.dl, CURLOPT_MAXREDIRS, 5L);
	qcurl_easy_setopt(updater.dl, CURLOPT_CONNECTTIMEOUT, 30L);
	qcurl_easy_setopt(updater.dl, CURLOPT_LOW_SPEED_LIMIT, 1L);
	qcurl_easy_setopt(updater.dl, CURLOPT_LOW_SPEED_TIME, 30L);
	qcurl_easy_setopt(updater.dl, CURLOPT_NOSIGNAL, 1L);
	qcurl_easy_setopt(updater.dl, CURLOPT_SSL_VERIFYPEER, 1L);
	qcurl_easy_setopt(updater.dl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef UPDATER_SSL_OPTIONS
	qcurl_easy_setopt(updater.dl, CURLOPT_SSL_OPTIONS, (long)(UPDATER_SSL_OPTIONS));
#endif
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	qcurl_easy_setopt(updater.dl, CURLOPT_PROTOCOLS_STR, "https");
	qcurl_easy_setopt(updater.dl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	qcurl_easy_setopt(updater.dl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
	qcurl_easy_setopt(updater.dl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif

	if(qcurl_multi_add_handle(updater.dlM, updater.dl) != CURLM_OK) {
		CL_Update_Fail("curl_multi_add_handle failed");
		return qfalse;
	}
	return qtrue;
}

static void CL_Update_Confirm(void) {
	const updaterAsset_t *asset;
	char err[UPDATER_PATH_SIZE + 64];

	if(CL_Update_Busy()) {
		Com_Printf("An update is already in progress.\n");
		return;
	}
	if(updater.state == UPDATER_STATE_CHECKING) {
		Com_Printf("Checking for updates...\n");
		return;
	}
	if(!updater.haveRelease) {
		// retry after a failure reported at startup: nothing to install yet
		if(updater.state == UPDATER_STATE_FAILED) {
			CL_Update_StartCheck(qtrue);
		} else {
			Com_Printf("Run /update first.\n");
		}
		return;
	}
	if(updater.state != UPDATER_STATE_AVAILABLE && updater.state != UPDATER_STATE_FAILED) {
		Com_Printf("No update available (installed %s, latest %s).\n", WOLFPRO_COMMIT, updater.release.tag);
		return;
	}
	if(CL_Update_RefuseIfActive()) {
		return;
	}
	asset = Updater_PickAsset(&updater.release, sizeof(void *) == 8);
	if(!asset) {
		CL_Update_Fail(va("Release %s has no %s package.", updater.release.tag,
						  sizeof(void *) == 8 ? "windows-x64" : "windows-x86"));
		return;
	}
	if(!asset->sha256[0]) {
		CL_Update_Fail("Release has no checksum; not installing.");
		return;
	}
	if(strpbrk(updater.release.tag, "\r\n")) {
		CL_Update_Fail("Release tag contains a line break; not installing.");
		return;
	}
	if(!CL_Update_ChooseDirs(err, sizeof(err))) {
		CL_Update_Fail(err);
		return;
	}
	updater.asset = *asset;
	Q_strncpyz(updater.tag, updater.release.tag, sizeof(updater.tag));

	// clear whatever an earlier attempt left behind
	Sys_UpdateRemoveFile(updater.zipPath);
	if(!Sys_UpdateRemoveTree(updater.stagingDir)) {
		CL_Update_Fail(va("couldn't delete the old staging directory %s", updater.stagingDir));
		return;
	}
	if(!Sys_UpdateMkdirs(updater.baseDir)) {
		CL_Update_Fail(va("couldn't create %s", updater.baseDir));
		return;
	}
	updater.dlFile = Sys_UpdateFOpen(updater.zipPath, "wb");
	if(!updater.dlFile) {
		CL_Update_Fail(va("couldn't write %s", updater.zipPath));
		return;
	}
	updater.dlError[0] = '\0';
	updater.dlWriteFailed = updater.dlTooLarge = qfalse;
	updater.dlWritten = updater.dlNow = updater.dlTotal = 0;
	updater.dlReported = -1;
	updater.dlTenth = 0;
	if(!CL_Update_StartDownload()) {
		return;
	}
	CL_Update_SetState(UPDATER_STATE_DOWNLOADING);
	Cvar_Set("cl_updateError", "");
	Cvar_Set("cl_updateDlCount", "0");
	Cvar_Set("cl_updateDlTotal", va("%lld", updater.asset.size > 0 ? updater.asset.size : 0));
	Com_Printf("Downloading %s (%lld KB)...\n", updater.asset.name, updater.asset.size / 1024);
}

// Stricter than Updater_IsSafeZipPath: printable ASCII only, no Windows-reserved
// characters, and no segment Win32 would silently trim (trailing dot/space).
static qboolean CL_Update_SafeEntry(const char *name) {
	const char *p;

	if(!Updater_IsSafeZipPath(name)) {
		return qfalse;
	}
	// update\ holds staging, backups and the pending file
	if(!Q_stricmpn(name, "update", 6) && (name[6] == '\0' || name[6] == '/' || name[6] == '\\')) {
		return qfalse;
	}
	for(p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;

		if(c < 0x20 || c >= 0x7f || strchr(":*?\"<>|", c)) {
			return qfalse;
		}
		if((c == '/' || c == '\\') && (p == name || p[-1] == '/' || p[-1] == '\\')) {
			return qfalse; // empty segment
		}
		if((c == '.' || c == ' ') && (p[1] == '\0' || p[1] == '/' || p[1] == '\\')) {
			return qfalse;
		}
	}
	return qtrue;
}

static qboolean CL_Update_EntryInfo(char *name, unz_file_info *info) {
	if(unzGetCurrentFileInfo(updater.zip, info, name, UPDATER_ZIP_NAME, NULL, 0, NULL, 0) != UNZ_OK) {
		return qfalse;
	}
	if(info->size_filename >= UPDATER_ZIP_NAME) {
		Q_strncpyz(name, "(name too long)", UPDATER_ZIP_NAME);
		return qfalse;
	}
	name[info->size_filename] = '\0';
	return qtrue;
}

static void CL_Update_BeginExtract(void) {
	char ansi[UPDATER_PATH_SIZE], name[UPDATER_ZIP_NAME];
	unz_global_info gi;
	unz_file_info info;
	qboolean haveUpdater = qfalse;
	unsigned long i = 0;
	int r;

	if(!Sys_UpdateAnsiPath(updater.zipPath, ansi, sizeof(ansi))) {
		CL_Update_Fail(va("can't open %s: path isn't representable in the system code page", updater.zipPath));
		return;
	}
	updater.zip = unzOpen(ansi);
	if(!updater.zip) {
		CL_Update_Fail(va("%s is not a valid zip file", updater.zipPath));
		return;
	}
	if(unzGetGlobalInfo(updater.zip, &gi) != UNZ_OK || gi.number_entry == 0) {
		CL_Update_Fail(va("%s is empty or damaged", updater.zipPath));
		return;
	}
	// vet every name before anything is written
	for(r = unzGoToFirstFile(updater.zip); r == UNZ_OK; r = unzGoToNextFile(updater.zip), i++) {
		name[0] = '\0';
		if(!CL_Update_EntryInfo(name, &info) || !CL_Update_SafeEntry(name)) {
			CL_Update_Fail(va("Unsafe path in update package: %s", name));
			return;
		}
		if(!Q_stricmp(name, UPDATER_EXE_NAME)) {
			haveUpdater = qtrue;
		}
	}
	if(r != UNZ_END_OF_LIST_OF_FILE || i != gi.number_entry) {
		CL_Update_Fail(va("%s is damaged (entry %lu of %lu, error %d)", updater.zipPath, i, gi.number_entry, r));
		return;
	}
	if(!haveUpdater) {
		CL_Update_Fail("Update package is missing " UPDATER_EXE_NAME ".");
		return;
	}
	if(!Sys_UpdateMkdirs(updater.stagingDir)) {
		CL_Update_Fail(va("couldn't create %s", updater.stagingDir));
		return;
	}
	updater.zipBuf = (unsigned char *)malloc(UPDATER_ZIP_CHUNK);
	if(!updater.zipBuf) {
		CL_Update_Fail("out of memory");
		return;
	}
	updater.zipCount = gi.number_entry;
	updater.zipIndex = 0;
	if(unzGoToFirstFile(updater.zip) != UNZ_OK) {
		CL_Update_Fail(va("%s is damaged", updater.zipPath));
		return;
	}
	Com_Printf("Extracting update...\n");
}

static void CL_Update_FinishDownload(void) {
	char hash[65];

	if(updater.asset.size > 0 && updater.dlWritten != updater.asset.size) {
		CL_Update_Fail(va("Download incomplete (expected %lld bytes, got %lld)", updater.asset.size, updater.dlWritten));
		return;
	}
	if(!Sys_UpdateSHA256File(updater.zipPath, hash)) {
		CL_Update_Fail(va("couldn't read %s", updater.zipPath));
		return;
	}
	if(Q_stricmp(hash, updater.asset.sha256)) {
		CL_Update_Fail(va("Download corrupted (expected %s, got %s)", updater.asset.sha256, hash));
		return;
	}
	CL_Update_SetState(UPDATER_STATE_STAGING);
	CL_Update_BeginExtract();
}

static void CL_Update_PollDownload(void) {
	CURLMsg *msg;
	CURLcode result;
	CURLMcode mres;
	long code = 0;
	int running, queued, closeErr;

	mres = qcurl_multi_perform(updater.dlM, &running);
	if(mres != CURLM_OK) {
		CL_Update_Fail(va("curl_multi_perform: %s", qcurl_multi_strerror(mres)));
		return;
	}
	CL_Update_DlReport();
	msg = qcurl_multi_info_read(updater.dlM, &queued);
	if(!msg || msg->msg != CURLMSG_DONE) {
		return;
	}
	result = msg->data.result;
	qcurl_easy_getinfo(updater.dl, CURLINFO_RESPONSE_CODE, &code);
	if(result != CURLE_OK) {
		if(updater.dlTooLarge) {
			CL_Update_Fail(va("download exceeds the expected %lld bytes", updater.asset.size));
		} else if(updater.dlWriteFailed) {
			CL_Update_Fail(va("couldn't write %s", updater.zipPath));
		} else {
			CL_Update_Fail(va("download failed: %s (HTTP %ld)",
							  updater.dlError[0] ? updater.dlError : qcurl_easy_strerror(result), code));
		}
		return;
	}
	closeErr = fclose(updater.dlFile);
	updater.dlFile = NULL;
	CL_Update_StopDownload();
	if(closeErr) {
		CL_Update_Fail(va("couldn't write %s", updater.zipPath));
		return;
	}
	CL_Update_DlReport();
	CL_Update_FinishDownload();
}

// Args the updater relaunches us with; dropped entirely if any won't fit its
// limits, rather than relaunching with a mangled command line.
static void CL_Update_WriteArgs(FILE *f, const char *exe) {
	static char args[UPDATER_MAX_ARGS][UPDATER_MAX_ARG_LEN];
	static char cmd[UPDATER_RELAUNCH_CMD];
	const char *argv[UPDATER_MAX_ARGS + 1];
	int n = Sys_UpdateArgCount(), i;

	if(n <= 0) {
		return;
	}
	if(n > UPDATER_MAX_ARGS) {
		Com_Printf("Command line has %d arguments (max %d); the new version will start without them.\n",
				   n, UPDATER_MAX_ARGS);
		return;
	}
	argv[0] = exe;
	for(i = 0; i < n; i++) {
		if(!Sys_UpdateArg(i, args[i], UPDATER_MAX_ARG_LEN) || strpbrk(args[i], "\r\n")) {
			Com_Printf("Command-line argument %d is too long or unprintable; the new version will start without arguments.\n",
					   i + 1);
			return;
		}
		argv[i + 1] = args[i];
	}
	if(!Updater_BuildCommandLine(cmd, sizeof(cmd), argv, n + 1)) {
		Com_Printf("Command line exceeds %d characters; the new version will start without arguments.\n",
				   UPDATER_RELAUNCH_CMD - 1);
		return;
	}
	for(i = 0; i < n; i++) {
		fprintf(f, "arg %s\r\n", args[i]);
	}
}

static qboolean CL_Update_WritePending(const char *path, qboolean apply, char *err, int errSize) {
	char exe[UPDATER_PATH_SIZE];
	FILE *f;
	int writeErr;

	Sys_UpdateExePath(exe, sizeof(exe));
	if(!exe[0]) {
		Q_strncpyz(err, "couldn't resolve the game's exe path", errSize);
		return qfalse;
	}
	// truncating overwrites any stale file an earlier timeout left behind
	f = Sys_UpdateFOpen(path, "wb");
	if(!f) {
		Com_sprintf(err, errSize, "couldn't write %s", path);
		return qfalse;
	}
	fprintf(f, "pid %d\r\n", Sys_UpdatePid());
	fprintf(f, "install %s\r\n", updater.installDir);
	if(apply) {
		fprintf(f, "staging %s\r\n", updater.stagingDir);
		fprintf(f, "tag %s\r\n", updater.tag);
		fprintf(f, "from %s\r\n", WOLFPRO_COMMIT);
	}
	fprintf(f, "relaunch %s\r\n", exe);
	CL_Update_WriteArgs(f, exe);
	writeErr = ferror(f);
	if(fclose(f) || writeErr) {
		Sys_UpdateRemoveFile(path);
		Com_sprintf(err, errSize, "couldn't write %s", path);
		return qfalse;
	}
	return qtrue;
}

// Starts the updater and quits; on failure the pending file is removed.
static qboolean CL_Update_LaunchUpdater(const char *updaterExe, const char *mode, char *err, int errSize) {
	char pending[UPDATER_PATH_SIZE], cmd[UPDATER_PATH_SIZE + 64];
	const char *argv[2];

	if(!CL_Update_Join(pending, sizeof(pending), updater.baseDir, "pending.txt")) {
		Com_sprintf(err, errSize, "path too long: %s", updater.baseDir);
		return qfalse;
	}
	if(!Sys_UpdateMkdirs(updater.baseDir)) {
		Com_sprintf(err, errSize, "couldn't create %s", updater.baseDir);
		return qfalse;
	}
	argv[0] = mode;
	argv[1] = pending;
	if(!Updater_BuildCommandLine(cmd, sizeof(cmd), argv, 2)) {
		Com_sprintf(err, errSize, "updater command line too long for %s", pending);
		return qfalse;
	}
	if(!CL_Update_WritePending(pending, !strcmp(mode, "--apply"), err, errSize)) {
		return qfalse;
	}
	if(!Sys_UpdateLaunch(updaterExe, cmd, err, errSize)) {
		Sys_UpdateRemoveFile(pending);
		return qfalse;
	}
	Cbuf_ExecuteText(EXEC_APPEND, "quit\n");
	return qtrue;
}

static void CL_Update_HandOff(void) {
	char updaterExe[UPDATER_PATH_SIZE], err[UPDATER_PATH_SIZE + 64];

	Sys_UpdateRemoveFile(updater.zipPath);
	if(!CL_Update_Join(updaterExe, sizeof(updaterExe), updater.stagingDir, UPDATER_EXE_NAME)) {
		CL_Update_Fail(va("path too long: %s", updater.stagingDir));
		return;
	}
	CL_Update_SetState(UPDATER_STATE_LAUNCHING);
	if(!CL_Update_LaunchUpdater(updaterExe, "--apply", err, sizeof(err))) {
		CL_Update_Fail(err);
		return;
	}
	Com_Printf("Installing %s; the game will restart.\n", updater.tag);
}

static qboolean CL_Update_AdvanceEntry(void) {
	int r;

	updater.zipIndex++;
	if(updater.zipIndex >= updater.zipCount) {
		return qtrue;
	}
	r = unzGoToNextFile(updater.zip);
	if(r != UNZ_OK) {
		CL_Update_Fail(va("%s is damaged (entry %lu, error %d)", updater.zipPath, updater.zipIndex, r));
		return qfalse;
	}
	return qtrue;
}

static qboolean CL_Update_OpenEntry(void) {
	char name[UPDATER_ZIP_NAME], path[UPDATER_PATH_SIZE], *p, *slash;
	unz_file_info info;
	size_t len;
	int r;

	// names were vetted up front; re-check in case the reads disagree
	name[0] = '\0';
	if(!CL_Update_EntryInfo(name, &info) || !CL_Update_SafeEntry(name)) {
		CL_Update_Fail(va("Unsafe path in update package: %s", name));
		return qfalse;
	}
	Q_strncpyz(updater.outName, name, sizeof(updater.outName));
	for(p = name; *p; p++) {
		if(*p == '/') {
			*p = '\\';
		}
	}
	len = strlen(name);
	if(name[len - 1] == '\\') {
		name[len - 1] = '\0';
		if(!CL_Update_Join(path, sizeof(path), updater.stagingDir, name) || !Sys_UpdateMkdirs(path)) {
			CL_Update_Fail(va("couldn't create %s\\%s", updater.stagingDir, name));
			return qfalse;
		}
		return CL_Update_AdvanceEntry();
	}
	if(!CL_Update_Join(path, sizeof(path), updater.stagingDir, name)) {
		CL_Update_Fail(va("path too long: %s\\%s", updater.stagingDir, name));
		return qfalse;
	}
	slash = strrchr(path, '\\');
	*slash = '\0';
	if(!Sys_UpdateMkdirs(path)) {
		CL_Update_Fail(va("couldn't create %s", path));
		return qfalse;
	}
	*slash = '\\';
	r = unzOpenCurrentFile(updater.zip);
	if(r != UNZ_OK) {
		CL_Update_Fail(va("couldn't extract %s (error %d)", updater.outName, r));
		return qfalse;
	}
	updater.outFile = Sys_UpdateFOpen(path, "wb");
	if(!updater.outFile) {
		CL_Update_Fail(va("couldn't write %s", path));
		return qfalse;
	}
	updater.outExpected = info.uncompressed_size;
	updater.outWritten = 0;
	return qtrue;
}

static qboolean CL_Update_CloseEntry(void) {
	int r, closeErr;

	r = unzCloseCurrentFile(updater.zip);
	closeErr = fclose(updater.outFile);
	updater.outFile = NULL;
	if(r != UNZ_OK) {
		CL_Update_Fail(va("couldn't extract %s (error %d)", updater.outName, r));
		return qfalse;
	}
	if(closeErr) {
		CL_Update_Fail(va("couldn't write %s\\%s", updater.stagingDir, updater.outName));
		return qfalse;
	}
	if(updater.outWritten != updater.outExpected) {
		CL_Update_Fail(va("%s: expected %lu bytes, got %lu", updater.outName, updater.outExpected, updater.outWritten));
		return qfalse;
	}
	return CL_Update_AdvanceEntry();
}

static void CL_Update_ExtractStep(void) {
	int budget = UPDATER_ZIP_FRAME_BUDGET, n;

	while(budget > 0) {
		if(!updater.outFile) {
			if(updater.zipIndex >= updater.zipCount) {
				CL_Update_StopExtract();
				CL_Update_HandOff();
				return;
			}
			if(!CL_Update_OpenEntry()) {
				return;
			}
			continue;
		}
		n = unzReadCurrentFile(updater.zip, updater.zipBuf, UPDATER_ZIP_CHUNK);
		if(n < 0) {
			CL_Update_Fail(va("couldn't extract %s (error %d)", updater.outName, n));
			return;
		}
		if(n == 0) {
			if(!CL_Update_CloseEntry()) {
				return;
			}
			continue;
		}
		if(updater.outWritten + (unsigned long)n > updater.outExpected) {
			CL_Update_Fail(va("%s: more data than the expected %lu bytes", updater.outName, updater.outExpected));
			return;
		}
		if(fwrite(updater.zipBuf, 1, (size_t)n, updater.outFile) != (size_t)n) {
			CL_Update_Fail(va("couldn't write %s\\%s", updater.stagingDir, updater.outName));
			return;
		}
		updater.outWritten += (unsigned long)n;
		budget -= n;
	}
}

static void CL_Update_Rollback(void) {
	char manifest[UPDATER_PATH_SIZE], updaterExe[UPDATER_PATH_SIZE], err[UPDATER_PATH_SIZE + 64];
	updaterState_t prev = updater.state;
	FILE *f;

	if(CL_Update_Busy()) {
		Com_Printf("An update is already in progress.\n");
		return;
	}
	if(CL_Update_RefuseIfActive()) {
		return;
	}
	// backup.new is what an interrupted install leaves behind
	f = NULL;
	if(updater.installDir[0]) {
		if(CL_Update_Join(manifest, sizeof(manifest), updater.installDir, "update\\backup\\manifest.txt")) {
			f = Sys_UpdateFOpen(manifest, "rb");
		}
		if(!f && CL_Update_Join(manifest, sizeof(manifest), updater.installDir, "update\\backup.new\\manifest.txt")) {
			f = Sys_UpdateFOpen(manifest, "rb");
		}
	}
	if(!f) {
		Com_Printf("No previous version to roll back to.\n");
		return;
	}
	fclose(f);
	if(!CL_Update_ChooseDirs(err, sizeof(err))) {
		Cvar_Set("cl_updateError", va("Rollback failed: %s", err));
		Com_Printf("Rollback failed: %s\n", err);
		return;
	}
	if(!CL_Update_Join(updaterExe, sizeof(updaterExe), updater.installDir, UPDATER_EXE_NAME)) {
		Com_Printf("Rollback failed: install path too long: %s\n", updater.installDir);
		return;
	}
	CL_Update_StopCheck();
	CL_Update_SetState(UPDATER_STATE_LAUNCHING);
	if(!CL_Update_LaunchUpdater(updaterExe, "--rollback", err, sizeof(err))) {
		CL_Update_SetState(prev == UPDATER_STATE_CHECKING ? UPDATER_STATE_IDLE : prev);
		Cvar_Set("cl_updateError", va("Rollback failed: %s", err));
		Com_Printf("Rollback failed: %s\n", err);
		return;
	}
	Com_Printf("Rolling back to the previous version; the game will restart.\n");
}

// Staging from an install that already finished; kept while a pending file
// still exists, since the updater may not have run yet.
static void CL_Update_CleanLeftovers(void) {
	char err[UPDATER_PATH_SIZE + 64], pending[UPDATER_PATH_SIZE];
	FILE *f;

	if(!CL_Update_ChooseDirs(err, sizeof(err))) {
		return;
	}
	if(CL_Update_Join(pending, sizeof(pending), updater.baseDir, "pending.txt")) {
		f = Sys_UpdateFOpen(pending, "rb");
		if(f) {
			fclose(f);
			return;
		}
	}
	CL_Update_DiscardFiles(qtrue);
}

// A pending file whose game has exited, with no updater running, means the
// updater gave up or died; only a leftover backup.new means files changed.
static void CL_Update_CheckInterrupted(void) {
	char pending[UPDATER_PATH_SIZE], manifest[UPDATER_PATH_SIZE], line[256];
	int pid = 0;
	FILE *f;

	if(!updater.installDir[0] ||
	   !CL_Update_Join(pending, sizeof(pending), updater.installDir, "update\\pending.txt")) {
		return;
	}
	f = Sys_UpdateFOpen(pending, "rb");
	if(!f) {
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		if(!strncmp(line, "pid ", 4)) {
			pid = atoi(line + 4);
		}
	}
	fclose(f);
	if(pid != Sys_UpdatePid() && Sys_UpdatePidRunning(pid)) {
		return;
	}
	if(Sys_UpdateUpdaterRunning()) {
		return;
	}
	if(!Sys_UpdateRemoveFile(pending)) {
		Com_Printf("Couldn't delete %s\n", pending);
	}
	f = NULL;
	if(CL_Update_Join(manifest, sizeof(manifest), updater.installDir, "update\\backup.new\\manifest.txt")) {
		f = Sys_UpdateFOpen(manifest, "rb");
	}
	if(!f) {
		return;
	}
	fclose(f);
	Com_Printf("%s\n", UPDATER_INTERRUPTED_MSG);
	CL_Update_SetState(UPDATER_STATE_FAILED);
	Cvar_Set("cl_updateError", UPDATER_INTERRUPTED_MSG);
	Cvar_Set("cl_updatePrompt", "1");
}

static void CL_Update_Skip(void) {
	if(!updater.haveRelease) {
		Com_Printf("Run /update first.\n");
		return;
	}
	Cvar_Set("cl_updateSkip", updater.release.tag);
	Cvar_Set("cl_updatePrompt", "0");
	Com_Printf("Skipping %s until a newer release is out.\n", updater.release.tag);
}

static void CL_Update_f(void) {
	const char *arg = Cmd_Argc() > 1 ? Cmd_Argv(1) : "";

	if(CL_Update_IsLocalBuild() && (!arg[0] || !Q_stricmp(arg, "confirm") ||
									!Q_stricmp(arg, "cancel") || !Q_stricmp(arg, "rollback"))) {
		Com_Printf("Local build - auto-update disabled.\n");
		return;
	}
	if(!arg[0]) {
		if(CL_Update_Busy()) {
			Com_Printf("An update is already in progress.\n");
			return;
		}
		CL_Update_StartCheck(qtrue);
	} else if(!Q_stricmp(arg, "later") || !Q_stricmp(arg, "shown")) {
		Cvar_Set("cl_updatePrompt", "0");
	} else if(!Q_stricmp(arg, "skip")) {
		CL_Update_Skip();
	} else if(!Q_stricmp(arg, "confirm")) {
		CL_Update_Confirm();
	} else if(!Q_stricmp(arg, "cancel")) {
		if(updater.state == UPDATER_STATE_DOWNLOADING || updater.state == UPDATER_STATE_STAGING) {
			CL_Update_Cancel();
		} else if(updater.state == UPDATER_STATE_LAUNCHING) {
			Com_Printf("The update is already being installed.\n");
		} else {
			Com_Printf("No update download in progress.\n");
		}
	} else if(!Q_stricmp(arg, "rollback")) {
		CL_Update_Rollback();
	} else {
		Com_Printf("usage: update [confirm|cancel|later|skip|shown|rollback]\n");
	}
}

void CL_Update_Init(void) {
	char exe[UPDATER_PATH_SIZE], *slash;

	cl_autoUpdateCheck = Cvar_Get("cl_autoUpdateCheck", "1", CVAR_ARCHIVE);
	cl_updateSkip = Cvar_Get("cl_updateSkip", "", CVAR_ARCHIVE);
	cl_updateRepo = Cvar_Get("cl_updateRepo", "snappas/WolfPro", CVAR_INIT);

	Cvar_Get("cl_updateState", "0", CVAR_ROM);
	Cvar_Get("cl_updateInstalled", WOLFPRO_COMMIT, CVAR_ROM);
	Cvar_Get("cl_updateLatest", "", CVAR_ROM);
	Cvar_Get("cl_updatePrompt", "0", CVAR_ROM);
	Cvar_Get("cl_updateDlCount", "0", CVAR_ROM);
	Cvar_Get("cl_updateDlTotal", "0", CVAR_ROM);
	Cvar_Get("cl_updateError", "", CVAR_ROM);
	Cvar_Set("cl_updateInstalled", WOLFPRO_COMMIT);

	Cmd_AddCommand("update", CL_Update_f);

	if(updater.initialized) {
		return;
	}
	memset(&updater, 0, sizeof(updater));
	updater.initialized = qtrue;
	CL_Update_SetState(UPDATER_STATE_IDLE);
	Cvar_Set("cl_updateLatest", "");
	Cvar_Set("cl_updatePrompt", "0");
	Cvar_Set("cl_updateDlCount", "0");
	Cvar_Set("cl_updateDlTotal", "0");
	Cvar_Set("cl_updateError", "");

	Sys_UpdateExePath(exe, sizeof(exe));
	slash = strrchr(exe, '\\');
	if(slash) {
		*slash = '\0';
		Q_strncpyz(updater.installDir, exe, sizeof(updater.installDir));
	} else {
		Com_DPrintf("CL_Update_Init: couldn't resolve the exe path \"%s\"\n", exe);
	}

	CL_Update_ConsumeResult();
	if(!CL_Update_IsLocalBuild()) {
		CL_Update_CheckInterrupted();
		CL_Update_CleanLeftovers();
	}
	if(cl_autoUpdateCheck->integer && !CL_Update_IsLocalBuild()) {
		CL_Update_StartCheck(qfalse);
	}
}

void CL_Update_Frame(void) {
	if(updater.checkM) {
		CL_Update_PollCheck();
	}
	if(updater.state == UPDATER_STATE_DOWNLOADING || updater.state == UPDATER_STATE_STAGING) {
		if(CL_Update_GameActive()) {
			CL_Update_Cancel();
			return;
		}
		if(updater.dlM) {
			CL_Update_PollDownload();
		} else if(updater.zip) {
			CL_Update_ExtractStep();
		}
	}
}

void CL_Update_Shutdown(void) {
	CL_Update_StopCheck();
	if(updater.state == UPDATER_STATE_DOWNLOADING || updater.state == UPDATER_STATE_STAGING) {
		CL_Update_Abort();
	} else {
		// launching: the staged files belong to the updater now
		CL_Update_StopDownload();
		CL_Update_StopExtract();
	}
	Cmd_RemoveCommand("update");
	updater.initialized = qfalse;
}

int CL_Update_GetNotes(char *buf, int size) {
	if(size <= 0) {
		return 0;
	}
	if(!updater.haveRelease) {
		buf[0] = '\0';
		return 0;
	}
	Updater_SanitizeNotes(updater.notes, buf, size);
	return (int)strlen(buf);
}

#else // !_WIN32

static void CL_Update_f(void) {
	Com_Printf("Auto-update is only available on Windows.\n");
}

void CL_Update_Init(void) {
	Cmd_AddCommand("update", CL_Update_f);
}

void CL_Update_Frame(void) {
}

void CL_Update_Shutdown(void) {
	Cmd_RemoveCommand("update");
}

int CL_Update_GetNotes(char *buf, int size) {
	if(size > 0) {
		buf[0] = '\0';
	}
	return 0;
}

#endif // _WIN32
