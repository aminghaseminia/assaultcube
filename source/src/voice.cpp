// voice.cpp: in-game push-to-talk voice chat (client side)
//
// Design: one talker at a time, raw 16-bit mono PCM at 16 kHz, 20 ms frames
// (640 bytes). The server only relays frames, it never decodes them.
//
// The feature is opt-in on both ends:
//   - client: /voicechat 0|1 (F9 by default)
//   - server:  -R
//
// Compatibility: the client probes the server with the standard SV_EXTENSION
// message ("acvoice::hello"). Stock servers ignore unknown extensions. Only a
// server that answers with SV_VOICESUPPORT gets voice data, so stock clients
// never receive any of the new messages.

#include "cube.h"

static const int VOICE_RATE          = 16000;
static const int VOICE_FRAME_SAMPLES = 320;                 // 20 ms
static const int VOICE_FRAME_BYTES   = VOICE_FRAME_SAMPLES * 2;
static const int VOICE_MAX_WORDS     = VOICE_FRAME_BYTES / 4;

VARP(voicechat, 0, 0, 1);          // persisted; off by default
VARP(voicevolume, 0, 100, 200);    // playback volume, percent
VARP(voiceinputgain, 0, 100, 300); // microphone gain, percent

static SDL_AudioDeviceID capdev = 0, outdev = 0;
static SDL_AudioStream *capstream = NULL, *outstream = NULL;
static bool audiostarted = false, micfailed = false, outfailed = false;
static int lastouttry = -10000;

static bool talking = false, pttheld = false;
static int voicesupported = -1;    // -1 unknown, 0 server says no, 1 ready
static int lastprobe = -10000, probes = 0;
static ENetPeer *lastpeer = NULL;

static ushort outseq = 0;
static ushort lastinseq = 0;
static int lastincn = -1;

static uchar framebuf[VOICE_FRAME_BYTES];
static int framelen = 0;

static void voice_stop();
static void voice_start();

static short inbuf[VOICE_FRAME_SAMPLES];

static bool voice_audio_init()
{
    if(audiostarted) return true;
    if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        conoutf("voice: could not initialize SDL audio: %s", SDL_GetError());
        return false;
    }
    audiostarted = true;
    return true;
}

static bool voice_open_output()
{
    if(outdev) return true;
    // don't retry (and re-log) on every received frame if the device is gone
    if(outfailed && totalmillis - lastouttry < 5000) return false;
    lastouttry = totalmillis;
    if(!voice_audio_init()) return false;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = VOICE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = NULL;

    outdev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if(!outdev)
    {
        conoutf("voice: could not open audio output: %s", SDL_GetError());
        outfailed = true;
        return false;
    }
    outstream = SDL_NewAudioStream(AUDIO_S16SYS, 1, VOICE_RATE, have.format, have.channels, have.freq);
    if(!outstream)
    {
        conoutf("voice: could not create output converter: %s", SDL_GetError());
        SDL_CloseAudioDevice(outdev);
        outdev = 0;
        outfailed = true;
        return false;
    }
    outfailed = false;
    SDL_PauseAudioDevice(outdev, 0);
    return true;
}

