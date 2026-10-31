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

#include "cl_update_logic.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifndef UPDATER_NO_JSON
#include <jansson.h>
#endif

static void Updater_Copy(char *dst, int dstSize, const char *src) {
	int n;

	if(dstSize <= 0) {
		return;
	}
	if(!src) {
		src = "";
	}
	n = (int)strlen(src);
	if(n > dstSize - 1) {
		n = dstSize - 1;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static int Updater_IsHex(int c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int Updater_AllHex(const char *s, int n) {
	int i;

	for(i = 0; i < n; i++) {
		if(!Updater_IsHex((unsigned char)s[i])) {
			return 0;
		}
	}
	return 1;
}

#ifndef UPDATER_NO_JSON

static const char *Updater_JsonString(json_t *obj, const char *key) {
	json_t *v = json_object_get(obj, key);

	return json_is_string(v) ? json_string_value(v) : NULL;
}

int Updater_ParseRelease(const char *json, updaterRelease_t *out, char *notes, int notesSize) {
	json_t *root, *assets;
	const char *tag, *s;
	size_t i, n, len;

	if(!json || !out) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if(notes && notesSize > 0) {
		notes[0] = '\0';
	}

	root = json_loads(json, 0, NULL);
	if(!root) {
		return 0;
	}
	if(!json_is_object(root)) {
		json_decref(root);
		return 0;
	}
	tag = Updater_JsonString(root, "tag_name");
	if(!tag || !tag[0]) {
		json_decref(root);
		return 0;
	}
	Updater_Copy(out->tag, sizeof(out->tag), tag);

	s = Updater_JsonString(root, "target_commitish");
	if(s && strlen(s) == 40 && Updater_AllHex(s, 40)) {
		int k;
		for(k = 0; k < 7; k++) {
			out->commit[k] = (char)tolower((unsigned char)s[k]);
		}
		out->commit[7] = '\0';
	} else {
		len = strlen(out->tag);
		if(len >= 8 && out->tag[len - 8] == '-' && Updater_AllHex(out->tag + len - 7, 7)) {
			int k;
			for(k = 0; k < 7; k++) {
				out->commit[k] = (char)tolower((unsigned char)out->tag[len - 7 + k]);
			}
			out->commit[7] = '\0';
		}
	}

	s = Updater_JsonString(root, "published_at");
	out->publishedAt = s ? Updater_ParseIso8601(s) : -1;
	out->prerelease = json_is_true(json_object_get(root, "prerelease")) ? 1 : 0;
	out->draft = json_is_true(json_object_get(root, "draft")) ? 1 : 0;

	if(notes && notesSize > 0) {
		Updater_Copy(notes, notesSize, Updater_JsonString(root, "body"));
	}

	assets = json_object_get(root, "assets");
	if(json_is_array(assets)) {
		n = json_array_size(assets);
		for(i = 0; i < n && out->numAssets < UPDATER_MAX_ASSETS; i++) {
			json_t *a = json_array_get(assets, i);
			updaterAsset_t *dst;
			const char *name, *url, *digest;

			if(!json_is_object(a)) {
				continue;
			}
			name = Updater_JsonString(a, "name");
			url = Updater_JsonString(a, "browser_download_url");
			if(!name || !url) {
				continue;
			}
			dst = &out->assets[out->numAssets++];
			Updater_Copy(dst->name, sizeof(dst->name), name);
			Updater_Copy(dst->url, sizeof(dst->url), url);
			dst->size = json_is_integer(json_object_get(a, "size"))
							? (long long)json_integer_value(json_object_get(a, "size"))
							: 0;
			digest = Updater_JsonString(a, "digest");
			if(digest && strncmp(digest, "sha256:", 7) == 0 && strlen(digest + 7) == 64 && Updater_AllHex(digest + 7, 64)) {
				int k;
				for(k = 0; k < 64; k++) {
					dst->sha256[k] = (char)tolower((unsigned char)digest[7 + k]);
				}
				dst->sha256[64] = '\0';
			}
		}
	}

	json_decref(root);
	return 1;
}

#endif /* UPDATER_NO_JSON */

long long Updater_ParseIso8601(const char *s) {
	int y, mo, d, h, mi, sec, n = 0;
	long long era, yoe, doy, doe, days;
	long long yy;

	if(!s) {
		return -1;
	}
	if(sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6) {
		return -1;
	}
	if(n != 19 || s[n] != 'Z' || s[n + 1] != '\0') {
		return -1;
	}
	if(y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) {
		return -1;
	}

	/* days-from-civil */
	yy = (mo <= 2) ? y - 1 : y;
	era = yy / 400;
	yoe = yy - era * 400;
	doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	days = era * 146097 + doe - 719468;

	return days * 86400 + h * 3600 + mi * 60 + sec;
}

static int Updater_EqualNoCase(const char *a, const char *b) {
	while(*a && *b) {
		if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == *b;
}

/* Unknown or empty strings map to the local (never-update) channel. */
updaterChannel_t Updater_ChannelFromString(const char *s) {
	if(s) {
		if(Updater_EqualNoCase(s, "release")) {
			return UPDATER_CHANNEL_RELEASE;
		}
		if(Updater_EqualNoCase(s, "dev")) {
			return UPDATER_CHANNEL_DEV;
		}
	}
	return UPDATER_CHANNEL_LOCAL;
}

int Updater_ShouldOffer(const updaterRelease_t *latest, const char *installedCommit,
					updaterChannel_t channel, long long buildTime,
					const char *skippedTag, int force) {
	if(!latest || channel == UPDATER_CHANNEL_LOCAL) {
		return 0;
	}
	if(!latest->commit[0]) {
		return 0;
	}
	if(installedCommit && strncmp(installedCommit, latest->commit, 7) == 0) {
		return 0;
	}
	if(channel == UPDATER_CHANNEL_DEV && latest->publishedAt <= buildTime) {
		return 0;
	}
	if(!force && skippedTag && skippedTag[0] && strcmp(skippedTag, latest->tag) == 0) {
		return 0;
	}
	return 1;
}

static int Updater_HasPrefix(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int Updater_HasSuffix(const char *s, const char *suffix) {
	size_t ls = strlen(s), lx = strlen(suffix);

	return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

const updaterAsset_t *Updater_PickAsset(const updaterRelease_t *rel, int is64) {
	const char *prefix = is64 ? "windows-x64-" : "windows-x86-";
	int i;

	if(!rel) {
		return NULL;
	}
	for(i = 0; i < rel->numAssets; i++) {
		const updaterAsset_t *a = &rel->assets[i];

		if(Updater_HasPrefix(a->name, prefix) && Updater_HasSuffix(a->name, ".zip")) {
			return a;
		}
	}
	return NULL;
}

/* Reduces release-note markdown to plain ASCII text, bounded by outSize. */
void Updater_SanitizeNotes(const char *in, char *out, int outSize) {
	static const char empty[] = "No release notes for this version.";
	char *tmp;
	size_t o = 0, i, len;
	int lineStart = 1, blank, outLen;

	if(outSize <= 0) {
		return;
	}
	out[0] = '\0';
	len = in ? strlen(in) : 0;
	tmp = (char *)malloc(len + 1);
	if(!tmp) {
		return;
	}

	for(i = 0; i < len;) {
		unsigned char c = (unsigned char)in[i];

		if(c == '\r') {
			i++;
			if(i < len && in[i] == '\n') {
				i++;
			}
			tmp[o++] = '\n';
			lineStart = 1;
			continue;
		}
		if(c == '\n') {
			tmp[o++] = '\n';
			i++;
			lineStart = 1;
			continue;
		}
		if(lineStart && c == '#') {
			while(i < len && in[i] == '#') {
				i++;
			}
			while(i < len && in[i] == ' ') {
				i++;
			}
			lineStart = 0;
			continue;
		}
		lineStart = 0;
		if(c == '`') {
			i++;
			continue;
		}
		if((c == '*' || c == '_') && i + 1 < len && in[i + 1] == (char)c) {
			i += 2;
			continue;
		}
		if(c >= 0x80) {
			i++;
			while(i < len && ((unsigned char)in[i] & 0xC0) == 0x80) {
				i++;
			}
			tmp[o++] = '?';
			continue;
		}
		if(c == '\t') {
			c = ' ';
		}
		if(c < 0x20 || c == 0x7F) {
			i++;
			continue;
		}
		tmp[o++] = (char)c;
		i++;
	}
	tmp[o] = '\0';

	blank = 1;
	for(i = 0; i < o; i++) {
		if(!isspace((unsigned char)tmp[i])) {
			blank = 0;
			break;
		}
	}
	if(blank) {
		Updater_Copy(out, outSize, empty);
		free(tmp);
		return;
	}

	if(o > (size_t)(outSize - 1)) {
		outLen = outSize - 1;
		memcpy(out, tmp, outLen);
		out[outLen] = '\0';
		if(outLen >= 3) {
			memcpy(out + outLen - 3, "...", 3);
		}
	} else {
		memcpy(out, tmp, o + 1);
	}
	free(tmp);
}

int Updater_IsSafeZipPath(const char *path) {
	const char *p;

	if(!path || !path[0]) {
		return 0;
	}
	if(path[0] == '/' || path[0] == '\\') {
		return 0;
	}
	if(isalpha((unsigned char)path[0]) && path[1] == ':') {
		return 0;
	}
	p = path;
	while(*p) {
		const char *seg = p;
		size_t n;

		while(*p && *p != '/' && *p != '\\') {
			p++;
		}
		n = (size_t)(p - seg);
		if(n == 2 && seg[0] == '.' && seg[1] == '.') {
			return 0;
		}
		if(*p) {
			p++;
		}
	}
	return 1;
}

/* Returns 1 if the whole command line fit in out, 0 if truncated. */
int Updater_BuildCommandLine(char *out, int outSize, const char *const *argv, int argc) {
	int pos = 0, a;

#define UPDATER_PUT(ch) do { char c_ = (char)(ch); if(pos < outSize - 1) { out[pos] = c_; pos++; } else { pos = outSize; } } while(0)

	if(outSize <= 0) {
		return 0;
	}
	for(a = 0; a < argc; a++) {
		const char *s = argv[a] ? argv[a] : "";
		int quote = (s[0] == '\0') || strchr(s, ' ') || strchr(s, '\t') || strchr(s, '"');

		if(a > 0) {
			UPDATER_PUT(' ');
		}
		if(!quote) {
			while(*s) {
				UPDATER_PUT(*s++);
			}
			continue;
		}
		UPDATER_PUT('"');
		for(;;) {
			int bs = 0, k;

			while(*s == '\\') {
				bs++;
				s++;
			}
			if(*s == '\0') {
				for(k = 0; k < bs * 2; k++) {
					UPDATER_PUT('\\');
				}
				break;
			}
			if(*s == '"') {
				for(k = 0; k < bs * 2 + 1; k++) {
					UPDATER_PUT('\\');
				}
				UPDATER_PUT('"');
			} else {
				for(k = 0; k < bs; k++) {
					UPDATER_PUT('\\');
				}
				UPDATER_PUT(*s);
			}
			s++;
		}
		UPDATER_PUT('"');
	}
	out[pos < outSize - 1 ? pos : outSize - 1] = '\0';
	return pos < outSize;
#undef UPDATER_PUT
}
