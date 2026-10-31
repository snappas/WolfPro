# Updater test kit

Tests the client auto-updater end to end without touching real releases or
players. It builds an "old" and a "new" client, then points the old one at a
throwaway GitHub repo through `cl_updateRepo`. Real GitHub lookups, downloads,
checksums and the updater all run; only the repo is different.

Local builds never check for updates, so these builds carry fake commits
(`1111111` and `2222222`) and the `release` channel.

Windows only. Needs Visual Studio 2022, a retail RTCW install, and the
[GitHub CLI](https://cli.github.com/) logged in (`gh auth login`). Run the
scripts with `powershell -ExecutionPolicy Bypass -File <script>`.

## Layout

Everything is created under `C:\rtcw_updatetest` (`-Root` changes it), outside
the repo:

| Path | Contents |
|---|---|
| `install x64 (test)\`, `install x86 (test)\` | Test installs of the old build, each with `start_test.cmd` |
| `zips\` | The release-shaped "new" zips, plus the bad ones |
| `build\`, `stage\`, `pack\` | Build output |

The install folder names contain spaces and parentheses on purpose. Retail paks
are hard links, so they use no extra disk space. `-Retail` points at the retail
`main` folder if it isn't `C:\rtcw_vk\main`.

Don't copy an existing `wolfpro\` folder into a test install. An undated
`wolfpro_assets.pk3` sorts after the dated ones and silently overrides them.

## Scripts

| Script | What it does |
|---|---|
| `updater_selftest.ps1` | Tests `wolfpro_updater.exe` alone in a temp folder; no game, no GitHub |
| `build_test_clients.ps1` | Builds old and new for x64 and x86, resets both installs, makes the zips |
| `make_bad_zips.ps1` | Makes the deliberately bad packages from the good zips |
| `publish_test_release.ps1 -Step <step>` | Publishes to the throwaway repo (steps below) |
| `fingerprint_installs.ps1 -Mode save\|compare` | Proves a failed update changed no game files |

`build_test_clients.ps1` always resets the installs, so rerun it after changing
code or to start over. Run it with the game closed.

## Running a test

```powershell
.\build_test_clients.ps1
.\make_bad_zips.ps1
.\publish_test_release.ps1 -Step repo
.\publish_test_release.ps1 -Step good
```

Run `start_test.cmd` in a test install. The popup should offer the update, and
after installing, `version` should show `2222222`. Then try `/update rollback`,
Not now, Skip, the Options button, and `connect` during a download.

For each failure case, publish the step, run `fingerprint_installs.ps1 -Mode
save`, try the update in game, quit, and run `-Mode compare`.

| Step | Expected result |
|---|---|
| `noupdater` | "Update package is missing wolfpro_updater.exe." |
| `traversal` | "Unsafe path in update package", and no `updatetest_escape.txt` anywhere |
| `notzip` | "... is not a valid zip file", and Retry works |
| `newer` | A new tag brings the popup back after Skip |

Each published release becomes Latest, so run the good update first.

Loading a map unpacks DLLs into `wolfpro\`, and `-Mode compare` reports those as
new files. That's normal: only changed exes or pk3s point at the updater.

A checksum mismatch can't be tested this way, because GitHub computes the digest
from whatever is uploaded.

## Cleanup

```powershell
gh auth refresh -h github.com -s delete_repo
.\publish_test_release.ps1 -Step delete
```

Then delete `C:\rtcw_updatetest`. The retail paks there are hard links, so this
doesn't affect the retail install.
