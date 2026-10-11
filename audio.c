// Procedural audio: a tiny SDL callback mixer. Each SFX is synthesized per-sample
// from oscillators + noise + envelopes; a continuous ambient drone is mixed under
// everything and slows/quietens when time freezes.
#include "audio.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>

#define SR 44100
#define NVOICE 24
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { SND_SHOOT, SND_HIT, SND_DEATH, SND_WHOOSH };
typedef struct { int active, type, pos, dur; float freq, vol; } Voice;

static Voice voices[NVOICE];
static SDL_AudioDeviceID dev;
static volatile float gTime = 1.0f;
static unsigned long gSample = 0;
static uint32_t rng = 2463534242u;
// 8-bit music: square melody + pulse bass, A-minor-ish loop
static double musicPhase = 0; static float melPh = 0, basPh = 0;
static const float MELODY[16] = {220,0,329.63f,0, 293.66f,0,329.63f,392, 440,0,392,329.63f, 293.66f,0,220,0};
static const float BASS[16]   = {110,110,110,110, 98,98,98,98, 87.31f,87.31f,87.31f,87.31f, 82.41f,82.41f,82.41f,82.41f};

static float frand(void) { // white noise in [-1,1]
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 8388608.0f - 1.0f;
}

static float gen(Voice *v) {
    float t  = (float)v->pos / SR;
    float td = (float)v->pos / v->dur;      // progress 0..1
    float env = 1.0f - td; if (env < 0) env = 0;
    switch (v->type) {
        case SND_SHOOT: {                    // square sweep down + noise crack
            float sweep = v->freq * (1.0f - td * 0.85f);
            float sq = sinf(2*M_PI*sweep*t) > 0 ? 1.0f : -1.0f;
            return (0.55f*sq + 0.45f*frand()) * env*env * v->vol;
        }
        case SND_HIT:                        // short high click
            return (sinf(2*M_PI*900.0f*t) + 0.3f*frand()) * env*env * v->vol;
        case SND_DEATH: {                    // descending tone + noise burst
            float tone = sinf(2*M_PI*(180.0f - 120.0f*td)*t);
            return (0.5f*tone + 0.5f*frand()) * env * v->vol;
        }
        case SND_WHOOSH: {                    // bell-enveloped rising noise/tone
            float bell = sinf(M_PI*td);
            float tone = sinf(2*M_PI*(300.0f + 500.0f*td)*t);
            return (0.4f*tone + 0.6f*frand()) * bell * v->vol;
        }
    }
    return 0;
}

static void callback(void *u, Uint8 *stream, int len) {
    (void)u;
    int16_t *out = (int16_t*)stream;
    int n = len / 2;
    for (int i = 0; i < n; i++) {
        double ta = (double)gSample / SR;
        float amb = sinf(2*M_PI*55.0f*ta)*0.06f + sinf(2*M_PI*82.5f*ta)*0.035f;
        amb *= (0.35f + 0.35f*gTime) * (0.85f + 0.15f*sinf(2*M_PI*0.13f*ta));
        // 8-bit music; tempo + volume bend with time (slows when frozen)
        musicPhase += (double)gTime * 7.0 / SR;
        int step = ((long)musicPhase) % 16; if (step < 0) step += 16;
        float mf = MELODY[step], bf = BASS[step];
        melPh += mf / (float)SR; if (melPh > 1) melPh -= 1;
        basPh += bf / (float)SR; if (basPh > 1) basPh -= 1;
        float mus = 0;
        if (mf > 0) mus += (melPh < 0.5f ? 1.0f : -1.0f) * 0.07f;
        if (bf > 0) mus += (basPh < 0.25f ? 1.0f : -1.0f) * 0.06f;
        mus *= (0.45f + 0.55f * gTime);
        float s = amb + mus;
        for (int v = 0; v < NVOICE; v++) if (voices[v].active) {
            s += gen(&voices[v]);
            if (++voices[v].pos >= voices[v].dur) voices[v].active = 0;
        }
        gSample++;
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = (int16_t)(s * 28000.0f);
    }
}

static void trigger(int type, float freq, float vol, float secs) {
    if (!dev) return;
    SDL_LockAudioDevice(dev);
    for (int i = 0; i < NVOICE; i++) if (!voices[i].active) {
        voices[i] = (Voice){1, type, 0, (int)(secs*SR), freq, vol};
        break;
    }
    SDL_UnlockAudioDevice(dev);
}

int audio_init(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SR; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 1024; want.callback = callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return -1;
    SDL_PauseAudioDevice(dev, 0);
    return 0;
}
void audio_shutdown(void) { if (dev) SDL_CloseAudioDevice(dev); }
void audio_shoot(void)  { trigger(SND_SHOOT, 320.0f, 0.5f,  0.14f); }
void audio_hit(void)    { trigger(SND_HIT,   900.0f, 0.35f, 0.06f); }
void audio_death(void)  { trigger(SND_DEATH, 180.0f, 0.5f,  0.5f);  }
void audio_whoosh(void) { trigger(SND_WHOOSH,300.0f, 0.4f,  0.35f); }
void audio_set_time(float t) { gTime = t; }
