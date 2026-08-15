#include "server.h"
#include "sv_wtvdemo.h"
#include "sv_wtvdiscord.h"
#include <curl/curl.h>
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#endif

// Discards the response body — curl writes it to stdout by default if no
// write callback is set, which we don't want in a server log.
static size_t WTV_DiscordDiscardResponse( char *ptr, size_t size, size_t nmemb, void *userdata ) {
	(void)ptr;
	(void)userdata;
	return size * nmemb;
}

static void WTV_DiscordRetrySleep( int seconds ) {
#ifdef _WIN32
	Sleep( seconds * 1000 ); // Windows Sleep() takes milliseconds
#else
	sleep( seconds );
#endif
}

// Escapes a string for embedding as a JSON string value (quotes, backslashes,
// newline/CR/tab, other control characters dropped).
static void WTV_JSONEscape( const char *in, char *out, int outSize ) {
	int outLen = 0;
	const unsigned char *p = (const unsigned char *)in;

	while ( *p && outLen < outSize - 1 ) {
		unsigned char c = *p;
		const char *esc = NULL;

		switch ( c ) {
			case '"': esc = "\\\""; break;
			case '\\': esc = "\\\\"; break;
			case '\n': esc = "\\n"; break;
			case '\r': esc = "\\r"; break;
			case '\t': esc = "\\t"; break;
			default: break;
		}

		if ( esc ) {
			int escLen = (int)strlen( esc );
			if ( outLen + escLen > outSize - 1 ) {
				break; // would overflow out — stop here rather than write a truncated escape sequence
			}
			Com_Memcpy( out + outLen, esc, escLen );
			outLen += escLen;
		} else if ( c < 0x20 ) {
			// drop other control characters rather than \u-escape them — none
			// should occur in sanitized player-name-derived text anyway
		} else {
			out[outLen++] = (char)c;
		}
		p++;
	}
	out[outLen] = '\0';
}

