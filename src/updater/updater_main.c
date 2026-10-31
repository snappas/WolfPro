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

// wolfpro_updater: applies or rolls back a staged client update, then
// relaunches the game. Results go to update/result.txt; only a manual
// --rollback (no pending file) also shows a message box.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#include "updater_core.h"
#include "../client/cl_update_logic.h"

#define WAIT_TIMEOUT_MS 60000

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

typedef BOOL(WINAPI *setDefaultDllDirectories_t)(DWORD flags);

// Keeps DLLs planted next to the exe from loading. Looked up at runtime
// since Windows 7 without KB2533623 lacks it.
static void RestrictDllSearch(void) {
	HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
	setDefaultDllDirectories_t fn;

	if(!k32) {
		return;
	}
	fn = (setDefaultDllDirectories_t)(void *)GetProcAddress(k32, "SetDefaultDllDirectories");
	if(fn) {
		fn(LOAD_LIBRARY_SEARCH_SYSTEM32);
	}
}

/* Returns 1 once the process is gone (or pid is 0), 0 on timeout/failure. */
static int WaitForGame(int pid, char *err, int errSize) {
	HANDLE h;
	DWORD r;

	if(pid <= 0) {
		return 1;
	}
	h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
	if(!h) {
		DWORD e = GetLastError();

		if(e == ERROR_INVALID_PARAMETER) {
			return 1; /* already exited */
		}
		_snprintf(err, errSize - 1, "cannot wait for process %d (error %lu)", pid, e);
		err[errSize - 1] = '\0';
		return 0;
	}
	r = WaitForSingleObject(h, WAIT_TIMEOUT_MS);
	CloseHandle(h);
	if(r != WAIT_OBJECT_0) {
		_snprintf(err, errSize - 1, "timed out waiting for process %d to exit", pid);
		err[errSize - 1] = '\0';
		return 0;
	}
	return 1;
}

static void Relaunch(const updaterPending_t *p) {
	const char *argv[UPDATER_MAX_ARGS + 1];
	char cmd[UPDATER_MAX_CMDLINE];
	wchar_t exeW[2048], cmdW[UPDATER_MAX_CMDLINE], dirW[2048];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	int i, argc = 0;

	if(!p->relaunch[0]) {
		return;
	}
	argv[argc++] = p->relaunch;
	for(i = 0; i < p->numArgs; i++) {
		argv[argc++] = p->args[i];
	}
	if(!Updater_BuildCommandLine(cmd, sizeof(cmd), argv, argc)) {
		return; /* truncated command line: do not launch */
	}
	if(!MultiByteToWideChar(CP_UTF8, 0, p->relaunch, -1, exeW, 2048) ||
	   !MultiByteToWideChar(CP_UTF8, 0, cmd, -1, cmdW, UPDATER_MAX_CMDLINE) ||
	   !MultiByteToWideChar(CP_UTF8, 0, p->install, -1, dirW, 2048)) {
		return;
	}
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	if(CreateProcessW(exeW, cmdW, NULL, NULL, FALSE, 0, NULL, dirW, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}

/* Our own directory, minus `up` trailing components. */
static void DirOfSelf(char *out, int n, int up) {
	wchar_t w[2048];
	wchar_t *slash;

	out[0] = '\0';
	if(!GetModuleFileNameW(NULL, w, 2048)) {
		return;
	}
	for(up++; up > 0; up--) {
		slash = wcsrchr(w, L'\\');
		if(!slash) {
			break;
		}
		*slash = 0;
	}
	WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
}

static void ShowResult(int ok, const char *tag, const char *err) {
	char msg[768];
	wchar_t msgW[768];

	if(ok) {
		_snprintf(msg, sizeof(msg) - 1, "Rolled back from %s to the previous WolfPro version.", tag);
	} else {
		_snprintf(msg, sizeof(msg) - 1, "Rollback failed: %s", err);
	}
	msg[sizeof(msg) - 1] = '\0';
	if(!MultiByteToWideChar(CP_UTF8, 0, msg, -1, msgW, 768)) {
		wcscpy(msgW, ok ? L"Rolled back to the previous WolfPro version." : L"Rollback failed.");
	}
	MessageBoxW(NULL, msgW, L"WolfPro Updater", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmdLine, int show) {
	int argc, rollback;
	wchar_t **argvW;
	char pendingPath[UPDATER_MAX_PATH] = "";
	char err[512] = "", tag[UPDATER_MAX_TAG];
	updaterPending_t p;
	int havePending = 0, ok, waited = 1;

	RestrictDllSearch();
	(void)hInst;
	(void)hPrev;
	(void)cmdLine;
	(void)show;

	// lets the game tell a running install from an interrupted one
	CreateMutexW(NULL, FALSE, UPDATER_MUTEX_NAME);

	argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
	if(!argvW || argc < 2) {
		return 2;
	}
	if(!wcscmp(argvW[1], L"--apply")) {
		rollback = 0;
	} else if(!wcscmp(argvW[1], L"--rollback")) {
		rollback = 1;
	} else {
		return 2;
	}
	if(argc > 2) {
		if(!WideCharToMultiByte(CP_UTF8, 0, argvW[2], -1, pendingPath, sizeof(pendingPath), NULL, NULL)) {
			return 2; /* path too long or unconvertible */
		}
	}
	LocalFree(argvW);

	memset(&p, 0, sizeof(p));
	if(pendingPath[0]) {
		havePending = UpdaterCore_ReadPending(pendingPath, &p);
		if(!havePending) {
			/* Apply runs from <install>\update\staging, rollback from <install>. */
			DirOfSelf(p.install, sizeof(p.install), rollback ? 0 : 2);
			_snprintf(err, sizeof(err) - 1, "cannot read pending file %s", pendingPath);
			UpdaterCore_WriteResult(p.install, "failed", "unknown", err);
			return 1;
		}
	} else if(rollback) {
		DirOfSelf(p.install, sizeof(p.install), 0);
	} else {
		return 2;
	}

	if(havePending) {
		waited = WaitForGame(p.pid, err, sizeof(err));
	}
	if(!waited) {
		/* The game is still running: touching its files would be unsafe. */
		UpdaterCore_WriteResult(p.install, "failed", p.tag, err);
		return 1;
	}

	if(rollback) {
		/* read first: rolling back from backup.new deletes it */
		if(!UpdaterCore_ReadManifestValue(p.install, "rolledfrom", tag, sizeof(tag)) || !tag[0]) {
			strcpy(tag, "unknown");
		}
		ok = UpdaterCore_Rollback(p.install, err, sizeof(err));
		if(ok) {
			UpdaterCore_WriteResult(p.install, "rolledback", tag, "");
		} else {
			UpdaterCore_WriteResult(p.install, "failed", tag, err);
		}
		if(!havePending) {
			ShowResult(ok, tag, err);
		}
	} else {
		ok = UpdaterCore_Apply(&p, err, sizeof(err));
		if(ok) {
			UpdaterCore_WriteResult(p.install, "ok", p.tag, "");
		} else {
			UpdaterCore_WriteResult(p.install, "failed", p.tag, err);
		}
	}

	if(havePending) {
		wchar_t pw[2048];

		if(MultiByteToWideChar(CP_UTF8, 0, pendingPath, -1, pw, 2048)) {
			DeleteFileW(pw);
		}
		Relaunch(&p);
	}
	return ok ? 0 : 1;
}
