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

// Win32 file operations for wolfpro_updater (no engine headers).

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "updater_core.h"

#define WPATH 2048
#define BACKUP_DIR "update\\backup"
#define BACKUP_NEW_DIR "update\\backup.new"

typedef struct {
	char rel[UPDATER_MAX_PATH]; /* '/' separated, relative to the install root */
	int existed; /* present in the install before the update */
	int touched; /* swapped into the install */
} updaterEntry_t;

typedef struct {
	updaterEntry_t *e;
	int count, cap;
} updaterList_t;

static void Err(char *err, int errSize, const char *fmt, const char *a, unsigned long code) {
	if(err && errSize > 0) {
		_snprintf(err, errSize - 1, fmt, a, code);
		err[errSize - 1] = '\0';
	}
}

static int ToWide(const char *s, wchar_t *out, int n) {
	return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n) > 0;
}

static int ToUtf8(const wchar_t *s, char *out, int n) {
	return WideCharToMultiByte(CP_UTF8, 0, s, -1, out, n, NULL, NULL) > 0;
}

/* root [+ '\' + sub] [+ '\' + rel] as a wide path with backslashes. */
static int BuildPathW(wchar_t *out, int n, const char *root, const char *sub, const char *rel) {
	char tmp[UPDATER_MAX_PATH * 3];
	int i;

	if(_snprintf(tmp, sizeof(tmp) - 1, "%s%s%s%s%s", root, sub ? "\\" : "", sub ? sub : "",
				 rel ? "\\" : "", rel ? rel : "") < 0) {
		return 0;
	}
	tmp[sizeof(tmp) - 1] = '\0';
	for(i = 0; tmp[i]; i++) {
		if(tmp[i] == '/') {
			tmp[i] = '\\';
		}
	}
	return ToWide(tmp, out, n);
}

static void MakeParentDirs(const wchar_t *path) {
	wchar_t buf[WPATH];
	int i, len = (int)wcslen(path);

	if(len >= WPATH) {
		return;
	}
	wcscpy(buf, path);
	for(i = 3; i < len; i++) { /* skip the drive root */
		if(buf[i] == L'\\') {
			buf[i] = L'\0';
			CreateDirectoryW(buf, NULL);
			buf[i] = L'\\';
		}
	}
}

static int MakeDirsUtf8(const char *dir) {
	wchar_t w[WPATH];

	if(!BuildPathW(w, WPATH, dir, "x", NULL)) {
		return 0;
	}
	MakeParentDirs(w);
	return 1;
}

/* Recursive delete that clears read-only bits. */
static void DeleteTreeW(const wchar_t *dir) {
	wchar_t pat[WPATH], sub[WPATH];
	WIN32_FIND_DATAW fd;
	HANDLE h;

	if(_snwprintf(pat, WPATH - 1, L"%s\\*", dir) < 0) {
		return;
	}
	pat[WPATH - 1] = 0;
	h = FindFirstFileW(pat, &fd);
	if(h != INVALID_HANDLE_VALUE) {
		do {
			if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
				continue;
			}
			if(_snwprintf(sub, WPATH - 1, L"%s\\%s", dir, fd.cFileName) < 0) {
				continue;
			}
			sub[WPATH - 1] = 0;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				DeleteTreeW(sub);
			} else {
				SetFileAttributesW(sub, FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(sub);
			}
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir);
}

static int ListAdd(updaterList_t *l, const char *rel) {
	if(strlen(rel) >= UPDATER_MAX_PATH) {
		return 0;
	}
	if(l->count == l->cap) {
		int cap = l->cap ? l->cap * 2 : 64;
		updaterEntry_t *n = (updaterEntry_t *)realloc(l->e, cap * sizeof(updaterEntry_t));

		if(!n) {
			return 0;
		}
		l->e = n;
		l->cap = cap;
	}
	memset(&l->e[l->count], 0, sizeof(updaterEntry_t));
	strcpy(l->e[l->count].rel, rel);
	l->count++;
	return 1;
}

