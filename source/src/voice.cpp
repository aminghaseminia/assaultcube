// voice.cpp: in-game push-to-talk voice chat (client side)
//
// Design: any number of talkers, raw 16-bit mono PCM at 16 kHz, 20 ms frames
// (640 bytes). The server only relays frames, it never decodes them.
//
// The feature is opt-in on both ends:
//   - client: /voicechat 0|1 (F9 by default)
//   - server:  -R
//
// Compatibility: the client probes the server with the standard SV_EXTENSION
// message ("dsvoice::hello"). Unpatched servers ignore unknown extensions. Only a
// server that answers with SV_VOICESUPPORT gets voice data, so unpatched clients
// never receive any of the new messages.

#include "cube.h"

static const int VOICE_RATE          = 16000;
static const int VOICE_FRAME_SAMPLES = 320;                 // 20 ms
static const int VOICE_FRAME_BYTES   = VOICE_FRAME_SAMPLES * 2;
static const int VOICE_MAX_WORDS     = VOICE_FRAME_BYTES / 4;

VARP(voicechat, 0, 1, 1);          // on by default: push-to-talk cannot be a hot mic
VARP(voicevolume, 0, 100, 200);    // playback volume, percent
VARP(voiceinputgain, 0, 100, 300); // microphone gain, percent

static SDL_AudioDeviceID capdev = 0, outdev = 0;
static SDL_AudioStream *capstream = NULL, *outstream = NULL;
static bool audiostarted = false, micfailed = false, outfailed = false;
static int lastouttry = -10000;

static bool talking = false, pttheld = false;
static bool testtalking = false;
static int locallevel = 0; // 0..100, drives the local VU meter
static int voicesupported = -1;    // -1 unknown, 0 server says no, 1 ready
static int lastprobe = -10000, probes = 0;
static ENetPeer *lastpeer = NULL;

static ushort outseq = 0;

// every remote talker gets its own small frame queue; the mixer sums one
// 20 ms frame from each active queue per tick, so any number of people can
// hold X at the same time
static const int VOICE_STREAM_FRAMES = 8;
struct voicestream
{
    bool active;
    ushort seq;
    int lastmillis;
    short frames[VOICE_STREAM_FRAMES][VOICE_FRAME_SAMPLES];
    int head, count;
};
static voicestream streams[MAXPLAYERS];
static int lastmix = -10000;

static void voice_stream_reset(voicestream &s)
{
    s.active = false;
    s.seq = 0;
    s.lastmillis = 0;
    s.head = s.count = 0;
}

static void voice_streams_clear()
{
    loopi(MAXPLAYERS) voice_stream_reset(streams[i]);
}

static void voice_stream_push(voicestream &s, const short *data)
{
    if(s.count >= VOICE_STREAM_FRAMES)
    {
        // queue is full: drop the oldest frame to keep latency down
        s.head = (s.head + 1) % VOICE_STREAM_FRAMES;
        s.count--;
    }
    int idx = (s.head + s.count) % VOICE_STREAM_FRAMES;
    memcpy(s.frames[idx], data, VOICE_FRAME_BYTES);
    s.count++;
}

static uchar framebuf[VOICE_FRAME_BYTES];
static int framelen = 0;

static void voice_stop();
static void voice_start();

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

static void voice_output(const short *samples, int count)
{
    if(!outdev || count <= 0) return;

    uchar converted[4096];
    if(outstream)
    {
        if(SDL_AudioStreamPut(outstream, samples, count * 2) < 0) return;
        int got;
        while((got = SDL_AudioStreamGet(outstream, converted, sizeof(converted))) > 0)
            SDL_QueueAudio(outdev, converted, got);
    }
    else SDL_QueueAudio(outdev, samples, count * 2);
}

