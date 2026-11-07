# In-game voice chat

Push-to-talk voice chat for multiplayer games. On the client it is **on by
default**; the server script enables it too, so `./server.sh` is enough. Any
number of players can hold X at the same time and everyone hears all of them.

## Quick start

**Server** — `server.sh` passes `-R` for you:

```sh
./server.sh
```

If you run the server binary yourself, pass `-R` to it.

AssaultCube command-line flags must be attached to their values: `-c10`, not
`-c 10`; `-n"Name"`, not `-n "Name"`.

Without `-R` (or with `-R0`) the server stays silent and clients are told
`Voice chat: the server has it disabled`.

**Client**

1. Join a multiplayer server.
2. Voice chat is already enabled. Hold **X** to talk, release to stop.
   On macOS the first use asks for microphone permission — allow it.
3. Press **F9** to turn voice chat off (`Voice chat: OFF`), and F9 again to
   turn it back on (`Voice chat: ON - hold X to talk`).

Since it is push-to-talk, leaving it enabled cannot create a hot microphone.
If you press X while voice chat is off, the game reminds you to press F9.

## On-screen indicator

While transmitting, a small panel under the radar shows:

- `MIC transmitting` plus a green/yellow/red VU meter for your own microphone.
- The names of every remote player whose voice is currently being received.

The panel disappears when a menu (including the scoreboard) is open.

## Several people talking at once

Every player who holds X is transmitted. Each client receives one stream per
speaker, buffers it separately, and sums them in 20 ms steps before playback.

Practical consequences:

- No one has to wait for the channel to be free.
- Simultaneous loud speakers are clamped at full scale, so extreme overlap can
  sound clipped. Speech normally does not reach that point.
- Each extra talker costs bandwidth for every listener (see below).

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `voicechat` | `1` | Master switch. F9 toggles it and it is saved. |
| `voicevolume` | `100` | Playback volume, 0–200 %. |
| `voiceinputgain` | `100` | Microphone gain, 0–300 %. |
| `sv_voice` | `1` | Server switch, set by `server.sh` (`-R`). Disable with `-R0`. |
| `sv_voiceteam` | `0` | `1` = only teammates hear each other (listen-server builds). |

Change the keys in `config/autoexec.cfg`:

```c
bind F9 togglevoice
bind X [ voiceon; onrelease voiceoff ]
```

The existing scoreboard **mute/ignore** also silences that player's voice.

## Requirements

- **Headphones.** There is no echo cancellation; with speakers everyone will
  hear themselves, and with several talkers that gets worse quickly.
- A microphone the OS can access. On macOS grant microphone permission to the
  game (or to the terminal/app that launches it).
- For voice itself, both sides need this build. Stock AssaultCube clients can
  still connect and play; they simply never send or receive voice.

## What it costs

Raw 16-bit mono PCM at 16 kHz, 20 ms frames (640 bytes per frame). Because the
protocol encodes integers, a frame is roughly 700–800 bytes on the wire, so
about **35–40 KB/s per person talking**.

The server forwards every talker to every listener. Eight people talking at
once therefore costs each listener roughly 300 KB/s and the server roughly
2.4 MB/s of upstream traffic. That is fine on a LAN; for internet play, keep the
number of simultaneous talkers low.

## How it works (and why it is safe)

- The client probes the server with the standard `SV_EXTENSION` message
  (`acvoice::hello`). Stock servers ignore unknown extensions.
- Only a server that answers with `SV_VOICESUPPORT` is sent voice data. Stock
  clients never send the probe, so they never receive any of the new messages.
- Four message types were added at the end of the protocol, and
  `PROTOCOL_VERSION` was **not** changed. Patched and stock copies still
  interoperate; voice is simply unavailable in a mixed pair.
- The server never decodes audio. It relays frames and applies a per-client
  token-bucket rate limit.
- Voice is **not** written into demo recordings.
- A speaker's "talking" flag is cleared after 500 ms without frames.

## Commands

```c
togglevoice          // same as F9
voiceon              // start transmitting (used by the X bind)
voiceoff             // stop transmitting
voicetest 1000       // diagnostic: send 1 second of a test tone
```

`voicetest` is useful for verifying a connection without a microphone.

## Limitations

- No echo cancellation, noise suppression, or automatic gain control.
- No Opus; raw PCM only.
- No per-player volume slider (global volume plus the scoreboard mute).
- Voice is intentionally not available in singleplayer/bot games.
- All simultaneous speakers are mixed; there is no separate panning or
  positional audio.
