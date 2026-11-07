# AGENTS.md — Dune Strike (aminghaseminia/dunestrike)

Read this before touching builds, releases, the app-drawer entry, or the local server.

## Golden rule: rebuild after every pull

This repo is wired so that "pull → rebuild" is the only update workflow:

```sh
cd ~/Projects/dunestrike
git pull --rebase
./build-linux.sh          # production build of client + server → bin_unix/
systemctl --user restart dunestrike-server   # only if the server service is running
```

Everything below exists to make that flow safe. Do not break it.

## Local Linux build

- Build dependencies (sudo apt): `clang g++ libsdl2-dev libsdl2-image-dev libopenal-dev libvorbis-dev zlib1g-dev libx11-dev`.
- Always build with `PRODUCTION` defined (`build-linux.sh` passes `-DPRODUCTION`).
  - Production: `DS_VERSION 1302`, master port `28760`.
  - Plain `make` (no flag): `DS_VERSION -1302`, master port `28758` — a dev/test build, wrong for releases.
- `bin_unix/` is git-ignored (binaries never committed). The launcher uses `bin_unix/dunestrike_client` / `dunestrike_server`.
- `build-linux.sh` is the canonical rebuild command (configures+builds bundled enet on first run, then `make client server install`). Keep it working.

## App drawer entry (auto-updates by design)

- `~/.local/share/applications/dunestrike.desktop` has `Exec=/path/to/dunestrike/dunestrike.sh`.
- `dunestrike.sh` resolves **its own directory at launch** and execs `bin_unix/dunestrike_client` → the drawer always runs whatever was built last. **Never copy/install the binary to a separate location for the launcher** — that would freeze it at an old version.
- If the repo moves, update `Exec` in the .desktop file (and the systemd unit below). Otherwise the entry needs no maintenance after pulls/rebuilds.
- Icon: `~/.local/share/icons/hicolor/256x256/apps/dunestrike.png` (converted from `packages/misc/icon.png`). Regenerate only if the source icon changes.
- Do not delete/replace the .desktop file when "cleaning up"; it is intentional state outside the repo.

## Dedicated server — systemd user service

- Unit: `~/.config/systemd/user/dunestrike-server.service`, **enabled**, and the user has `Linger=yes` → starts at boot without login.
- `WorkingDirectory` = this repo; `ExecStart` = repo `dunestrike-server.sh` (voice chat `-R` + `config/servercmdline.txt`).
- **After any rebuild of the server binary, run `systemctl --user restart dunestrike-server`** — the service keeps running the old inode otherwise (nothing breaks, but the new build is not live until restart). `build-linux.sh` does not restart it automatically on purpose.
- Runs **LAN-only** (no `-m` masterserver → no `-Y` private key required). Port **UDP 28763**.
- `ufw` is active on this machine: local play works via loopback; other LAN devices need `sudo ufw allow 28763/udp`.
- Useful: `systemctl --user status|restart dunestrike-server`, `journalctl --user -u dunestrike-server -f` (console output may be block-buffered; game file logs are in `logs/`).

## CI release pipeline (`.github/workflows/build-release.yml`)

- Trigger: **push to `main`** rebuilds Linux + Windows + macOS and updates the versioned release automatically (the workflow moves the `v<version>` tag to the new commit). Pushing a tag `v*` or running `workflow_dispatch` builds an exact version. Assets are uploaded with `--clobber`, so re-runs are safe.
- Keep the "Enable production build" sed step — without it the shipped binaries are dev builds (negative version, wrong master port).
- **Windows job** (mingw cross, i686/32-bit — matches the shipped `bin_win32` DLLs):
  - `-rdynamic` must be stripped from `source/src/Makefile` (Linux-only flag, the Makefile `override`s it in).
  - Link against bundled import libs in `source/lib`: `-lvorbisfile` (file is `vorbisfile.lib`, **not** `libvorbisfile`), `-lSDL2main`, `-lOpenAL32`, `-lzdll`.
  - `-static-libgcc/-static-libstdc++` belong in **link flags only** (CLIENT_LIBS/SERVER_LIBS) — putting them in `CXXFLAGS` breaks the PCH rule.
  - `libgcc_s_dw2-1.dll` + `libwinpthread-1.dll` must be copied from the toolchain into `bin_win32/` before zipping, or the exe will not start on Windows.
  - The standalone server links SDL2 too (`DS_USE_SDL_THREADS` is forced on `WIN32`/`__APPLE__` in `platform.h`).
- **macOS job**: `macos-latest` (arm64); brew SDL2/libvorbis; server needs SDL cflags in `SERVER_INCLUDES`; Homebrew dylibs are bundled into the zip with `@executable_path/libs` rewrites (otherwise the archive only runs on machines with Homebrew).
- **Packaging**: `git archive` of the tree already contains `bin_unix/` — when adding built binaries, remove the old directory first or you get `bin_unix/bin_unix/` nesting.
- Tag hygiene: after moving/creating a tag, verify with `git ls-remote origin refs/tags/v<ver>` that it points at the intended commit — a stale tag rebuilds old code while you think you fixed it.

## Git / GitHub auth gotchas (this machine)

- The local SSH key authenticates to GitHub as **`majidasgari`**, which has **no access** to this repo — SSH pushes fail.
- Push over **HTTPS**: remote push URL is set to `https://github.com/aminghaseminia/dunestrike.git` with the `gh` credential helper (`gh auth setup-git`).
- The `gh` token needs the **`workflow` scope** to push anything under `.github/workflows/` (`gh auth refresh -h github.com -s workflow` if rejected).
- Concurrent edits are normal: `git pull --rebase origin main` before pushing; the owner pushes their own fixes too. If your commit duplicates theirs and becomes empty during rebase, just drop it (`git rebase --skip`) and confirm with `git diff <tag> main`.

## Origin and version

Dune Strike is an altered distribution built on the Cube engine and the
AssaultCube codebase. See `CREDITS.md` for the required origin and license
notices. `DS_VERSION 1302` corresponds to game version 1.3.0.2. `cube.h` keeps
`//#define PRODUCTION` commented; enable it per-build as described above.
