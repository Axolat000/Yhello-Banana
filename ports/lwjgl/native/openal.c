/*
 * libopenal.so for Banana OS: the OpenAL 1.1 API (what LWJGL's org.lwjgl.openal
 * binds - Minecraft's sound) over Banana OS's sound output (banana_play).
 * LWJGL loads it by name with the SDK's dlopen and finds the functions with
 * dlsym or alcGetProcAddress; banana_play and the C library come from the
 * app's export table (the java app).
 *
 * What there is: one output device and one context; buffers (8 or 16 bit,
 * mono or stereo, any rate - kept as 16 bit); sources with gain, pitch
 * (resampling), looping, a static buffer or a queue (streaming: processed /
 * unqueued buffers), play / pause / stop / rewind, offsets; the listener's
 * position, orientation and gain; the distance models. A thread mixes the
 * playing sources at 44.1 kHz stereo - mono ones attenuated with distance and
 * panned left / right around the listener - and plays the result. Time runs
 * on the clock, so streams advance without a sound card too. Extensions:
 * AL_EXT_source_distance_model and AL_EXT_LINEAR_DISTANCE (Minecraft will
 * not start its sound without them). No capture, cones, Doppler or EFX
 * (their values are kept but not applied).
 */
#include <banana.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define API __attribute__((visibility("default")))

typedef char ALboolean;
typedef char ALchar;
typedef int ALint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;
typedef float ALfloat;
typedef double ALdouble;
typedef void ALvoid;
typedef char ALCboolean;
typedef char ALCchar;
typedef int ALCint;
typedef int ALCsizei;
typedef int ALCenum;
typedef unsigned int ALCuint;
typedef void ALCvoid;
typedef struct { int unused; } ALCdevice;
typedef struct { int unused; } ALCcontext;

#define AL_NONE 0
#define AL_FALSE 0
#define AL_TRUE 1
#define AL_SOURCE_DISTANCE_MODEL 0x200     /* AL_EXT_source_distance_model */
#define AL_SOURCE_RELATIVE 0x202
#define AL_CONE_INNER_ANGLE 0x1001
#define AL_CONE_OUTER_ANGLE 0x1002
#define AL_PITCH 0x1003
#define AL_POSITION 0x1004
#define AL_DIRECTION 0x1005
#define AL_VELOCITY 0x1006
#define AL_LOOPING 0x1007
#define AL_BUFFER 0x1009
#define AL_GAIN 0x100A
#define AL_MIN_GAIN 0x100D
#define AL_MAX_GAIN 0x100E
#define AL_ORIENTATION 0x100F
#define AL_SOURCE_STATE 0x1010
#define AL_INITIAL 0x1011
#define AL_PLAYING 0x1012
#define AL_PAUSED 0x1013
#define AL_STOPPED 0x1014
#define AL_BUFFERS_QUEUED 0x1015
#define AL_BUFFERS_PROCESSED 0x1016
#define AL_REFERENCE_DISTANCE 0x1020
#define AL_ROLLOFF_FACTOR 0x1021
#define AL_CONE_OUTER_GAIN 0x1022
#define AL_MAX_DISTANCE 0x1023
#define AL_SEC_OFFSET 0x1024
#define AL_SAMPLE_OFFSET 0x1025
#define AL_BYTE_OFFSET 0x1026
#define AL_SOURCE_TYPE 0x1027
#define AL_STATIC 0x1028
#define AL_STREAMING 0x1029
#define AL_UNDETERMINED 0x1030
#define AL_FORMAT_MONO8 0x1100
#define AL_FORMAT_MONO16 0x1101
#define AL_FORMAT_STEREO8 0x1102
#define AL_FORMAT_STEREO16 0x1103
#define AL_FORMAT_MONO_FLOAT32 0x10010
#define AL_FORMAT_STEREO_FLOAT32 0x10011
#define AL_FREQUENCY 0x2001
#define AL_BITS 0x2002
#define AL_CHANNELS 0x2003
#define AL_SIZE 0x2004
#define AL_NO_ERROR 0
#define AL_INVALID_NAME 0xA001
#define AL_INVALID_ENUM 0xA002
#define AL_INVALID_VALUE 0xA003
#define AL_INVALID_OPERATION 0xA004
#define AL_OUT_OF_MEMORY 0xA005
#define AL_VENDOR 0xB001
#define AL_VERSION 0xB002
#define AL_RENDERER 0xB003
#define AL_EXTENSIONS 0xB004
#define AL_DOPPLER_FACTOR 0xC000
#define AL_DOPPLER_VELOCITY 0xC001
#define AL_SPEED_OF_SOUND 0xC003
#define AL_DISTANCE_MODEL 0xD000
#define AL_INVERSE_DISTANCE 0xD001
#define AL_INVERSE_DISTANCE_CLAMPED 0xD002
#define AL_LINEAR_DISTANCE 0xD003
#define AL_LINEAR_DISTANCE_CLAMPED 0xD004
#define AL_EXPONENT_DISTANCE 0xD005
#define AL_EXPONENT_DISTANCE_CLAMPED 0xD006

#define ALC_FALSE 0
#define ALC_TRUE 1
#define ALC_FREQUENCY 0x1007
#define ALC_REFRESH 0x1008
#define ALC_SYNC 0x1009
#define ALC_MONO_SOURCES 0x1010
#define ALC_STEREO_SOURCES 0x1011
#define ALC_NO_ERROR 0
#define ALC_INVALID_DEVICE 0xA001
#define ALC_INVALID_CONTEXT 0xA002
#define ALC_INVALID_ENUM 0xA003
#define ALC_INVALID_VALUE 0xA004
#define ALC_OUT_OF_MEMORY 0xA005
#define ALC_MAJOR_VERSION 0x1000
#define ALC_MINOR_VERSION 0x1001
#define ALC_ATTRIBUTES_SIZE 0x1002
#define ALC_ALL_ATTRIBUTES 0x1003
#define ALC_DEFAULT_DEVICE_SPECIFIER 0x1004
#define ALC_DEVICE_SPECIFIER 0x1005
#define ALC_EXTENSIONS 0x1006
#define ALC_CAPTURE_DEVICE_SPECIFIER 0x310
#define ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER 0x311
#define ALC_CAPTURE_SAMPLES 0x312
#define ALC_CONNECTED 0x313
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#define ALC_ALL_DEVICES_SPECIFIER 0x1013

#define RATE        44100               /* the mix */
#define CHUNK       441                 /* frames per mix: 10 ms */
#define AHEAD_MS    120                 /* sound queued ahead at most */
#define MAX_SOURCES 256
#define MAX_QUEUE   64
#define DEVICE_NAME "Banana OS Audio"

typedef struct {
    int      used;
    int16_t* data;                      /* frames * channels samples */
    uint32_t frames;
    int      channels, freq;
} buffer_t;