/* Collects every file under dirW; rel is the UTF-8 prefix ("" at the root). */
static int ListDir(const wchar_t *dirW, const char *rel, updaterList_t *l, char *err, int errSize) {
	wchar_t pat[WPATH], sub[WPATH];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	int ok = 1;

	if(_snwprintf(pat, WPATH - 1, L"%s\\*", dirW) < 0) {
		Err(err, errSize, "staging path too long: %s", rel, 0);
		return 0;
	}
	pat[WPATH - 1] = 0;
	h = FindFirstFileW(pat, &fd);
	if(h == INVALID_HANDLE_VALUE) {
		Err(err, errSize, "cannot list staging directory %s (error %lu)", rel[0] ? rel : "(root)", GetLastError());
		return 0;
	}
	do {
		char name[UPDATER_MAX_PATH], childRel[UPDATER_MAX_PATH * 2];

		if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
			continue;
		}
		if(!ToUtf8(fd.cFileName, name, sizeof(name))) {
			Err(err, errSize, "bad file name under staging: %s (error %lu)", rel, GetLastError());
			ok = 0;
			break;
		}
		if(_snprintf(childRel, sizeof(childRel) - 1, "%s%s%s", rel, rel[0] ? "/" : "", name) < 0) {
			Err(err, errSize, "path too long: %s", name, 0);
			ok = 0;
			break;
		}
		childRel[sizeof(childRel) - 1] = '\0';
		if(_snwprintf(sub, WPATH - 1, L"%s\\%s", dirW, fd.cFileName) < 0) {
			Err(err, errSize, "path too long: %s", childRel, 0);
			ok = 0;
			break;
		}
		sub[WPATH - 1] = 0;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if(!ListDir(sub, childRel, l, err, errSize)) {
				ok = 0;
				break;
			}
		} else if(!ListAdd(l, childRel)) {
			Err(err, errSize, "cannot list %s", childRel, 0);
			ok = 0;
			break;
		}
	} while(FindNextFileW(h, &fd));
	FindClose(h);
	return ok;
}

static int SafeRel(const char *rel) {
	const char *p = rel;

	if(!*rel || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) {
		return 0;
	}
	/* update/ holds staging, backups and the pending file */
	if(!_strnicmp(rel, "update", 6) && (rel[6] == '\0' || rel[6] == '/' || rel[6] == '\\')) {
		return 0;
	}
	while(*p) {
		if(p[0] == '.' && p[1] == '.' && (p == rel || p[-1] == '/' || p[-1] == '\\') &&
		   (p[2] == '\0' || p[2] == '/' || p[2] == '\\')) {
			return 0;
		}
		p++;
	}
	return 1;
}

