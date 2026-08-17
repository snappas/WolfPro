# Changelog

All notable WolfPro releases, newest first. WolfPro is built from tagged
commits in this repository; each entry below corresponds to one or more
build tags. See [README.md](README.md) for what WolfPro is, and
[CVARS.md](CVARS.md) for the current cvar reference.

## 2026-08-15 — 2026-08-16

- Demo system overhaul: reworked client-side demo recording/playback,
  and added automatic upload of server-recorded WTV demos to a Discord
  webhook after each match.
- TTF font support: hudchars/consolechars can now be baked
  at runtime from a TrueType font (`r_hudFontFile`/`r_hudFontEnabled`)
  instead of shipping only the original pixel-art format, via a new
  stb_truetype-based baking step in the renderer.
- Widescreen HUD: cgame HUD layout now scales correctly on
  non-4:3 aspect ratios instead of stretching/cropping.
- Added `r_mode` compatibility handling to the Vulkan client so video
  mode selection behaves consistently with the legacy GL client.
- Follow-up fixes from the 2026-08-15 playtest session.

## 2026-07-18

- Added a demo browser to the main menu for picking recorded demos
  without using the console.
- Added a realtime profiler: instrumentation-only CPU/GPU profiler
  with ImGui Graphs/Timeline/Functions views, including real Vulkan
  GPU timestamp queries on the renderer_vk backend.

## 2026-07-11 — 2026-07-12

- Stood up the project's GitHub Actions release workflow: builds
  Windows x64/x86 natively and Linux x64 via Docker, packages combined
  pk3s, pushes the rtcw-server64 image, and publishes a GitHub
  Release (manually-triggered).
- Added hitbox visualization for debugging hit registration.
- Follow-up fixes from the previous (2026-06-28 – 2026-07-03) release.

## 2026-06-28 — 2026-07-03

- Ported networking, snapshot handling, memory management, and file
  downloads from quake3e.
- Added Rocket Launcher mode and full-screen stretch display
  support.
- Added per-bone body hitboxes (previously coarser hit volumes).
- Added Omni-bot integration, alongside the existing native
  botlib/botai.
- Added a no-ammo autoswitch option and a protocol extension for
  command backup.
- Made sniper rifle recoil frame-rate independent.

## 2026-03-07

- Added a timelimit callvote.
- Server: reworked last-phase spawn point prioritization.
- Antilag/unlag tuning: added a cvar to ignore antilag for
  low-ping players, switched non-antilagged players to entity-state
  extrapolation, and made the antilag minimum threshold configurable.
- Made footstep and weapon-heat timing frame-rate independent.
- Improved config string management in the engine.

## 2026-02-08 — 2026-02-20

- Fixed stopwatch end-time handling and a spectator HUD issue.
- Added cvar restrictions: server-enforced limits on which cvars
  clients may change, for competitive integrity.
- Fixed ping/score display while spectating, and disabled world damage
  during pause.
- Added health/ammo stat tracking.
- Unlag: server-side handling for smoothed ("laggy") clients under
  antilag.
- Implemented 125fps-equivalent gravity scaling for movement
  consistency across client frame rates.
- Added the direct-draw HUD popup windows for weapon stats, match
  stats, and top shots.
- Server now requires server-referenced pk3s to actually be loaded by
  connecting clients.
- Minor fixes to demo playback, the scoreboard, and spectator mode.
- Cleaned up headshot hit-registration code.

## 2026-01-11

- Added speclock (restrict spectating) and team locks.
- Added new match stats and a players console command.
- Fixed centerview being triggerable when it shouldn't be.
- Grenades no longer instantly explode on the holder's death if
  already ticking.
- Fixed a case of players clipping through walls.
- Added qwfwd proxy support on the client.
- Switched to using the skeletal model to attach the head hitbox,
  instead of a fixed approximation.
- Fixed a player-movement jump-prediction error.
- Added automatic demo recording, and an `r_noborder` borderless
  window mode.
- Guarded against precision loss from large server time values in
  cgame/renderer time math.

## 2025-11-11

- Added HUD element customization (position/scale of individual HUD
  pieces).
- Added map voting.
- Allowed reviving through enemy players.
- Added fast downloads (curl-based) for client/server map/asset
  transfer.

## 2025-11-02 — WolfPro baseline

Starting from id Software's original RTCW MP GPL release (August 12,
2010):

- Build system: migrated from the original VC++/cons setup to
  CMake, added Linux and cross-compiled Windows-from-Linux builds,
  Docker-based build/deploy, and x64 support alongside the original
  x86.
- Cleanup: dropped MacOS support, removed IPX networking, replaced
  the vendored jpeg-6 library with libjpeg-turbo, removed dead
  splines/extractfuncs sub-projects.
- Competitive gameplay foundations: a ready/not-ready pre-match
  system, per-weapon stats/scores/statsall, hitsounds, a respawn
  timer, an unlag antilag implementation, and stats upload to an
  external service.
- Customization: cvar-driven crosshair settings (including a
  centered sniper reticle and colored crosshairs), a crosshair gap
  cvar, and a toggle to disable blood damage effects.
- Rendering/engine: switched to the volk Vulkan loader, added
  dynamic light through walls for dynamite, fixed a 2D-overbright bug,
  raised the max image count to 2048, and fixed a render dropped-frame
  issue during screen updates.
- Dev tooling: added an ImGui-based main menu bar and client stats
  overlay.
- Gameplay/bugfixes: team-only doors, map/class-specific autoexec
  configs, opt-in/out server-registered player names, grenade ammo
  edge cases, primed-grenade-on-death handling, medic-count-based
  initial health, spawn point and map-mutation support in the engine,
  and numerous crash/bug fixes tracked as GitHub issues (#1–#107) from
  this period.