typedef struct {
    int      used;
    ALenum   state, type;
    float    gain, pitch, min_gain, max_gain;
    float    pos[3], vel[3], dir[3];
    float    ref_dist, max_dist, rolloff;
    float    cone_inner, cone_outer, cone_outer_gain;
    int      relative, looping;
    ALenum   model;                     /* AL_DISTANCE_MODEL: its own (AL_SOURCE_DISTANCE_MODEL) */
    ALuint   queue[MAX_QUEUE];
    int      queued, cur;               /* the buffer playing: queue[cur] */
    uint64_t at;                        /* in it, frames as 32.32 fixed point */
} source_t;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static buffer_t*  g_bufs;
static int        g_nbufs;
static source_t   g_src[MAX_SOURCES];
static float      g_lpos[3], g_lvel[3], g_lat[3] = { 0, 0, -1 }, g_lup[3] = { 0, 1, 0 }, g_lgain = 1;
static ALenum     g_model = AL_INVERSE_DISTANCE_CLAMPED;
static int        g_source_models;     /* alEnable(AL_SOURCE_DISTANCE_MODEL) */
static float      g_doppler = 1, g_doppler_vel = 1, g_sound_speed = 343.3f;
static ALenum     g_err, g_alc_err;
static int        g_dev_open, g_ctx_made, g_current;
static ALCdevice  g_dev;
static ALCcontext g_ctx;
static pthread_t  g_mixer;
static volatile int g_mixing;

static void al_error(ALenum e) { if (!g_err) g_err = e; }
static void alc_error(ALenum e) { if (!g_alc_err) g_alc_err = e; }

static source_t* src(ALuint id) {
    return id >= 1 && id <= MAX_SOURCES && g_src[id - 1].used ? &g_src[id - 1] : NULL;
}
static buffer_t* buf(ALuint id) {
    return id >= 1 && (int)id <= g_nbufs && g_bufs[id - 1].used ? &g_bufs[id - 1] : NULL;
}

/* ── the mixer ─────────────────────────────────────────────────────── */

static float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static float distance_gain(const source_t* s, float d) {
    float ref = s->ref_dist, mx = s->max_dist, r = s->rolloff;
    switch (g_source_models ? s->model : g_model) {
    case AL_NONE: return 1;
    case AL_INVERSE_DISTANCE_CLAMPED: if (d < ref) d = ref; if (d > mx) d = mx; /* fall through */
    case AL_INVERSE_DISTANCE: return ref + r * (d - ref) > 0 ? ref / (ref + r * (d - ref)) : 1;
    case AL_LINEAR_DISTANCE_CLAMPED: if (d < ref) d = ref; if (d > mx) d = mx; /* fall through */
    case AL_LINEAR_DISTANCE: return mx > ref ? 1 - r * (d - ref) / (mx - ref) : 1;
    case AL_EXPONENT_DISTANCE_CLAMPED: if (d < ref) d = ref; if (d > mx) d = mx; /* fall through */
    case AL_EXPONENT_DISTANCE: return ref > 0 && d > 0 ? powf(d / ref, -r) : 1;
    }
    return 1;
}

/* a source's left / right gains now */
static void source_gains(const source_t* s, int channels, float* gl, float* gr) {
    float g = s->gain * g_lgain;
    if (channels == 1) {
        float rel[3];
        for (int i = 0; i < 3; i++) rel[i] = s->relative ? s->pos[i] : s->pos[i] - g_lpos[i];
        float d = sqrtf(dot3(rel, rel));
        float dg = distance_gain(s, d);
        if (dg < 0) dg = 0;
        g *= dg;
        /* pan: the side of the listener it is on (right = at x up) */
        float pan = 0;
        if (d > 1e-4f) {
            float right[3] = { g_lat[1] * g_lup[2] - g_lat[2] * g_lup[1], g_lat[2] * g_lup[0] - g_lat[0] * g_lup[2],
                               g_lat[0] * g_lup[1] - g_lat[1] * g_lup[0] };
            float rl = sqrtf(dot3(right, right));
            if (rl > 1e-6f) pan = dot3(rel, right) / (rl * d);
        }
        if (g < s->min_gain) g = s->min_gain;
        if (g > s->max_gain) g = s->max_gain;
        *gl = g * sqrtf((1 - pan) * 0.5f);
        *gr = g * sqrtf((1 + pan) * 0.5f);
    } else {                            /* stereo buffers are not placed (the spec) */
        if (g < s->min_gain) g = s->min_gain;
        if (g > s->max_gain) g = s->max_gain;
        *gl = *gr = g;
    }
}

/* adds n frames of source s into mix[n * 2] (under the lock) */
static void mix_source(source_t* s, float* mix, int n) {
    int i = 0;
    while (i < n && s->state == AL_PLAYING) {
        if (s->cur >= s->queued) { s->state = AL_STOPPED; break; }
        buffer_t* b = buf(s->queue[s->cur]);
        if (!b || !b->frames) { s->cur++; s->at = 0; continue; }
        float gl, gr;
        source_gains(s, b->channels, &gl, &gr);
        float p = s->pitch > 0 ? s->pitch : 1;
        uint64_t step = (uint64_t)((double)b->freq * p / RATE * 4294967296.0);
        if (!step) step = 1;
        uint64_t end = (uint64_t)b->frames << 32;
        for (; i < n && s->at < end; i++, s->at += step) {
            uint32_t f = (uint32_t)(s->at >> 32);
            float t = (float)(uint32_t)s->at / 4294967296.0f;
            uint32_t f2 = f + 1 < b->frames ? f + 1 : f;
            if (b->channels == 1) {
                float v = (b->data[f] + (b->data[f2] - b->data[f]) * t) / 32768.0f;
                mix[i * 2] += v * gl;
                mix[i * 2 + 1] += v * gr;
            } else {
                float l = (b->data[f * 2] + (b->data[f2 * 2] - b->data[f * 2]) * t) / 32768.0f;
                float r = (b->data[f * 2 + 1] + (b->data[f2 * 2 + 1] - b->data[f * 2 + 1]) * t) / 32768.0f;
                mix[i * 2] += l * gl;
                mix[i * 2 + 1] += r * gr;
            }
        }
        if (s->at >= end) {             /* this buffer is done: the next, or again */
            s->at -= end;
            if (s->type == AL_STATIC || (s->looping && s->queued == 1)) {
                if (s->looping) continue;
                s->cur = s->queued;
            } else {
                s->cur++;
                if (s->cur >= s->queued && s->looping) s->cur = 0;
            }
            if (s->cur >= s->queued) s->state = AL_STOPPED;
        }
    }
}

/* milliseconds, from Banana OS's tick count (32 bits: widened as it wraps) */
static uint64_t now_ms(void) {
    static uint32_t last;
    static uint64_t high;
    uint32_t t = banana_ticks();
    if (t < last) high += 1ull << 32;
    last = t;
    return high + t;
}

