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

// Win32 helpers for the client auto-updater (hashing, paths, launching).

#include "../client/client.h"
#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include "../updater/updater_core.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

#define UPDATER_WPATH 1024

static qboolean Sys_UpdateWide(const char *in, wchar_t *out, int outChars) {
	if(!in || outChars <= 0) {
		return qfalse;
	}
	return MultiByteToWideChar(CP_UTF8, 0, in, -1, out, outChars) > 0 ? qtrue : qfalse;
}

FILE *Sys_UpdateFOpen(const char *path, const char *mode) {
	wchar_t wpath[UPDATER_WPATH], wmode[16];

	if(!Sys_UpdateWide(path, wpath, UPDATER_WPATH) || !Sys_UpdateWide(mode, wmode, 16)) {
		return NULL;
	}
	return _wfopen(wpath, wmode);
}

qboolean Sys_UpdateRemoveFile(const char *path) {
	wchar_t wpath[UPDATER_WPATH];

	if(!Sys_UpdateWide(path, wpath, UPDATER_WPATH)) {
		return qfalse;
	}
	return DeleteFileW(wpath) ? qtrue : qfalse;
}

qboolean Sys_UpdateSHA256File(const char *path, char hexOut[65]) {
	static const char hex[] = "0123456789abcdef";
	BCRYPT_ALG_HANDLE alg = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	unsigned char digest[32];
	unsigned char *chunk;
	qboolean ok = qfalse;
	FILE *f;
	size_t n;
	int i;

	hexOut[0] = '\0';
	f = Sys_UpdateFOpen(path, "rb");
	if(!f) {
		return qfalse;
	}
	chunk = (unsigned char *)malloc(65536);
	if(!chunk) {
		fclose(f);
		return qfalse;
	}
	if(!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0))) {
		goto done;
	}
	if(!NT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
		goto done;
	}
	while((n = fread(chunk, 1, 65536, f)) > 0) {
		if(!NT_SUCCESS(BCryptHashData(hash, chunk, (ULONG)n, 0))) {
			goto done;
		}
	}
	if(ferror(f)) {
		goto done;
	}
	if(!NT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
		goto done;
	}
	for(i = 0; i < 32; i++) {
		hexOut[i * 2] = hex[digest[i] >> 4];
		hexOut[i * 2 + 1] = hex[digest[i] & 15];
	}
	hexOut[64] = '\0';
	ok = qtrue;

done:
	if(hash) {
		BCryptDestroyHash(hash);
	}
	if(alg) {
		BCryptCloseAlgorithmProvider(alg, 0);
	}
	free(chunk);
	fclose(f);
	return ok;
}

// Real ACL check, used when UAC file virtualization would make a write test
// succeed against VirtualStore instead of the actual directory.
static qboolean Sys_UpdateAccessCheck(const wchar_t *wdir) {
	GENERIC_MAPPING mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
	PRIVILEGE_SET privileges;
	DWORD privLen = sizeof(privileges), needed = 0, granted = 0;
	DWORD desired = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | DELETE;
	BOOL status = FALSE;
	HANDLE token = NULL, imp = NULL;
	PSECURITY_DESCRIPTOR sd = NULL;
	SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

	GetFileSecurityW(wdir, si, NULL, 0, &needed);
	if(!needed) {
		return qfalse;
	}
	sd = (PSECURITY_DESCRIPTOR)malloc(needed);
	if(!sd) {
		return qfalse;
	}
	if(GetFileSecurityW(wdir, si, sd, needed, &needed) &&
	   OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &token) &&
	   DuplicateToken(token, SecurityImpersonation, &imp)) {
		MapGenericMask(&desired, &mapping);
		if(!AccessCheck(sd, imp, desired, &mapping, &privileges, &privLen, &granted, &status)) {
			status = FALSE;
		}
	}
	if(imp) {
		CloseHandle(imp);
	}
	if(token) {
		CloseHandle(token);
	}
	free(sd);
	return status ? qtrue : qfalse;
}

static qboolean Sys_UpdateVirtualized(void) {
	HANDLE token;
	DWORD virt = 0, len = 0;

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return qfalse;
	}
	if(!GetTokenInformation(token, TokenVirtualizationEnabled, &virt, sizeof(virt), &len)) {
		virt = 0;
	}
	CloseHandle(token);
	return virt ? qtrue : qfalse;
}

