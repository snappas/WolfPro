# WolfPro Cvar Reference

This is a reference for cvars a player or server admin would realistically
set — server administration, competitive/anti-cheat features, gameplay
rules, HUD/client configuration, and the full renderer/client tuning
surface (video, performance, netcode, input, sound, downloads).

It intentionally **excludes**: pure internal/engine-managed state
(`CVAR_ROM` bookkeeping like `sv_serverid`, `gamename`, pak-checksum
lists), legacy single-player-only leftovers, and dev/debug-only
instrumentation (renderer `CVAR_CHEAT` debug toggles like `r_showtris`,
profiler internals, native-botlib/`aicast_*` dev tools). A few debug-ish
cvars with real competitive use (`r_speeds`, `cl_shownet`-adjacent netcode
diagnostics) are kept and marked **[debug/advanced]**.

Flags shown are as passed to `Cvar_Get`/`trap_Cvar_Register` in source:
`ARCHIVE` (saved to config), `LATCH` (takes effect on `vid_restart`/map
change, not immediately — see the `CVAR_LATCH` invariant in
[CLAUDE.md](CLAUDE.md)), `CHEAT` (requires `sv_cheats 1`), `SERVERINFO`
/`SYSTEMINFO`/`USERINFO`/`WOLFINFO` (mirrored into the relevant info
string), `ROM`/`INIT`/`TEMP`/`NORESTART` (engine-managed, not normally
hand-set).

Where a cvar is registered from more than one module (common for cvars
the UI/client needs before `cgame`/`game` loads), the primary
authoritative registration is listed with a note about the other site.

## Contents