static void* mixer(void* arg) {
    (void)arg;
    float mix[CHUNK * 2];
    int16_t out[CHUNK * 2];
    uint64_t start = now_ms(), done = 0;     /* frames mixed since start */
    while (g_mixing) {
        uint64_t due = (now_ms() - start + AHEAD_MS) * RATE / 1000;
        if (due > done + RATE / 2) done = due - RATE / 2;     /* (fell behind: skip, do not pile up) */
        while (done + CHUNK <= due) {
            memset(mix, 0, sizeof(mix));
            pthread_mutex_lock(&g_lock);
            for (int k = 0; k < MAX_SOURCES; k++)
                if (g_src[k].used && g_src[k].state == AL_PLAYING) mix_source(&g_src[k], mix, CHUNK);
            pthread_mutex_unlock(&g_lock);
            for (int i = 0; i < CHUNK * 2; i++) {
                float v = mix[i] * 32767.0f;
                out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            if (banana_audio_queued_ms() < 2 * AHEAD_MS) banana_play(out, sizeof(out), RATE, 2, 16);
            done += CHUNK;
        }
        usleep(5000);
    }
    return NULL;
}

/* ── ALC ───────────────────────────────────────────────────────────── */

API ALCdevice* alcOpenDevice(const ALCchar* name) {
    (void)name;                         /* (one device, whatever it is called) */
    pthread_mutex_lock(&g_lock);
    g_dev_open = 1;
    pthread_mutex_unlock(&g_lock);
    return &g_dev;
}

API ALCboolean alcCloseDevice(ALCdevice* d) {
    if (d != &g_dev || !g_dev_open) { alc_error(ALC_INVALID_DEVICE); return ALC_FALSE; }
    g_dev_open = 0;
    return ALC_TRUE;
}

API ALCcontext* alcCreateContext(ALCdevice* d, const ALCint* attrs) {
    (void)attrs;
    if (d != &g_dev || !g_dev_open) { alc_error(ALC_INVALID_DEVICE); return NULL; }
    if (g_ctx_made) { alc_error(ALC_INVALID_VALUE); return NULL; }     /* one context */
    g_ctx_made = 1;
    if (!g_mixing) {
        g_mixing = 1;
        if (pthread_create(&g_mixer, NULL, mixer, NULL) != 0) g_mixing = 0;
    }
    return &g_ctx;
}

API ALCboolean alcMakeContextCurrent(ALCcontext* c) {
    if (c && (c != &g_ctx || !g_ctx_made)) { alc_error(ALC_INVALID_CONTEXT); return ALC_FALSE; }
    g_current = c != NULL;
    return ALC_TRUE;
}

API void alcProcessContext(ALCcontext* c) { (void)c; }
API void alcSuspendContext(ALCcontext* c) { (void)c; }

API void alcDestroyContext(ALCcontext* c) {
    if (c != &g_ctx || !g_ctx_made) { alc_error(ALC_INVALID_CONTEXT); return; }
    if (g_mixing) {
        g_mixing = 0;
        pthread_join(g_mixer, NULL);
    }
    pthread_mutex_lock(&g_lock);
    memset(g_src, 0, sizeof(g_src));
    g_ctx_made = 0;
    g_current = 0;
    pthread_mutex_unlock(&g_lock);
}

API ALCcontext* alcGetCurrentContext(void) { return g_current ? &g_ctx : NULL; }
API ALCdevice* alcGetContextsDevice(ALCcontext* c) { return c == &g_ctx ? &g_dev : NULL; }

API ALCenum alcGetError(ALCdevice* d) {
    (void)d;
    ALCenum e = g_alc_err;
    g_alc_err = ALC_NO_ERROR;
    return e;
}

API ALCboolean alcIsExtensionPresent(ALCdevice* d, const ALCchar* name) {
    (void)d;
    return name && !strcasecmp(name, "ALC_ENUMERATION_EXT") ? ALC_TRUE : ALC_FALSE;
}

API ALCenum alcGetEnumValue(ALCdevice* d, const ALCchar* name);

API const ALCchar* alcGetString(ALCdevice* d, ALCenum p) {
    switch (p) {
    case ALC_NO_ERROR:          return "No Error";
    case ALC_INVALID_DEVICE:    return "Invalid Device";
    case ALC_INVALID_CONTEXT:   return "Invalid Context";
    case ALC_INVALID_ENUM:      return "Invalid Enum";
    case ALC_INVALID_VALUE:     return "Invalid Value";
    case ALC_OUT_OF_MEMORY:     return "Out of Memory";
    case ALC_DEFAULT_DEVICE_SPECIFIER:
    case ALC_DEFAULT_ALL_DEVICES_SPECIFIER: return DEVICE_NAME;
    case ALC_DEVICE_SPECIFIER:
    case ALC_ALL_DEVICES_SPECIFIER:
        /* (no device: the list of them, names one after the other, "" at the end) */
        return d ? DEVICE_NAME : DEVICE_NAME "\0";
    case ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER:
    case ALC_CAPTURE_DEVICE_SPECIFIER: return "\0";
    case ALC_EXTENSIONS:        return "ALC_ENUMERATION_EXT";
    }
    alc_error(ALC_INVALID_ENUM);
    return NULL;
}

API void alcGetIntegerv(ALCdevice* d, ALCenum p, ALCsizei n, ALCint* out) {
    static const ALCint attrs[] = { ALC_FREQUENCY, RATE, ALC_REFRESH, 100, ALC_SYNC, ALC_FALSE,
                                    ALC_MONO_SOURCES, MAX_SOURCES - 1, ALC_STEREO_SOURCES, 1, 0 };
    if (!out || n <= 0) { alc_error(ALC_INVALID_VALUE); return; }
    switch (p) {
    case ALC_MAJOR_VERSION:   out[0] = 1; return;
    case ALC_MINOR_VERSION:   out[0] = 1; return;
    case ALC_ATTRIBUTES_SIZE: out[0] = (ALCint)(sizeof(attrs) / sizeof(attrs[0])); return;
    case ALC_ALL_ATTRIBUTES:
        if (n < (ALCsizei)(sizeof(attrs) / sizeof(attrs[0]))) { alc_error(ALC_INVALID_VALUE); return; }
        memcpy(out, attrs, sizeof(attrs));
        return;
    case ALC_FREQUENCY:       out[0] = RATE; return;
    case ALC_REFRESH:         out[0] = 100; return;
    case ALC_SYNC:            out[0] = ALC_FALSE; return;
    case ALC_MONO_SOURCES:    out[0] = MAX_SOURCES - 1; return;
    case ALC_STEREO_SOURCES:  out[0] = 1; return;
    case ALC_CONNECTED:       out[0] = d == &g_dev && g_dev_open; return;
    case ALC_CAPTURE_SAMPLES: out[0] = 0; return;
    }
    /* anything else - ALC_SOFT_HRTF's ALC_NUM_HRTF_SPECIFIERS_SOFT, which
     * Minecraft asks for even when the extension is not there: 0, and no
     * error (one left pending made its next check turn the sound off) */
    out[0] = 0;
}

/* no capture (microphones) */
API ALCdevice* alcCaptureOpenDevice(const ALCchar* name, ALCuint freq, ALCenum fmt, ALCsizei size) {
    (void)name; (void)freq; (void)fmt; (void)size;
    alc_error(ALC_INVALID_VALUE);
    return NULL;
}
API ALCboolean alcCaptureCloseDevice(ALCdevice* d) { (void)d; alc_error(ALC_INVALID_DEVICE); return ALC_FALSE; }
API void alcCaptureStart(ALCdevice* d) { (void)d; alc_error(ALC_INVALID_DEVICE); }
API void alcCaptureStop(ALCdevice* d) { (void)d; alc_error(ALC_INVALID_DEVICE); }
API void alcCaptureSamples(ALCdevice* d, ALCvoid* b, ALCsizei n) { (void)d; (void)b; (void)n; alc_error(ALC_INVALID_DEVICE); }

/* ── AL: state ─────────────────────────────────────────────────────── */

API ALenum alGetError(void) {
    pthread_mutex_lock(&g_lock);
    ALenum e = g_err;
    g_err = AL_NO_ERROR;
    pthread_mutex_unlock(&g_lock);
    return e;
}

API void alEnable(ALenum c) { if (c == AL_SOURCE_DISTANCE_MODEL) g_source_models = 1; else al_error(AL_INVALID_ENUM); }
API void alDisable(ALenum c) { if (c == AL_SOURCE_DISTANCE_MODEL) g_source_models = 0; else al_error(AL_INVALID_ENUM); }
API ALboolean alIsEnabled(ALenum c) {
    if (c == AL_SOURCE_DISTANCE_MODEL) return g_source_models ? AL_TRUE : AL_FALSE;
    al_error(AL_INVALID_ENUM);
    return AL_FALSE;
}

API const ALchar* alGetString(ALenum p) {
    switch (p) {
    case AL_VENDOR:   return "Banana OS";
    case AL_VERSION:  return "1.1 Banana OS";
    case AL_RENDERER: return "Banana OS software mixer";
    case AL_EXTENSIONS: return "AL_EXT_source_distance_model AL_EXT_LINEAR_DISTANCE";
    case AL_NO_ERROR: return "No Error";
    case AL_INVALID_NAME: return "Invalid Name";
    case AL_INVALID_ENUM: return "Invalid Enum";
    case AL_INVALID_VALUE: return "Invalid Value";
    case AL_INVALID_OPERATION: return "Invalid Operation";
    case AL_OUT_OF_MEMORY: return "Out of Memory";
    }
    al_error(AL_INVALID_ENUM);
    return NULL;
}

static int state_value(ALenum p, double* v) {
    switch (p) {
    case AL_DOPPLER_FACTOR:   *v = g_doppler; return 1;
    case AL_DOPPLER_VELOCITY: *v = g_doppler_vel; return 1;
    case AL_SPEED_OF_SOUND:   *v = g_sound_speed; return 1;
    case AL_DISTANCE_MODEL:   *v = g_model; return 1;
    }
    al_error(AL_INVALID_ENUM);
    return 0;
}
API ALboolean alGetBoolean(ALenum p) { double v = 0; return state_value(p, &v) && v != 0; }
API ALint alGetInteger(ALenum p) { double v = 0; state_value(p, &v); return (ALint)v; }
API ALfloat alGetFloat(ALenum p) { double v = 0; state_value(p, &v); return (ALfloat)v; }
API ALdouble alGetDouble(ALenum p) { double v = 0; state_value(p, &v); return v; }
API void alGetBooleanv(ALenum p, ALboolean* v) { if (v) *v = alGetBoolean(p); }
API void alGetIntegerv(ALenum p, ALint* v) { if (v) *v = alGetInteger(p); }
API void alGetFloatv(ALenum p, ALfloat* v) { if (v) *v = alGetFloat(p); }
API void alGetDoublev(ALenum p, ALdouble* v) { if (v) *v = alGetDouble(p); }

API void alDistanceModel(ALenum m) {
    if (m != AL_NONE && (m < AL_INVERSE_DISTANCE || m > AL_EXPONENT_DISTANCE_CLAMPED)) { al_error(AL_INVALID_VALUE); return; }
    g_model = m;
}
API void alDopplerFactor(ALfloat v) { if (v < 0) al_error(AL_INVALID_VALUE); else g_doppler = v; }
API void alDopplerVelocity(ALfloat v) { if (v <= 0) al_error(AL_INVALID_VALUE); else g_doppler_vel = v; }
API void alSpeedOfSound(ALfloat v) { if (v <= 0) al_error(AL_INVALID_VALUE); else g_sound_speed = v; }

API ALboolean alIsExtensionPresent(const ALchar* name) {
    return name && (!strcasecmp(name, "AL_EXT_source_distance_model") || !strcasecmp(name, "AL_EXT_LINEAR_DISTANCE"));
}

/* ── AL: the listener ──────────────────────────────────────────────── */

API void alListenerfv(ALenum p, const ALfloat* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    switch (p) {
    case AL_GAIN:        if (v[0] < 0) al_error(AL_INVALID_VALUE); else g_lgain = v[0]; break;
    case AL_POSITION:    memcpy(g_lpos, v, sizeof(g_lpos)); break;
    case AL_VELOCITY:    memcpy(g_lvel, v, sizeof(g_lvel)); break;
    case AL_ORIENTATION: memcpy(g_lat, v, sizeof(g_lat)); memcpy(g_lup, v + 3, sizeof(g_lup)); break;
    default:             al_error(AL_INVALID_ENUM);
    }
    pthread_mutex_unlock(&g_lock);
}
API void alListenerf(ALenum p, ALfloat v) {
    if (p != AL_GAIN) { al_error(AL_INVALID_ENUM); return; }
    alListenerfv(p, &v);
}
API void alListener3f(ALenum p, ALfloat x, ALfloat y, ALfloat z) {
    if (p != AL_POSITION && p != AL_VELOCITY) { al_error(AL_INVALID_ENUM); return; }
    ALfloat v[3] = { x, y, z };
    alListenerfv(p, v);
}
API void alListeneri(ALenum p, ALint v) { (void)v; (void)p; al_error(AL_INVALID_ENUM); }
API void alListener3i(ALenum p, ALint x, ALint y, ALint z) { alListener3f(p, (ALfloat)x, (ALfloat)y, (ALfloat)z); }
API void alListeneriv(ALenum p, const ALint* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    ALfloat f[6];
    int n = p == AL_ORIENTATION ? 6 : 3;
    for (int i = 0; i < n; i++) f[i] = (ALfloat)v[i];
    alListenerfv(p, f);
}

API void alGetListenerfv(ALenum p, ALfloat* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    switch (p) {
    case AL_GAIN:        v[0] = g_lgain; break;
    case AL_POSITION:    memcpy(v, g_lpos, sizeof(g_lpos)); break;
    case AL_VELOCITY:    memcpy(v, g_lvel, sizeof(g_lvel)); break;
    case AL_ORIENTATION: memcpy(v, g_lat, sizeof(g_lat)); memcpy(v + 3, g_lup, sizeof(g_lup)); break;
    default:             al_error(AL_INVALID_ENUM);
    }
    pthread_mutex_unlock(&g_lock);
}
API void alGetListenerf(ALenum p, ALfloat* v) {
    if (p != AL_GAIN) { al_error(AL_INVALID_ENUM); return; }
    alGetListenerfv(p, v);
}
API void alGetListener3f(ALenum p, ALfloat* x, ALfloat* y, ALfloat* z) {
    ALfloat v[3] = { 0, 0, 0 };
    if (p != AL_POSITION && p != AL_VELOCITY) { al_error(AL_INVALID_ENUM); return; }
    alGetListenerfv(p, v);
    if (x) *x = v[0];
    if (y) *y = v[1];
    if (z) *z = v[2];
}
API void alGetListeneri(ALenum p, ALint* v) { (void)v; (void)p; al_error(AL_INVALID_ENUM); }
API void alGetListener3i(ALenum p, ALint* x, ALint* y, ALint* z) {
    ALfloat a, b, c;
    alGetListener3f(p, &a, &b, &c);
    if (x) *x = (ALint)a;
    if (y) *y = (ALint)b;
    if (z) *z = (ALint)c;
}
API void alGetListeneriv(ALenum p, ALint* v) {
    ALfloat f[6] = { 0 };
    int n = p == AL_ORIENTATION ? 6 : p == AL_GAIN ? 1 : 3;
    alGetListenerfv(p, f);
    for (int i = 0; v && i < n; i++) v[i] = (ALint)f[i];
}

/* ── AL: buffers ───────────────────────────────────────────────────── */

API void alGenBuffers(ALsizei n, ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    for (ALsizei k = 0; k < n; k++) {
        int i = 0;
        while (i < g_nbufs && g_bufs[i].used) i++;
        if (i == g_nbufs) {
            buffer_t* nb = (buffer_t*)realloc(g_bufs, (size_t)(g_nbufs + 64) * sizeof(buffer_t));
            if (!nb) { al_error(AL_OUT_OF_MEMORY); break; }
            memset(nb + g_nbufs, 0, 64 * sizeof(buffer_t));
            g_bufs = nb;
            g_nbufs += 64;
        }
        memset(&g_bufs[i], 0, sizeof(buffer_t));
        g_bufs[i].used = 1;
        g_bufs[i].channels = 1;
        g_bufs[i].freq = RATE;
        ids[k] = (ALuint)i + 1;
    }
    pthread_mutex_unlock(&g_lock);
}

/* is buffer id queued on a source? (under the lock) */
static int buffer_in_use(ALuint id) {
    for (int k = 0; k < MAX_SOURCES; k++)
        for (int q = 0; g_src[k].used && q < g_src[k].queued; q++)
            if (g_src[k].queue[q] == id) return 1;
    return 0;
}

API void alDeleteBuffers(ALsizei n, const ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    for (ALsizei k = 0; k < n; k++)
        if (ids[k] && (!buf(ids[k]) || buffer_in_use(ids[k]))) {
            al_error(buf(ids[k]) ? AL_INVALID_OPERATION : AL_INVALID_NAME);
            pthread_mutex_unlock(&g_lock);
            return;
        }
    for (ALsizei k = 0; k < n; k++) {
        buffer_t* b = buf(ids[k]);
        if (!b) continue;
        free(b->data);
        memset(b, 0, sizeof(*b));
    }
    pthread_mutex_unlock(&g_lock);
}

API ALboolean alIsBuffer(ALuint id) { return id == 0 || buf(id) != NULL; }

API void alBufferData(ALuint id, ALenum fmt, const ALvoid* data, ALsizei size, ALsizei freq) {
    int ch, bytes;
    switch (fmt) {
    case AL_FORMAT_MONO8:          ch = 1; bytes = 1; break;
    case AL_FORMAT_MONO16:         ch = 1; bytes = 2; break;
    case AL_FORMAT_STEREO8:        ch = 2; bytes = 1; break;
    case AL_FORMAT_STEREO16:       ch = 2; bytes = 2; break;
    case AL_FORMAT_MONO_FLOAT32:   ch = 1; bytes = 4; break;
    case AL_FORMAT_STEREO_FLOAT32: ch = 2; bytes = 4; break;
    default: al_error(AL_INVALID_ENUM); return;
    }
    if (size < 0 || freq <= 0 || (size && !data) || size % (ch * bytes)) { al_error(AL_INVALID_VALUE); return; }
    uint32_t frames = (uint32_t)size / (uint32_t)(ch * bytes);
    int16_t* d = (int16_t*)malloc((size_t)frames * ch * sizeof(int16_t) + 2);
    if (!d) { al_error(AL_OUT_OF_MEMORY); return; }
    for (uint32_t i = 0; i < frames * (uint32_t)ch; i++) {
        if (bytes == 1) d[i] = (int16_t)(((int)((const uint8_t*)data)[i] - 128) << 8);
        else if (bytes == 2) { int16_t v; memcpy(&v, (const uint8_t*)data + i * 2, 2); d[i] = v; }
        else {
            float f;
            memcpy(&f, (const uint8_t*)data + i * 4, 4);
            f = f > 1 ? 1 : f < -1 ? -1 : f;
            d[i] = (int16_t)(f * 32767);
        }
    }
    pthread_mutex_lock(&g_lock);
    buffer_t* b = buf(id);
    if (!b || buffer_in_use(id)) {
        al_error(b ? AL_INVALID_OPERATION : AL_INVALID_NAME);
        pthread_mutex_unlock(&g_lock);
        free(d);
        return;
    }
    free(b->data);
    b->data = d;
    b->frames = frames;
    b->channels = ch;
    b->freq = freq;
    pthread_mutex_unlock(&g_lock);
}

API void alGetBufferi(ALuint id, ALenum p, ALint* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    buffer_t* b = buf(id);
    if (!b) al_error(AL_INVALID_NAME);
    else switch (p) {
        case AL_FREQUENCY: *v = b->freq; break;
        case AL_BITS:      *v = 16; break;
        case AL_CHANNELS:  *v = b->channels; break;
        case AL_SIZE:      *v = (ALint)(b->frames * (uint32_t)b->channels * 2); break;
        default:           al_error(AL_INVALID_ENUM);
    }
    pthread_mutex_unlock(&g_lock);
}
API void alGetBufferiv(ALuint id, ALenum p, ALint* v) { alGetBufferi(id, p, v); }
API void alGetBufferf(ALuint id, ALenum p, ALfloat* v) { (void)id; (void)p; (void)v; al_error(AL_INVALID_ENUM); }
API void alGetBufferfv(ALuint id, ALenum p, ALfloat* v) { alGetBufferf(id, p, v); }
API void alGetBuffer3f(ALuint id, ALenum p, ALfloat* x, ALfloat* y, ALfloat* z) { (void)id; (void)p; (void)x; (void)y; (void)z; al_error(AL_INVALID_ENUM); }
API void alGetBuffer3i(ALuint id, ALenum p, ALint* x, ALint* y, ALint* z) { (void)id; (void)p; (void)x; (void)y; (void)z; al_error(AL_INVALID_ENUM); }
/* (no settable buffer properties in AL 1.1) */
API void alBufferf(ALuint id, ALenum p, ALfloat v) { (void)id; (void)p; (void)v; al_error(AL_INVALID_ENUM); }
API void alBuffer3f(ALuint id, ALenum p, ALfloat x, ALfloat y, ALfloat z) { (void)id; (void)p; (void)x; (void)y; (void)z; al_error(AL_INVALID_ENUM); }
API void alBufferfv(ALuint id, ALenum p, const ALfloat* v) { (void)id; (void)p; (void)v; al_error(AL_INVALID_ENUM); }
API void alBufferi(ALuint id, ALenum p, ALint v) { (void)id; (void)p; (void)v; al_error(AL_INVALID_ENUM); }
API void alBuffer3i(ALuint id, ALenum p, ALint x, ALint y, ALint z) { (void)id; (void)p; (void)x; (void)y; (void)z; al_error(AL_INVALID_ENUM); }
API void alBufferiv(ALuint id, ALenum p, const ALint* v) { (void)id; (void)p; (void)v; al_error(AL_INVALID_ENUM); }

/* ── AL: sources ───────────────────────────────────────────────────── */

static void source_reset(source_t* s) {
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->state = AL_INITIAL;
    s->type = AL_UNDETERMINED;
    s->gain = s->pitch = s->max_gain = 1;
    s->ref_dist = s->rolloff = 1;
    s->max_dist = 3.402823466e38f;
    s->cone_inner = s->cone_outer = 360;
    s->model = AL_INVERSE_DISTANCE_CLAMPED;
}

API void alGenSources(ALsizei n, ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    int free_ = 0;
    for (int k = 0; k < MAX_SOURCES; k++) free_ += !g_src[k].used;
    if (n > free_) {                    /* (all or nothing: Minecraft counts its sources this way) */
        al_error(AL_OUT_OF_MEMORY);
        pthread_mutex_unlock(&g_lock);
        return;
    }
    for (ALsizei i = 0, k = 0; i < n; k++)
        if (!g_src[k].used) {
            source_reset(&g_src[k]);
            ids[i++] = (ALuint)k + 1;
        }
    pthread_mutex_unlock(&g_lock);
}

API void alDeleteSources(ALsizei n, const ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    for (ALsizei i = 0; i < n; i++)
        if (!src(ids[i])) { al_error(AL_INVALID_NAME); pthread_mutex_unlock(&g_lock); return; }
    for (ALsizei i = 0; i < n; i++) memset(src(ids[i]), 0, sizeof(source_t));
    pthread_mutex_unlock(&g_lock);
}

API ALboolean alIsSource(ALuint id) { return src(id) != NULL; }

/* buffers of the queue already played (under the lock) */
static int processed(const source_t* s) {
    if (s->type == AL_STATIC) return 0;
    if (s->state == AL_STOPPED) return s->queued;
    if (s->state == AL_INITIAL) return 0;
    return s->cur < s->queued ? s->cur : s->queued;
}

static uint32_t queue_frames_before(const source_t* s, int upto, int* freq, int* ch) {
    uint32_t f = 0;
    *freq = RATE;
    *ch = 1;
    for (int q = 0; q < s->queued && q <= upto; q++) {
        buffer_t* b = buf(s->queue[q]);
        if (!b) continue;
        if (q == upto) { *freq = b->freq; *ch = b->channels; break; }
        f += b->frames;
    }
    return f;
}

/* the play position, in frames from the start of the queue */
static double offset_frames(const source_t* s, int* freq, int* ch) {
    if (s->state != AL_PLAYING && s->state != AL_PAUSED) { *freq = RATE; *ch = 1; return 0; }
    return queue_frames_before(s, s->cur, freq, ch) + (double)(s->at >> 32);
}

/* moves to frame f of the queue */
static void seek_frames(source_t* s, double f) {
    for (int q = 0; q < s->queued; q++) {
        buffer_t* b = buf(s->queue[q]);
        uint32_t n = b ? b->frames : 0;
        if (f < n) { s->cur = q; s->at = (uint64_t)f << 32; return; }
        f -= n;
    }
    al_error(AL_INVALID_VALUE);
}

static void set_float(source_t* s, ALenum p, const float* v) {
    switch (p) {
    case AL_PITCH:              if (v[0] < 0) al_error(AL_INVALID_VALUE); else s->pitch = v[0]; return;
    case AL_GAIN:               if (v[0] < 0) al_error(AL_INVALID_VALUE); else s->gain = v[0]; return;
    case AL_MIN_GAIN:           s->min_gain = v[0]; return;
    case AL_MAX_GAIN:           s->max_gain = v[0]; return;
    case AL_REFERENCE_DISTANCE: s->ref_dist = v[0]; return;
    case AL_ROLLOFF_FACTOR:     s->rolloff = v[0]; return;
    case AL_MAX_DISTANCE:       s->max_dist = v[0]; return;
    case AL_CONE_INNER_ANGLE:   s->cone_inner = v[0]; return;
    case AL_CONE_OUTER_ANGLE:   s->cone_outer = v[0]; return;
    case AL_CONE_OUTER_GAIN:    s->cone_outer_gain = v[0]; return;
    case AL_POSITION:           memcpy(s->pos, v, sizeof(s->pos)); return;
    case AL_VELOCITY:           memcpy(s->vel, v, sizeof(s->vel)); return;
    case AL_DIRECTION:          memcpy(s->dir, v, sizeof(s->dir)); return;
    case AL_SEC_OFFSET: case AL_SAMPLE_OFFSET: case AL_BYTE_OFFSET: {
        if (s->state != AL_PLAYING && s->state != AL_PAUSED) return;   /* (applied at play: not kept) */
        int freq, ch;
        queue_frames_before(s, 0, &freq, &ch);
        double f = p == AL_SEC_OFFSET ? v[0] * freq : p == AL_SAMPLE_OFFSET ? v[0] : v[0] / (2.0 * ch);
        seek_frames(s, f);
        return;
    }
    }
    al_error(AL_INVALID_ENUM);
}

API void alSourcefv(ALuint id, ALenum p, const ALfloat* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    if (!s) al_error(AL_INVALID_NAME);
    else set_float(s, p, v);
    pthread_mutex_unlock(&g_lock);
}
API void alSourcef(ALuint id, ALenum p, ALfloat v) {
    if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) { al_error(AL_INVALID_ENUM); return; }
    alSourcefv(id, p, &v);
}
API void alSource3f(ALuint id, ALenum p, ALfloat x, ALfloat y, ALfloat z) {
    if (p != AL_POSITION && p != AL_VELOCITY && p != AL_DIRECTION) { al_error(AL_INVALID_ENUM); return; }
    ALfloat v[3] = { x, y, z };
    alSourcefv(id, p, v);
}

