#ifndef SV_WTVDISCORD_H
#define SV_WTVDISCORD_H

// Uploads every fragment of a finished round to the webhook configured via
// g_wtvDiscordWebhookURL; discordScoreboard is sent with fragment 1 only.
void WTV_DiscordUploadRound( const char *finalBasePath, int fragmentCount, const char *discordScoreboard );

#endif // SV_WTVDISCORD_H