// Uploads one file as a Discord webhook attachment, retrying up to retryCount
// times. discordScoreboard (may be NULL/empty) becomes the message content.
static qboolean WTV_DiscordUploadFile( const char *filePath, const char *webhookURL, int retryCount, int retryDelaySeconds, const char *discordScoreboard ) {
	int attempt;

	if ( retryCount < 1 ) {
		retryCount = 1;
	}

	for ( attempt = 0; attempt < retryCount; attempt++ ) {
		CURL *curl;
		CURLcode res;
		curl_mime *mime;
		curl_mimepart *part;
		const char *baseName;
		long httpCode = 0;

		curl = curl_easy_init();
		if ( !curl ) {
			Com_Printf( "WTV Discord: curl_easy_init failed\n" );
			return qfalse;
		}

		baseName = strrchr( filePath, '/' );
#ifdef _WIN32
		{
			const char *baseNameWin = strrchr( filePath, '\\' );
			if ( baseNameWin && ( !baseName || baseNameWin > baseName ) ) {
				baseName = baseNameWin;
			}
		}
#endif
		baseName = baseName ? baseName + 1 : filePath;

		mime = curl_mime_init( curl );
		if ( !mime ) {
			Com_Printf( "WTV Discord: curl_mime_init failed\n" );
			curl_easy_cleanup( curl );
			return qfalse;
		}

		// Must be added before the file part, or Discord renders the
		// attachment above the message content instead of below it.
		if ( discordScoreboard && discordScoreboard[0] ) {
			char escaped[2 * WTV_DISCORD_SCOREBOARD_MAX];
			char json[2 * WTV_DISCORD_SCOREBOARD_MAX + 64];
			curl_mimepart *jsonPart;

			WTV_JSONEscape( discordScoreboard, escaped, sizeof( escaped ) );
			Com_sprintf( json, sizeof( json ), "{\"content\":\"```\\n%s\\n```\"}", escaped );

			jsonPart = curl_mime_addpart( mime );
			if ( jsonPart ) {
				curl_mime_data( jsonPart, json, CURL_ZERO_TERMINATED );
				curl_mime_name( jsonPart, "payload_json" );
			} else {
				Com_Printf( "WTV Discord: curl_mime_addpart (payload_json) failed — uploading %s without the scoreboard\n", filePath );
			}
		}

		part = curl_mime_addpart( mime );
		if ( !part ) {
			Com_Printf( "WTV Discord: curl_mime_addpart failed\n" );
			curl_mime_free( mime );
			curl_easy_cleanup( curl );
			return qfalse;
		}

		curl_mime_filedata( part, filePath ); // curl streams straight from disk, no manual fopen/fread buffering
		curl_mime_name( part, "files[0]" ); // Discord's documented multipart file field format (files[n])
		curl_mime_filename( part, baseName );

		curl_easy_setopt( curl, CURLOPT_URL, webhookURL );
		curl_easy_setopt( curl, CURLOPT_MIMEPOST, mime );
		curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 1L );
		curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, WTV_DiscordDiscardResponse );
		curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT, 30L );
		curl_easy_setopt( curl, CURLOPT_LOW_SPEED_LIMIT, 1000L ); // bytes/sec
		curl_easy_setopt( curl, CURLOPT_LOW_SPEED_TIME, 60L );    // seconds below the limit before aborting
		// No CURLOPT_SSL_VERIFYPEER/CURLOPT_USE_SSL overrides — curl's own
		// default (verify peer, TLS via the URL scheme) is what's wanted here.

		res = curl_easy_perform( curl );
		if ( res == CURLE_OK ) {
			curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &httpCode );
		}

		curl_mime_free( mime );
		curl_easy_cleanup( curl );

		if ( res == CURLE_OK && httpCode >= 200 && httpCode < 300 ) {
			Com_Printf( "WTV Discord: uploaded %s\n", filePath );
			return qtrue;
		}

		if ( res != CURLE_OK ) {
			Com_Printf( "WTV Discord: upload of %s failed: %s\n", filePath, curl_easy_strerror( res ) );
		} else {
			Com_Printf( "WTV Discord: upload of %s failed: HTTP %ld\n", filePath, httpCode );
		}

		if ( attempt + 1 < retryCount ) {
			Com_Printf( "WTV Discord: retrying %s...\n", filePath );
			WTV_DiscordRetrySleep( retryDelaySeconds );
		}
	}

	Com_Printf( "WTV Discord: giving up on %s after %i attempt(s)\n", filePath, retryCount );
	return qfalse;
}

void WTV_DiscordUploadRound( const char *finalBasePath, int fragmentCount, const char *discordScoreboard ) {
	char webhookURL[1024];
	char varRetryCount[10];
	char varRetryDelay[10];
	int retryCount;
	int retryDelay;
	int partNumber;
	int uploadedCount = 0;

	Cvar_VariableStringBuffer( "g_wtvDiscordWebhookURL", webhookURL, sizeof( webhookURL ) );
	if ( !webhookURL[0] ) {
		return; // Discord upload not configured — recording-only is a fully supported mode
	}

	Cvar_VariableStringBuffer( "g_wtvDiscordRetryCount", varRetryCount, sizeof( varRetryCount ) );
	Cvar_VariableStringBuffer( "g_wtvDiscordRetryDelay", varRetryDelay, sizeof( varRetryDelay ) );
	retryCount = atoi( varRetryCount );
	retryDelay = atoi( varRetryDelay );
	if ( retryDelay < 0 ) {
		retryDelay = 0;
	}

	for ( partNumber = 1; partNumber <= fragmentCount; partNumber++ ) {
		char filePath[MAX_OSPATH + 16];
		// Scoreboard is per-round, not per-fragment — only attach it to the
		// first message, or a multi-fragment round would repeat it N times.
		const char *scoreboardForThisFragment = ( partNumber == 1 ) ? discordScoreboard : NULL;

		WTV_BuildFragmentPath( finalBasePath, partNumber, filePath, sizeof( filePath ) );
		if ( WTV_DiscordUploadFile( filePath, webhookURL, retryCount, retryDelay, scoreboardForThisFragment ) ) {
			uploadedCount++;
		}
	}

	Com_Printf( "WTV Discord: uploaded %i/%i fragment(s) for %s\n", uploadedCount, fragmentCount, finalBasePath );
}