API void alSourcei(ALuint id, ALenum p, ALint v) {
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    if (!s) { al_error(AL_INVALID_NAME); pthread_mutex_unlock(&g_lock); return; }
    switch (p) {
    case AL_SOURCE_RELATIVE: s->relative = v != 0; break;
    case AL_LOOPING:         s->looping = v != 0; break;
    case AL_DISTANCE_MODEL:
        if (v != AL_NONE && (v < AL_INVERSE_DISTANCE || v > AL_EXPONENT_DISTANCE_CLAMPED)) al_error(AL_INVALID_VALUE);
        else s->model = v;
        break;
    case AL_BUFFER:
        if (s->state == AL_PLAYING || s->state == AL_PAUSED) { al_error(AL_INVALID_OPERATION); break; }
        if (v && !buf((ALuint)v)) { al_error(AL_INVALID_VALUE); break; }
        s->queued = v ? 1 : 0;
        s->queue[0] = (ALuint)v;
        s->cur = 0;
        s->at = 0;
        s->type = v ? AL_STATIC : AL_UNDETERMINED;
        break;
    default: {
        float f[3] = { (float)v, 0, 0 };
        if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) al_error(AL_INVALID_ENUM);
        else set_float(s, p, f);
    }
    }
    pthread_mutex_unlock(&g_lock);
}
API void alSource3i(ALuint id, ALenum p, ALint x, ALint y, ALint z) { alSource3f(id, p, (ALfloat)x, (ALfloat)y, (ALfloat)z); }
API void alSourceiv(ALuint id, ALenum p, const ALint* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) alSource3i(id, p, v[0], v[1], v[2]);
    else alSourcei(id, p, v[0]);
}