static int FileExistsW(const wchar_t *w) {
	DWORD a = GetFileAttributesW(w);

	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static char *ReadAll(const wchar_t *path) {
	FILE *f = _wfopen(path, L"rb");
	long len;
	char *buf;

	if(!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(len < 0 || !(buf = (char *)malloc(len + 1))) {
		fclose(f);
		return NULL;
	}
	len = (long)fread(buf, 1, len, f);
	buf[len] = '\0';
	fclose(f);
	return buf;
}

/* Splits the next line off *cur (in place); returns NULL at the end. */
static char *NextLine(char **cur) {
	char *s = *cur, *e;

	if(!s || !*s) {
		return NULL;
	}
	e = s;
	while(*e && *e != '\n') {
		e++;
	}
	if(*e) {
		*e = '\0';
		*cur = e + 1;
	} else {
		*cur = e;
	}
	e = s + strlen(s);
	while(e > s && e[-1] == '\r') {
		*--e = '\0';
	}
	return s;
}

/* Splits "key value" in place; value runs to end of line. */
static char *SplitKV(char *line) {
	char *sp = strchr(line, ' ');

	if(!sp) {
		return line + strlen(line);
	}
	*sp = '\0';
	return sp + 1;
}

static void CopyStr(char *dst, int n, const char *src) {
	_snprintf(dst, n - 1, "%s", src);
	dst[n - 1] = '\0';
}

int UpdaterCore_ReadPending(const char *path, updaterPending_t *out) {
	wchar_t w[WPATH];
	char *buf, *cur, *line;

	memset(out, 0, sizeof(*out));
	if(!ToWide(path, w, WPATH) || !(buf = ReadAll(w))) {
		return 0;
	}
	cur = buf;
	while((line = NextLine(&cur)) != NULL) {
		char *val = SplitKV(line);

		if(!strcmp(line, "pid")) {
			out->pid = atoi(val);
		} else if(!strcmp(line, "install")) {
			CopyStr(out->install, sizeof(out->install), val);
		} else if(!strcmp(line, "staging")) {
			CopyStr(out->staging, sizeof(out->staging), val);
		} else if(!strcmp(line, "tag")) {
			CopyStr(out->tag, sizeof(out->tag), val);
		} else if(!strcmp(line, "from")) {
			CopyStr(out->from, sizeof(out->from), val);
		} else if(!strcmp(line, "relaunch")) {
			CopyStr(out->relaunch, sizeof(out->relaunch), val);
		} else if(!strcmp(line, "arg") && out->numArgs < UPDATER_MAX_ARGS) {
			CopyStr(out->args[out->numArgs], UPDATER_MAX_ARG_LEN, val);
			out->numArgs++;
		}
	}
	free(buf);
	return out->install[0] != '\0';
}

int UpdaterCore_WriteResult(const char *installDir, const char *status, const char *tag, const char *err) {
	wchar_t w[WPATH];
	char msg[512];
	FILE *f;
	int i;

	if(!BuildPathW(w, WPATH, installDir, "update", "result.txt")) {
		return 0;
	}
	MakeParentDirs(w);
	f = _wfopen(w, L"wb");
	if(!f) {
		return 0;
	}
	CopyStr(msg, sizeof(msg), err ? err : "");
	for(i = 0; msg[i]; i++) {
		if(msg[i] == '\r' || msg[i] == '\n') {
			msg[i] = ' ';
		}
	}
	fprintf(f, "status %s\r\n", status);
	fprintf(f, "tag %s\r\n", tag && tag[0] ? tag : "unknown");
	if(msg[0]) {
		fprintf(f, "error %s\r\n", msg);
	}
	return fclose(f) == 0;
}

/* A backup.new with a manifest is left by an install that didn't finish or
   couldn't fully undo itself, so it is the one to roll back from. */
static const char *BackupDirFor(const char *installDir) {
	wchar_t w[WPATH];

	if(BuildPathW(w, WPATH, installDir, BACKUP_NEW_DIR, "manifest.txt") && FileExistsW(w)) {
		return BACKUP_NEW_DIR;
	}
	return BACKUP_DIR;
}

int UpdaterCore_ReadManifestValue(const char *installDir, const char *key, char *out, int outSize) {
	wchar_t w[WPATH];
	char *buf, *cur, *line;
	int found = 0;

	out[0] = '\0';
	if(!BuildPathW(w, WPATH, installDir, BackupDirFor(installDir), "manifest.txt") || !(buf = ReadAll(w))) {
		return 0;
	}
	cur = buf;
	while(!found && (line = NextLine(&cur)) != NULL) {
		char *val = SplitKV(line);

		if(!strcmp(line, key)) {
			CopyStr(out, outSize, val);
			found = 1;
		}
	}
	free(buf);
	return found;
}

/* Copies src to <dst>.new, then renames that over dst, so dst is always a
   whole old or new file. Returns 0 on success, else the Win32 error. */
static unsigned long CopyReplaceW(const wchar_t *src, const wchar_t *dst) {
	wchar_t tmp[WPATH];
	unsigned long e;

	if(_snwprintf(tmp, WPATH - 1, L"%s.new", dst) < 0) {
		return ERROR_FILENAME_EXCED_RANGE;
	}
	tmp[WPATH - 1] = 0;
	SetFileAttributesW(tmp, FILE_ATTRIBUTE_NORMAL);
	if(!CopyFileW(src, tmp, FALSE)) {
		return GetLastError();
	}
	SetFileAttributesW(tmp, FILE_ATTRIBUTE_NORMAL);
	if(FileExistsW(dst)) {
		SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
	}
	if(!MoveFileExW(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		e = GetLastError();
		DeleteFileW(tmp);
		return e;
	}
	return 0;
}

/* Puts the copy of rel kept in the backup dir bak back into the install. */
static int RestoreOne(const char *install, const char *bak, const char *rel, char *err, int errSize) {
	wchar_t src[WPATH], dst[WPATH];
	unsigned long e;

	if(!BuildPathW(src, WPATH, install, bak, rel) || !BuildPathW(dst, WPATH, install, NULL, rel)) {
		Err(err, errSize, "bad path in backup: %s", rel, 0);
		return 0;
	}
	if((e = CopyReplaceW(src, dst)) != 0) {
		Err(err, errSize, "restore of %s failed (error %lu)", rel, e);
		return 0;
	}
	return 1;
}

/* Deletes an added file; returns 0 on success, else the Win32 error. */
static unsigned long RemoveAdded(const char *install, const char *rel) {
	wchar_t dst[WPATH];

	if(!BuildPathW(dst, WPATH, install, NULL, rel)) {
		return ERROR_BAD_PATHNAME;
	}
	if(!FileExistsW(dst)) {
		return 0;
	}
	SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
	if(!DeleteFileW(dst)) {
		return GetLastError();
	}
	return 0;
}

/* True if the install-relative path names the running executable. */
static int IsSelf(const char *install, const char *rel) {
	wchar_t dst[WPATH], full[WPATH], self[WPATH], selfFull[WPATH];

	if(!BuildPathW(dst, WPATH, install, NULL, rel) || !GetModuleFileNameW(NULL, self, WPATH)) {
		return 0;
	}
	if(!GetFullPathNameW(dst, WPATH, full, NULL) || !GetFullPathNameW(self, WPATH, selfFull, NULL)) {
		return 0;
	}
	return _wcsicmp(full, selfFull) == 0;
}

static void ErrAppend(char *err, int errSize, const char *msg) {
	int len;

	if(!err || errSize <= 0) {
		return;
	}
	len = (int)strlen(err);
	_snprintf(err + len, errSize - 1 - len, "; %s", msg);
	err[errSize - 1] = '\0';
}

static int WriteManifest(const updaterPending_t *p, const updaterList_t *l) {
	wchar_t w[WPATH];
	FILE *f;
	int i;

	if(!BuildPathW(w, WPATH, p->install, BACKUP_NEW_DIR, "manifest.txt")) {
		return 0;
	}
	MakeParentDirs(w);
	f = _wfopen(w, L"wb");
	if(!f) {
		return 0;
	}
	fprintf(f, "tag %s\r\n", p->from[0] ? p->from : "unknown");
	fprintf(f, "rolledfrom %s\r\n", p->tag);
	for(i = 0; i < l->count; i++) {
		fprintf(f, "%s %s\r\n", l->e[i].existed ? "file" : "added", l->e[i].rel);
	}
	return fclose(f) == 0;
}

/* Swaps backup.new in as the one-version backup once an install succeeded.
   If the rename fails, backup.new keeps its manifest and still wins. */
static void PromoteBackup(const char *install) {
	wchar_t bak[WPATH], bakNew[WPATH];

	if(!BuildPathW(bak, WPATH, install, BACKUP_DIR, NULL) ||
	   !BuildPathW(bakNew, WPATH, install, BACKUP_NEW_DIR, NULL)) {
		return;
	}
	DeleteTreeW(bak);
	MoveFileExW(bakNew, bak, 0);
}

int UpdaterCore_Apply(const updaterPending_t *p, char *err, int errSize) {
	wchar_t stagingW[WPATH], backupW[WPATH];
	updaterList_t l = {0};
	int i, ok = 0, restored = 1;

	if(!p->staging[0] || !p->install[0]) {
		Err(err, errSize, "pending file lacks install/staging paths%s", "", 0);
		return 0;
	}
	if(!BuildPathW(stagingW, WPATH, p->staging, NULL, NULL) ||
	   !BuildPathW(backupW, WPATH, p->install, BACKUP_NEW_DIR, NULL)) {
		Err(err, errSize, "path too long: %s", p->staging, 0);
		return 0;
	}
	if(!ListDir(stagingW, "", &l, err, errSize)) {
		goto done;
	}
	if(l.count == 0) {
		Err(err, errSize, "staging directory is empty: %s", p->staging, 0);
		goto done;
	}

	/* Back up everything that will be overwritten into a fresh backup.new;
	   the previous good backup stays untouched until the install succeeds. */
	DeleteTreeW(backupW);
	if(!MakeDirsUtf8(p->install)) {
		Err(err, errSize, "bad install path: %s", p->install, 0);
		goto done;
	}
	for(i = 0; i < l.count; i++) {
		wchar_t dst[WPATH], bak[WPATH];

		if(!SafeRel(l.e[i].rel)) {
			Err(err, errSize, "unsafe path in update: %s", l.e[i].rel, 0);
			goto done;
		}
		if(!BuildPathW(dst, WPATH, p->install, NULL, l.e[i].rel) ||
		   !BuildPathW(bak, WPATH, p->install, BACKUP_NEW_DIR, l.e[i].rel)) {
			Err(err, errSize, "path too long: %s", l.e[i].rel, 0);
			goto done;
		}
		l.e[i].existed = FileExistsW(dst);
		if(l.e[i].existed) {
			MakeParentDirs(bak);
			if(!CopyFileW(dst, bak, FALSE)) {
				Err(err, errSize, "cannot back up %s (error %lu)", l.e[i].rel, GetLastError());
				goto done;
			}
		}
	}
	if(!WriteManifest(p, &l)) {
		Err(err, errSize, "cannot write backup manifest under %s", p->install, 0);
		goto done;
	}

	for(i = 0; i < l.count; i++) {
		wchar_t src[WPATH], dst[WPATH];
		unsigned long e;

		if(!BuildPathW(src, WPATH, p->staging, NULL, l.e[i].rel) ||
		   !BuildPathW(dst, WPATH, p->install, NULL, l.e[i].rel)) {
			Err(err, errSize, "path too long: %s", l.e[i].rel, 0);
			goto done;
		}
		MakeParentDirs(dst);
		/* a failed swap leaves dst as it was, so it needs no undo */
		if((e = CopyReplaceW(src, dst)) != 0) {
			Err(err, errSize, "cannot replace %s (error %lu)", l.e[i].rel, e);
			goto done;
		}
		l.e[i].touched = 1;
	}
	ok = 1;

done:
	if(ok) {
		PromoteBackup(p->install);
	} else {
		char msg[512];

		/* Undo whatever was touched: restore replaced files, delete new ones. */
		for(i = 0; i < l.count; i++) {
			if(!l.e[i].touched) {
				continue;
			}
			if(l.e[i].existed) {
				if(!RestoreOne(p->install, BACKUP_NEW_DIR, l.e[i].rel, msg, sizeof(msg))) {
					ErrAppend(err, errSize, msg);
					restored = 0;
				}
			} else {
				unsigned long e = RemoveAdded(p->install, l.e[i].rel);

				if(e) {
					_snprintf(msg, sizeof(msg) - 1, "removal of %s failed (error %lu)", l.e[i].rel, e);
					msg[sizeof(msg) - 1] = '\0';
					ErrAppend(err, errSize, msg);
					restored = 0;
				}
			}
		}
		/* Fully undone: the install is the old version again, so the previous
		   backup is still the right one. Otherwise keep backup.new for rollback. */
		if(restored) {
			DeleteTreeW(backupW);
		}
	}
	free(l.e);
	return ok;
}

int UpdaterCore_Rollback(const char *installDir, char *err, int errSize) {
	wchar_t mw[WPATH], bakW[WPATH];
	const char *bak = BackupDirFor(installDir);
	char *buf, *cur, *line;
	int ok = 1, any = 0;

	if(!BuildPathW(mw, WPATH, installDir, bak, "manifest.txt") || !(buf = ReadAll(mw))) {
		Err(err, errSize, "no backup manifest under %s (error %lu)", installDir, GetLastError());
		return 0;
	}
	cur = buf;
	while((line = NextLine(&cur)) != NULL) {
		char *val = SplitKV(line);
		unsigned long rc;

		if(strcmp(line, "file") && strcmp(line, "added")) {
			continue;
		}
		any = 1;
		if(!SafeRel(val)) {
			if(ok) {
				Err(err, errSize, "unsafe path in manifest: %s", val, 0);
			}
			ok = 0;
			continue;
		}
		/* The running updater can neither overwrite nor delete itself. */
		if(IsSelf(installDir, val)) {
			continue;
		}
		if(!strcmp(line, "file")) {
			if(!RestoreOne(installDir, bak, val, ok ? err : NULL, errSize)) {
				ok = 0;
			}
		} else if((rc = RemoveAdded(installDir, val)) != 0) {
			if(ok) {
				Err(err, errSize, "cannot remove %s (error %lu)", val, rc);
			}
			ok = 0;
		}
	}
	free(buf);
	if(!any && ok) {
		Err(err, errSize, "backup manifest lists no files: %s", installDir, 0);
		return 0;
	}
	/* backup.new only covered the interrupted install; update/backup remains */
	if(ok && !strcmp(bak, BACKUP_NEW_DIR) && BuildPathW(bakW, WPATH, installDir, BACKUP_NEW_DIR, NULL)) {
		DeleteTreeW(bakW);
	}
	return ok;
}