- [Server Administration](#server-administration)
- [Match/Round Control](#matchround-control)
- [Competitive & Anti-Cheat](#competitive--anti-cheat)
- [Gameplay Rules](#gameplay-rules)
- [Bot/AI](#botai)
- [HUD & Display](#hud--display)
- [Crosshair/Weapon](#crosshairweapon)
- [Demo Playback & Spectator](#demo-playback--spectator)
- [Stats Windows/Popups](#stats-windowspopups)
- [Sound](#sound)
- [Video/Display](#videodisplay)
- [Rendering Quality & Performance](#rendering-quality--performance)
- [Vulkan-specific](#vulkan-specific)
- [OpenGL-specific](#opengl-specific)
- [Netcode/Connection](#netcodeconnection)
- [Input](#input)
- [Downloads](#downloads)
- [Console/Client Misc](#consoleclient-misc)
- [Menu/UI](#menuui)

---

## Server Administration

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `sv_hostname` | `WolfHost` | SERVERINFO\|ARCHIVE | Server name shown in the browser/serverinfo. |
| `sv_maxclients` (`g_maxclients`) | `20` | SERVERINFO\|LATCH (ARCHIVE via `g_maxclients`) | Max client slots including bots/spectators; requires a map change/restart. |
| `sv_privateClients` | `0` | SERVERINFO | Slots reserved for players who know `sv_privatePassword`. |
| `sv_privatePassword` | `""` | TEMP | Password granting access to reserved private slots. |
| `rconPassword` | `""` | TEMP | Remote-console admin password; empty disables rcon. |
| `sv_maxclientsPerIP` | `3` | ARCHIVE | Caps simultaneous connections from one IP. |
| `sv_maxRate` | `0` | ARCHIVE | Server-enforced ceiling on a client's `rate` cvar; `0` = no cap. |
| `sv_minPing` / `sv_maxPing` | `0` / `0` | ARCHIVE | Reject/kick connections outside this ping band; `0` = disabled. |
| `sv_floodProtect` | `1` | ARCHIVE\|SERVERINFO | Throttles repeated client commands (chat/command spam protection). |
| `sv_pure` | `1` | SYSTEMINFO\|LATCH | Enforces pak-checksum matching between client/server. Set `sv_pure 0` to load unsigned/dev-built mod DLLs when testing local builds. |
| `sv_allowDownload` | `1` | ARCHIVE | Enables legacy UDP client downloads of missing paks. |
| `sv_dlURL` | `https://dl.rtcw.eu/maps/rtcw` | SERVERINFO\|ARCHIVE | Base URL advertised to clients for curl-based HTTP downloads. |
| `sv_dlRate` | `100` | ARCHIVE\|SERVERINFO | Advertised/throttled curl download rate in KB/s. |
| `sv_master1`…`sv_master5` | `wolfmaster.idsoftware.com` / `""`×4 | ARCHIVE (master1: none) | Master server addresses for heartbeat/listing. |
| `sv_reconnectlimit` | `3` | — | Limits rapid reconnect attempts per client. |
| `sv_lanForceRate` | `1` | ARCHIVE | Forces max rate for clients detected as LAN. |
| `sv_minUserCmdInterval` | `0` | ARCHIVE | Anti-cheat: minimum allowed spacing between client usercmds. |
| `sv_timeout` | `240` | TEMP | Seconds of no client packets before timeout/drop. |
| `sv_zombietime` | `2` | TEMP | Seconds a disconnected client slot lingers before reuse. |
| `sv_GameConfig` | `""` | SERVERINFO\|ARCHIVE\|ROM | Identifies the active competitive ruleset/config preset. |
| `sv_levelTimeReset` | `0` | ARCHIVE | Controls whether level time resets across map/round transitions. |
| `sv_keywords` | `""` | SERVERINFO | Free-text server-browser search keywords. |
| `sv_cheats` | `1` | SYSTEMINFO\|ROM | Whether `CVAR_CHEAT` cvars/commands are usable; set from `map` vs `devmap`. |
| `g_password` | `""` | USERINFO | Server join password. Empty or `none` = no password. |
| `g_banIPs` | `""` | ARCHIVE | IP ban list string. |
| `g_filterBan` | `1` | ARCHIVE | Whether the ban list is a blacklist (`1`) or whitelist (`0`). |
| `g_motd` | `""` | ARCHIVE | Message-of-the-day shown to connecting clients. |
| `g_log` | `""` | ARCHIVE | Path/name of the plaintext server log file. |
| `g_logSync` | `0` | ARCHIVE | Force-flush the log file after every write. |
| `URL` | `""` | SERVERINFO\|ARCHIVE | Admin-configured server website URL, shown in serverinfo. |
| `g_developer` (`developer`) | `0` | TEMP | Enables verbose game-module dev/debug output. |

## Match/Round Control

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_gametype` | `5` (GT_WOLF) | SERVERINFO\|LATCH | Selects the gametype (Objective/Stopwatch/Checkpoint/etc). |
| `g_gameskill` | `3` | SERVERINFO\|LATCH | AI/bot difficulty; also feeds shared movement-code skill scaling. |
| `g_tournament` | `1` | ARCHIVE\|LATCH\|SERVERINFO | Master switch for the ready/unready competitive match-mode system (team-switch locking, timeouts, warmup countdown, min-player/ready-percent gating). |
| `team_maxplayers` | `0` | — | Max players per team (`0` = unlimited). |
| `team_nocontrols` | `0` | ARCHIVE | Disables in-game "team controls" UI/commands for players. |
| `match_minplayers` | `2` | — | Minimum non-spectator players needed before ready-up/countdown can proceed. |
| `match_readypercent` | `100` | — | Percentage of players that must be ready to auto-start the match. |
| `match_latejoin` | `1` | — | Allows/blocks joining a team mid-match. |
| `match_warmupDamage` | `1` | — | Whether damage is applied during warmup. |
| `match_timeoutlength` | `180` | — | Length (seconds) of a called timeout/pause. |
| `match_timeoutcount` | `3` | — | Number of timeouts each team is allotted per match. |
| `match_mutespecs` | `0` | — | Mutes spectator chat during a match. |
| `g_warmup` | `20` | ARCHIVE | Warmup countdown length (seconds) before a round starts. |
| `g_doWarmup` | `0` | — | Forces a warmup phase even outside match mode. |
| `g_noTeamSwitching` (`sv_tourney`) | `0` | ARCHIVE | Locks players to their joined team, for stopwatch/competitive integrity. |
| `g_altStopwatchMode` | `0` | ARCHIVE | Alternate stopwatch-gametype round/side-swap rule variant. |
| `g_userTimeLimit` | `0` | — | UI-set custom timelimit override. |
| `g_userAlliedRespawnTime` / `g_userAxisRespawnTime` | `0` | — | UI-set per-team respawn-time overrides. |
| `g_redlimbotime` / `g_bluelimbotime` | `30000` | SERVERINFO\|LATCH | Reinforcement/respawn wait time (ms) per team. |
| `dmflags` | `0` | ARCHIVE | Deathmatch flag bitmask: `8`=no falling damage, `16`=fixed FOV, `32`=no footsteps, `64`=no weapon reload. |
| `fraglimit` | `0` | ARCHIVE\|NORESTART | Frag-count win condition (non-objective gametypes). |
| `timelimit` | `0` | SERVERINFO\|ARCHIVE\|NORESTART | Match/round time limit in minutes. |
| `capturelimit` | `8` | ARCHIVE\|NORESTART | Flag-capture win condition (CTF-like modes). |
| `g_maxlives` (`sv_maxlives`) | `0` | ARCHIVE\|LATCH\|SERVERINFO | Global per-life limit before a player is out for the round (`0` = unlimited). |
| `g_alliedmaxlives` / `g_axismaxlives` | `0` | LATCH\|SERVERINFO | Per-team override of `g_maxlives`. |
| `g_enforcemaxlives` | `1` | ARCHIVE | Strictly enforces maxlives — temp-bans players who rejoin/team-hop to bypass it. |
| `g_fastres` | `0` | ARCHIVE | Enables fast medic-revive behavior. |
| `g_fastResMsec` | `1000` | ARCHIVE | Revive time (ms) when `g_fastres` is enabled. |
| `g_complaintlimit` | `3` | ARCHIVE | Number of player complaints (e.g. against a team-killer) before automatic action. |
| `g_voteFlags` | `255` | ARCHIVE\|SERVERINFO | Bitmask of permitted `/callvote` types; `0` disables voting. |
| `g_teamForceBalance` | `0` | ARCHIVE | Auto-balances team sizes on join. |
| `g_minGameClients` | `8` | SERVERINFO | Minimum players before a "real" game (vs. bot-filled) is considered started. |
| `g_maxGameClients` | `0` | SERVERINFO\|LATCH\|ARCHIVE | Caps active game-playing clients separately from `sv_maxclients`. |

## Competitive & Anti-Cheat

Referee status is granted per-client via the `ref`/`rcon` server command
path (`g_svcmds.c`), not a settable cvar — there is no `g_refereePassword`.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_antilag` | `2` | ARCHIVE\|SERVERINFO | Master antilag/lag-compensation switch. `2` specifically enables full hitscan rewind. |
| `g_delagHitscan` | `1` | ARCHIVE\|SERVERINFO | Enables lag-compensated (rewound) hit detection for hitscan weapons. |
| `g_delagMissiles` | `0` | ARCHIVE\|SERVERINFO | Enables lag compensation for projectile/missile weapons (off by default). |
| `g_maxLagCompensation` | `500` | ARCHIVE\|SERVERINFO | Caps how far back (ms) the antilag rewind will reach for a laggy client. |
| `g_maxExtrapolatedFrames` | `2` | — | Caps how many frames of movement can be extrapolated for a client with missed updates. |
| `g_ospmode` | `0` | ARCHIVE | OSP/rtcwPro legacy-compatibility mode — snaps/rounds player origin for entity-state and antilag history so hit-detection and demos match older OSP-derived netcode instead of WolfPro's float-precision path. |
| `g_smoothClients` | `1` | — | Extrapolates other players' movement between snapshots for smoother rendering. |
| `pmove_fixed` | `0` | SYSTEMINFO | Forces fixed-timestep player movement for netcode consistency. |
| `pmove_msec` | `8` | SYSTEMINFO | The fixed movement timestep (ms) used when `pmove_fixed` is enabled. |
| `sv_fps` | `20` | SYSTEMINFO\|ARCHIVE | Server simulation tick rate; also feeds extrapolation math directly. |
| `g_preciseHeadHitbox` | `1` | ARCHIVE | Enables the tag-based precise headshot hitbox instead of a bounding-box approximation. |
| `g_headMinX/Y/Z`, `g_headMaxX/Y/Z` | `-6/-6/0`, `6/6/12` | ARCHIVE | Bounds of the precise head hitbox volume, in map units. |
| `g_preciseBodyBox` | `1` | — | Enables a more precise body hitbox model versus the legacy capsule. |
| `g_capsuleScale` | `1.0` | ARCHIVE | Global scale multiplier applied to player hit-capsule radius. |
| `g_noSelfDamage` | `1` | ARCHIVE | Disables rocket/self-splash damage to the firer. |
| `g_rocketMode` | `0` | SYSTEMINFO | Alternate rocket-launcher physics/handling mode. |
| `g_rocketMidairInstagib` | `1` | ARCHIVE | Instantly kills a target hit by rocket splash while airborne (with lethal-adjacent damage). |
| `g_rocketDamageMultiplier` | `0.34` | ARCHIVE | Multiplier applied to rocket self-damage; balance tuning for `g_rocketMode`. |
| `g_gameStatslog` | `16` | ARCHIVE | Bitmask of which stat categories get JSON-logged; the default `16` specifically enables JSON stat-saving. Central gate for stats logging across the codebase. |
| `g_statsDebug` | `0` | ARCHIVE | Writes extra debug info to help diagnose stats-related crashes. |
| `g_stats_curl_submit` | `0` | ARCHIVE | Enables curl-based automatic submission of end-of-match stats to a remote API. |
| `g_stats_curl_submit_URL` | `https://rtcwproapi.donkanator.com/submit` | ARCHIVE | Target URL for stats submission. |
| `g_statsRetryCount` / `g_statsRetryDelay` | `3` / `2` | ARCHIVE | Retry count/delay (seconds) for stats submission. |
| `g_apiquery_curl_URL` | `https://rtcwproapi.donkanator.com/serverquery` | ARCHIVE | Endpoint used for server-query API calls. |
| `g_wtvdemos` | `1` | ARCHIVE | Enables the server-side WTV full-round demo recording system. |
| `g_wtvDiscordWebhookURL` | `""` | ARCHIVE | Discord webhook URL for automatic WTV round-demo uploads; empty disables the upload. |
| `g_wtvDiscordRetryCount` / `g_wtvDiscordRetryDelay` | `3` / `5` | ARCHIVE | Retry count/delay (seconds) for a failed Discord WTV upload. |
| `g_hitsounds` | `1` | ARCHIVE | Server-side enable for hit-confirmation sound feedback (requires client soundpack). Client controls the client half via `cg_hitsounds`, below. |
| `g_showHeadshotRatio` | `0` | — | Toggles display of a player's headshot-percentage stat. |
| `g_allowForceTapout` | `1` | ARCHIVE | Allows a downed player to force a "give up"/tapout instead of waiting for revive/bleed-out. |
| `g_userAim` | `1` | CHEAT | Toggles the weapon-spread/accuracy-degradation ("aim spread scale") system. |
| `sv_minUserCmdInterval` | — | — | See [Server Administration](#server-administration) — command-rate anti-cheat. |

## Gameplay Rules

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_friendlyFire` (`sv_friendlyFire`) | `1` | SERVERINFO\|ARCHIVE | Enables damage between teammates. |
| `g_speed` | `320` | — | Base player movement speed. |
| `g_gravity` | `800` | — | World gravity constant. |
| `g_gravityModifier` | `0.9475` | ARCHIVE | Additional multiplier on top of `g_gravity` for player physics/jump arcs. |
| `g_knockback` | `1000` | — | Scales physical knockback force from weapon hits. |
| `g_weaponrespawn` | `5` | — | Seconds before a dropped/map weapon item respawns. |
| `g_weaponTeamRespawn` | `30` | — | Team-specific weapon respawn timer variant. |
| `g_forcerespawn` | `0` | — | Forces automatic respawn after N seconds dead. |
| `g_inactivity` | `0` | — | Seconds of inactivity before a player is kicked/moved to spectator (`0` = disabled). |
| `g_knifeonly` | `0` | — | Knife-only gameplay restriction mode. |
| `g_enableBreath` | `1` | SERVERINFO | Enables the visible breath/cold-weather effect. |
| `g_voiceChatsAllowed` | `4` | ARCHIVE | Rate-limits voice-chat command usage. |
| `g_spawnOffset` | `9` | ARCHIVE | Randomization offset seed count applied to reinforcement/spawn positions (clamped ≥1). |
| `g_allowEnemySpawnTimer` | `1` | ARCHIVE\|SERVERINFO | Lets players see the enemy team's spawn/reinforcement countdown. |
| `g_disableDeadBodyFlagGrab` | `1` | ARCHIVE | Prevents grabbing an objective flag/item off a dead body. |
| `g_medicChargeTime` / `g_engineerChargeTime` / `g_LTChargeTime` / `g_soldierChargeTime` | `45000`/`30000`/`40000`/`20000` | SERVERINFO\|LATCH | Class special-ability charge/cooldown times (ms) per player class. |
| `sv_screenshake` | `5` | ARCHIVE | Screen-shake magnitude multiplier (consumed by cgame). |
| `g_mg42arc` | `0` | TEMP | MG42 mounted-gun firing-arc tuning value. |
| `g_footstepAudibleRange` | `256` | CHEAT | Range at which footstep sounds are audible to other players. |
| `g_rankings` | `0` | — | Toggles a player-ranking/leaderboard feature. |
| `g_mapScriptDirectory` | `""` | ARCHIVE | Custom directory to load map scripts from, if set. |

## Bot/AI

The actively-used bot system is Omni-bot (`g_OmniBot*`), distinct from the
legacy native botlib/AAS system (`bot_*`) and the single-player-derived
AI-cast system (`aicast_*`), both mostly dev/debug tools not documented
here in detail.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `omnibot_enable` | `1` | ARCHIVE\|NORESTART | Master switch for the Omni-bot integration. |
| `omnibot_path` | `./wolfpro/omni-bot` | ARCHIVE\|NORESTART | Filesystem path to the Omni-bot installation. |
| `omnibot_flags` | `0` | ARCHIVE\|NORESTART | Bitflags configuring Omni-bot behavior. |
| `g_botGib` | `1` | — | Whether bots can be gibbed. |
| `g_botTeam` | `0` | — | Forces all bots onto a specific team (`1`=axis, `2`=allies) when set. |

## HUD & Display

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_fov` | `90` | ARCHIVE | Player field-of-view. |
| `cg_viewsize` | `100` | ARCHIVE | 3D viewport size (screen %) vs. HUD border. |
| `cg_stereoSeparation` | `0.4` | ARCHIVE | Red/blue stereo-3D eye separation. |
| `cg_shadows` | `1` | ARCHIVE | Player/entity blob-shadow toggle. Also registered by the renderer (as `r_shadows`) reading the same cvar name. |
| `cg_gibs` | `1` | ARCHIVE | Gore/gib effects toggle. |
| `cg_draw2D` | `1` | CHEAT | Master HUD-overlay toggle; cheat-protected to stop it hiding the sniper-scope zoom overlay. |
| `cg_drawFrags` | `1` | ARCHIVE | Frags/scoreboard HUD element toggle. |
| `cg_drawStatus` | `1` | ARCHIVE | Status bar (health/ammo icons) toggle. |
| `cg_drawTimer` | `0` | ARCHIVE | Match clock HUD element toggle. |
| `cg_drawFPS` | `0` | ARCHIVE | FPS counter HUD element toggle. |
| `cg_drawSnapshot` | `0` | ARCHIVE | Snapshot/netcode debug counter HUD toggle. |
| `cg_draw3dIcons` | `1` | ARCHIVE | 3D world-space item/pickup icons toggle. |
| `cg_drawIcons` | `1` | ARCHIVE | 2D HUD icons (ammo/weapon icons) toggle. |
| `cg_drawAmmoWarning` | `1` | ARCHIVE | Low-ammo warning HUD element toggle. |
| `cg_drawAttacker` | `1` | ARCHIVE | "Who's attacking you" HUD icon toggle. |
| `cg_drawRewards` | `1` | ARCHIVE | Reward-medal icon popups toggle. |
| `cg_hudAlpha` | `1` | ARCHIVE | Global HUD element opacity. |
| `cg_cursorHints` | `1` | ARCHIVE | Interact-cursor hint icons (doors, MG use, etc.) toggle. |
| `cg_markTime` | `10000` (ms) | ARCHIVE | Bullet/explosion decal-mark lifetime on surfaces. |
| `cg_lagometer` | `0` | ARCHIVE | Lagometer graph toggle. |
| `cg_centertime` | `5` | CHEAT | Center-screen print message duration (seconds). |
| `cg_bobup` / `cg_bobpitch` / `cg_bobroll` / `cg_runpitch` / `cg_runroll` | `0.005`/`0.002`/`0.002`/`0.002`/`0.005` | ARCHIVE | View-bob amounts while running/walking. |
| `cg_bloodTime` | `120` | ARCHIVE | Blood-decal effect lifetime. |
| `cg_skybox` | `1` | CHEAT | Skybox rendering toggle. Also registered by the renderer as `r_portalsky`. |
| `cg_simpleItems` | `0` | ARCHIVE | Draw items as flat 2D icons instead of 3D models. |
| `cg_teamChatTime` / `cg_teamChatHeight` | `8000` / `8` | ARCHIVE | Team-chat overlay message lifetime / row height. |
| `cg_coronafardist` | `1536` | ARCHIVE | Max render distance for light coronas. |
| `cg_coronas` | `1` | ARCHIVE | Light-corona (lens flare dot) rendering toggle. |
| `cg_drawTeamOverlay` | `2` | ARCHIVE | Team status overlay display mode (off/compact/full). |
| `cg_blinktime` | `100` | ARCHIVE | Blink interval for blinking HUD icons. |
| `cg_voiceSpriteTime` | `6000` | ARCHIVE | Duration of the voice-command icon shown above a player's head. |
| `cg_smallFont` / `cg_bigFont` | `0.25` / `0.4` | ARCHIVE | HUD text small/big font scale. |
| `cg_hudFiles` | `ui_mp/hud.txt` | ARCHIVE | Path to the HUD layout definition file. |
| `cg_showblood` | `1` | ARCHIVE | Blood/gore visibility toggle. |
| `cg_wolfparticles` | `1` | ARCHIVE | Particle-effects toggle. |
| `cg_drawCompass` | `1` | ARCHIVE | Compass HUD element toggle. |
| `cg_drawNotifyText` | `1` | ARCHIVE | Kill-feed/notify-text HUD element toggle. |
| `cg_bloodDamageBlend` / `cg_bloodFlash` | `1.0` / `1.0` | ARCHIVE | Screen blood-flash-on-damage overlay intensity. |
| `cg_showPriorityText` / `cg_priorityTextX` / `cg_priorityTextY` | `1` / `0` / `350` | ARCHIVE | Priority/objective text banner toggle + position. |
| `cg_drawReinforcementTime` / `cg_drawEnemyTimer` | `1` / `1` | ARCHIVE | Own-team reinforcement / enemy reinforcement timer HUD toggle. |
| `cg_enemyTimerColor/X/Y/ProX/ProY`, `cg_reinforcementTimeColor/X/Y/ProX/ProY` | various | ARCHIVE | Reinforcement-timer colors and positions (normal + widescreen "pro" layout). |
| `cg_muzzleFlash` | `1` | ARCHIVE | Weapon muzzle-flash effect toggle. |
| `cg_tracers` | `1` | ARCHIVE | Bullet tracer effect toggle. |
| `ch_font` | `0` | ARCHIVE\|LATCH | HUD character-set style: `0`=default wolf hudchars, `1`/`2`=OSP1/OSP2 style. |
| `cg_teamOverlayX` / `cg_teamOverlayY` | `-1` / `0` | ARCHIVE | Team status-overlay position. |
| `cg_teamOverlayMaxLocationWidth` | `20` | ARCHIVE | Max width for the location string in the team overlay. |
| `cg_drawSpeed` / `cg_speedX` / `cg_speedY` | `0` / `315` / `340` | ARCHIVE | Speedometer HUD element toggle + position. |
| `cg_weaponIconX`, `cg_ammoIconX`, `cg_ammoValueX`, `cg_chargeBarX`, `cg_sprintBarX`, `cg_healthX` | `-99999` each | ARCHIVE | Per-element HUD X-position overrides for widescreen layouts; `-99999` = use the `hud.txt`-authored position unchanged. |
| `cg_chatX` / `cg_chatY` | `0` / `385` | ARCHIVE | Chat overlay position. |
| `cg_compassX` / `cg_compassY` | `-99999` / `420` | ARCHIVE | Compass HUD position override. |
| `cg_lagometerX` / `cg_lagometerY` | `-55` / `340` | ARCHIVE | Lagometer position. |
| `cg_widescreen` | `0` | ARCHIVE | Widescreen HUD layout toggle. `0` = classic non-uniform stretch (pixel-identical to pre-widescreen-fix engine), `1` = corrected/extended layout. Defaults off so upgrades are a silent visual no-op. |
| `cg_drawCI` | `1` | ARCHIVE | Toggles the "connection interrupted" text + icon shown when the client's command buffer is exhausted. |
| `cg_chatAlpha` | `0.33` | ARCHIVE | Chat overlay background opacity. |
| `cg_chatBackgroundColor` | `""` | ARCHIVE | Chat overlay background color override. |
| `cg_noChat` | `0` | ARCHIVE | Suppress chat display entirely. |
| `cg_notifyTextX/Y/Shadow/Width/Height/Lines/PlayerOnly` | `0`/`42`/`0`/`8`/`8`/`5`/`0` | ARCHIVE | Kill-feed-style notify-text position, drop-shadow, cell size, max lines, and "only events involving me" filter. |
| `cg_teamObituaryColors` | `0` | ARCHIVE | Enable custom-colored kill-feed (obituary) text. |
| `cg_teamObituaryColorSame/SameTK/Enemy/EnemyTK` | green/mdgreen/red/mdred | ARCHIVE | Kill-feed text colors for same-team kill, team-kill, enemy kill, enemy team-kill. |
| `cg_fragsY` / `cg_fragsWidth` | `0` / `16` | ARCHIVE | Frags counter position/width. |
| `cg_predictItems` | `1` | ARCHIVE | Client-side prediction of item pickups. |
| `cg_deferPlayers` | `1` | ARCHIVE | Defer loading other players' models/skins (perf vs. pop-in tradeoff). |

## Crosshair/Weapon

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_autoswitch` | `2` | ARCHIVE | Auto-switch to picked-up weapons (client-side pass-through default is `0`). |
| `cg_drawGun` / `cg_drawFPGun` | `1` / `1` | ARCHIVE | Draw the first-person weapon view model. |
| `cg_zoomfov` | `22.5` | ARCHIVE | Base zoomed FOV. |
| `cg_zoomDefaultBinoc/Sniper/Snooper/FG` | `22.5`/`20`/`40`/`55` | ARCHIVE | Default zoom FOV per scoped-item type. |
| `cg_zoomStepBinoc/Sniper/Snooper/FG` | `3`/`2`/`5`/`10` | ARCHIVE | Zoom-in/out step size per scope type. |
| `cg_useWeapsForZoom` | `1` | ARCHIVE | Whether weapon-fire is usable while zoomed. |
| `cg_weaponCycleDelay` | `150` (ms) | ARCHIVE | Delay between weapon-cycle inputs. |
| `cg_cycleAllWeaps` | `1` | ARCHIVE | Include all weapons (not just class-relevant) in weapon cycling. |
| `cg_drawAllWeaps` | `1` | ARCHIVE | Weapon bar: show all owned weapons vs. current only. |
| `cg_drawSpreadScale` | `1` | ARCHIVE | Weapon accuracy/spread-cone visualization toggle. |
| `cg_drawCrosshair` | `1` (cgame) / `4` (UI default) | ARCHIVE | Crosshair style/shape selector. |
| `cg_drawCrosshairNames` | `1` | ARCHIVE | Show enemy player names when crosshair is over them. |
| `cg_drawCrosshairPickups` | `1` | ARCHIVE | Show pickup-item name when crosshair is over it. |
| `cg_crosshairSize` | `48` | ARCHIVE | Crosshair size. |
| `cg_crosshairHealth` | `1` | ARCHIVE | Color the crosshair by own health. |
| `cg_crosshairX` / `cg_crosshairY` | `0` / `0` | ARCHIVE | Crosshair position offset. |
| `cg_brassTime` | `2500` | ARCHIVE | Ejected-brass shell casing lifetime. |
| `cg_reticles` | `1` | CHEAT | Scope reticle overlay toggle. |
| `cg_reticleType` | `1` | ARCHIVE | Scope reticle style. |
| `cg_reticleBrightness` | `0.7` | ARCHIVE | Scope reticle/overlay brightness. |
| `cg_autoactivate` | `1` | ARCHIVE | Auto-pickup items on touch. |
| `cg_autoReload` | `1` | ARCHIVE | Auto-reload weapon when magazine empties. |
| `cg_customCrosshair` | `0` | ARCHIVE | Enable custom-drawn crosshair instead of the built-in shader crosshair. |
| `cg_customCrosshairHeight/Width/Thickness/ThicknessAlt` | `5`/`5`/`1`/`1` | ARCHIVE | Custom crosshair line dimensions (primary + alt-fire). |
| `cg_customCrosshairColor` / `ColorAlt` | `000000FF` / `FFFFFFFF` | ARCHIVE | Custom crosshair RGBA color (primary/alt). |
| `cg_customCrosshairXOffset/YOffset` | `0` / `0` | ARCHIVE | Custom crosshair position offset. |
| `cg_customCrosshairXGap/YGap` | `0` / `0` | ARCHIVE | Gap between custom crosshair segments (center dead-zone). |
| `cg_customCrosshairVMirror` | `1` | ARCHIVE | Vertically mirror the custom crosshair line layout. |
| `cg_crosshairPulse` | `1` | ARCHIVE | Pulse/animate the crosshair on hit or fire. |
| `cg_crosshairAlpha` / `cg_crosshairAlphaAlt` | `1.0` / `1.0` | ARCHIVE | Crosshair opacity (normal / alt-fire). |
| `cg_crosshairColor` / `cg_crosshairColorAlt` | White / White | ARCHIVE | Crosshair color name (normal / alt-fire). |
| `cg_zoomedFOV` | `90` | ARCHIVE | FOV while scoped/zoomed. |
| `cg_zoomedSensLock` | `0` | ARCHIVE | Lock mouse sensitivity while zoomed instead of scaling it. |
| `cg_zoomedSens` | `0.3` | ARCHIVE | Mouse sensitivity multiplier while zoomed. |
| `cg_noAmmoAutoSwitch` | `0` | ARCHIVE | Disable auto weapon-switch on running out of ammo. |

## Demo Playback & Spectator

Covers both the client-side demo recorder/player and the NDP
(non-destructive-playback, seek/scrub) system described in
[CLAUDE.md](CLAUDE.md), plus the server-side WTV round-demo system
(cvars listed under [Competitive & Anti-Cheat](#competitive--anti-cheat)).

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_thirdPerson` | `0` | CHEAT | Third-person camera toggle — used for spectating/demo camera work. |
| `cg_thirdPersonRange` / `cg_thirdPersonAngle` | `80` / `0` | CHEAT | Third-person camera distance/angle. |
| `cg_wtvFreecam` | `0` | CHEAT | Enable a free-fly spectator camera. |
| `cg_wtvFreecamSpeed` | `480` | ARCHIVE | Free-cam movement speed. |
| `cg_wtvFreecamSprintMultiplier` | `2.5` | ARCHIVE | Free-cam sprint speed multiplier. |
| `cg_antilagDemoView` | `1` | ARCHIVE | Toggle antilag-corrected rendering of other players while watching demos. |
| `cg_cameraOrbit` | `0` | CHEAT | Auto-orbit camera (e.g. at intermission). |
| `cg_cameraOrbitDelay` | `50` | ARCHIVE | Orbit camera update interval. |
| `timescale` | `1` | — | Demo playback speed multiplier — used for slow-motion/fast-forward review. |
| `ui_demoDir` | `demos` | ARCHIVE | Directory the in-game demo browser lists. |
| `timedemo` | `0` | — | Benchmarks demo playback at max speed, reporting FPS. |
| `cl_avidemo` / `cl_forceavidemo` | `0` / `0` | — | Captures demo playback frames to disk for AVI export. |
| `cl_freezeDemo` | `0` | TEMP | Pauses demo playback. |
| `cl_demoPlayer` | `1` | ARCHIVE | Selects/enables the newer non-destructive demo player (NDP) vs. legacy playback. |

## Stats Windows/Popups

Direct-draw HUD popups (`CG_DrawStatsWindows` in `cg_draw.c` — no
window-allocation system).

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_wstatsX` / `cg_wstatsY` | `5` / `385` | ARCHIVE | Position of the weapon-stats (`+wstats`) popup window. |
| `cg_statsX` / `cg_statsY` | `5` / `385` | ARCHIVE | Position of the client-game-stats (`+stats`) popup window. |
| `cg_topshotsX` / `cg_topshotsY` | `388` / `385` | ARCHIVE | Position of the "topshots" (`+wtopshots`) popup window. |
| `cg_popupLimboMenu` | `1` | ARCHIVE | Auto-pop the limbo/class-select menu open on death/spawn wait. |
| `cg_descriptiveText` | `1` | ARCHIVE | Show descriptive tooltip text in the limbo menu. |
| `cg_registeredPlayers` | `1` | ARCHIVE | Selects which server chat/print command family is used for "registered player" (username-based) vs. plain netname display/chat. |

## Sound

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `s_volume` | `0.8` | ARCHIVE | Master sound volume. |
| `s_musicvolume` | `0.25` | ARCHIVE | Background music volume. |
| `s_mute` | `0` | TEMP | Full audio mute. |
| `s_separation` | `0.5` | ARCHIVE | Stereo separation for positional audio. |
| `s_doppler` | `1` | ARCHIVE | Doppler effect toggle for moving sound sources. |
| `s_khz` | `22` | ARCHIVE | Audio device sample rate (kHz). |
| `s_mixahead` | `0.2` | ARCHIVE | Audio mix-ahead buffer time (latency/underrun tradeoff). |
| `s_mixPreStep` | `0.05` | ARCHIVE | Mix pre-step time, affects mixing granularity. |
| `s_defaultsound` | `0` | ARCHIVE | Plays a placeholder beep for missing sound assets. |
| `s_wavonly` | `0` | ARCHIVE\|LATCH | Forces WAV-only playback, disabling compressed formats. |
| `s_nocompressed` | `0` | INIT | Disables compressed (ADPCM/wavelet) sound loading. |
| `s_initsound` | `1` | — | Master enable for the sound subsystem; if `0`, sound init aborts early. |
| `com_soundMegs` | (build default) | LATCH\|ARCHIVE | Sound-system memory pool size (MB). |
| `cl_wavefilerecord` | `0` | TEMP | Enables recording gameplay audio to a WAV file. |
| `cg_hitsounds` | `0` | ARCHIVE | Client-side enable for hit-confirmation sound on landing a shot; sent to the server as part of userinfo. Server-side companion is `g_hitsounds`. |
| `cg_hitsoundBodyStyle` / `cg_hitsoundHeadStyle` | `1` / `1` | ARCHIVE | Body-hit / headshot sound variant, sent to the server as part of hitsound config. |
| `cg_footsteps` | `1` | CHEAT | Footstep sound toggle. |
| `cg_noTaunt` | `0` | ARCHIVE | Disable taunt/voice-command playback. |
| `cg_teamChatsOnly` | `0` | ARCHIVE | Restrict incoming chat display/sound to team chat only. |
| `cg_noVoiceChats` / `cg_noVoiceText` | `0` / `0` | ARCHIVE | Suppress voice-chat command sounds / text display. |
| `cg_chatBeep` | `0` | ARCHIVE | Play a beep sound on incoming chat message. |
| `cg_announcer` | `1` | ARCHIVE | Announcer voice-over toggle (round start/end, objectives). |

## Video/Display

Cvars marked **(VK)**/**(GL)** are backend-specific; unmarked cvars are
registered independently by both backends with the same name/behavior.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_fullscreen` | `1` | ARCHIVE\|LATCH | Toggles fullscreen vs. windowed mode. |
| `r_mode` | GL `3`, VK `-3` | ARCHIVE\|LATCH | Video mode index into the resolution table. VK's default (`-3` = unmanaged) means VK ignores this by default and uses the explicit `r_windowed*`/`r_fullscreen*` cvars below instead. |
| `r_customwidth` / `r_customheight` | `1600` / `1024` | ARCHIVE\|LATCH | Custom resolution when `r_mode -1`/custom is selected. |
| `r_displayRefresh` | `0` | LATCH | Requested display refresh rate (`0` = desktop default). |
| `r_noborder` | `0` | ARCHIVE\|LATCH | Borderless-window mode. |
| `r_stereo` | `0` | ARCHIVE\|LATCH | Stereoscopic (red/blue) rendering. |
| `r_gamma` | `1.2`–`1.3` | ARCHIVE | Display gamma correction. |
| `r_swapInterval` | `0` | ARCHIVE | Vsync (buffer swap wait). |
| `r_highQualityVideo` | `1` | ARCHIVE | Quality toggle for in-engine cinematic/video playback. |
| `r_inGameVideo` | `1` | ARCHIVE | Enables in-game cinematic (RoQ) playback. |
| `r_fullscreenDesktop` **(VK)** | `1` | ARCHIVE\|LATCH | Use desktop resolution/borderless fullscreen instead of an explicit fullscreen mode. |
| `r_fullscreenStretch` **(VK)** | `0` | ARCHIVE\|LATCH | Stretch a non-native resolution to fill the display rather than letterbox/native-scale. |
| `r_windowedWidth` / `r_windowedHeight` **(VK)** | `1280` / `720` | ARCHIVE\|LATCH | Windowed-mode dimensions. |
| `r_fullscreenWidth` / `r_fullscreenHeight` **(VK)** | `1920` / `1080` | ARCHIVE\|LATCH | Exclusive-fullscreen dimensions. |
| `r_customaspect` **(GL)** | `1` | ARCHIVE\|LATCH | Aspect-ratio override for custom resolutions. |
| `r_colorbits` / `r_texturebits` / `r_stencilbits` / `r_depthbits` **(GL)** | `0` each | ARCHIVE\|LATCH | Framebuffer/texture/stencil/Z-buffer bit depth (`0` = auto). |
| `r_ignorehwgamma` **(GL)** | `1` | ARCHIVE\|LATCH | Forces software gamma ramp instead of hardware gamma. |
| `r_glDriver` **(GL)** | (platform default) | ARCHIVE\|LATCH | Name of the OpenGL driver/library to load. |

## Rendering Quality & Performance

Registered by both backends unless marked; behavior is shared.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_picmip` | `1` | ARCHIVE\|LATCH | Texture downscale level (mip bias) — main quality/performance tradeoff, clamped 0–16. |
| `r_roundImagesDown` | `1` | ARCHIVE\|LATCH | Round non-power-of-two image dimensions down instead of up on load. |
| `r_detailtextures` | `1` | ARCHIVE\|LATCH | Enables the detail-texture overlay pass. |
| `r_overBrightBits` | `1` | ARCHIVE\|LATCH | Overbright lighting scale bits. |
| `r_mapOverBrightBits` | `2` | LATCH | Per-map overbright bit override. |
| `r_intensity` | `1` | LATCH | Global texture/light intensity multiplier. |
| `r_subdivisions` | `4` | ARCHIVE\|LATCH | Curved-surface (patch) tessellation LOD granularity. |
| `r_lodCurveError` | `250` | ARCHIVE | LOD error tolerance for curve tessellation. |
| `r_lodbias` | `0` | ARCHIVE | Model LOD bias. |
| `r_simpleMipMaps` | `1` | ARCHIVE\|LATCH | Uses simplified (box-filter) mip generation vs. a higher-quality filter. |
| `r_vertexLight` | `0` | ARCHIVE\|LATCH | Forces vertex lighting instead of lightmaps — large perf win, quality loss. |
| `r_ignoreFastPath` | `1` | ARCHIVE\|LATCH | Disables an optimized vertex-array fast path. |
| `r_flares` | `1` | ARCHIVE | Lens-flare rendering toggle. |
| `r_flareSize` | `40` | CHEAT | Flare sprite size. |
| `r_flareFade` | `5` | CHEAT | Flare fade rate. |
| `r_znear` | `4` | CHEAT | Near clip plane distance (clamped 0.001–200). |
| `r_zfar` | `0` | CHEAT | Far clip plane override (`0` = auto). |
| `r_fastsky` | `0` | ARCHIVE | Renders a flat-color sky instead of a skybox (perf). |
| `r_drawSun` | `1` | ARCHIVE | Sun-flare rendering toggle. |
| `r_dynamiclight` | `1` | ARCHIVE | Dynamic light rendering (muzzle flashes, explosions). |
| `r_dlightBacks` | `1` | ARCHIVE | Lights back-facing surfaces from dynamic lights. |
| `r_finish` | `0` | ARCHIVE | Forces GPU sync each frame (perf-costly, reduces latency). |
| `r_textureMode` | `GL_LINEAR_MIPMAP_NEAREST` | ARCHIVE | Texture filtering mode. |
| `r_facePlaneCull` | `1` | ARCHIVE | Backface culling on axial planes (perf). |
| `r_railWidth` / `r_railCoreWidth` / `r_railSegmentLength` | `16` / `1` / `32` | ARCHIVE | Rail trail (weapon beam) visual dimensions. |
| `r_ambientScale` / `r_directedScale` | `0.5` / `1` | CHEAT | Ambient / directed lighting scale. |
| `r_wolffog` | `1` | CHEAT | Enables RTCW's custom fog system. |
| `r_maxpolys` / `r_maxpolyverts` | (build defaults) | — | Max dynamic polys/verts per frame budget. |
| `r_ext_texture_filter_anisotropic` **(GL)** | `0` | ARCHIVE\|LATCH | Anisotropic texture filtering level (GL; VK's equivalent is `r_anisotropy`, default `16`). |
| `r_speeds` | `0` | CHEAT **[debug/advanced]** | Prints a render timing/stat overlay — commonly used by competitive players to check frame cost. |
| `r_smp` **(GL)** | auto (CPU-count based) | ARCHIVE\|LATCH | Multi-threaded render backend. |
| `r_cache` / `r_cacheShaders` / `r_cacheModels` **(GL)** | `1` each | LATCH | Model/shader disk caching. |
| `r_ignoreGLErrors` **(GL)** | `1` | ARCHIVE | Suppresses GL error checking/aborts. |
| `r_offsetfactor` / `r_offsetunits` **(GL)** | `-1` / `-2` | CHEAT | Polygon-offset factor/units (decal Z-fighting tuning). |

## Vulkan-specific

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_anisotropy` | `16` | ARCHIVE\|LATCH | Anisotropic texture filtering level (VK's equivalent of GL's `r_ext_texture_filter_anisotropic`). |
| `r_gpu` | `0` | ARCHIVE\|LATCH | Selects which physical GPU device to use (`0` = auto/first). |
| `r_msaa` | `8` | ARCHIVE\|LATCH | MSAA sample count. |
| `r_mipFilter` | `1` | ARCHIVE\|LATCH | Mipmap filter mode selection. |
| `r_sleepThreshold` | `2500` | ARCHIVE | Microsecond threshold controlling when the render thread sleeps vs. spin-waits (frame pacing/CPU tradeoff). |
| `r_alphaboost` | `1.0` | ARCHIVE | Alpha-blend intensity boost. |

## OpenGL-specific

Mostly legacy vendor-extension cvars (ATI TruForm, NVIDIA fog distance)
with little relevance on modern hardware, kept for completeness.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_allowExtensions` | `1` | ARCHIVE\|LATCH | Master switch for using any GL extensions. |
| `r_ext_compressed_textures` | `1` | ARCHIVE\|LATCH | Enables S3TC/DXT texture compression. |
| `r_ext_gamma_control` | `1` | ARCHIVE\|LATCH | Uses the hardware gamma-ramp extension. |
| `r_ext_multitexture` | `1` | ARCHIVE\|LATCH | Multitexture extension usage. |
| `r_ext_compiled_vertex_array` | `1` | ARCHIVE\|LATCH | Compiled-vertex-array extension usage (legacy perf path). |
| `r_ati_truform_tess` / `r_ati_truform_normalmode` / `r_ati_truform_pointmode` | `1` / mode strings | ARCHIVE | ATI TruForm N-patch tessellation tuning (legacy ATI hardware). |
| `r_ati_fsaa_samples` | `1` | ARCHIVE | ATI-specific FSAA sample count (valid: 1/2/4). |
| `r_ext_NV_fog_dist` / `r_nv_fogdist_mode` | `0` / mode string | ARCHIVE\|LATCH / ARCHIVE | NVIDIA distance-fog extension and mode. |
| `r_ext_texture_env_add` | platform-dependent | ARCHIVE\|LATCH | `GL_ADD` texture environment extension. |

## Netcode/Connection

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_maxpackets` | `30` (client-registered) — but see note | ARCHIVE | Max outgoing packets/sec sent to the server. **Note:** cgame also registers `cl_maxpackets` as a pass-through with default `125` — whichever module's registration runs first on a fresh profile wins the archived default; worth double-checking your actual value with `\cl_maxpackets`. |
| `cl_packetdup` | `1` | ARCHIVE | Number of duplicate copies of each outgoing packet (loss mitigation). |
| `cl_timeout` | `200` | — | Seconds of no server response before disconnecting. |
| `cl_timeNudge` | `0` | TEMP | Manual client-side time nudge (interpolation delay adjustment) — classic competitive netcode cvar. |
| `cl_autoNudge` | `0` | TEMP | Automatic time-nudge adjustment toggle. |
| `rate` | `5000` | USERINFO\|ARCHIVE | Client bandwidth cap sent to the server (bytes/sec). |
| `snaps` | `20` | USERINFO\|ARCHIVE | Requested server snapshot rate. |
| `cl_run` | `1` | ARCHIVE | Always-run movement mode; also gates the prediction path. |
| `cl_nodelta` | `0` | — | Forces full (non-delta) snapshots from the server — netcode debug/compat cvar. |
| `net_proxy` | `""` | TEMP | Proxy/relay address used when connecting through a relay. |
| `rconAddress` | `""` | — | Address rcon commands are sent to when not connected. |
| `rconPassword` (client, `rcon_client_password`) | `""` | TEMP | Remote-console password used by the client. |
| `cl_maxPing` | `800` | ARCHIVE | Ping threshold used by the server browser/UI to flag high-ping servers. |
| `cl_motd` | `1` | — | Enables message-of-the-day fetch on connect. |
| `cl_serverStatusResendTime` | `750` | — | Milliseconds between server-status query retries. |
| `name`, `model`, `head`, `color`, `handicap`, `sex`, `cl_anonymous`, `password` | various | USERINFO(\|ARCHIVE) | Player identity/config fields sent as connection userinfo. |
| `cl_guid` | (generated) | ROM\|USERINFO | Client hardware/identity GUID sent to the server for anti-cheat/stats tracking. |
| `con_restricted` | `0` | INIT | Restricts console command execution (competitive/anti-cheat related). |

## Input

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `sensitivity` | `5` | ARCHIVE | Mouse sensitivity. |
| `cl_mouseAccel` | `0` | ARCHIVE | Mouse acceleration factor. |
| `cl_freelook` | `1` | ARCHIVE | Mouse-look toggle. |
| `m_pitch` / `m_yaw` | `0.022` / `0.022` | ARCHIVE | Mouse pitch/yaw scale. |
| `m_forward` / `m_side` | `0.25` / `0.25` | ARCHIVE | Mouse forward-movement/strafe scale (forward rarely used). |
| `m_filter` | `0` | ARCHIVE | Mouse input filtering/smoothing toggle. |
| `cl_yawspeed` / `cl_pitchspeed` | `140` / `140` | ARCHIVE | Keyboard turn speed (yaw/pitch). |
| `cl_anglespeedkey` | `1.5` | — | Multiplier applied when the "speed" key is held while turning. |

## Downloads

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_allowDownload` | `0` | ARCHIVE | Bitmask controlling client auto-download behavior (enable/disable, UDP vs. redirect/curl, no-disconnect). |
| `sv_allowDownload`, `sv_dlURL`, `sv_dlRate` | — | — | See [Server Administration](#server-administration) for the server-side counterparts. |

## Console/Client Misc

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `con_notifytime` | `7` | — | Seconds a chat/console notify line stays on-screen. |
| `scr_conspeed` | `3` | — | Console drop/raise animation speed. |
| `con_scale` | `1.0` | ARCHIVE\|LATCH | Console/HUD font scale — drives the vector-font atlas bake size. |
| `con_type` | `0` | ARCHIVE | Console rendering style variant. |
| `con_colorRed/Green/Blue/Alpha` | `0.5`/`0.5`/`0.5`/`1` | ARCHIVE | Console background tint RGBA. |
| `con_height` | `0.5` | ARCHIVE | Console drop-down height, as a fraction of screen height. |
| `cl_language` | `0` | ARCHIVE | UI/string localization selection. |
| `r_hudFontFile` | `hudchars.ttf` | ARCHIVE\|LATCH | Path (under `fonts/`) of the TTF baked into the hudchars/console atlas. |
| `r_hudFontEnabled` | `1` | ARCHIVE\|LATCH | Master toggle for the TTF-baked vector font system vs. the classic bitmap `hudchars.tga`. |

## Menu/UI

Host-game screen defaults, server browser, and options-menu widgets.
Several of these are UI-side re-registrations of the same underlying
cgame cvar documented above, used purely so menu sliders/checkboxes can
read/write it (e.g. `cg_drawCrosshair`↔`ui_drawCrosshair`,
`cg_marktime`↔`ui_marks` — note the two sometimes ship different
defaults, e.g. `cg_marktime` 10000 vs. `ui_marks` 20000; whichever
module registers first on a fresh profile wins).

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `ui_ffa_fraglimit`/`timelimit`, `ui_tourney_fraglimit`/`timelimit`, `ui_team_fraglimit`/`timelimit`/`friendly`, `ui_ctf_capturelimit`/`timelimit`/`friendly` | various | ARCHIVE | Host-game screen defaults per gametype. |
| `ui_userTimeLimit` / `ui_userAlliedRespawnTime` / `ui_userAxisRespawnTime` | `0` each | — | Host-game screen custom time-limit/respawn-time entry fields. |
| `ui_glCustom` | `4` | ARCHIVE | Custom graphics-quality preset index shown in the options menu. |
| `ui_master` | `0` | ARCHIVE | Selected master server in the server browser. |
| `server1`…`server16` | `""` each | ARCHIVE | Favorites server-list slots. |
| `ui_dedicated` | `0` | ARCHIVE | "Start dedicated server" checkbox state in the host-game menu. |
| `ui_netSource` | `0` | ARCHIVE | Server-browser source selector (internet/LAN/favorites). |
| `ui_gametype` / `ui_joinGametype` / `ui_netGametype` / `ui_actualNetGametype` | `3`/`0`/`0`/`5` | ARCHIVE | Gametype selectors for hosting/browsing/joining. |
| `ui_browserMaster` / `ui_browserGameType` / `ui_browserSortKey` | `0`/`0`/`4` | ARCHIVE | Server-browser master/gametype filter and sort column. |
| `ui_browserShowFull/ShowEmpty/ShowFriendlyFire/ShowMaxlives/ShowTourney/ShowPunkBuster/ShowAntilag` | `1`/`1`/`0`/`1`/`1`/`0`/`0` | ARCHIVE | Server-browser row-filter checkboxes. |
| `ui_serverStatusTimeOut` | `7000` | ARCHIVE | Timeout (ms) for the server-info/status query popup. |
| `ui_demoDir` | `demos` | ARCHIVE | Demo browser directory (also listed above). |