API void alGetSourcefv(ALuint id, ALenum p, ALfloat* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    int freq, ch;
    if (!s) al_error(AL_INVALID_NAME);
    else switch (p) {
        case AL_PITCH:              v[0] = s->pitch; break;
        case AL_GAIN:               v[0] = s->gain; break;
        case AL_MIN_GAIN:           v[0] = s->min_gain; break;
        case AL_MAX_GAIN:           v[0] = s->max_gain; break;
        case AL_REFERENCE_DISTANCE: v[0] = s->ref_dist; break;
        case AL_ROLLOFF_FACTOR:     v[0] = s->rolloff; break;
        case AL_MAX_DISTANCE:       v[0] = s->max_dist; break;
        case AL_CONE_INNER_ANGLE:   v[0] = s->cone_inner; break;
        case AL_CONE_OUTER_ANGLE:   v[0] = s->cone_outer; break;
        case AL_CONE_OUTER_GAIN:    v[0] = s->cone_outer_gain; break;
        case AL_POSITION:           memcpy(v, s->pos, sizeof(s->pos)); break;
        case AL_VELOCITY:           memcpy(v, s->vel, sizeof(s->vel)); break;
        case AL_DIRECTION:          memcpy(v, s->dir, sizeof(s->dir)); break;
        case AL_SEC_OFFSET:         { double f = offset_frames(s, &freq, &ch); v[0] = (float)(f / freq); break; }
        case AL_SAMPLE_OFFSET:      v[0] = (float)offset_frames(s, &freq, &ch); break;
        case AL_BYTE_OFFSET:        { double f = offset_frames(s, &freq, &ch); v[0] = (float)(f * 2 * ch); break; }
        default:                    al_error(AL_INVALID_ENUM);
    }
    pthread_mutex_unlock(&g_lock);
}
API void alGetSourcef(ALuint id, ALenum p, ALfloat* v) {
    if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) { al_error(AL_INVALID_ENUM); return; }
    ALfloat f[3];
    alGetSourcefv(id, p, f);
    if (v) *v = f[0];
}
API void alGetSource3f(ALuint id, ALenum p, ALfloat* x, ALfloat* y, ALfloat* z) {
    ALfloat v[3] = { 0, 0, 0 };
    if (p != AL_POSITION && p != AL_VELOCITY && p != AL_DIRECTION) { al_error(AL_INVALID_ENUM); return; }
    alGetSourcefv(id, p, v);
    if (x) *x = v[0];
    if (y) *y = v[1];
    if (z) *z = v[2];
}

