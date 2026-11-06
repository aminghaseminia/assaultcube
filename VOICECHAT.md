# In-game voice chat

Push-to-talk voice chat for multiplayer games. It is **off by default** on both
the client and the server, and it is only available in multiplayer.

## Quick start

**Server** — start it with `-R`:

```sh
./server.sh -R -n"AssaultCube LAN" -c10
```

AssaultCube command-line flags must be attached to their values: `-c10`, not
`-c 10`; `-n"Name"`, not `-n "Name"`.

Without `-R` the server stays silent and clients are told
`Voice chat: the server has it disabled`.

**Client**

1. Join a multiplayer server.
2. Press **F9**. You will see `Voice chat: ON - hold X to talk`.
   On macOS the first use asks for microphone permission — allow it.
3. Hold **X** to talk, release to stop. Only one person can talk at a time.
4. Press **F9** again to turn it off: `Voice chat: OFF`.

If you press **X** while voice chat is off, the game tells you to press F9.
If someone else is already talking, your audio is dropped and their name is
shown on screen.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| `voicechat` | `0` | Master switch. F9 toggles it and it is saved. |
| `voicevolume` | `100` | Playback volume, 0–200 %. |
| `voiceinputgain` | `100` | Microphone gain, 0–300 %. |
| `sv_voice` | `0` | Server switch. Standalone servers use `-R`. |
| `sv_voiceteam` | `0` | `1` = only teammates hear each other (listen-server builds). |

Change the keys in `config/autoexec.cfg`:

```c
bind F9 togglevoice
bind X [ voiceon; onrelease voiceoff ]
```

The existing scoreboard **mute/ignore** also silences that player's voice.

## Requirements

- **Headphones.** There is no echo cancellation; with speakers everyone will
  hear themselves.
- A microphone the OS can access. On macOS grant microphone permission to the
  game (or to the terminal/app that launches it).
- For voice itself, both sides need this build. Stock AssaultCube clients can
  still connect and play; they simply never send or receive voice.

## What it costs

Raw 16-bit mono PCM at 16 kHz, 20 ms frames (640 bytes per frame).
Because the protocol encodes integers, a frame is roughly 700–800 bytes on the
wire, so about **35–40 KB/s while someone is talking**.

- LAN: negligible.
- Internet: usable for a few players, but this is not Opus. The server forwards
  one stream to every listener, so 8 listeners cost the server ~300 KB/s.

## How it works (and why it is safe)

- The client probes the server with the standard `SV_EXTENSION` message
  (`acvoice::hello`). Stock servers ignore unknown extensions.
- Only a server that answers with `SV_VOICESUPPORT` is sent voice data. Stock
  clients never send the probe, so they never receive any of the new messages.
- Four message types were added at the end of the protocol, and
  `PROTOCOL_VERSION` was **not** changed. Patched and stock copies still
  interoperate; voice is simply unavailable in a mixed pair.
- The server never decodes audio. It relays frames, enforces one talker at a
  time, and applies a token-bucket rate limit.
- Voice is **not** written into demo recordings.
- If the talker disappears, the channel is released after 500 ms of silence.

## Commands

```c
togglevoice          // same as F9
voiceon              // start transmitting (used by the X bind)
voiceoff             // stop transmitting
voicetest 1000       // diagnostic: send 1 second of a test tone
```

`voicetest` is useful for verifying a connection without a microphone.

## Limitations

- One speaker at a time (walkie-talkie style), not full duplex.
- No echo cancellation, noise suppression, or automatic gain control.
- No Opus; raw PCM only.
- No per-player volume slider (global volume plus the scoreboard mute).
- Voice is intentionally not available in singleplayer/bot games.