// Sum one frame from every active speaker. This is what makes simultaneous
// push-to-talk work: N people can hold X and all of them are audible.
static void voice_mix()
{
    if(!outdev) return;
    if(SDL_GetQueuedAudioSize(outdev) > (Uint32)(VOICE_FRAME_BYTES * 6)) return;

    int mix[VOICE_FRAME_SAMPLES];
    memset(mix, 0, sizeof(mix));
    int speakers = 0;

    loopi(MAXPLAYERS)
    {
        voicestream &s = streams[i];
        if(!s.active) continue;
        if(s.count > 0)
        {
            short *f = s.frames[s.head];
            s.head = (s.head + 1) % VOICE_STREAM_FRAMES;
            s.count--;
            for(int j = 0; j < VOICE_FRAME_SAMPLES; j++) mix[j] += f[j];
            speakers++;
        }
        else if(totalmillis - s.lastmillis > 300)
        {
            s.active = false;
        }
    }
    if(!speakers) return;

    short out[VOICE_FRAME_SAMPLES];
    int vol = voicevolume;
    for(int i = 0; i < VOICE_FRAME_SAMPLES; i++)
    {
        int v = mix[i] * vol / 100;
        out[i] = (short)clamp(v, -32768, 32767);
    }
    voice_output(out, VOICE_FRAME_SAMPLES);
}

static void voice_send_frame(const uchar *data, int len)
{
    if(!curpeer || voicesupported != 1 || len != VOICE_FRAME_BYTES) return;

    short samples[VOICE_FRAME_SAMPLES];
    memcpy(samples, data, VOICE_FRAME_BYTES);
    int gain = voiceinputgain;
    int peak = 0;

    packetbuf p(MAXTRANS);
    putint(p, SV_VOICEDATA);
    putint(p, outseq++);
    putint(p, VOICE_MAX_WORDS);
    for(int i = 0; i < VOICE_MAX_WORDS; i++)
    {
        int s0 = clamp(samples[i * 2] * gain / 100, -32768, 32767);
        int s1 = clamp(samples[i * 2 + 1] * gain / 100, -32768, 32767);
        int a0 = s0 < 0 ? -s0 : s0;
        int a1 = s1 < 0 ? -s1 : s1;
        if(a0 > peak) peak = a0;
        if(a1 > peak) peak = a1;
        unsigned int word = ((unsigned int)(s0 & 0xFFFF)) | (((unsigned int)(s1 & 0xFFFF)) << 16);
        putint(p, (int)word);
    }
    int target = peak * 100 / 32768;
    if(target > locallevel) locallevel = target;             // fast attack
    else locallevel = (locallevel * 3 + target) / 4;          // slow decay
    sendpackettoserv(1, p.finalize());
}

void voicesupport(int supported)
{
    int before = voicesupported;
    voicesupported = supported ? 1 : 0;
    if(!voicesupported)
    {
        voice_stop();
        voice_streams_clear();
        if(outdev) SDL_ClearQueuedAudio(outdev);
    }
    if(!voicechat) return;
    if(voicesupported != before)
    {
        if(voicesupported) hudoutf("Voice chat: server ready - hold X to talk");
        else hudoutf("Voice chat: the server has it disabled");
    }
}