API void alGetSourcei(ALuint id, ALenum p, ALint* v) {
    if (!v) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    if (!s) { al_error(AL_INVALID_NAME); pthread_mutex_unlock(&g_lock); return; }
    switch (p) {
    case AL_SOURCE_STATE:      *v = s->state; break;
    case AL_SOURCE_TYPE:       *v = s->type; break;
    case AL_SOURCE_RELATIVE:   *v = s->relative; break;
    case AL_LOOPING:           *v = s->looping; break;
    case AL_DISTANCE_MODEL:    *v = s->model; break;
    case AL_BUFFER:            *v = s->queued && s->cur < s->queued ? (ALint)s->queue[s->cur] : s->queued ? (ALint)s->queue[s->queued - 1] : 0; break;
    case AL_BUFFERS_QUEUED:    *v = s->queued; break;
    case AL_BUFFERS_PROCESSED: *v = processed(s); break;
    default: {
        pthread_mutex_unlock(&g_lock);
        ALfloat f[3] = { 0, 0, 0 };
        if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) { al_error(AL_INVALID_ENUM); return; }
        alGetSourcefv(id, p, f);
        *v = (ALint)f[0];
        return;
    }
    }
    pthread_mutex_unlock(&g_lock);
}
API void alGetSource3i(ALuint id, ALenum p, ALint* x, ALint* y, ALint* z) {
    ALfloat a = 0, b = 0, c = 0;
    alGetSource3f(id, p, &a, &b, &c);
    if (x) *x = (ALint)a;
    if (y) *y = (ALint)b;
    if (z) *z = (ALint)c;
}
API void alGetSourceiv(ALuint id, ALenum p, ALint* v) {
    if (p == AL_POSITION || p == AL_VELOCITY || p == AL_DIRECTION) alGetSource3i(id, p, v, v ? v + 1 : NULL, v ? v + 2 : NULL);
    else alGetSourcei(id, p, v);
}