qboolean Sys_UpdateDirWritable(const char *dir) {
	wchar_t wdir[UPDATER_WPATH], wfile[UPDATER_WPATH];
	HANDLE h;

	if(!Sys_UpdateWide(dir, wdir, UPDATER_WPATH)) {
		return qfalse;
	}
	if(Sys_UpdateVirtualized()) {
		return Sys_UpdateAccessCheck(wdir);
	}
	if(_snwprintf(wfile, UPDATER_WPATH, L"%s\\.wolfpro_write_test_%lu", wdir, GetCurrentProcessId()) < 0) {
		return qfalse;
	}
	wfile[UPDATER_WPATH - 1] = L'\0';
	h = CreateFileW(wfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
					FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if(h == INVALID_HANDLE_VALUE) {
		return qfalse;
	}
	CloseHandle(h);
	return qtrue;
}

// CreateProcessW never elevates: an exe that demands admin fails with
// ERROR_ELEVATION_REQUIRED instead of raising a UAC prompt.
qboolean Sys_UpdateLaunch(const char *exe, const char *cmdline, char *err, int errSize) {
	wchar_t wexe[UPDATER_WPATH], wdir[UPDATER_WPATH];
	wchar_t *wcmd, *slash;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	int argChars = 0, exeChars;
	DWORD e;
	BOOL ok;

	if(!Sys_UpdateWide(exe, wexe, UPDATER_WPATH)) {
		Com_sprintf(err, errSize, "couldn't start %s: path too long", exe);
		return qfalse;
	}
	if(cmdline && cmdline[0]) {
		argChars = MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, NULL, 0);
		if(argChars <= 0) {
			Com_sprintf(err, errSize, "couldn't start %s: bad command line", exe);
			return qfalse;
		}
	}
	// "exe" args; Windows paths can't contain quotes
	exeChars = (int)wcslen(wexe);
	wcmd = (wchar_t *)malloc((exeChars + 4 + argChars) * sizeof(wchar_t));
	if(!wcmd) {
		Com_sprintf(err, errSize, "couldn't start %s: out of memory", exe);
		return qfalse;
	}
	wcmd[0] = L'"';
	memcpy(wcmd + 1, wexe, exeChars * sizeof(wchar_t));
	wcmd[exeChars + 1] = L'"';
	wcmd[exeChars + 2] = L'\0';
	if(argChars > 0) {
		wcmd[exeChars + 2] = L' ';
		MultiByteToWideChar(CP_UTF8, 0, cmdline, -1, wcmd + exeChars + 3, argChars);
	}
	wcscpy(wdir, wexe);
	slash = wcsrchr(wdir, L'\\');
	if(slash) {
		*slash = L'\0';
	}

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	memset(&pi, 0, sizeof(pi));
	ok = CreateProcessW(wexe, wcmd, NULL, NULL, FALSE, 0, NULL, slash ? wdir : NULL, &si, &pi);
	e = ok ? 0 : GetLastError();
	free(wcmd);
	if(!ok) {
		if(e == ERROR_ELEVATION_REQUIRED) {
			Com_sprintf(err, errSize, "couldn't start %s: it asks for administrator rights (error %lu)", exe, e);
		} else {
			Com_sprintf(err, errSize, "couldn't start %s (error %lu)", exe, e);
		}
		return qfalse;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return qtrue;
}

// qtrue if a process with this pid is still running.
qboolean Sys_UpdatePidRunning(int pid) {
	HANDLE h;
	DWORD code = 0;
	BOOL ok;

	if(pid <= 0) {
		return qfalse;
	}
	h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
	if(!h) {
		// access denied means it exists but belongs to someone else
		return GetLastError() == ERROR_ACCESS_DENIED ? qtrue : qfalse;
	}
	ok = GetExitCodeProcess(h, &code);
	CloseHandle(h);
	return ok && code == STILL_ACTIVE ? qtrue : qfalse;
}

void Sys_UpdateExePath(char *out, int size) {
	wchar_t wpath[UPDATER_WPATH];
	DWORD n;

	if(size <= 0) {
		return;
	}
	out[0] = '\0';
	n = GetModuleFileNameW(NULL, wpath, UPDATER_WPATH);
	if(n == 0 || n >= UPDATER_WPATH) {
		return;
	}
	if(!WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out, size, NULL, NULL)) {
		out[0] = '\0';
	}
}

int Sys_UpdatePid(void) {
	return (int)GetCurrentProcessId();
}

qboolean Sys_UpdateMkdirs(const char *dir) {
	wchar_t w[UPDATER_WPATH], *p;
	DWORD attr;

	if(!Sys_UpdateWide(dir, w, UPDATER_WPATH)) {
		return qfalse;
	}
	for(p = w; *p; p++) {
		if(*p == L'/') {
			*p = L'\\';
		}
	}
	// failures on the drive or UNC share prefix are expected and harmless
	for(p = w + 1; *p; p++) {
		if(*p == L'\\' && p[-1] != L'\\') {
			*p = L'\0';
			CreateDirectoryW(w, NULL);
			*p = L'\\';
		}
	}
	CreateDirectoryW(w, NULL);
	attr = GetFileAttributesW(w);
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) ? qtrue : qfalse;
}