void voicestate(int cn, int on)
{
    if(!on || !player1 || cn == player1->clientnum) return;
    playerent *d = getclient(cn);
    if(d && !(d->muted || d->ignored)) clientlogf("voice: %s is talking", d->name);
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
    for(int i = n * 2; i < VOICE_FRAME_SAMPLES; i++) samples[i] = 0;

    if(!voicechat || !player1 || cn == player1->clientnum || n <= 0) return;
    if(cn < 0 || cn >= MAXPLAYERS) return;
    playerent *d = getclient(cn);
    if(d && (d->muted || d->ignored)) return; // respect the scoreboard mute
    if(!voice_open_output()) return;

    voicestream &s = streams[cn];
    ushort q = (ushort)seq;

    if(!s.active)
    {
        s.active = true;
        s.seq = (ushort)(q - 1); // treat this frame as the next one
        s.head = s.count = 0;
        s.lastmillis = totalmillis;
        clientlogf("voice: receiving audio from cn %d", cn);
    }

    ushort gap = (ushort)(q - s.seq);
    if(gap == 0 || gap > 0x8000) return; // duplicate or too old
    if(gap > 1 && gap <= 4)
    {
        // conceal a small gap with silence to keep this speaker's timing
        static short silence[VOICE_FRAME_SAMPLES] = { 0 };
        for(int i = 1; i < gap; i++) voice_stream_push(s, silence);
    }
    s.seq = q;
    s.lastmillis = totalmillis;
    voice_stream_push(s, samples);
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
        voice_streams_clear();
        if(outdev) SDL_ClearQueuedAudio(outdev); // stop any buffered remote audio now
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
    testtalking = true;
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
        voice_streams_clear();
        lastmix = totalmillis;
        if(!curpeer)
        {
            voice_stop();
            if(outdev) SDL_ClearQueuedAudio(outdev);
        }
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
            addmsg(SV_EXTENSION, "rsi", "dsvoice::hello", 0);
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
    if(testframes <= 0) testtalking = false;
    if(!talking && !testtalking) locallevel = (locallevel * 7) / 8;

    // mix whatever remote speakers have queued; this runs even when we are not
    // transmitting ourselves
    if(voicechat && totalmillis - lastmix >= 20)
    {
        lastmix = totalmillis;
        voice_mix();
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

void voicehudrender()
{
    if(!voicechat || !curpeer || !player1 || menuvisible()) return;

    bool local = talking || testtalking;
    int cns[8], n = 0;
    loopi(MAXPLAYERS) if(i != player1->clientnum && streams[i].active)
    {
        playerent *d = getclient(i);
        if(!d || d->muted || d->ignored) continue;
        if(n < (int)(sizeof(cns) / sizeof(cns[0]))) cns[n++] = i;
    }
    if(!local && !n) return;

    const int y = 820, pad = 16, lineh = FONTH + 8;
    const int shown = min(n, 6);

    int textw = text_width("\f2MIC\fr  transmitting");
    for(int i = 0; i < shown; i++)
    {
        playerent *d = getclient(cns[i]);
        if(d) textw = max(textw, text_width(colorname(d)));
    }
    int w = max(520, textw + pad * 2);
    int x = VIRTW * 2 - w - 30;
    if(x < 24) x = 24;

    int h = pad * 2;
    if(local) h += lineh + 42;
    h += (shown + (n > shown ? 1 : 0)) * lineh;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, VIRTW * 2, VIRTH * 2, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // opaque panel with a green accent strip; blend is turned off so the
    // background is visible regardless of the font's additive blend mode
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glColor4ub(8, 12, 10, 255);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glColor4ub(60, 200, 100, 255);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + 10, y);
    glVertex2f(x + 10, y + h);
    glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    int ty = y + pad;
    if(local)
    {
        draw_text("\f2MIC\fr  transmitting", x + pad, ty);
        ty += lineh;

        // VU meter: green -> yellow -> red
        const int segs = 14, segw = 20, segpitch = 25, segh = 28;
        int bx = x + pad, by = ty;
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        for(int i = 0; i < segs; i++)
        {
            int threshold = (i + 1) * 100 / segs;
            int r = 45, g = 45, b = 45, a = 255;
            if(locallevel >= threshold)
            {
                if(i < segs * 6 / 10) { r = 50; g = 220; b = 90; }
                else if(i < segs * 85 / 100) { r = 240; g = 200; b = 40; }
                else { r = 240; g = 60; b = 40; }
            }
            glColor4ub(r, g, b, a);
            glBegin(GL_QUADS);
            glVertex2f(bx + i * segpitch, by);
            glVertex2f(bx + i * segpitch + segw, by);
            glVertex2f(bx + i * segpitch + segw, by + segh);
            glVertex2f(bx + i * segpitch, by + segh);
            glEnd();
        }
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        ty += segh + 12;
    }

    for(int i = 0; i < shown; i++)
    {
        playerent *d = getclient(cns[i]);
        if(d) draw_text(colorname(d), x + pad, ty);
        ty += lineh;
    }
    if(n > shown)
    {
        draw_textf("\f7+%d more", x + pad, ty, n - shown);
        ty += lineh;
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void voicecleanup()
{
    voice_streams_clear();
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