/* play / pause / stop / rewind, of one or several sources at once */
enum { DO_PLAY, DO_PAUSE, DO_STOP, DO_REWIND };

static void source_do(source_t* s, int what) {
    switch (what) {
    case DO_PLAY:
        if (s->state != AL_PAUSED) { s->cur = 0; s->at = 0; }
        s->state = s->queued ? AL_PLAYING : AL_STOPPED;
        break;
    case DO_PAUSE:  if (s->state == AL_PLAYING) s->state = AL_PAUSED; break;
    case DO_STOP:   if (s->state != AL_INITIAL) { s->state = AL_STOPPED; s->cur = s->queued; s->at = 0; } break;
    case DO_REWIND: s->state = AL_INITIAL; s->cur = 0; s->at = 0; break;
    }
}

static void sources_do(ALsizei n, const ALuint* ids, int what) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    for (ALsizei i = 0; i < n; i++)
        if (!src(ids[i])) { al_error(AL_INVALID_NAME); pthread_mutex_unlock(&g_lock); return; }
    for (ALsizei i = 0; i < n; i++) source_do(src(ids[i]), what);
    pthread_mutex_unlock(&g_lock);
}

API void alSourcePlayv(ALsizei n, const ALuint* ids) { sources_do(n, ids, DO_PLAY); }
API void alSourcePausev(ALsizei n, const ALuint* ids) { sources_do(n, ids, DO_PAUSE); }
API void alSourceStopv(ALsizei n, const ALuint* ids) { sources_do(n, ids, DO_STOP); }
API void alSourceRewindv(ALsizei n, const ALuint* ids) { sources_do(n, ids, DO_REWIND); }
API void alSourcePlay(ALuint id) { sources_do(1, &id, DO_PLAY); }
API void alSourcePause(ALuint id) { sources_do(1, &id, DO_PAUSE); }
API void alSourceStop(ALuint id) { sources_do(1, &id, DO_STOP); }
API void alSourceRewind(ALuint id) { sources_do(1, &id, DO_REWIND); }