static bool voice_open_capture()
{
    if(capdev) return true;
    if(!voice_audio_init()) return false;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = NULL;

    capdev = SDL_OpenAudioDevice(NULL, 1, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if(!capdev)
    {
        if(!micfailed) hudoutf("Voice chat: no microphone (%s)", SDL_GetError());
        micfailed = true;
        return false;
    }
    capstream = SDL_NewAudioStream(have.format, have.channels, have.freq, AUDIO_S16SYS, 1, VOICE_RATE);
    if(!capstream)
    {
        conoutf("voice: could not create capture converter: %s", SDL_GetError());
        SDL_CloseAudioDevice(capdev);
        capdev = 0;
        return false;
    }
    SDL_PauseAudioDevice(capdev, 1); // start paused; unpause while transmitting
    return true;
}

static void voice_queue(const short *samples, int count)
{
    if(!outdev || count <= 0) return;

    // cap latency at roughly 120 ms; drop instead of growing an ever longer delay
    if(SDL_GetQueuedAudioSize(outdev) > (Uint32)(VOICE_FRAME_BYTES * 6)) return;

    int vol = voicevolume;
    for(int i = 0; i < count; i++)
    {
        int s = samples[i] * vol / 100;
        inbuf[i] = (short)clamp(s, -32768, 32767);
    }

    uchar converted[4096];
    if(outstream)
    {
        if(SDL_AudioStreamPut(outstream, inbuf, count * 2) < 0) return;
        int got;
        while((got = SDL_AudioStreamGet(outstream, converted, sizeof(converted))) > 0)
            SDL_QueueAudio(outdev, converted, got);
    }
    else SDL_QueueAudio(outdev, inbuf, count * 2);
}

static void voice_send_frame(const uchar *data, int len)
{
    if(!curpeer || voicesupported != 1 || len != VOICE_FRAME_BYTES) return;

    short samples[VOICE_FRAME_SAMPLES];
    memcpy(samples, data, VOICE_FRAME_BYTES);
    int gain = voiceinputgain;

    packetbuf p(MAXTRANS);
    putint(p, SV_VOICEDATA);
    putint(p, outseq++);
    putint(p, VOICE_MAX_WORDS);
    for(int i = 0; i < VOICE_MAX_WORDS; i++)
    {
        int s0 = clamp(samples[i * 2] * gain / 100, -32768, 32767);
        int s1 = clamp(samples[i * 2 + 1] * gain / 100, -32768, 32767);
        unsigned int word = ((unsigned int)(s0 & 0xFFFF)) | (((unsigned int)(s1 & 0xFFFF)) << 16);
        putint(p, (int)word);
    }
    sendpackettoserv(1, p.finalize());
}

void voicesupport(int supported)
{
    int before = voicesupported;
    voicesupported = supported ? 1 : 0;
    if(!voicesupported) voice_stop();
    if(!voicechat) return;
    if(voicesupported != before)
    {
        if(voicesupported) hudoutf("Voice chat: server ready - hold X to talk");
        else hudoutf("Voice chat: the server has it disabled");
    }
}

void voicestate(int cn, int on)
{
    if(!voicechat || !on || !player1 || cn == player1->clientnum) return;
    playerent *d = getclient(cn);
    if(d && !(d->muted || d->ignored)) hudoutf("\f2%s\fr is talking", colorname(d));
}

void voicereceive(int cn, int seq, int n, ucharbuf &p)
{
    static short samples[VOICE_FRAME_SAMPLES];
    if(n < 0) n = 0;
    if(n > VOICE_MAX_WORDS) n = VOICE_MAX_WORDS;

    // always consume the payload, even when voice is disabled
    for(int i = 0; i < n; i++)
    {
        int v = getint(p);
        samples[i * 2] = (short)(v & 0xFFFF);
        samples[i * 2 + 1] = (short)((v >> 16) & 0xFFFF);
    }

    if(!voicechat || !player1 || cn == player1->clientnum || n <= 0) return;
    playerent *d = getclient(cn);
    if(d && (d->muted || d->ignored)) return; // respect the scoreboard mute
    if(!voice_open_output()) return;

    if(cn != lastincn)
    {
        lastincn = cn;
        lastinseq = (ushort)(seq - 1); // treat this frame as the next one
        clientlogf("voice: receiving audio from cn %d", cn);
    }

    ushort s = (ushort)seq;
    ushort gap = (ushort)(s - lastinseq);
    if(gap == 0 || gap > 0x8000) return; // duplicate or too old
    if(gap > 1 && gap <= 8)
    {
        // conceal a small gap with silence to keep the timing intact
        static short silence[VOICE_FRAME_SAMPLES] = { 0 };
        for(int i = 1; i < gap; i++) voice_queue(silence, VOICE_FRAME_SAMPLES);
    }
    lastinseq = s;

    voice_queue(samples, n * 2);
}

static void voice_stop()
{
    if(!talking) return;
    talking = false;
    if(capdev) SDL_PauseAudioDevice(capdev, 1);
    if(curpeer && voicesupported == 1) addmsg(SV_VOICEOFF, "r");
    clientlogf("voice: microphone stopped");
}

static void voice_start()
{
    if(talking || !voicechat || !curpeer || voicesupported != 1) return;
    if(!voice_open_capture()) return;

    SDL_ClearQueuedAudio(capdev);
    if(capstream) SDL_AudioStreamClear(capstream);
    framelen = 0;
    outseq = 0;
    micfailed = false;
    SDL_PauseAudioDevice(capdev, 0);
    talking = true;
    clientlogf("voice: microphone started");
}

void voiceon()
{
    pttheld = true;
    if(talking) return;
    if(!voicechat)
    {
        pttheld = false;
        hudoutf("Voice chat is OFF - press F9 to enable it");
        return;
    }
    if(!curpeer)
    {
        pttheld = false;
        hudoutf("Voice chat is only available in multiplayer");
        return;
    }
    if(voicesupported == 0)
    {
        pttheld = false;
        hudoutf("Voice chat: the server has it disabled");
        return;
    }
    if(voicesupported != 1)
    {
        hudoutf("Voice chat: checking the server...");
        lastprobe = -10000; // probe again on the next frame; start as soon as it answers
        return;
    }
    if(!voice_open_capture())
    {
        pttheld = false;
        return;
    }
    voice_start();
}
COMMAND(voiceon, "");

void voiceoff()
{
    pttheld = false;
    voice_stop();
}
COMMAND(voiceoff, "");

void togglevoice()
{
    if(!voicechat)
    {
        if(!curpeer)
        {
            hudoutf("Voice chat is only available in multiplayer");
            return;
        }
        voicechat = 1;
        lastprobe = -10000;
        probes = 0;
        hudoutf("Voice chat: ON - hold X to talk");
    }
    else
    {
        pttheld = false;
        voice_stop();
        voicechat = 0;
        hudoutf("Voice chat: OFF");
    }
}
COMMAND(togglevoice, "");

// used by the automated test: send a tone through the normal send path
static int testframes = 0, testlast = 0;
void voicetest(int *millis)
{
    if(!curpeer || voicesupported != 1)
    {
        conoutf("voice: not connected to a voice-enabled server");
        return;
    }
    testframes = max(1, *millis / 20);
    testlast = totalmillis - 20;
    conoutf("voice: scheduled %d test frames", testframes);
}
COMMAND(voicetest, "i");

void voiceupdate()
{
    if(curpeer != lastpeer)
    {
        lastpeer = curpeer;
        voicesupported = -1;
        lastprobe = -10000;
        probes = 0;
        lastincn = -1;
        if(!curpeer) voice_stop();
    }
    if(!curpeer) return;

    if(voicechat && voicesupported == -1 && player1 && player1->clientnum >= 0
        && totalmillis - lastprobe > 2000)
    {
        if(probes >= 3)
        {
            voicesupported = 0;
            hudoutf("Voice chat: this server does not support it");
        }
        else
        {
            lastprobe = totalmillis;
            probes++;
            addmsg(SV_EXTENSION, "rsi", "acvoice::hello", 0);
        }
    }

    if(pttheld && !talking && voicesupported == 1) voice_start();

    if(testframes > 0 && totalmillis - testlast >= 20)
    {
        short tone[VOICE_FRAME_SAMPLES];
        for(int i = 0; i < VOICE_FRAME_SAMPLES; i++)
            tone[i] = (short)(((testframes * VOICE_FRAME_SAMPLES) + i) % 40 < 20 ? 6000 : -6000);
        testlast = totalmillis;
        testframes--;
        voice_send_frame((uchar *)tone, VOICE_FRAME_BYTES);
    }

    if(!talking)
    {
        if(!voicechat) voice_stop();
        return;
    }
    if(!voicechat)
    {
        voice_stop();
        return;
    }
    if(!capdev) return;

    uchar raw[4096];
    Uint32 got;
    while((got = SDL_DequeueAudio(capdev, raw, sizeof(raw))) > 0)
    {
        if(!capstream) break;
        if(SDL_AudioStreamPut(capstream, raw, got) < 0) break;
    }
    if(!capstream) return;

    // don't let a slow frame build an unbounded backlog
    if(SDL_AudioStreamAvailable(capstream) > VOICE_FRAME_BYTES * 10) SDL_AudioStreamClear(capstream);

    int frames = 0;
    while(frames < 4 && SDL_AudioStreamAvailable(capstream) >= VOICE_FRAME_BYTES)
    {
        int n = SDL_AudioStreamGet(capstream, framebuf, VOICE_FRAME_BYTES);
        if(n != VOICE_FRAME_BYTES) break;
        voice_send_frame(framebuf, n);
        frames++;
    }
}

void voicecleanup()
{
    if(capdev)
    {
        SDL_CloseAudioDevice(capdev);
        capdev = 0;
    }
    if(outdev)
    {
        SDL_CloseAudioDevice(outdev);
        outdev = 0;
    }
    if(capstream)
    {
        SDL_FreeAudioStream(capstream);
        capstream = NULL;
    }
    if(outstream)
    {
        SDL_FreeAudioStream(outstream);
        outstream = NULL;
    }
    audiostarted = false;
}