// Does not follow junctions or symlinks; the link itself is removed.
static qboolean Sys_UpdateRemoveTreeW(wchar_t *path, size_t cap) {
	WIN32_FIND_DATAW fd;
	HANDLE h;
	DWORD attr, e;
	size_t len, n;
	qboolean ok = qtrue;

	attr = GetFileAttributesW(path);
	if(attr == INVALID_FILE_ATTRIBUTES) {
		e = GetLastError();
		return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? qtrue : qfalse;
	}
	if(attr & FILE_ATTRIBUTE_READONLY) {
		SetFileAttributesW(path, attr & ~FILE_ATTRIBUTE_READONLY);
	}
	if(!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
		return DeleteFileW(path) ? qtrue : qfalse;
	}
	if(attr & FILE_ATTRIBUTE_REPARSE_POINT) {
		return RemoveDirectoryW(path) ? qtrue : qfalse;
	}
	len = wcslen(path);
	if(len + 3 >= cap) {
		return qfalse;
	}
	wcscpy(path + len, L"\\*");
	h = FindFirstFileW(path, &fd);
	path[len] = L'\0';
	if(h != INVALID_HANDLE_VALUE) {
		do {
			if(!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
				continue;
			}
			n = wcslen(fd.cFileName);
			if(len + 1 + n >= cap) {
				ok = qfalse;
				continue;
			}
			path[len] = L'\\';
			wcscpy(path + len + 1, fd.cFileName);
			if(!Sys_UpdateRemoveTreeW(path, cap)) {
				ok = qfalse;
			}
			path[len] = L'\0';
		} while(FindNextFileW(h, &fd));
		FindClose(h);
	}
	if(!RemoveDirectoryW(path)) {
		ok = qfalse;
	}
	return ok;
}

qboolean Sys_UpdateRemoveTree(const char *dir) {
	wchar_t w[UPDATER_WPATH];

	if(!Sys_UpdateWide(dir, w, UPDATER_WPATH)) {
		return qfalse;
	}
	return Sys_UpdateRemoveTreeW(w, UPDATER_WPATH);
}

// The updater holds this mutex for its whole run.
qboolean Sys_UpdateUpdaterRunning(void) {
	HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, UPDATER_MUTEX_NAME);

	if(!h) {
		return GetLastError() == ERROR_ACCESS_DENIED ? qtrue : qfalse;
	}
	CloseHandle(h);
	return qtrue;
}

// Arguments after argv[0] of the real (wide) process command line.
int Sys_UpdateArgCount(void) {
	wchar_t **argv;
	int argc = 0;

	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if(!argv) {
		return 0;
	}
	LocalFree(argv);
	return argc > 1 ? argc - 1 : 0;
}

qboolean Sys_UpdateArg(int index, char *out, int size) {
	wchar_t **argv;
	int argc = 0;
	qboolean ok = qfalse;

	if(size <= 0) {
		return qfalse;
	}
	out[0] = '\0';
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if(!argv) {
		return qfalse;
	}
	if(index >= 0 && index + 1 < argc) {
		ok = WideCharToMultiByte(CP_UTF8, 0, argv[index + 1], -1, out, size, NULL, NULL) > 0 ? qtrue : qfalse;
		if(!ok) {
			out[0] = '\0';
		}
	}
	LocalFree(argv);
	return ok;
}

// For libc-only consumers such as unzOpen: the ANSI path, or its 8.3 form
// when the long name has characters outside the code page.
qboolean Sys_UpdateAnsiPath(const char *path, char *out, int size) {
	wchar_t w[UPDATER_WPATH], s[UPDATER_WPATH];
	BOOL lossy = FALSE;
	DWORD n;

	if(size <= 0 || !Sys_UpdateWide(path, w, UPDATER_WPATH)) {
		return qfalse;
	}
	if(GetACP() == CP_UTF8) {
		if((int)strlen(path) >= size) {
			return qfalse;
		}
		strcpy(out, path);
		return qtrue;
	}
	if(WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, size, NULL, &lossy) > 0 && !lossy) {
		return qtrue;
	}
	n = GetShortPathNameW(w, s, UPDATER_WPATH);
	if(n == 0 || n >= UPDATER_WPATH) {
		return qfalse;
	}
	lossy = FALSE;
	if(WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, s, -1, out, size, NULL, &lossy) > 0 && !lossy) {
		return qtrue;
	}
	out[0] = '\0';
	return qfalse;
}