API void alSourceQueueBuffers(ALuint id, ALsizei n, const ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    if (!s) al_error(AL_INVALID_NAME);
    else if (s->type == AL_STATIC) al_error(AL_INVALID_OPERATION);
    else if (s->queued + n > MAX_QUEUE) al_error(AL_INVALID_VALUE);
    else {
        int ok = 1;
        for (ALsizei i = 0; i < n; i++) if (ids[i] && !buf(ids[i])) ok = 0;
        if (!ok) al_error(AL_INVALID_NAME);
        else {
            /* a stopped stream that gets more: what was played stays processed */
            int was_done = s->state == AL_STOPPED && s->cur >= s->queued;
            for (ALsizei i = 0; i < n; i++) s->queue[s->queued++] = ids[i];
            if (was_done) s->cur = s->queued - n;
            s->type = AL_STREAMING;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

API void alSourceUnqueueBuffers(ALuint id, ALsizei n, ALuint* ids) {
    if (n < 0 || (n && !ids)) { al_error(AL_INVALID_VALUE); return; }
    pthread_mutex_lock(&g_lock);
    source_t* s = src(id);
    if (!s) al_error(AL_INVALID_NAME);
    else if (n > processed(s)) al_error(AL_INVALID_VALUE);
    else if (n) {
        for (ALsizei i = 0; i < n; i++) ids[i] = s->queue[i];
        memmove(s->queue, s->queue + n, (size_t)(s->queued - n) * sizeof(ALuint));
        s->queued -= n;
        s->cur = s->cur > n ? s->cur - n : 0;
        if (s->state == AL_STOPPED) s->cur = s->queued;
        if (!s->queued) s->type = AL_UNDETERMINED;
    }
    pthread_mutex_unlock(&g_lock);
}

/* ── names ─────────────────────────────────────────────────────────── */

API ALenum alGetEnumValue(const ALchar* name);

#define FN(f) { #f, (void*)f }
static const struct { const char* name; void* fn; } g_fns[] = {
    FN(alcOpenDevice), FN(alcCloseDevice), FN(alcCreateContext), FN(alcMakeContextCurrent), FN(alcProcessContext),
    FN(alcSuspendContext), FN(alcDestroyContext), FN(alcGetCurrentContext), FN(alcGetContextsDevice),
    FN(alcGetError), FN(alcIsExtensionPresent), FN(alcGetEnumValue), FN(alcGetString), FN(alcGetIntegerv),
    FN(alcCaptureOpenDevice), FN(alcCaptureCloseDevice), FN(alcCaptureStart), FN(alcCaptureStop),
    FN(alcCaptureSamples),
    FN(alGetError), FN(alEnable), FN(alDisable), FN(alIsEnabled), FN(alGetString), FN(alGetBoolean),
    FN(alGetInteger), FN(alGetFloat), FN(alGetDouble), FN(alGetBooleanv), FN(alGetIntegerv), FN(alGetFloatv),
    FN(alGetDoublev), FN(alDistanceModel), FN(alDopplerFactor), FN(alDopplerVelocity), FN(alSpeedOfSound),
    FN(alIsExtensionPresent), FN(alGetEnumValue),
    FN(alListenerf), FN(alListener3f), FN(alListenerfv), FN(alListeneri), FN(alListener3i), FN(alListeneriv),
    FN(alGetListenerf), FN(alGetListener3f), FN(alGetListenerfv), FN(alGetListeneri), FN(alGetListener3i),
    FN(alGetListeneriv),
    FN(alGenBuffers), FN(alDeleteBuffers), FN(alIsBuffer), FN(alBufferData), FN(alBufferf), FN(alBuffer3f),
    FN(alBufferfv), FN(alBufferi), FN(alBuffer3i), FN(alBufferiv), FN(alGetBufferf), FN(alGetBuffer3f),
    FN(alGetBufferfv), FN(alGetBufferi), FN(alGetBuffer3i), FN(alGetBufferiv),
    FN(alGenSources), FN(alDeleteSources), FN(alIsSource), FN(alSourcef), FN(alSource3f), FN(alSourcefv),
    FN(alSourcei), FN(alSource3i), FN(alSourceiv), FN(alGetSourcef), FN(alGetSource3f), FN(alGetSourcefv),
    FN(alGetSourcei), FN(alGetSource3i), FN(alGetSourceiv), FN(alSourcePlay), FN(alSourcePause),
    FN(alSourceStop), FN(alSourceRewind), FN(alSourcePlayv), FN(alSourcePausev), FN(alSourceStopv),
    FN(alSourceRewindv), FN(alSourceQueueBuffers), FN(alSourceUnqueueBuffers),
};

static const struct { const char* name; int value; } g_enums[] = {
    { "AL_NONE", AL_NONE }, { "AL_FALSE", AL_FALSE }, { "AL_TRUE", AL_TRUE },
    { "AL_SOURCE_RELATIVE", AL_SOURCE_RELATIVE }, { "AL_SOURCE_DISTANCE_MODEL", AL_SOURCE_DISTANCE_MODEL }, { "AL_PITCH", AL_PITCH }, { "AL_POSITION", AL_POSITION },
    { "AL_DIRECTION", AL_DIRECTION }, { "AL_VELOCITY", AL_VELOCITY }, { "AL_LOOPING", AL_LOOPING },
    { "AL_BUFFER", AL_BUFFER }, { "AL_GAIN", AL_GAIN }, { "AL_ORIENTATION", AL_ORIENTATION },
    { "AL_SOURCE_STATE", AL_SOURCE_STATE }, { "AL_PLAYING", AL_PLAYING }, { "AL_STOPPED", AL_STOPPED },
    { "AL_BUFFERS_QUEUED", AL_BUFFERS_QUEUED }, { "AL_BUFFERS_PROCESSED", AL_BUFFERS_PROCESSED },
    { "AL_FORMAT_MONO8", AL_FORMAT_MONO8 }, { "AL_FORMAT_MONO16", AL_FORMAT_MONO16 },
    { "AL_FORMAT_STEREO8", AL_FORMAT_STEREO8 }, { "AL_FORMAT_STEREO16", AL_FORMAT_STEREO16 },
    { "AL_FORMAT_MONO_FLOAT32", AL_FORMAT_MONO_FLOAT32 }, { "AL_FORMAT_STEREO_FLOAT32", AL_FORMAT_STEREO_FLOAT32 },
    { "AL_DISTANCE_MODEL", AL_DISTANCE_MODEL }, { "AL_LINEAR_DISTANCE", AL_LINEAR_DISTANCE },
    { "AL_LINEAR_DISTANCE_CLAMPED", AL_LINEAR_DISTANCE_CLAMPED },
    { "AL_INVERSE_DISTANCE_CLAMPED", AL_INVERSE_DISTANCE_CLAMPED },
    { "ALC_FREQUENCY", ALC_FREQUENCY }, { "ALC_MONO_SOURCES", ALC_MONO_SOURCES },
    { "ALC_STEREO_SOURCES", ALC_STEREO_SOURCES }, { "ALC_DEFAULT_DEVICE_SPECIFIER", ALC_DEFAULT_DEVICE_SPECIFIER },
    { "ALC_DEVICE_SPECIFIER", ALC_DEVICE_SPECIFIER }, { "ALC_EXTENSIONS", ALC_EXTENSIONS },
    { "ALC_CONNECTED", ALC_CONNECTED },
};

static void* lookup_fn(const char* name) {
    for (size_t i = 0; name && i < sizeof(g_fns) / sizeof(g_fns[0]); i++)
        if (!strcmp(g_fns[i].name, name)) return g_fns[i].fn;
    return NULL;
}
static int lookup_enum(const char* name) {
    for (size_t i = 0; name && i < sizeof(g_enums) / sizeof(g_enums[0]); i++)
        if (!strcmp(g_enums[i].name, name)) return g_enums[i].value;
    return 0;
}

API void* alcGetProcAddress(ALCdevice* d, const ALCchar* name) { (void)d; return lookup_fn(name); }
API void* alGetProcAddress(const ALchar* name) { return lookup_fn(name); }
API ALCenum alcGetEnumValue(ALCdevice* d, const ALCchar* name) { (void)d; return lookup_enum(name); }
API ALenum alGetEnumValue(const ALchar* name) { return lookup_enum(name); }
