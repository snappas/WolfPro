## Contents

- [Server Administration](#server-administration)
- [Match/Round Control](#matchround-control)
- [Competitive](#competitive)
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

**Commands:**
- [Server Administration (Commands)](#server-administration-commands)
- [Match/Round Control (Commands)](#matchround-control-commands)
- [Gameplay Rules (Commands)](#gameplay-rules-commands)
- [Bot/AI (Commands)](#botai-commands)
- [Chat & Communication (Commands)](#chat--communication-commands)
- [Demo Playback & Spectator (Commands)](#demo-playback--spectator-commands)
- [Stats Windows/Popups (Commands)](#stats-windowspopups-commands)
- [Video/Display (Commands)](#videodisplay-commands)
- [Sound (Commands)](#sound-commands)
- [Netcode/Connection (Commands)](#netcodeconnection-commands)
- [Movement/Weapon (Commands)](#movementweapon-commands)
- [Input (Commands)](#input-commands)
- [Filesystem (Commands)](#filesystem-commands)
- [Console/Client Misc (Commands)](#consoleclient-misc-commands)

---

## Server Administration

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `dedicated` | `0` | LATCH | `0`=listen (playable client+server), `1`=dedicated. A `DEDICATED`-only build instead defaults this to `2` (internet dedicated) and marks it `ROM`. |
| `fs_basegame` | `""` | INIT | Base game directory name override; normally left unset in favor of `fs_game`. |
| `fs_basepath` | (install dir) | INIT | Base game-data path scanned for paks; rarely hand-set outside packaging/testing. |
| `fs_cdpath` | (platform default) | INIT | Legacy CD-install search path. |
| `fs_copyfiles` | `0` | INIT **[debug]** | Copies every loaded file out to `fs_homepath` (asset-extraction dev tool). |
| `fs_debug` | `0` | — **[debug]** | Logs filesystem/pak search activity. |
| `fs_game` | `wolfpro` | INIT\|SYSTEMINFO | Active mod/game directory; changing it switches which pk3 tree the engine loads. |
| `fs_homepath` | (per-OS user dir) | INIT | Writable user path for configs/demos/downloads, layered over `fs_basepath`. |
| `fs_restrict` | `""` | INIT | Restricts filesystem access to a single path (legacy kiosk/demo-disc builds). |
| `g_banIPs` | `""` | ARCHIVE | IP ban list string. |
| `g_developer` (`developer`) | `0` | TEMP | Enables verbose game-module dev/debug output. |
| `g_filterBan` | `1` | ARCHIVE | Whether the ban list is a blacklist (`1`) or whitelist (`0`). |
| `g_log` | `""` | ARCHIVE | Path/name of the plaintext server log file. |
| `g_logSync` | `0` | ARCHIVE | Force-flush the log file after every write. |
| `g_motd` | `""` | ARCHIVE | Message-of-the-day shown to connecting clients. |
| `g_password` | `""` | USERINFO | Server join password. Empty or `none` = no password. |
| `rconPassword` | `""` | TEMP | Remote-console admin password; empty disables rcon. |
| `sv_allowDownload` | `1` | ARCHIVE | Bitmask of server download settings: `1`=enable auto-download, `2`=disable HTTP/curl redirect downloads (force UDP), `4`=disable legacy UDP downloads, `8`=don't force-disconnect a client during a curl download. Add the values together to combine. |
| `sv_cheats` | `1` | SYSTEMINFO\|ROM | Whether `CVAR_CHEAT` cvars/commands are usable; set from `map` vs `devmap`. |
| `sv_dlRate` | `100` | ARCHIVE\|SERVERINFO | Advertised/throttled curl download rate in KB/s. |
| `sv_dlURL` | `https://dl.rtcw.eu/maps/rtcw` | SERVERINFO\|ARCHIVE | Base URL advertised to clients for curl-based HTTP downloads. |
| `sv_floodProtect` | `1` | ARCHIVE\|SERVERINFO | Throttles repeated client commands (chat/command spam protection). |
| `sv_GameConfig` | `""` | SERVERINFO\|ARCHIVE\|ROM | Identifies the active competitive ruleset/config preset. |
| `sv_hostname` | `WolfHost` | SERVERINFO\|ARCHIVE | Server name shown in the browser/serverinfo. |
| `sv_keywords` | `""` | SERVERINFO | Free-text server-browser search keywords. |
| `sv_killserver` | `0` | — **[debug]** | Set nonzero to force an immediate server shutdown; the engine resets it to `0` after acting on it. |
| `sv_lanForceRate` | `1` | ARCHIVE | Forces max rate for clients detected as LAN. |
| `sv_levelTimeReset` | `0` | ARCHIVE | Controls whether level time resets across map/round transitions. |
| `sv_master1`…`sv_master5` | `wolfmaster.idsoftware.com` / `""`×4 | ARCHIVE (master1: none) | Master server addresses for heartbeat/listing. |
| `sv_maxclients` (`g_maxclients`) | `20` | SERVERINFO\|LATCH (ARCHIVE via `g_maxclients`) | Max client slots including bots/spectators; requires a map change/restart. |
| `sv_maxclientsPerIP` | `3` | ARCHIVE | Caps simultaneous connections from one IP. |
| `sv_maxRate` | `0` | ARCHIVE | Server-enforced ceiling on a client's `rate` cvar; `0` = no cap. |
| `sv_minPing` / `sv_maxPing` | `0` / `0` | ARCHIVE | Reject/kick connections outside this ping band; `0` = disabled. |
| `sv_padPackets` | `0` | — **[debug]** | Pads outgoing server packets by N bytes (netcode testing). |
| `sv_privateClients` | `0` | SERVERINFO | Slots reserved for players who know `sv_privatePassword`. |
| `sv_privatePassword` | `""` | TEMP | Password granting access to reserved private slots. |
| `sv_pure` | `1` | SYSTEMINFO\|LATCH | Enforces pak-checksum matching between client/server. Set `sv_pure 0` to load unsigned/dev-built mod DLLs when testing local builds. |
| `sv_reconnectlimit` | `3` | — | Limits rapid reconnect attempts per client. |
| `sv_serverIP` | `""` | LATCH | Advertised server IP override, for multi-homed/NAT setups. |
| `sv_timeout` | `240` | TEMP | Seconds of no client packets before timeout/drop. |
| `sv_zombietime` | `2` | TEMP | Seconds a disconnected client slot lingers before reuse. |
| `ttycon` | `1` | — | Enables the interactive TTY console on the Linux dedicated server. |
| `URL` | `""` | SERVERINFO\|ARCHIVE | Admin-configured server website URL, shown in serverinfo. |

## Match/Round Control

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `capturelimit` | `8` | ARCHIVE\|NORESTART | Flag-capture win condition (CTF-like modes). |
| `dmflags` | `0` | ARCHIVE | Deathmatch flag bitmask: `8`=no falling damage, `16`=fixed FOV, `32`=no footsteps, `64`=no weapon reload. Add the values together to combine. |
| `fraglimit` | `0` | ARCHIVE\|NORESTART | Frag-count win condition (non-objective gametypes). |
| `g_alliedmaxlives` / `g_axismaxlives` | `0` | LATCH\|SERVERINFO | Per-team override of `g_maxlives`. |
| `g_altStopwatchMode` | `0` | ARCHIVE | Alternate stopwatch-gametype round/side-swap rule variant. |
| `g_doWarmup` | `0` | — | Forces a warmup phase even outside match mode. |
| `g_enforcemaxlives` | `1` | ARCHIVE | Strictly enforces maxlives — temp-bans players who rejoin/team-hop to bypass it. |
| `g_fastres` | `0` | ARCHIVE | Enables fast medic-revive behavior. |
| `g_fastResMsec` | `1000` | ARCHIVE | Revive time (ms) when `g_fastres` is enabled. |
| `g_gameskill` | `3` | SERVERINFO\|LATCH | AI/bot difficulty; also feeds shared movement-code skill scaling. |
| `g_gametype` | `5` (GT_WOLF) | SERVERINFO\|LATCH | Selects the gametype (Objective/Stopwatch/Checkpoint/etc). |
| `g_maxGameClients` | `0` | SERVERINFO\|LATCH\|ARCHIVE | Caps active game-playing clients separately from `sv_maxclients`. |
| `g_maxlives` (`sv_maxlives`) | `0` | ARCHIVE\|LATCH\|SERVERINFO | Global per-life limit before a player is out for the round (`0` = unlimited). |
| `g_minGameClients` | `8` | SERVERINFO | Minimum players before a "real" game (vs. bot-filled) is considered started. |
| `g_noTeamSwitching` (`sv_tourney`) | `0` | ARCHIVE | Locks players to their joined team, for stopwatch/competitive integrity. |
| `g_redlimbotime` / `g_bluelimbotime` | `30000` | SERVERINFO\|LATCH | Reinforcement/respawn wait time (ms) per team. |
| `g_teamForceBalance` | `0` | ARCHIVE | Auto-balances team sizes on join. |
| `g_tournament` | `1` | ARCHIVE\|LATCH\|SERVERINFO | Master switch for the ready/unready competitive match-mode system (team-switch locking, timeouts, warmup countdown, min-player/ready-percent gating). |
| `g_userAlliedRespawnTime` / `g_userAxisRespawnTime` | `0` | — | UI-set per-team respawn-time overrides. |
| `g_userTimeLimit` | `0` | — | UI-set custom timelimit override. |
| `g_voteFlags` | `255` | ARCHIVE\|SERVERINFO | Bitmask of permitted `/callvote` types: `1`=map_restart, `2`=reset_match, `4`=start_match, `8`=nextmap, `16`=swap_teams, `32`=gametype change, `64`=kick, `128`=map change. Add the values together to combine; `0` disables voting entirely. Only the master on/off (any nonzero value) is currently enforced in `Cmd_CallVote_f` — the individual bits are read/written by the UI's vote-options menu but not yet checked per-vote-type server-side. |
| `g_warmup` | `20` | ARCHIVE | Warmup countdown length (seconds) before a round starts. |
| `match_latejoin` | `1` | — | Allows/blocks joining a team mid-match. |
| `match_minplayers` | `2` | — | Minimum non-spectator players needed before ready-up/countdown can proceed. |
| `match_mutespecs` | `0` | — | Mutes spectator chat during a match. |
| `match_readypercent` | `100` | — | Percentage of players that must be ready to auto-start the match. |
| `match_timeoutcount` | `3` | — | Number of timeouts each team is allotted per match. |
| `match_timeoutlength` | `180` | — | Length (seconds) of a called timeout/pause. |
| `match_warmupDamage` | `1` | — | Whether damage is applied during warmup. |
| `team_maxplayers` | `0` | — | Max players per team (`0` = unlimited). |
| `team_nocontrols` | `0` | ARCHIVE | Disables in-game "team controls" UI/commands for players. |
| `timelimit` | `0` | SERVERINFO\|ARCHIVE\|NORESTART | Match/round time limit in minutes. |

## Competitive

Referee status is granted per player using the `ref`/`rcon` server
commands, not a cvar — there is no `g_refereePassword`.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_allowForceTapout` | `1` | ARCHIVE | Allows a downed player to force a "give up"/tapout instead of waiting for revive/bleed-out. |
| `g_antilag` | `2` | ARCHIVE\|SERVERINFO | Master antilag/lag-compensation switch. `2` specifically enables full hitscan rewind. |
| `g_apiquery_curl_URL` | `https://rtcwproapi.donkanator.com/serverquery` | ARCHIVE | Endpoint used for server-query API calls. |
| `g_camShakeDuration` | `1.0` | ARCHIVE\|SERVERINFO | Multiplier on explosion camera-shake duration (how long it takes to settle). |
| `g_camShakeScale` | `1.0` | ARCHIVE\|SERVERINFO | Multiplier on explosion camera-shake amplitude (how far the view swings). |
| `g_capsuleScale` | `1.0` | ARCHIVE | Global scale multiplier applied to player hit-capsule radius. |
| `g_cr0`…`g_cr4` | `15.0`/`7.0`×4 | CHEAT **[debug/advanced]** | Per-body-part hit-capsule radii (torso, L/R thigh, L/R calf) used to tune `g_capsuleScale`. |
| `g_dbgRevive` | `0` | — **[debug]** | Logs medic-revive debug info. |
| `g_debugAlloc` | `0` | — **[debug]** | Logs entity allocation/free debug info. |
| `g_debugBullets` | `0` | CHEAT **[debug]** | Logs hitscan bullet-trace debug info. |
| `g_debugDamage` | `0` | CHEAT **[debug]** | Logs damage-calculation debug info. |
| `g_debugHitboxes` | `0` | CHEAT **[debug/advanced]** | Draws the active hit-capsule volumes. |
| `g_debugMove` | `0` | — **[debug]** | Logs player-movement debug info. |
| `g_delagHitscan` | `1` | ARCHIVE\|SERVERINFO | Enables lag-compensated (rewound) hit detection for hitscan weapons. |
| `g_dmgFeedbackCeiling` | `10` | ARCHIVE\|SERVERINFO | Maximum final view-kick magnitude, after the health-scaled multiplier. |
| `g_dmgFeedbackFloor` | `5` | ARCHIVE\|SERVERINFO | Minimum final view-kick magnitude, after the health-scaled multiplier. |
| `g_dmgFeedbackLegacy` | `0` | ARCHIVE\|SERVERINFO | Reverts the damage view-kick (screen punch on taking a hit) to the original single-value-per-tick delivery and hardcoded health curve, ignoring the four cvars below. |
| `g_dmgFeedbackScaleFullHealth` | `0.4` | ARCHIVE\|SERVERINFO | View-kick multiplier applied to a hit at full health. |
| `g_dmgFeedbackScaleLowHealth` | `0.5` | ARCHIVE\|SERVERINFO | View-kick multiplier applied to a hit at 1 HP; health in between linearly interpolates between the two. |
| `g_gameStatslog` | `16` | ARCHIVE | Bitmask of which stat categories get JSON-logged: `1`=master stat-output enable (in practice, any nonzero value here also enables logging), `2`=include wstats in player stats, `4`=break out stats by category, `8`=break out stats by team, `16`=include extra kill-event data. Add the values together to combine. Central gate for stats logging across the codebase. |
| `g_headMinX/Y/Z`, `g_headMaxX/Y/Z` | `-6/-6/0`, `6/6/12` | ARCHIVE | Bounds of the precise head hitbox volume, in map units. |
| `g_hitsounds` | `1` | ARCHIVE | Server-side enable for hit-confirmation sound feedback (requires client soundpack). Client controls the client half via `cg_hitsounds`, below. |
| `g_listEntity` | `0` | — **[debug]** | Set nonzero to dump the entity list to the server console; auto-resets to `0`. |
| `g_maxLagCompensation` | `500` | ARCHIVE\|SERVERINFO | Caps how far back (ms) the antilag rewind will reach for a laggy client. |
| `g_noSelfDamage` | `1` | ARCHIVE | Disables rocket-splash self-damage to the firer; the rocket launcher is only obtainable when `g_rocketMode` is enabled. |
| `g_ospmode` | `0` | ARCHIVE | Snaps/rounds the player's origin to integer precision for entity-state linking and antilag history, instead of WolfPro's normal float-precision origin — affects hit-detection precision. |
| `g_preciseBodyBox` | `1` | — | Enables a more precise per-body-part capsule hitbox model versus the legacy single bounding box. |
| `g_preciseHeadHitbox` | `1` | ARCHIVE | Enables the tag-based precise headshot hitbox instead of a bounding-box approximation. |
| `g_rocketDamageMultiplier` | `0.34` | ARCHIVE | Multiplier applied to rocket damage dealt to other players; the rocket launcher is only obtainable when `g_rocketMode` is enabled. |
| `g_rocketMidairInstagib` | `1` | ARCHIVE | Instantly kills a target hit by rocket splash while airborne (with lethal-adjacent damage); the rocket launcher is only obtainable when `g_rocketMode` is enabled. |
| `g_rocketMode` | `0` | SYSTEMINFO | Alternate rocket-launcher physics/handling mode. |
| `g_showHeadshotRatio` | `0` | — | Toggles display of a player's headshot-percentage stat. |
| `g_smoothClients` | `1` | — | Extrapolates other players' movement between snapshots for smoother rendering. |
| `g_spreadAddSmg` | `24` | ARCHIVE\|SERVERINFO | MP40/Thompson/Sten per-shot aim-spread increase base (a random 0-9 is still added on top each shot). |
| `g_spreadScaleSmg` | `0.5` | ARCHIVE\|SERVERINFO | MP40/Thompson/Sten aim-spread recovery-speed scale (lower = faster recovery/tighter spread). |
| `g_stats_curl_submit` | `0` | ARCHIVE | Enables curl-based automatic submission of end-of-match stats to a remote API. |
| `g_stats_curl_submit_URL` | `https://rtcwproapi.donkanator.com/submit` | ARCHIVE | Target URL for stats submission. |
| `g_statsDebug` | `0` | ARCHIVE | Writes extra debug info to help diagnose stats-related crashes. |
| `g_statsRetryCount` / `g_statsRetryDelay` | `3` / `2` | ARCHIVE | Retry count/delay (seconds) for stats submission. |
| `g_synchronousClients` | `0` | SYSTEMINFO | Disables client-side movement prediction for all clients; recommended `1` for smoother demo recording (the engine warns on record if it's `0`). |
| `g_testPain` | `0` | CHEAT **[debug]** | Forces pain animations/sounds for testing. |
| `g_userAim` | `1` | CHEAT | Toggles the weapon-spread/accuracy-degradation ("aim spread scale") system. |
| `g_wtvdemos` | `1` | ARCHIVE | Enables the server-side WTV full-round demo recording system. |
| `g_wtvDiscordRetryCount` / `g_wtvDiscordRetryDelay` | `3` / `5` | ARCHIVE | Retry count/delay (seconds) for a failed Discord WTV upload. |
| `g_wtvDiscordWebhookURL` | `""` | ARCHIVE | Discord webhook URL for automatic WTV round-demo uploads; empty disables the upload. |
| `pmove_fixed` | `0` | SYSTEMINFO | Forces fixed-timestep player movement for netcode consistency. |
| `pmove_msec` | `8` | SYSTEMINFO | The fixed movement timestep (ms) used when `pmove_fixed` is enabled. |
| `sv_fps` | `20` | SYSTEMINFO\|ARCHIVE | Server simulation tick rate; also feeds extrapolation math directly. |

## Gameplay Rules

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_allowEnemySpawnTimer` | `1` | ARCHIVE\|SERVERINFO | Lets players see the enemy team's spawn/reinforcement countdown. |
| `g_disableDeadBodyFlagGrab` | `1` | ARCHIVE | Prevents grabbing an objective flag/item off a dead body. |
| `g_enableBreath` | `1` | SERVERINFO | Enables the visible breath/cold-weather effect. |
| `g_forcerespawn` | `0` | — | Forces automatic respawn after N seconds dead. |
| `g_friendlyFire` (`sv_friendlyFire`) | `1` | SERVERINFO\|ARCHIVE | Enables damage between teammates. |
| `g_gravity` | `800` | — | World gravity constant. |
| `g_gravityModifier` | `0.9475` | ARCHIVE | Additional multiplier on top of `g_gravity` for player physics/jump arcs. |
| `g_inactivity` | `0` | — | Seconds of inactivity before a player is kicked/moved to spectator (`0` = disabled). |
| `g_knifeonly` | `0` | — | Knife-only gameplay restriction mode. |
| `g_knockback` | `1000` | — | Scales physical knockback force from weapon hits. |
| `g_mapScriptDirectory` | `""` | ARCHIVE | Custom directory to load map scripts from, if set. |
| `g_medicChargeTime` / `g_engineerChargeTime` / `g_LTChargeTime` / `g_soldierChargeTime` | `45000`/`30000`/`40000`/`20000` | SERVERINFO\|LATCH | Class special-ability charge/cooldown times (ms) per player class. |
| `g_scriptDebug` | `0` | — **[debug]** | Logs map-script (`.script`) trigger/event execution debug info. |
| `g_spawnOffset` | `9` | ARCHIVE | Randomization offset seed count applied to reinforcement/spawn positions (clamped ≥1). |
| `g_speed` | `320` | — | Base player movement speed. |
| `g_voiceChatsAllowed` | `4` | ARCHIVE | Rate-limits voice-chat command usage. |
| `g_weaponrespawn` | `5` | — | Seconds before a dropped/map weapon item respawns. |
| `g_weaponTeamRespawn` | `30` | — | Team-specific weapon respawn timer variant. |

## Bot/AI

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `g_botGib` | `1` | — | Whether bots can be gibbed. |
| `g_botTeam` | `0` | — | Forces all bots onto a specific team (`1`=axis, `2`=allies) when set. |
| `omnibot_enable` | `1` | ARCHIVE\|NORESTART | Master switch for the Omni-bot integration. |
| `omnibot_flags` | `0` | ARCHIVE\|NORESTART | Bitflags configuring bot behavior. `1`=disable XP-save for bots, `2`=bots can't mount tanks, `4`=bots can't mount emplaced guns, `8`=don't count bots (e.g. toward `bot_minplayers`), `32`=no killing-spree/multikill announcements for bots, `64`=slightly faster bot movement. Add the values together to combine. |
| `omnibot_path` | `./wolfpro/omni-bot` | ARCHIVE\|NORESTART | Filesystem path to the Omni-bot installation. |

## HUD & Display

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_animspeed` | `1` | CHEAT **[debug]** | Global animation playback speed multiplier. |
| `cg_animState` | `0` | CHEAT **[debug]** | Forces a specific player animation state, for animation debugging. |
| `cg_autoAction` | `0` | ARCHIVE | Auto-action bitmask: `1`=auto-record a demo, `2`=auto-screenshot (defined but not currently wired to any action), `4`=auto-dump stats. Add the values together to combine. |
| `cg_bloodDamageBlend` / `cg_bloodFlash` | `1.0` / `1.0` | ARCHIVE | Screen blood-flash-on-damage overlay intensity. |
| `cg_bloodTime` | `120` | ARCHIVE | Blood-decal effect lifetime. |
| `cg_bobup` / `cg_bobpitch` / `cg_bobroll` / `cg_runpitch` / `cg_runroll` | `0.005`/`0.002`/`0.002`/`0.002`/`0.005` | ARCHIVE | View-bob amounts while running/walking. |
| `cg_centertime` | `5` | CHEAT | Center-screen print message duration (seconds). |
| `cg_chatAlpha` | `0.33` | ARCHIVE | Chat overlay background opacity. |
| `cg_chatBackgroundColor` | `""` | ARCHIVE | Chat overlay background color override. |
| `cg_chatX` / `cg_chatY` | `0` / `385` | ARCHIVE | Chat overlay position. |
| `cg_compassX` / `cg_compassY` | `-99999` / `420` | ARCHIVE | Compass HUD position override. |
| `cg_coronafardist` | `1536` | ARCHIVE | Max render distance for light coronas. |
| `cg_coronas` | `1` | ARCHIVE | Light-corona (lens flare dot) rendering toggle. |
| `cg_cursorHints` | `1` | ARCHIVE | Interact-cursor hint icons (doors, MG use, etc.) toggle. |
| `cg_debuganim` | `0` | CHEAT **[debug]** | Logs player animation-state debug info. |
| `cg_debugevents` | `0` | CHEAT **[debug]** | Logs entity-event debug info. |
| `cg_debugposition` | `0` | CHEAT **[debug]** | Logs entity position/interpolation debug info. |
| `cg_deferPlayers` | `1` | ARCHIVE | Defer loading other players' models/skins (perf vs. pop-in tradeoff). |
| `cg_draw2D` | `1` | CHEAT | Master HUD-overlay toggle; cheat-protected to stop it hiding the sniper-scope zoom overlay. `2` is a demo-playback-only minimal mode: crosshair, notify text, center-print, and the demo timeline only, nothing else. |
| `cg_draw3dIcons` | `1` | ARCHIVE | 3D world-space item/pickup icons toggle. |
| `cg_drawCI` | `1` | ARCHIVE | Toggles the "connection interrupted" text + icon shown when the client's command buffer is exhausted. |
| `cg_drawCompass` | `1` | ARCHIVE | Compass HUD element toggle. |
| `cg_drawFPS` | `0` | ARCHIVE | FPS counter HUD element toggle. |
| `cg_drawFrags` | `1` | ARCHIVE | Frags/scoreboard HUD element toggle. |
| `cg_drawGamemodels` | `1` | CHEAT **[debug]** | Toggles rendering of "game model" entities. |
| `cg_drawIcons` | `1` | ARCHIVE | 2D HUD icons (ammo/weapon icons) toggle. |
| `cg_drawNotifyText` | `1` | ARCHIVE | Kill-feed/notify-text HUD element toggle. |
| `cg_drawReinforcementTime` / `cg_drawEnemyTimer` | `1` / `1` | ARCHIVE | Own-team reinforcement / enemy reinforcement timer HUD toggle. |
| `cg_drawRewards` | `1` | ARCHIVE | Reward-medal icon popups toggle. |
| `cg_drawSnapshot` | `0` | ARCHIVE | Snapshot/netcode debug counter HUD toggle. |
| `cg_drawSpeed` / `cg_speedX` / `cg_speedY` | `0` / `315` / `340` | ARCHIVE | Speedometer HUD element toggle + position. |
| `cg_drawStatus` | `1` | ARCHIVE | Status bar (health/ammo icons) toggle. |
| `cg_drawTeamOverlay` | `2` | ARCHIVE | Team status overlay display mode (off/compact/full). |
| `cg_drawTimer` | `0` | ARCHIVE | Match clock HUD element toggle. |
| `cg_drawWeaponIconFlash` | `0` | ARCHIVE | Flash the weapon-bar icon on certain weapon events. |
| `cg_enemyTimerColor/X/Y/ProX/ProY`, `cg_reinforcementTimeColor/X/Y/ProX/ProY` | various | ARCHIVE | Reinforcement-timer colors and positions (normal + widescreen "pro" layout). |
| `cg_errordecay` | `100` | — **[debug/advanced]** | Prediction-error smoothing decay rate. |
| `cg_fov` | `90` | ARCHIVE | Player field-of-view. |
| `cg_fragsY` / `cg_fragsWidth` | `0` / `16` | ARCHIVE | Frags counter position/width. |
| `cg_gun_frame` | `0` | TEMP **[debug]** | Forces the view-weapon to a specific animation frame. |
| `cg_gunX` / `cg_gunY` / `cg_gunZ` | `0` each | ARCHIVE | View-weapon position offset, for muzzle/viewmodel tuning. |
| `cg_hudAlpha` | `1` | ARCHIVE | Global HUD element opacity. |
| `cg_hudFiles` | `ui_mp/hud.txt` | ARCHIVE | Path to the HUD layout definition file. |
| `cg_lagometer` | `0` | ARCHIVE | Lagometer graph toggle. |
| `cg_lagometerX` / `cg_lagometerY` | `-55` / `340` | ARCHIVE | Lagometer position. |
| `cg_markTime` | `10000` (ms) | ARCHIVE | Bullet/explosion decal-mark lifetime on surfaces. |
| `cg_muzzleFlash` | `1` | ARCHIVE | Weapon muzzle-flash effect toggle. |
| `cg_noChat` | `0` | ARCHIVE | Suppress chat display entirely. |
| `cg_noplayeranims` | `0` | CHEAT **[debug]** | Disables other players' animation playback. |
| `cg_nopredict` | `0` | — **[debug/advanced]** | Disables client-side movement prediction. |
| `cg_notifyPlayerOnly` | `0` | ARCHIVE | Demo-playback-only "only events involving me" filter for the `cg_notifyText*` notify-text feed (not `cg_notifyTextPlayerOnly` despite the naming pattern); has no effect during live gameplay. |
| `cg_notifyTextX/Y/Shadow/Width/Height/Lines` | `0`/`42`/`0`/`8`/`8`/`5` | ARCHIVE | Kill-feed-style notify-text position, drop-shadow, cell size, and max lines. |
| `cg_predictItems` | `1` | ARCHIVE | Client-side prediction of item pickups. |
| `cg_quickMessageAlt` | `1` | ARCHIVE | Selects the alternate layout for the quick-message (radio command) menu. |
| `cg_railTrailColor` | `FF0000FF` | ARCHIVE **[debug]** | Hex RGBA color override for the same debug bullet-trail visualization, parsed via `Com_ParseHexColor`. |
| `cg_railTrailTime` | `400` | ARCHIVE **[debug]** | Lifetime (ms) of the rail-style debug bullet-trail visualization (`EV_RAILTRAIL`/`CG_RailTrail`) — per its own code comment, re-inserted as a debug mechanism for bullets, not a normal weapon effect. |
| `cg_shadows` | `1` | ARCHIVE | Player/entity blob-shadow toggle. Also registered by the renderer (as `r_shadows`) reading the same cvar name. |
| `cg_showblood` | `1` | ARCHIVE | Blood/gore visibility toggle. |
| `cg_showmiss` | `0` | — **[debug]** | Logs prediction-miss debug info. |
| `cg_showPriorityText` / `cg_priorityTextX` / `cg_priorityTextY` | `1` / `0` / `350` | ARCHIVE | Priority/objective text banner toggle + position. |
| `cg_simpleItems` | `0` | ARCHIVE | Draw items as flat 2D icons instead of 3D models. |
| `cg_skybox` | `1` | CHEAT | Skybox rendering toggle. Also registered by the renderer as `r_portalsky`. |
| `cg_smallFont` / `cg_bigFont` | `0.25` / `0.4` | ARCHIVE | HUD text small/big font scale. |
| `cg_spawnTimer_set` / `cg_spawnTimer_period` | `-1` / `0` | TEMP **[debug/advanced]** | Internal state for the player-settable spawn-timer display; not meant to be hand-edited directly. |
| `cg_stats` | `0` | — **[debug]** | Prints the current cgame client-frame counter to the console every frame. |
| `cg_stereoSeparation` | `0.4` | ARCHIVE | Red/blue stereo-3D eye separation. |
| `cg_teamChatTime` / `cg_teamChatHeight` | `8000` / `8` | ARCHIVE | Team-chat overlay message lifetime / row height. |
| `cg_teamObituaryColors` | `0` | ARCHIVE | Enable custom-colored kill-feed (obituary) text. |
| `cg_teamObituaryColorSame/SameTK/Enemy/EnemyTK` | green/mdgreen/red/mdred | ARCHIVE | Kill-feed text colors for same-team kill, team-kill, enemy kill, enemy team-kill. |
| `cg_teamOverlayMaxLocationWidth` | `20` | ARCHIVE | Max width for the location string in the team overlay. |
| `cg_teamOverlayX` / `cg_teamOverlayY` | `-1` / `0` | ARCHIVE | Team status-overlay position. |
| `cg_tracerchance` | `0.4` | CHEAT **[debug/advanced]** | Probability a given hitscan shot spawns a visible tracer. |
| `cg_tracers` | `1` | ARCHIVE | Bullet tracer effect toggle. |
| `cg_tracerSpeed` | `4500` | CHEAT **[debug/advanced]** | Tracer effect travel speed. |
| `cg_tracerwidth` / `cg_tracerlength` | `0.8` / `160` | CHEAT **[debug/advanced]** | Tracer effect dimensions. |
| `cg_uselessNostalgia` | `0` | ARCHIVE | Minimal-HUD mode — draws only the crosshair and a bare 2D layer, skipping the rest of the HUD. |
| `cg_viewsize` | `100` | ARCHIVE | 3D viewport size (screen %) vs. HUD border. |
| `cg_voiceSpriteTime` | `6000` | ARCHIVE | Duration of the voice-command icon shown above a player's head. |
| `cg_weaponIconX`, `cg_ammoIconX`, `cg_ammoValueX`, `cg_chargeBarX`, `cg_sprintBarX`, `cg_healthX` | `-99999` each | ARCHIVE | Per-element HUD X-position overrides for widescreen layouts; `-99999` = use the `hud.txt`-authored position unchanged. |
| `cg_widescreen` | `0` | ARCHIVE | Widescreen HUD layout toggle. `0` = classic non-uniform stretch (pixel-identical to pre-widescreen-fix engine), `1` = corrected/extended layout. Defaults off so upgrades are a silent visual no-op. |
| `cg_wolfparticles` | `1` | ARCHIVE | Particle-effects toggle. |
| `ch_font` | `0` | ARCHIVE\|LATCH | HUD character-set style: `0`=default wolf hudchars, `1`/`2`=OSP1/OSP2 style. |
| `com_buildScript` | `0` | — **[debug]** | Build-time data-validation pass — forces loading of all possible assets and errors on any failure; also registered independently by both renderer backends under the same name. |

## Crosshair/Weapon

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_autoactivate` | `1` | ARCHIVE | Auto-pickup items on touch. |
| `cg_autoReload` | `1` | ARCHIVE | Auto-reload weapon when magazine empties. |
| `cg_autoswitch` | `2` | ARCHIVE | Auto-switch to picked-up weapons (client-side pass-through default is `0`). |
| `cg_brassTime` | `2500` | ARCHIVE | Ejected-brass shell casing lifetime. |
| `cg_crosshairAlpha` / `cg_crosshairAlphaAlt` | `1.0` / `1.0` | ARCHIVE | Crosshair opacity (normal / alt-fire). |
| `cg_crosshairColor` / `cg_crosshairColorAlt` | White / White | ARCHIVE | Crosshair color name (normal / alt-fire). |
| `cg_crosshairHealth` | `1` | ARCHIVE | Color the crosshair by own health. |
| `cg_crosshairPulse` | `1` | ARCHIVE | Pulse/animate the crosshair on hit or fire. |
| `cg_crosshairSize` | `48` | ARCHIVE | Crosshair size. |
| `cg_crosshairX` / `cg_crosshairY` | `0` / `0` | ARCHIVE | Crosshair position offset. |
| `cg_customCrosshair` | `0` | ARCHIVE | Enable custom-drawn crosshair instead of the built-in shader crosshair. |
| `cg_customCrosshairColor` / `ColorAlt` | `000000FF` / `FFFFFFFF` | ARCHIVE | Custom crosshair RGBA color (primary/alt). |
| `cg_customCrosshairHeight/Width/Thickness/ThicknessAlt` | `5`/`5`/`1`/`1` | ARCHIVE | Custom crosshair line dimensions (primary + alt-fire). |
| `cg_customCrosshairVMirror` | `1` | ARCHIVE | Vertically mirror the custom crosshair line layout. |
| `cg_customCrosshairXGap/YGap` | `0` / `0` | ARCHIVE | Gap between custom crosshair segments (center dead-zone). |
| `cg_customCrosshairXOffset/YOffset` | `0` / `0` | ARCHIVE | Custom crosshair position offset. |
| `cg_cycleAllWeaps` | `1` | ARCHIVE | Include all weapons (not just class-relevant) in weapon cycling. |
| `cg_drawCrosshair` | `1` (cgame) / `4` (UI default) | ARCHIVE | Crosshair style/shape selector. |
| `cg_drawCrosshairNames` | `1` | ARCHIVE | Show enemy player names when crosshair is over them. |
| `cg_drawCrosshairPickups` | `1` | ARCHIVE | Show pickup-item name when crosshair is over it. |
| `cg_drawGun` / `cg_drawFPGun` | `1` / `1` | ARCHIVE | Draw the first-person weapon view model. |
| `cg_drawSpreadScale` | `1` | ARCHIVE | Weapon accuracy/spread-cone visualization toggle. |
| `cg_noAmmoAutoSwitch` | `0` | ARCHIVE | Disable auto weapon-switch on running out of ammo. |
| `cg_reticleBrightness` | `0.7` | ARCHIVE | Scope reticle/overlay brightness. |
| `cg_reticles` | `1` | CHEAT | Scope reticle overlay toggle. |
| `cg_reticleType` | `1` | ARCHIVE | Scope reticle style. |
| `cg_useWeapsForZoom` | `1` | ARCHIVE | Whether weapon-fire is usable while zoomed. |
| `cg_weaponCycleDelay` | `150` (ms) | ARCHIVE | Delay between weapon-cycle inputs. |
| `cg_zoomDefaultSniper` | `20` | ARCHIVE | Default zoom FOV — used for all scoped/zoom weapon types, not just the sniper rifle (the Binoc/Snooper/FG variants of this cvar are unused). |
| `cg_zoomedFOV` | `90` | ARCHIVE | FOV while scoped/zoomed. |
| `cg_zoomedSens` | `0.3` | ARCHIVE | Mouse sensitivity multiplier while zoomed. |
| `cg_zoomedSensLock` | `0` | ARCHIVE | Lock mouse sensitivity while zoomed instead of scaling it. |
| `cg_zoomStepSniper` | `2` | ARCHIVE | Zoom-in/out step size — used for all scoped/zoom weapon types (the Binoc/Snooper/FG variants of this cvar are unused). |

## Demo Playback & Spectator

Covers demo recording/playback and the NDP (new demo player) seek/scrub
system. The server-side WTV round-demo system is listed under
[Competitive](#competitive) instead.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_antilagDemoView` | `1` | ARCHIVE | Toggle antilag-corrected rendering of other players while watching demos. |
| `cg_currentSelectedPlayer` | `0` | ARCHIVE | Spectator's currently-followed player index (cgame-side selection state). |
| `cg_selectedPlayer` / `cg_selectedPlayerName` | `0` / `""` | ARCHIVE | UI-side spectator player-selection state, used by the scoreboard/follow menu (separate registration from the cgame pair above). |
| `cg_thirdPerson` | `0` | CHEAT | Third-person camera toggle — used for spectating/demo camera work. |
| `cg_thirdPersonRange` / `cg_thirdPersonAngle` | `80` / `0` | CHEAT | Third-person camera distance/angle. |
| `cg_wtvFreecam` | `0` | CHEAT | Enable a free-fly spectator camera. |
| `cg_wtvFreecamSpeed` | `480` | ARCHIVE | Free-cam movement speed. |
| `cg_wtvFreecamSprintMultiplier` | `2.5` | ARCHIVE | Free-cam sprint speed multiplier. |
| `cl_avidemo` / `cl_forceavidemo` | `0` / `0` | — | Captures demo playback frames to disk for AVI export. |
| `cl_demoPlayer` | `1` | ARCHIVE | Selects/enables the new demo player (NDP) vs. legacy playback. |
| `cl_freezeDemo` | `0` | TEMP | Pauses demo playback. |
| `timedemo` | `0` | — | Benchmarks demo playback at max speed, reporting FPS. |
| `timescale` | `1` | — | Demo playback speed multiplier — used for slow-motion/fast-forward review. |

## Stats Windows/Popups

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_descriptiveText` | `1` | ARCHIVE | Show descriptive tooltip text in the limbo menu. |
| `cg_popupLimboMenu` | `1` | ARCHIVE | Auto-pop the limbo/class-select menu open on death/spawn wait. |
| `cg_registeredPlayers` | `1` | ARCHIVE | Selects which server chat/print command family is used for "registered player" (username-based) vs. plain netname display/chat. |
| `cg_statsX` / `cg_statsY` | `5` / `385` | ARCHIVE | Position of the client-game-stats (`+stats`) popup window. |
| `cg_topshotsX` / `cg_topshotsY` | `388` / `385` | ARCHIVE | Position of the "topshots" (`+wtopshots`) popup window. |
| `cg_wstatsX` / `cg_wstatsY` | `5` / `385` | ARCHIVE | Position of the weapon-stats (`+wstats`) popup window. |

## Sound

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cg_announcer` | `1` | ARCHIVE | Announcer voice-over toggle (round start/end, objectives). |
| `cg_chatBeep` | `0` | ARCHIVE | Play a beep sound on incoming chat message. |
| `cg_footsteps` | `1` | CHEAT | Footstep sound toggle. |
| `cg_hitsoundBodyStyle` / `cg_hitsoundHeadStyle` | `1` / `1` | ARCHIVE | Body-hit / headshot sound variant, sent to the server as part of hitsound config. |
| `cg_hitsounds` | `0` | ARCHIVE | Client-side enable for hit-confirmation sound on landing a shot; sent to the server as part of userinfo. Server-side companion is `g_hitsounds`. |
| `cg_noTaunt` | `0` | ARCHIVE | Disable taunt/voice-command playback. |
| `cg_noVoiceChats` / `cg_noVoiceText` | `0` / `0` | ARCHIVE | Suppress voice-chat command sounds / text display. |
| `cg_teamChatsOnly` | `0` | ARCHIVE | Restrict incoming chat display/sound to team chat only. |
| `cl_wavefilerecord` | `0` | TEMP | Enables recording gameplay audio to a WAV file. |
| `com_soundMegs` | (build default) | LATCH\|ARCHIVE | Sound-system memory pool size (MB). |
| `s_defaultsound` | `0` | ARCHIVE | Plays a placeholder beep for missing sound assets. |
| `s_doppler` | `1` | ARCHIVE | Doppler effect toggle for moving sound sources. |
| `s_initsound` | `1` | — | Master enable for the sound subsystem; if `0`, sound init aborts early. |
| `s_khz` | `22` | ARCHIVE | Audio device sample rate (kHz). |
| `s_mixahead` | `0.2` | ARCHIVE | Audio mix-ahead buffer time (latency/underrun tradeoff). |
| `s_mixPreStep` | `0.05` | ARCHIVE | Mix pre-step time, affects mixing granularity. |
| `s_musicvolume` | `0.25` | ARCHIVE | Background music volume. |
| `s_mute` | `0` | TEMP | Full audio mute. |
| `s_nocompressed` | `0` | INIT | Disables compressed (ADPCM/wavelet) sound loading. |
| `s_separation` | `0.5` | ARCHIVE | Stereo separation for positional audio. |
| `s_show` | `0` | CHEAT **[debug]** | Logs active sound-channel debug info. |
| `s_testsound` | `0` | CHEAT **[debug]** | Plays a test tone instead of actual sound data, for testing the sound system. |
| `s_volume` | `0.8` | ARCHIVE | Master sound volume. |
| `s_wavonly` | `0` | ARCHIVE\|LATCH | Forces WAV-only playback, disabling compressed formats. |
| `sndbits` / `sndspeed` / `sndchannels` / `snddevice` **(Linux)** | `16` / `0` / `2` / `/dev/dsp` | ARCHIVE | Legacy OSS sound-hardware config for the Linux `/dev/dsp` audio backend. |

## Video/Display

Cvars marked **(VK)** or **(GL)** only apply to that renderer; unmarked
cvars work the same on both.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_colorbits` / `r_texturebits` / `r_stencilbits` / `r_depthbits` **(GL)** | `0` each | ARCHIVE\|LATCH | Framebuffer/texture/stencil/Z-buffer bit depth (`0` = auto). |
| `r_customaspect` **(GL)** | `1` | ARCHIVE\|LATCH | Aspect-ratio override for custom resolutions. |
| `r_customwidth` / `r_customheight` | `1600` / `1024` | ARCHIVE\|LATCH | Custom resolution when `r_mode -1`/custom is selected. |
| `r_displayRefresh` | `0` | LATCH | Requested display refresh rate (`0` = desktop default). |
| `r_fullscreen` | `1` | ARCHIVE\|LATCH | Toggles fullscreen vs. windowed mode. |
| `r_fullscreenDesktop` **(VK)** | `1` | ARCHIVE\|LATCH | Use desktop resolution/borderless fullscreen instead of an explicit fullscreen mode. |
| `r_fullscreenStretch` **(VK)** | `0` | ARCHIVE\|LATCH | Stretch a non-native resolution to fill the display rather than letterbox/native-scale. |
| `r_fullscreenWidth` / `r_fullscreenHeight` **(VK)** | `1920` / `1080` | ARCHIVE\|LATCH | Exclusive-fullscreen dimensions. |
| `r_gamma` | `1.2`–`1.3` | ARCHIVE | Display gamma correction. |
| `r_glDriver` **(GL)** | (platform default) | ARCHIVE\|LATCH | Name of the OpenGL driver/library to load. |
| `r_highQualityVideo` | `1` | ARCHIVE | Quality toggle for in-engine cinematic/video playback. |
| `r_ignorehwgamma` **(GL)** | `1` | ARCHIVE\|LATCH | Forces software gamma ramp instead of hardware gamma. |
| `r_inGameVideo` | `1` | ARCHIVE | Enables in-game cinematic (RoQ) playback. |
| `r_mode` | GL `3`, VK `-3` | ARCHIVE\|LATCH | Video mode index into the resolution table. VK's default (`-3` = unmanaged) means VK ignores this by default and uses the explicit `r_windowed*`/`r_fullscreen*` cvars below instead. |
| `r_noborder` | `0` | ARCHIVE\|LATCH | Borderless-window mode. |
| `r_stereo` | `0` | ARCHIVE\|LATCH | Stereoscopic (red/blue) rendering. |
| `r_swapInterval` | `0` | ARCHIVE | Vsync (buffer swap wait). |
| `r_windowedWidth` / `r_windowedHeight` **(VK)** | `1280` / `720` | ARCHIVE\|LATCH | Windowed-mode dimensions. |
| `vid_xpos` / `vid_ypos` | `3` / `22` | ARCHIVE | Windowed-mode window position (Windows only). |

## Rendering Quality & Performance

These apply to both renderers unless marked otherwise.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cm_debugSize` | `2` | — **[debug]** | Debug-draw size for collision-model visualization. |
| `cm_noAreas` / `cm_noCurves` | `0` / `0` | CHEAT **[debug]** | Disable area-portal / curved-surface collision. |
| `cm_playerCurveClip` | `1` | ARCHIVE\|CHEAT **[debug/advanced]** | Whether player movement clips against curved (patch) surfaces. |
| `com_affinityMask` | `""` | ARCHIVE | CPU core affinity mask override. |
| `com_hunkMegs` | (build default) | LATCH\|ARCHIVE | Hunk (level/asset) memory-pool size (MB); commonly raised for large custom maps. |
| `com_maxfps` | `85` | ARCHIVE | Client frame-rate cap. |
| `com_yieldCPU` | `1` | ARCHIVE | Yields CPU time back to the OS instead of busy-waiting when idle/unfocused. |
| `com_zoneMegs` | (build default) | LATCH\|ARCHIVE | General-purpose engine memory-pool size (MB). |
| `r_ambientScale` / `r_directedScale` | `0.5` / `1` | CHEAT | Ambient / directed lighting scale. |
| `r_bonesDebug` | `0` | CHEAT **[debug]** | Draws model skeleton/bone debug overlays. |
| `r_cache` / `r_cacheShaders` **(GL)** | `1` each | LATCH | Model/shader disk caching. |
| `r_colorMipLevels` | `0` | LATCH **[debug]** | Tints each mip level a different color, to visualize mip selection. |
| `r_debuglight` | `0` | TEMP **[debug]** | Logs dynamic-light debug info. |
| `r_debugSort` | `0` | CHEAT **[debug]** | Logs surface-sort debug info. |
| `r_debugSurfaceUpdate` | `1` | — **[debug]** | Logs patch-surface (curve) update debug info. |
| `r_detailtextures` | `1` | ARCHIVE\|LATCH | Enables the detail-texture overlay pass. |
| `r_drawSun` | `1` | ARCHIVE | Sun-flare rendering toggle. |
| `r_dynamiclight` | `1` | ARCHIVE | Dynamic light rendering (muzzle flashes, explosions). |
| `r_ext_texture_filter_anisotropic` **(GL)** | `0` | ARCHIVE\|LATCH | Anisotropic texture filtering level (GL; VK's equivalent is `r_anisotropy`, default `16`). |
| `r_facePlaneCull` | `1` | ARCHIVE | Backface culling on axial planes (perf). |
| `r_fastsky` | `0` | ARCHIVE | Renders a flat-color sky instead of a skybox (perf). |
| `r_finish` | `0` | ARCHIVE | Forces GPU sync each frame (perf-costly, reduces latency). |
| `r_flareFade` | `5` | CHEAT | Flare fade rate. |
| `r_flares` | `1` | ARCHIVE | Lens-flare rendering toggle. |
| `r_flareSize` | `40` | CHEAT | Flare sprite size. |
| `r_ignoreFastPath` | `1` | ARCHIVE\|LATCH | Disables a rendering shortcut used to speed up drawing vertex data. |
| `r_ignoreGLErrors` **(GL)** | `1` | ARCHIVE | Suppresses GL error checking/aborts. |
| `r_intensity` | `1` | LATCH | Global texture/light intensity multiplier. |
| `r_lodbias` | `0` | ARCHIVE | Model LOD bias. |
| `r_lodCurveError` | `250` | ARCHIVE | LOD error tolerance for curve tessellation. |
| `r_mapOverBrightBits` | `2` | LATCH | Per-map overbright bit override. |
| `r_maxpolys` / `r_maxpolyverts` | (build defaults) | — | Max dynamic polys/verts per frame budget. |
| `r_nocurves` / `r_drawworld` / `r_lightmap` / `r_portalOnly` / `r_showSmp` / `r_skipBackEnd` / `r_measureOverdraw` / `r_lodscale` / `r_norefresh` / `r_drawentities` / `r_ignore` / `r_nocull` / `r_novis` / `r_showcluster` / `r_verbose` / `r_logFile` / `r_debugSurface` / `r_nobind` / `r_showtris` / `r_showsky` / `r_shownormals` / `r_clear` / `r_drawBuffer` **(GL)** / `r_lockpvs` / `r_noportals` | id-tech3 defaults | CHEAT **[debug]** | Classic id Tech 3 renderer debug/visualization toggles (world culling, wireframe, overdraw, normals, PVS lock, etc.) — registered by both backends. |
| `r_offsetfactor` / `r_offsetunits` **(GL)** | `-1` / `-2` | CHEAT | Polygon-offset factor/units (decal Z-fighting tuning). |
| `r_overBrightBits` | `1` | ARCHIVE\|LATCH | Overbright lighting scale bits. |
| `r_picmip` | `1` | ARCHIVE\|LATCH | Texture downscale level (mip bias) — main quality/performance tradeoff, clamped 0–16. |
| `r_primitives` **(GL)** | `0` | ARCHIVE **[debug]** | Forces a specific GL primitive-batching mode. |
| `r_printShaders` | `0` | — **[debug]** | Prints every registered shader's name on load. |
| `r_railWidth` / `r_railCoreWidth` / `r_railSegmentLength` | `16` / `1` / `32` | ARCHIVE | Rail trail (weapon beam) visual dimensions. |
| `r_rmse` | `0.0` | ARCHIVE\|LATCH **[debug/advanced]** | Error threshold used when resizing images during loading. |
| `r_roundImagesDown` | `1` | ARCHIVE\|LATCH | Round non-power-of-two image dimensions down instead of up on load. |
| `r_saveFontData` | `0` | — **[debug]** | Dumps baked font atlas data to disk. |
| `r_showImages` | `0` | TEMP **[debug]** | Draws all loaded textures to screen as a grid, instead of the normal scene. |
| `r_simpleMipMaps` | `1` | ARCHIVE\|LATCH | Uses simplified (box-filter) mip generation vs. a higher-quality filter. |
| `r_singleShader` | `0` | CHEAT\|LATCH **[debug]** | Forces every surface to render with a single shader, for perf isolation testing. |
| `r_smp` **(GL)** | auto (CPU-count based) | ARCHIVE\|LATCH | Multi-threaded render backend. |
| `r_speeds` | `0` | CHEAT **[debug/advanced]** | Prints a render timing/stat overlay — commonly used by competitive players to check frame cost. |
| `r_subdivisions` | `4` | ARCHIVE\|LATCH | Curved-surface (patch) tessellation LOD granularity. |
| `r_textureMode` | `GL_LINEAR_MIPMAP_NEAREST` | ARCHIVE | Texture filtering mode. |
| `r_vertexLight` | `0` | ARCHIVE\|LATCH | Forces vertex lighting instead of lightmaps — large perf win, quality loss. |
| `r_wolffog` | `1` | CHEAT | Enables RTCW's custom fog system. |
| `r_zfar` | `0` | CHEAT | Far clip plane override (`0` = auto). |
| `r_znear` | `4` | CHEAT | Near clip plane distance (clamped 0.001–200). |

## Vulkan-specific

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_alphaboost` | `1.0` | ARCHIVE | Alpha-blend intensity boost. |
| `r_anisotropy` | `16` | ARCHIVE\|LATCH | Anisotropic texture filtering level (VK's equivalent of GL's `r_ext_texture_filter_anisotropic`). |
| `r_debugInput` | `0` | TEMP **[debug]** | Logs debug info about input events on the Vulkan renderer. |
| `r_debugUI` | `0` | TEMP **[debug]** | Draws the Vulkan backend's ImGui debug UI. |
| `r_gpu` | `0` | ARCHIVE\|LATCH | Selects which physical GPU device to use (`0` = auto/first). |
| `r_mipFilter` | `1` | ARCHIVE\|LATCH | Mipmap filter mode selection. |
| `r_monitor` | `0` | ARCHIVE\|LATCH | Selects which display monitor to use in fullscreen mode; the OpenGL renderer has its own separate copy of this cvar too. |
| `r_msaa` | `8` | ARCHIVE\|LATCH | MSAA sample count. |
| `r_sleepThreshold` | `2500` | ARCHIVE | Microsecond threshold controlling when the render thread sleeps vs. spin-waits (frame pacing/CPU tradeoff). |

## OpenGL-specific

Mostly legacy vendor-extension cvars (ATI TruForm, NVIDIA fog distance)
with little relevance on modern hardware, kept for completeness.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `r_allowExtensions` | `1` | ARCHIVE\|LATCH | Master switch for using any GL extensions. |
| `r_allowSoftwareGL` | `0` | LATCH | Allows falling back to a software GL renderer. |
| `r_ati_truform_tess` | `1` | ARCHIVE | ATI TruForm N-patch tessellation tuning (legacy ATI hardware). |
| `r_ext_ATI_pntriangles` | `0` | ARCHIVE\|LATCH | Legacy ATI N-patch (PN-triangles) extension enable, separate from the TruForm tuning cvars above. |
| `r_ext_compiled_vertex_array` | `1` | ARCHIVE\|LATCH | Compiled-vertex-array extension usage (a legacy performance optimization). |
| `r_ext_compressed_textures` | `1` | ARCHIVE\|LATCH | Enables S3TC/DXT texture compression. |
| `r_ext_gamma_control` | `1` | ARCHIVE\|LATCH | Uses the hardware gamma-ramp extension. |
| `r_ext_multitexture` | `1` | ARCHIVE\|LATCH | Multitexture extension usage. |
| `r_ext_NV_fog_dist` / `r_nv_fogdist_mode` | `0` / mode string | ARCHIVE\|LATCH / ARCHIVE | NVIDIA distance-fog extension and mode. |
| `r_ext_texture_env_add` | platform-dependent | ARCHIVE\|LATCH | `GL_ADD` texture environment extension. |
| `r_glIgnoreWicked3D` | `0` | ARCHIVE\|LATCH | Ignores the legacy Wicked3D stereoscopic-driver extension. |
| `r_lastValidRenderer` | `(uninitialized)` | ARCHIVE **[debug]** | Records the last GL driver string that initialized successfully, for crash-recovery fallback. |
| `r_maskMinidriver` | `0` | LATCH | Hides the legacy "minidriver" OpenGL mode from detection. |
| `r_previousglDriver` | `""` | ROM **[debug]** | Engine-recorded previous GL driver name (Linux). |

## Netcode/Connection

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_autoNudge` | `0` | TEMP | Automatic time-nudge adjustment toggle. |
| `cl_debugMove` | `0` | — **[debug]** | Logs client movement-input debug info. |
| `cl_guid` | (generated) | ROM\|USERINFO | Client hardware/identity GUID sent to the server for anti-cheat/stats tracking. |
| `cl_maxpackets` | `30` (client-registered) — but see note | ARCHIVE | Max outgoing packets/sec sent to the server. **Note:** cgame also registers `cl_maxpackets` as a pass-through with default `125` — whichever module's registration runs first on a fresh profile wins the archived default; worth double-checking your actual value with `\cl_maxpackets`. |
| `cl_maxPing` | `800` | ARCHIVE | Ping threshold used by the server browser/UI to flag high-ping servers. |
| `cl_nodelta` | `0` | — | Forces full (non-delta) snapshots from the server — netcode debug/compat cvar. |
| `cl_packetdup` | `1` | ARCHIVE | Number of duplicate copies of each outgoing packet (loss mitigation). |
| `cl_run` | `1` | ARCHIVE | Always-run movement mode; also affects movement prediction. |
| `cl_serverStatusResendTime` | `750` | — | Milliseconds between server-status query retries. |
| `cl_shownet` | `0` | TEMP **[debug/advanced]** | Logs incoming server-message parsing, a classic netcode diagnostic. |
| `cl_showSend` | `0` | TEMP **[debug/advanced]** | Logs outgoing client packet sends. |
| `cl_showServerCommands` | `0` | — **[debug/advanced]** | Logs server-to-client reliable commands. |
| `cl_showTimeDelta` | `0` | TEMP **[debug/advanced]** | Logs client/server time-delta adjustments. |
| `cl_timeNudge` | `0` | TEMP | Manual client-side time nudge (interpolation delay adjustment) — classic competitive netcode cvar. **Note:** cgame also registers the same cvar (case-insensitively identical) as `cl_timenudge` with default `0` and `ARCHIVE\|LATCH`, so an archived value can persist across sessions unlike this module's plain `TEMP` registration. |
| `cl_timeout` | `200` | — | Seconds of no server response before disconnecting. |
| `con_restricted` | `0` | INIT | Restricts console command execution (competitive/anti-cheat related). |
| `debug_protocol` | `""` | — **[debug]** | Overrides the reported protocol version string, for compatibility testing. |
| `debuggraph` / `graphheight` / `graphscale` / `graphshift` | `0`/`32`/`1`/`0` | CHEAT **[debug/advanced]** | Enable/size/scale/shift for the generic debug-graph overlay (used by `timegraph` and similar). |
| `g_synchronousClients` | — | — | See [Competitive](#competitive) — disables prediction for smoother demo recording. |
| `name`, `model`, `head`, `color`, `handicap`, `password` | various | USERINFO(\|ARCHIVE) | Player identity/config fields sent as connection userinfo. |
| `net_dropsim` | `""` | TEMP **[debug]** | Simulates network packet loss (client). |
| `net_enabled` | `1` | LATCH\|ARCHIVE_ND\|NORESTART | Master enable for the network subsystem. |
| `net_ip` | `0.0.0.0` | LATCH | Local IP address to bind to. |
| `net_port` | `27960` | LATCH\|NORESTART | UDP port to listen/connect on. |
| `net_proxy` | `""` | TEMP | Proxy/relay address used when connecting through a relay. |
| `net_qport` | (randomized) | INIT | Client "qport" identifier used to disambiguate connections behind NAT. |
| `net_socksEnabled` | `0` | LATCH | Enables connecting through a SOCKS proxy. |
| `net_socksServer` / `net_socksPort` | `""` / `1080` | LATCH\|ARCHIVE_ND | SOCKS proxy address/port. |
| `net_socksUsername` / `net_socksPassword` | `""` / `""` | LATCH\|ARCHIVE_ND | SOCKS proxy credentials. |
| `rate` | `5000` | USERINFO\|ARCHIVE | Client bandwidth cap sent to the server (bytes/sec). |
| `rconAddress` | `""` | — | Address rcon commands are sent to when not connected. |
| `rconPassword` (client, `rcon_client_password`) | `""` | TEMP | Remote-console password used by the client. |
| `showpackets` / `showdrop` | `0` / `0` | TEMP **[debug]** | Logs every outgoing/incoming packet, or dropped packets, to the console. |
| `snaps` | `20` | USERINFO\|ARCHIVE | Requested server snapshot rate. |
| `sv_packetdelay` / `cl_packetdelay` | `0` / `0` | CHEAT **[debug]** | Simulates added network latency (server-side / client-side). |
| `timegraph` (`cl_timegraph`) | `0` | CHEAT **[debug/advanced]** | Draws the frame-time graph overlay. |

## Input

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_anglespeedkey` | `1.5` | — | Multiplier applied when the "speed" key is held while turning. |
| `cl_bypassMouseInput` | `0` | — **[debug/advanced]** | Bypasses mouse-look input processing (e.g. while a menu/UI has focus). |
| `cl_freelook` | `1` | ARCHIVE | Mouse-look toggle. |
| `cl_mouseAccel` | `0` | ARCHIVE | Mouse acceleration factor. |
| `cl_showmouserate` | `0` | — **[debug]** | Logs raw mouse-input sample-rate debug info. |
| `cl_yawspeed` / `cl_pitchspeed` | `140` / `140` | ARCHIVE | Keyboard turn speed (yaw/pitch). |
| `in_debugjoystick` **(Windows)** | `0` | TEMP **[debug]** | Logs raw joystick input debug info. |
| `in_dgamouse` **(Linux)** | `1` | ARCHIVE | Uses XFree86 DGA mouse mode instead of standard input events. |
| `in_joyBallScale` | `0.02` | ARCHIVE | Joystick trackball sensitivity scale (Windows). |
| `in_joystick` | `0` | ARCHIVE\|LATCH | Enables joystick/gamepad input. |
| `in_midi` / `in_midiport` / `in_midichannel` / `in_mididevice` **(Windows)** | `0`/`1`/`1`/`0` | ARCHIVE | Legacy MIDI music playback device selection. |
| `in_mouse` **(Linux)** | `1` | ARCHIVE | Master enable for mouse input. On Windows, see `in_raw` instead. |
| `in_nograb` **(Linux)** | `0` | — **[debug]** | Disables input grabbing, so the mouse stays free over a windowed client (dev/testing). |
| `in_raw` **(Windows)** | `1` | ARCHIVE\|LATCH | `0` = legacy Win32 keyboard/mouse input, `1` = raw input via a dedicated input thread. |
| `joy_threshold` | `0.15` | ARCHIVE | Joystick axis dead-zone threshold. |
| `m_filter` | `0` | ARCHIVE | Mouse input filtering/smoothing toggle. |
| `m_forward` / `m_side` | `0.25` / `0.25` | ARCHIVE | Mouse forward-movement/strafe scale (forward rarely used). |
| `m_pitch` / `m_yaw` | `0.022` / `0.022` | ARCHIVE | Mouse pitch/yaw scale. |
| `sensitivity` | `5` | ARCHIVE | Mouse sensitivity. |

## Downloads

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_allowDownload` | `0` | ARCHIVE | Bitmask of client download settings: `1`=enable auto-download, `2`=disable HTTP/curl redirect downloads (force UDP), `4`=disable legacy UDP downloads, `8`=don't force-disconnect during a curl download (in practice this bit is only consulted from the server's `sv_allowDownload` value, not this cvar). Add the values together to combine. |
| `sv_allowDownload`, `sv_dlURL`, `sv_dlRate` | — | — | See [Server Administration](#server-administration) for the server-side counterparts. |

## Console/Client Misc

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `cl_conXOffset` | `0` | — **[debug/advanced]** | Horizontal pixel offset applied to console text. |
| `cl_debugTranslation` | `0` | — **[debug]** | Logs localized-string lookup debug info. |
| `cl_language` | `0` | ARCHIVE | UI/string localization selection. |
| `cl_noprint` | `0` | — **[debug]** | Suppresses console print output. |
| `com_hunkused` | `0` | — **[debug]** | Read-only-in-practice counter reporting current hunk memory usage. |
| `com_introplayed` | `0` | ARCHIVE **[debug/advanced]** | Tracks whether the startup intro cinematic has already played. |
| `com_noErrorInterrupt` | `0` | — **[debug]** | Suppresses the debugger-interrupt on engine errors. |
| `com_recommendedSet` | `0` | ARCHIVE **[debug/advanced]** | Tracks whether the recommended-settings detection has already run/been applied. |
| `com_showtrace` | `0` | CHEAT **[debug]** | Logs collision-trace call-count debug stats. |
| `com_speeds` | `0` | — **[debug]** | Prints per-frame engine subsystem timing stats. |
| `con_colorRed/Green/Blue/Alpha` | `0.5`/`0.5`/`0.5`/`1` | ARCHIVE | Console background tint RGBA. |
| `con_height` | `0.5` | ARCHIVE | Console drop-down height, as a fraction of screen height. |
| `con_notifytime` | `7` | — | Seconds a chat/console notify line stays on-screen. |
| `con_scale` | `1.0` | ARCHIVE\|LATCH | Console/HUD font scale — drives the vector-font atlas bake size. |
| `con_type` | `0` | ARCHIVE | Console rendering style variant. |
| `fixedtime` | `0` | CHEAT **[debug]** | Forces a fixed per-frame time delta. |
| `logfile` (`com_logfile`) | `0` | TEMP **[debug]** | Writes console output to `qconsole.log`. |
| `r_hudFontEnabled` | `1` | ARCHIVE\|LATCH | Master toggle for the TTF-baked vector font system vs. the classic bitmap `hudchars.tga`. |
| `r_hudFontFile` | `hudchars.ttf` | ARCHIVE\|LATCH | Path (under `fonts/`) of the TTF baked into the hudchars/console atlas. |
| `scr_conspeed` | `3` | — | Console drop/raise animation speed. |
| `viewlog` | `0` | CHEAT **[debug]** | Opens a separate console log-view window (Windows). |

## Menu/UI

Defaults for the host-game screen, server browser, and options menu.
Some of these are just menu-side copies of a cvar already covered above,
used so a menu slider or checkbox can read and change it (e.g.
`cg_drawCrosshair` and `ui_drawCrosshair` control the same setting). A
few of these pairs ship with different default values — e.g.
`cg_marktime` defaults to `10000` but `ui_marks` defaults to `20000` —
and whichever one loads first on a brand-new profile is the one that
sticks.

| Cvar | Default | Flags | Description |
|---|---|---|---|
| `headModel` | `""` | — | Internal selected-head-model state for the player-setup menu. |
| `mp_pistol` / `mp_item1` | `0` / `0` | — | Internal selected sidearm / grenade-type index. |
| `mp_playerType` / `mp_currentPlayerType` | `0` / `0` | — | Internal selected/current player-class index, read by both the UI and cgame limbo menus. |
| `mp_team` / `mp_currentTeam` | `0` / `0` | — | Internal selected/current team index (`0`=spectator, `1`=axis, `2`=allies). |
| `mp_weapon` | `0` | — | Internal selected primary-weapon index. |
| `ui_browserShowFull/ShowEmpty/ShowFriendlyFire/ShowMaxlives/ShowTourney/ShowPunkBuster/ShowAntilag` | `1`/`1`/`0`/`1`/`1`/`0`/`0` | ARCHIVE | Server-browser row-filter checkboxes. |
| `ui_cmd` | `""` | — | Internal scratch cvar used to stage a console command from a menu widget. |
| `ui_dedicated` | `0` | ARCHIVE | "Start dedicated server" checkbox state in the host-game menu. |
| `ui_gametype` / `ui_joinGametype` / `ui_netGametype` / `ui_actualNetGametype` | `3`/`0`/`0`/`5` | ARCHIVE | Gametype selectors for hosting/browsing/joining. |
| `ui_glCustom` | `4` | ARCHIVE | Custom graphics-quality preset index shown in the options menu. |
| `ui_isSpectator` | `1` | — | Internal limbo-menu spectator-mode state. |
| `ui_limboMode` / `ui_limboOptions` / `ui_limboPrevOptions` / `ui_limboObjective` | `0` each | — | Internal limbo/class-select menu navigation state. |
| `ui_mapIndex` / `ui_currentMap` / `ui_currentNetMap` | `0` each | ARCHIVE | Currently-selected map index in the host-game / server-browser map lists. |
| `ui_menuFiles` | `ui_mp/menus.txt` | ARCHIVE | Path to the menu-definition file the UI module loads. |
| `ui_netSource` | `0` | ARCHIVE | Server-browser source selector (internet/LAN/favorites). |
| `ui_objective` | `""` | — | Internal currently-displayed objective text in the limbo menu. |
| `ui_prevTeam` / `ui_prevClass` / `ui_prevWeapon` | `-1` each | — | Internal limbo-menu "previous selection" state, for cancel/back navigation. |
| `ui_Q3Model` | `1` | — | Internal flag marking whether the selected player skin uses the custom-model ("Q3-style") mode in the player-setup menu. |
| `ui_serverStatusTimeOut` | `7000` | ARCHIVE | Timeout (ms) for the server-info/status query popup. |
| `ui_team` / `ui_class` / `ui_weapon` | `Axis` / `Soldier` / `MP 40` | — | Internal limbo-menu team/class/weapon selection state, mirrored to `mp_team`/`mp_playerType`/`mp_weapon` below on confirm. |
| `ui_teamArenaFirstRun` | `0` | ARCHIVE | Internal one-time-setup flag so certain sound settings only latch on first run. |
| `ui_userTimeLimit` / `ui_userAlliedRespawnTime` / `ui_userAxisRespawnTime` | `0` each | — | Host-game screen custom time-limit/respawn-time entry fields. |

---

# Commands

## Server Administration (Commands)

| Command | Usage | Description |
|---|---|---|
| `addip` | `addip <ip-mask>` | **[rcon]** Adds an IP (`*` wildcards allowed) to the `g_banIPs` filter list. |
| `banClient` | `banClient <client number>` | **[rcon]** Bans a player by client slot number. |
| `banUser` | `banUser <player name>` | **[rcon]** Bans a player through the auth-server ban system. |
| `clientkick` | `clientkick <client number>` | **[rcon]** Kicks a player by client slot number; also issued internally by the vote-kick system. |
| `config` | `config <config-name>` | **[rcon]** Loads a named competitive-ruleset config file, setting `sv_GameConfig`. |
| `devmap` / `spmap` / `spdevmap` | `devmap <mapname>` | **[debug]** Like `map`, but enables `sv_cheats` (`spmap`/`spdevmap` are legacy single-player-loader aliases of the same function). |
| `dumpuser` | `dumpuser <player name>` | **[rcon]** Prints a player's full userinfo string. |
| `entitylist` | | **[debug]** Dumps the active entity list to the console. |
| `forceteam` | `forceteam <player> <team>` | **[rcon]** Forces a player onto a team, bypassing normal team-join rules. |
| `game_memory` | | **[debug]** Prints game-module memory usage stats. |
| `gameCompleteStatus` | | **[rcon]** Reports match/round completion status to the master-server API. |
| `heartbeat` | | **[rcon]** Forces an immediate master-server heartbeat. |
| `kick` | `kick <player name>` / `kick all` / `kick allbots` | **[rcon]** Disconnects a player (or everyone / all bots). |
| `killserver` | | **[rcon]** Immediately shuts down the server. |
| `listip` | | **[rcon]** Prints the current IP filter list (`g_banIPs`). |
| `listmaxlivesip` | | **[debug/advanced]** Prints GUIDs currently tracked by the maxlives rejoin-bypass enforcement (see `g_enforcemaxlives`). |
| `loadgame` | | **[debug]** Legacy single-player savegame loader; unused in multiplayer. |
| `map` | `map <mapname>` | **[rcon]** Loads a new map with `sv_cheats 0`. |
| `map_restart` | | **[rcon]** Restarts the current map without a full reload. |
| `overridespawntarget` | `overridespawntarget <index> <x y z>` | **[debug/advanced]** Map-scripting dev tool: overrides an objective spawn-target location at runtime. |
| `printspawntargets` | | **[debug/advanced]** Prints the current list of objective spawn-target overrides. |
| `ref` | — | See [Match/Round Control (Commands)](#matchround-control-commands). |
| `removeentity` | `removeentity <entityname>` | **[debug/advanced]** Removes a named map entity at runtime. |
| `removeip` | `removeip <ip-mask>` | **[rcon]** Removes an IP from the filter list. |
| `removespawnpoint` | `removespawnpoint <x y z>` | **[debug/advanced]** Removes a spawn point at the given location. |
| `rename` | `rename <clientNum> <newname>` | **[rcon]** Force-renames a connected player. |
| `say` | `say <text>` | **[rcon]** Broadcasts a chat line from the server console (dedicated server only). |
| `sectorlist` | | **[debug]** Dumps the world's collision-sector (BSP area) entity counts. |
| `serverinfo` | | **[rcon]** Prints the current serverinfo cvars. |
| `spawnptaxis` / `spawnptallies` | `spawnptaxis <setspawnpt#> <x y z>` | **[debug/advanced]** Overrides a numbered per-team reinforcement spawn point's location. |
| `status` | | **[rcon]** Lists connected players (name, score, ping, address). |
| `systeminfo` | | **[rcon]** Prints the current systeminfo cvars. |

## Match/Round Control (Commands)

| Command | Usage | Description |
|---|---|---|
| `callvote` | `callvote <type> [arg]` | Calls a vote; valid `<type>`s include `map`, `map_restart`, `g_gametype`, `kick`/`clientkick`, `start_match`, `reset_match`, `swap_teams`, `nextmap`, `timelimit`, `config`, `g_rocketmode`, `difficulty`. Gated by `g_voteFlags`. |
| `lock` / `unlock` | | Locks/unlocks your team to further players joining. |
| `maps` | `maps [search]` | Lists (or searches) the maps available on the server. |
| `more` | | Continues a paginated `maps` listing. |
| `pause` | | Calls a match timeout (see `match_timeoutlength`/`match_timeoutcount`). |
| `players` | | Lists the players currently on your team. |
| `ready` | | Marks yourself ready for the tournament-mode countdown. |
| `readyteam` | | Marks your entire team ready at once. |
| `ref` | `ref <putallies\|putaxis\|putspec\|speclock\|specunlock> [player]` | **[rcon]** Referee/admin team-management sub-commands: `putallies`/`putaxis`/`putspec <clientNum>` moves a player to a team or spectator; `speclock`/`specunlock` toggles the global spectator lock. This is the referee command mentioned above under [Competitive](#competitive). |
| `specinvite` | `specinvite <player>` | Invites a specific spectator to view your locked team. |
| `speclock` / `specunlock` | | Locks/unlocks your team to spectators joining it. |
| `specuninvite` | `specuninvite <player>` | Revokes one spectator's invite. |
| `specuninviteall` | | Revokes all spectator invites for your team. |
| `start_match` / `reset_match` / `swap_teams` | | **[rcon]** Server-console/rcon-only equivalents of the matching `callvote` types. |
| `unpause` | | Ends an active timeout early. |
| `unready` / `notready` | | Un-marks yourself as ready. |
| `vote` | `vote <yes\|no>` | Casts a vote on the vote currently in progress. |

## Gameplay Rules (Commands)

| Command | Usage | Description |
|---|---|---|
| `follow` | `follow <player>` | Spectator-follows a specific player. |
| `follownext` / `followprev` | | Cycles the spectator follow target to the next/previous player. |
| `forcetapout` | | Forces a downed player to give up instead of waiting for revive/bleed-out; gated by `g_allowForceTapout`. |
| `give` | `give <item\|all>` | **[debug]** `sv_cheats`-gated: gives an item/weapon/ammo. |
| `god` | | **[debug]** `sv_cheats`-gated: toggles invulnerability. |
| `kill` | | Suicides your current player (e.g. to force a respawn). |
| `levelshot` | | **[debug]** `sv_cheats`-gated dev tool that jumps to intermission and captures a menu level-preview screenshot. |
| `noclip` | | **[debug]** `sv_cheats`-gated: toggles no-clip flight movement. |
| `nofatigue` | | **[debug]** `sv_cheats`-gated: toggles unlimited sprint/stamina. |
| `notarget` | | **[debug]** `sv_cheats`-gated: toggles being ignored by AI/bots. |
| `score` | | Requests current scoreboard data (feeds the scoreboard/`+scores` HUD overlay). |
| `setspawnpt` | `setspawnpt <index>` | Sets your preferred objective spawn-point index, on maps with selectable reinforcement spawns. |
| `setviewpos` | `setviewpos <x> <y> <z> <yaw>` | **[debug]** `sv_cheats`-gated: teleports you to an exact origin/yaw. |
| `team` | `team [red\|blue\|spectator\|free]` | Joins a team, or with no argument prints your current team. |
| `where` | | **[debug/advanced]** Prints your current origin coordinates (useful alongside the spawn-point/objective override tools above). |

## Bot/AI (Commands)

| Command | Usage | Description |
|---|---|---|
| `bot` | `bot <subcommand>` | Forwards its arguments to the Omni-bot library's own console command handler (e.g. `kickbot`, used internally by `callvote kick` against a bot). |

## Chat & Communication (Commands)

| Command | Usage | Description |
|---|---|---|
| `messagemode` | | Opens the chat-entry prompt for `say`. |
| `messagemode2` | | Opens the chat-entry prompt for `say_team`. |
| `messagemode3` | | Opens the chat-entry prompt for a private message to whoever is under your crosshair. |
| `messagemode4` | | Opens the chat-entry prompt for a private message to your last attacker. |
| `say` | `say <text>` | Sends a chat message to everyone. |
| `say_limbo` | `say_limbo <text>` | Sends a chat message visible to limbo/dead teammates. |
| `say_team` | `say_team <text>` | Sends a chat message to your team. |
| `say_teamnl` | `say_teamnl <text>` | Team chat without a location prefix. |
| `tell` | `tell <player> <text>` | Sends a private message to one player. |
| `vsay` / `vsay_team` | `vsay <voice command>` | Plays a voice-chat line, to everyone or to your team. |

## Demo Playback & Spectator (Commands)

| Command | Usage | Description |
|---|---|---|
| `demo` | `demo <demoname>` | Plays back a recorded demo. |
| `demotimeline` | | Toggles the NDP demo-scrubbing timeline HUD overlay. |
| `freecam` | | Toggles the free-fly spectator camera (`cg_wtvFreecam`). |
| `ndpnextfrag` / `ndpprevfrag` | | Seeks the NDP (new demo player) demo to the next/previous frag. |
| `ndpplaypause` | | Toggles play/pause during NDP demo playback. |
| `playwtv` | `playwtv <filename> <clientNum>` | Plays back a server-recorded WTV round demo, initially following the given client number. |
| `record` | `record [demoname]` | Starts recording a demo of the current game. |
| `stoprecord` | | Stops the current demo recording. |
| `wtvfollow` | `wtvfollow <clientNum\|next\|prev>` | Switches which player a WTV recording is following. |
| `wtvplayers` | | Lists the identified players available to follow in the currently-playing WTV recording. |

## Stats Windows/Popups (Commands)

Console-printed and pop-up-window versions of the same stats (see
[Stats Windows/Popups](#stats-windowspopups)).

| Command | Usage | Description |
|---|---|---|
| `bottomshots` | | Prints the bottom (worst) weapon-accuracy rankings to console. |
| `cstats` | | Prints your own client stats to console. |
| `gamestats` | | Prints overall game stats to console. |
| `scores` | | Prints the scoreboard to console. |
| `scoresdump` | | Dumps full scoreboard data to console. |
| `sgstats` | | Prints "sub-game"/round stats to console. |
| `stats` | | Prints your own current-match stats to console. |
| `+stats` / `-stats` | | Hold to show the client-game-stats popup window (`cg_statsX`/`Y`). |
| `statsall` | | Prints all players' stats to console. |
| `stshots` | | Prints top-shots weapon rankings to console. |
| `topshots` | | Prints the top weapon-accuracy rankings to console. |
| `weaponstats` | | Prints per-weapon stat breakdowns to console. |
| `wstats` | | Prints your own weapon stats to console. |
| `+wstats` / `-wstats` | | Hold to show the weapon-stats popup window (`cg_wstatsX`/`Y`). |
| `+wtopshots` / `-wtopshots` | | Hold to show the "topshots" popup window (`cg_topshotsX`/`Y`). |

## Video/Display (Commands)

| Command | Usage | Description |
|---|---|---|
| `cropimages` | | **[debug]** Dev tool that crops/re-saves loaded images to disk. |
| `gfxinfo` | | Prints renderer/GPU capability info. |
| `gpulist` **(VK)** | | **[debug/advanced]** Lists available physical GPUs, for use with `r_gpu`. |
| `imagelist` | | **[debug]** Lists all currently-loaded textures. |
| `modellist` / `modelist` | | **[debug]** Lists loaded models / available video modes. |
| `printpools` **(VK)** | | **[debug]** Prints the Vulkan renderer's memory-pool allocation stats. |
| `screenshot` | `screenshot [filename]` | Captures a TGA screenshot. |
| `screenshotJPEG` | `screenshotJPEG [filename]` | Captures a JPEG screenshot. |
| `shaderlist` | | **[debug]** Lists all currently-registered shaders. |
| `skinlist` | | **[debug]** Lists all currently-loaded skins. |
| `vid_restart` | | Restarts the video subsystem, applying any pending `LATCH` render cvars. |

## Sound (Commands)

| Command | Usage | Description |
|---|---|---|
| `midiinfo` **(Windows)** | | **[debug]** Prints available MIDI device info. |
| `music` | `music <musicfile>` | Plays a background music track. |
| `play` | `play <soundfile>` | Plays a sound file once. |
| `s_info` | | **[debug]** Prints sound-subsystem device/config info. |
| `s_list` | | **[debug]** Lists all currently-loaded sounds. |
| `s_stop` | | Stops all currently-playing sounds. |
| `snd_restart` | | Restarts the sound subsystem, applying any pending `LATCH` sound cvars. |
| `streamingsound` | `streamingsound <soundfile>` | Plays a streamed (non-preloaded) sound file. |

## Netcode/Connection (Commands)

| Command | Usage | Description |
|---|---|---|
| `clientinfo` | | **[debug]** Prints your own client info state. |
| `cmd` | `cmd <text>` | **[debug/advanced]** Forwards a raw command string to the server as a client command. |
| `configstrings` | | **[debug]** Dumps all received configstrings. |
| `connect` | `connect <address>` | Connects to a server. |
| `disconnect` | | Disconnects from the current server. |
| `globalservers` | | Queries the master server(s) for an internet server list. |
| `localservers` | | Queries the LAN for servers. |
| `net_restart` | | Restarts the network subsystem, applying any pending `LATCH` `net_*` cvars. |
| `ping` | `ping <server>` | Pings a specific server address. |
| `rcon` | `rcon <command>` | Sends a remote-console command to the currently-addressed server, authenticated with `rconPassword`. |
| `reconnect` | | Reconnects to the last-connected server. |
| `serverstatus` | `serverstatus [server]` | Queries and prints a server's detailed status. |
| `showip` | | Prints your own detected IP address. |

## Movement/Weapon (Commands)

| Command | Usage | Description |
|---|---|---|
| `+activate` / `-activate` | | Uses or interacts with what you're looking at (doors, mounted guns, switches) while held. |
| `+attack` / `-attack` | | Fires your weapon's primary trigger while held. |
| `+attack2` / `-attack2` | | **[debug]** Sent as secondary fire, but no weapon logic reads it — has no combat effect. Its only real effect is incidental: counts as activity, and toggles ready-to-continue at intermission (same as `+attack`). |
| `+back` / `-back` | | Moves backward while held. |
| `+button1` / `-button1` | | Shows a "talking" icon above your head and disables movement/attack input while held; not bound to a key by default. |
| `+button4` / `-button4` | | The actual walk-modifier key, despite the generic name — walking is slower and quieter than running. |
| `+dropweapon` / `-dropweapon` | | Drops your current two-handed weapon (e.g. a mounted MG42 you've picked up) while held. |
| `+forward` / `-forward` | | Moves forward while held. |
| `+leanleft` / `-leanleft` | | Leans left around cover while held. |
| `+leanright` / `-leanright` | | Leans right around cover while held. |
| `+left` / `-left` | | Turns left while held (keyboard turning). |
| `+lookdown` / `-lookdown` | | Looks down while held (keyboard look). |
| `+lookup` / `-lookup` | | Looks up while held (keyboard look). |
| `+mlook` / `-mlook` | | Toggles free mouse-look while held. |
| `+movedown` / `-movedown` | | Crouches (or swims downward in water) while held. |
| `+moveleft` / `-moveleft` | | Strafes left while held. |
| `+moveright` / `-moveright` | | Strafes right while held. |
| `+moveup` / `-moveup` | | Jumps (or swims upward in water) while held. |
| `+reload` / `-reload` | | Reloads your current weapon while held. |
| `+right` / `-right` | | Turns right while held (keyboard turning). |
| `+speed` / `-speed` | | Modifier: toggles between walk and run speed (opposite of `cl_run`). |
| `+sprint` / `-sprint` | | Sprints (temporary speed boost, limited by stamina) while held. |
| `+strafe` / `-strafe` | | Modifier: while held, turns the left/right turn keys into strafe keys instead. |
| `+useitem` / `-useitem` | | Uses your currently-held holdable item (e.g. a picked-up document) while held. |
| `+zoom` / `-zoom` | | Zooms in with a scoped weapon or binoculars while held. |

## Input (Commands)

| Command | Usage | Description |
|---|---|---|
| `bind` | `bind <key> <command>` | Binds a key to a command. |
| `bindlist` | | Lists all current key bindings. |
| `centerview` | | Recenters your view pitch to level. |
| `in_restart` | | **[debug]** Restarts the input subsystem. |
| `unbind` | `unbind <key>` | Clears a key's binding. |
| `unbindall` | | Clears every key binding. |

## Filesystem (Commands)

| Command | Usage | Description |
|---|---|---|
| `dir` | `dir <directory> [extension]` | **[debug/advanced]** Lists files in a directory across all search paths. |
| `fdir` | `fdir <filter>` | **[debug/advanced]** Lists files matching a wildcard filter, recursively. |
| `fs_openedList` | | **[debug/advanced]** Lists pk3 files that have been opened this session. |
| `fs_referencedList` | | **[debug/advanced]** Lists pk3 files referenced for pure/download-consistency checking. |
| `path` | | **[debug/advanced]** Prints the current filesystem/pak search path. |
| `touchFile` | `touchFile <file>` | **[debug/advanced]** Opens and closes a file, for use in map scripts to force-copy assets during an `fs_copyfiles 1` run. |

## Console/Client Misc (Commands)

| Command | Usage | Description |
|---|---|---|
| `clear` | | Clears the console text buffer. |
| `cmdlist` | | Lists all registered console commands. |
| `condump` | `condump <filename>` | Dumps the console text buffer to a file. |
| `cvar_restart` | | Resets all cvars to their default values. |
| `cvarlist` | `cvarlist [filter]` | Lists all registered cvars. |
| `echo` | `echo <text>` | Prints text to the console. |
| `error` / `crash` / `freeze` | | **[debug]** Deliberately triggers an engine error/crash/hang, for testing; only registered when `developer 1`. |
| `exec` | `exec <filename.cfg>` | Queues a config file's commands into the command buffer. |
| `execnow` | `execnow <filename.cfg>` | Like `exec`, but executes immediately instead of queuing. |
| `meminfo` | | **[debug]** Prints general engine memory usage stats. |
| `prof_togglepause` | | **[debug/advanced]** Pauses/resumes the CPU/GPU profiler's capture. |
| `quit` | | Exits the game/server. |
| `reset` | `reset <cvar>` | Resets a cvar to its default value. |
| `restrictedlist` / `violations` | | **[debug/advanced]** Lists cvar-restriction violations from the competitive cvar-restriction system (`restrictedlist` on dedicated server builds, `violations` on client builds — same underlying command). |
| `SaveTranslations` / `SaveNewTranslations` / `LoadTranslations` | | **[debug]** Localization dev tools for the string-translation system. |
| `set` | `set <cvar> <value>` | Sets a cvar. |
| `setenv` | `setenv <variable> [value]` | Sets a process environment variable. |
| `setRecommended` | | Applies the engine's auto-detected recommended settings. |
| `sets` / `setu` / `seta` | `seta <cvar> <value>` | Sets a cvar with `SERVERINFO`/`USERINFO`/`ARCHIVE` respectively applied. |
| `toggle` | `toggle <cvar> [value list]` | Toggles a cvar between two (or a listed set of) values. |
| `toggleconsole` | | Opens/closes the console. |
| `togglegui` / `toggleguiinput` | | **[debug/advanced]** Toggles the ImGui dev-tools overlay / its input capture. |
| `ui_restart` | | **[debug]** Reinitializes the UI module without a full `vid_restart`. |
| `updatehunkusage` | | **[debug]** Updates the stored hunk-usage estimate for the current map. |
| `updatescreen` | | **[debug]** Forces an immediate screen redraw. |
| `vminfo` | | **[debug]** Prints VM (game/cgame/ui module) info. |
| `vstr` | `vstr <cvar>` | Executes a cvar's string value as a command. |
| `wait` | `wait [frames]` | Delays subsequent buffered commands by N frames (default 1). |
| `waitms` | `waitms <milliseconds>` | Delays subsequent buffered commands by N milliseconds. |
| `writeconfig` | `writeconfig <filename.cfg>` | Writes current archived cvars/binds out to a config file. |
| `zonelog` / `hunklog` / `hunksmalllog` | | **[debug]** Dumps zone/hunk memory allocation logs. |
