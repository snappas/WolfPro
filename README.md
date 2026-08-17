# WolfPro

WolfPro is an actively developed, competitive-multiplayer fork of
**Return to Castle Wolfenstein** (id Tech 3 lineage), built from id
Software's original RTCW MP GPL source release (August 12, 2010). It's an
independent continuation of the engine — not iortcw or RealRTCW — focused
on modernizing the engine while adding the tooling a competitive scene
needs: referee mode, match/stats infrastructure, hit registration and
hitbox work, cvar restrictions, and antilag/unlag.

## What's different from vanilla RTCW

- **Modern engine core**: native x64 builds, a Vulkan renderer alongside
  the classic OpenGL one, a native Linux/SDL2 client, CMake build,
  curl-based downloads.
- **Competitive features**: referee mode, a ready/not-ready match system,
  team locks and speclock, cvar restrictions for server-enforced fair
  play, per-weapon stats and weapon-stats/topshots HUD popups, and
  configurable antilag/lag-compensation.
- **Precision hit registration**: per-bone body hitboxes and a
  tag-attached precise head hitbox, replacing the original bounding-box
  approximation.
- **Bot support**: [Omni-bot](http://www.omni-bot.com/) integration.
- **Demo tooling**: New Demo Player with seek/scrub support,
  server-side full-round demo recording with automatic Discord
  upload, and an in-game demo browser supporting most legacy demos.
- **Quality-of-life rendering**: widescreen HUD support, a realtime
  CPU/GPU profiler, and a TrueType-font-backed HUD/console character set
  as an alternative to the original pixel-art font.

See [CHANGELOG.md](CHANGELOG.md) for what shipped in each release, and
[CVARS.md](CVARS.md) for the full cvar reference.

## Getting the game data

This repository contains **engine source only** — no game assets
(`pak0.pk3`, maps, etc.). You need a legitimate RTCW installation; the
original game is available on
[Steam](https://store.steampowered.com/app/9010/). See
[README.txt](README.txt) for the original GPL release notes and asset
licensing terms.

## Building

Requires CMake + Ninja (or an MSVC/Visual Studio generator) and a
C99/C++11 toolchain.

```
cmake -B build -G Ninja -DINSTALL_DEFAULT_BASEDIR="C:/path/to/rtcw"
cmake --build build
cmake --install build
```

`INSTALL_DEFAULT_BASEDIR` should point at an existing RTCW install
(retail data + pak files); the client/server binaries and mod DLLs
install into `<basedir>/wolfpro/`.

Key CMake options (see `CMakeLists.txt`): `BUILD_CLIENT`, `BUILD_SERVER`,
`BUILD_MOD` (cgame/game/ui), `ENABLE_OMNIBOT`, `ENABLE_ASAN`,
`ENABLE_PROFILER`, `BUNDLED_LIBS`. Third-party dependencies (curl,
jansson, libjpeg-turbo, zlib, Omni-bot, etc.) are fetched by
`fetch-dependencies.sh`/`fetch-dependencies64.bat` into `deps/`/`deps64/`.
Linux cross-compile-to-Windows and Docker-based build/deploy flows live
under `docker/`.

To verify a build: launch the client and/or a local dedicated server
(`connect`ing with `sv_pure 0` so a freshly built, unsigned mod loads),
and exercise the gameplay path you're testing.

## Repository layout

- `src/qcommon/`, `src/client/`, `src/server/` — shared engine core,
  client, and dedicated server.
- `src/renderer_gl/`, `src/renderer_vk/`, `src/renderer_common/` — the
  two parallel renderer backends behind a common interface.
- `src/game/`, `src/cgame/`, `src/ui/` — the mod modules: server-side
  gameplay, client-side prediction/HUD, and menus.
- `src/botlib/`, `src/botai/`, `src/game/omnibot/` — bot AI (id's
  original native bot code, and the Omni-bot integration).
- `src/win32/`, `src/unix/`, `src/null/` — platform layers.
- `cmake/`, `docker/` — build tooling and containerized build/deploy
  scripts.

## License

GPL v3 — see [COPYING.txt](COPYING.txt). Some third-party code (zlib
`unzip`, MD4, the jpeg-6 lineage) carries separate license terms; see
[README.txt](README.txt) for details.

