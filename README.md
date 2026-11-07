# Dune Strike

**Dune Strike** is a free, fast, low-bandwidth multiplayer first-person shooter
built for LAN play and small online servers. It runs on Windows, Linux and
Apple Silicon macOS, starts in seconds, and is small enough to carry on a USB
stick.

The game uses the classic arena formula: quick movement, instant weapon
switching, compact maps, bots for offline play, and a built-in map editor.
It also adds **hijabi player skins** and **push-to-talk voice chat** with
simultaneous speakers.

---

## Features

- **Push-to-talk voice chat** — on by default, multiple people can talk at
  once. Hold **X** to transmit, **F9** to toggle voice chat. A small HUD panel
  shows your microphone level and the names of players you can hear.
- **Hijabi player skins** for both teams, selectable from the normal skin
  menu (no extra downloads).
- **LAN-first multiplayer** — one command starts a server; clients join by IP.
- **Classic game modes** — Deathmatch, Team Deathmatch, Capture the Flag,
  Hunt the Flag, Keep the Flag, Pistol Frenzy, Last Swiss Standing,
  One-Shot One-Kill and survivor modes.
- **Bots** for solo play and map testing.
- **Built-in map editor**, including cooperative real-time editing.
- **Demo recording** and playback.
- **Low bandwidth** — playable over a 56 kbps connection.
- **Cross-platform** — Windows, Linux and macOS builds; the source builds on
  all three.

---

## Download

Prebuilt archives are published on the
[releases page](https://github.com/aminghaseminia/dunestrike/releases):

| Platform | Archive |
|---|---|
| Linux x86-64 | `DuneStrike_v1.3.0.2_Linux.tar.gz` |
| Windows 32-bit | `DuneStrike_v1.3.0.2_Windows.zip` |
| macOS arm64 | `DuneStrike_v1.3.0.2_macOS.zip` |

The macOS archive includes its own libraries and runs without Homebrew.
The Windows archive includes the required DLLs. On Linux, install the usual
SDL2/OpenAL/Vorbis runtime packages listed below.

---

## Quick start

### Linux / macOS

```sh
tar xzf DuneStrike_*_Linux.tar.gz     # or unzip the macOS archive
cd DuneStrike_*
./dunestrike.sh
```

### Windows

Unzip the archive and run `dunestrike.bat`. For a self-contained profile in
the game folder, use `dunestrike_portable.bat`.

### Join a LAN game

Start a server on one machine:

```sh
./dunestrike-server.sh -n"Dune Strike LAN" -c10
```

Then, on the other machines, open the console with **T** (or **`**) and type:

```text
connect 192.168.1.10 28763
```

Replace the address with the server machine's LAN IP. The default port is
UDP **28763**.

---

## Voice chat

Voice chat is enabled by default on both the client and the server.

| Action | Key |
|---|---|
| Hold to talk | **X** |
| Toggle voice chat on/off | **F9** |
| Open console | **T** or **`** |

The on-screen voice panel shows a VU meter while you transmit and the names
of remote players whose audio is being received. The first time you transmit
on macOS, allow the microphone permission prompt.

For server settings, bandwidth costs and protocol details, see
[VOICECHAT.md](VOICECHAT.md).

---

## Build from source

### Dependencies

**Linux (Debian/Ubuntu):**

```sh
sudo apt install build-essential clang make \
  libsdl2-dev libsdl2-image-dev libopenal-dev libvorbis-dev \
  libx11-dev zlib1g-dev
```

**macOS (Homebrew):**

```sh
brew install sdl2 sdl2_image libvorbis
```

**Windows:** use the Visual Studio solution in `source/vcpp/dunestrike.sln`,
or cross-compile with MinGW-w64 (see the CI workflow for the exact flags).

### Build the client and server

```sh
cd source/enet
./configure --enable-shared=no --enable-static=yes
make -j4

cd ../src
make -j4 client_install server_install
```

The binaries are installed as:

```text
bin_unix/dunestrike_client
bin_unix/dunestrike_server
```

On macOS, the Makefile needs the SDL2 and OpenAL framework flags used by the
release workflow:

```sh
cd source/src
SDL_CFLAGS="$(sdl2-config --cflags)"
SDL_LIBS="$(sdl2-config --libs)"
make -j4 client_install server_install \
  CXX=clang++ \
  "CLIENT_LIBS=-L../enet/.libs -lenet $SDL_LIBS -lSDL2_image -lvorbisfile -lz -framework OpenGL -framework OpenAL -framework AudioToolbox" \
  "SERVER_INCLUDES=-DSTANDALONE -I. -Ibot -I../enet/include $SDL_CFLAGS" \
  "SERVER_LIBS=-L../enet/.libs -lenet $SDL_LIBS -lz"
```

### Run from the source tree

```sh
./dunestrike.sh
./dunestrike-server.sh
```

---

## Configuration and profiles

The game keeps user settings, demos and screenshots in a profile directory:

- Linux/macOS launcher: `~/.dunestrike/v1.3`
- Windows launcher: `Documents\My Games\Dune Strike\v1.3`
- Portable Windows launcher: `profile/` next to the game

To use a different profile, pass `--home=/path/to/profile`.

---

## Controls

| Action | Default key |
|---|---|
| Move | W A S D |
| Jump | Space |
| Fire | Left mouse |
| Aim / scope | Right mouse |
| Next / previous weapon | Mouse wheel, 1–5 |
| Scoreboard | Tab |
| Console | T |
| Push-to-talk | X |
| Toggle voice | F9 |

All keys can be changed in **Settings → Keyboard**.

---

## Project layout

```text
bin_unix/       Linux/macOS binaries after a build
bin_win32/      Windows binaries and DLLs
config/         game configuration, menus and docs
docs/           map editor and scripting reference
packages/       maps, models, textures, sounds
source/         C++ source, ENet, build files
  src/          game and engine code
  enet/         bundled ENet networking library
  vcpp/         Visual Studio solution
  xcode/        Xcode project
```

---

## Contributing

Bug reports, maps, skins and code changes are welcome. See
[CONTRIBUTING.md](CONTRIBUTING.md).

## Credits and license

Dune Strike is free software. The engine and original game code are distributed
under a zlib-style license; asset-specific terms are kept in the `license.txt`
files next to the assets. See [CREDITS.md](CREDITS.md) for the full engine
lineage, contributor list and licensing details.

The **Dune Strike** name, loading screen and icon are original to this project.
