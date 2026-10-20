#include "dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================== parameter tables */

static const char *const osc_modes[] = {"LIVE", "LOOP", "WAVE", "GRAIN", NULL};
static const char *const focuses[] = {"ALL SOUND", "VOICES ONLY", "MUSIC ONLY", NULL};
static const char *const directions[] = {"FORWARD", "REVERSE", "BOTH", NULL};
static const char *const stut_modes[] = {"REPEAT", "REVERSE", "RANDOM", "PITCH", "DROPOUT", "SCATTER", NULL};
const char *const category_names[] = {"ANY", "VOICE", "MUSIC", "SOUND", NULL};
static const char *const offon[] = {"OFF", "ON", NULL};
static const char *const osc_waves[] = {"OFF", "SINE", "TRIANGLE", "SAW", "SQUARE", NULL};
static const char *const vocodes[] = {"OFF", "A SPEAKS THRU B", "B SPEAKS THRU A", NULL};
static const char *const fm_dirs[] = {"B BENDS A", "A BENDS B", NULL};
static const char *const band_names[] = {"5", "8", "13", "21", "34", NULL};
static const int band_counts[] = {5, 8, 13, 21, 34};
static const char *const sync_modes[] = {"OFF", "AUTO", "LEAD", "FOLLOW", NULL};
static const char *const roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
static const char *const rates[] = {"1/32", "1/16", "1/8T", "1/8", "1/8.", "5/16", "1/4T", "1/4", "1/4.",
                                    "5/8", "1/2", "1 BAR", "2 BARS", "4 BARS", "5 BARS", NULL};
static const float rate_beats[] = {0.125f, 0.25f, 1.0f / 3, 0.5f, 0.75f, 1.25f, 2.0f / 3, 1, 1.5f, 2.5f, 2, 4, 8, 16, 20};

const char *const page_names[PAGE_COUNT] = {"VOICES", "RHYTHM", "ENGINE", "MIX"};

int euclid_hit(int steps, int pulses, int rotate, int step)
{
    if (steps <= 0 || pulses <= 0) return 0;
    if (pulses >= steps) return 1;
    int j = ((step - rotate) % steps + steps) % steps;
    return (j * pulses) % steps < pulses;
}

#define CELL_PARAMS(X, x, C, key, label, on, steps, pulses)                                                     \
    [P_##X##_##C##_ON] = {x "_" key "_on", label, PAGE_RHYTHM, 0, 1, on, 1, 0, "", offon},                       \
    [P_##X##_##C##_STEPS] = {x "_" key "_steps", label " Steps", PAGE_RHYTHM, 1, 16, steps, 1, 0, "int", NULL},  \
    [P_##X##_##C##_PULSES] = {x "_" key "_pulses", label " Pulses", PAGE_RHYTHM, 0, 16, pulses, 1, 0, "int", NULL}, \
    [P_##X##_##C##_ROT] = {x "_" key "_rot", label " Rotate", PAGE_RHYTHM, 0, 15, 0, 1, 0, "int", NULL}

#define VOICE_PARAMS(X, x, mode, size, tex, lvl, pan, steps, pulses, length)                                \
    [P_##X##_STATION] = {x "_station", "Station", PAGE_VOICES, -1, 511, -1, 1, 0, "station", NULL},              \
    [P_##X##_MODE] = {x "_engine", "Engine", PAGE_VOICES, 0, 3, mode, 1, 0, "", osc_modes},                      \
    [P_##X##_TUNE] = {x "_tune", "Pitch", PAGE_VOICES, -24, 24, 0, 1, 0, "st", NULL},                            \
    [P_##X##_SIZE] = {x "_size", "Size", PAGE_VOICES, 1, 2700, size, 1.08f, 1, "ms", NULL},                      \
    [P_##X##_TEXTURE] = {x "_texture", "Texture", PAGE_VOICES, 0, 1, tex, 0.02f, 0, "", NULL},                   \
    [P_##X##_DIR] = {x "_dir", "Direction", PAGE_VOICES, 0, 2, 0, 1, 0, "", directions},                        \
    [P_##X##_FOCUS] = {x "_focus", "Focus", PAGE_VOICES, 0, 2, 0, 1, 0, "", focuses},                            \
    [P_##X##_LEVEL] = {x "_level", "Level", PAGE_MIX, 0, 2, lvl, 0.02f, 0, "lvl", NULL},                       \
    [P_##X##_PAN] = {x "_pan", "Pan", PAGE_VOICES, -1, 1, pan, 0.05f, 0, "pan", NULL},                           \
    [P_##X##_DRIVE] = {x "_drive", "Drive", PAGE_MIX, 0, 1, 0, 0.02f, 0, "", NULL},                              \
    [P_##X##_OSC] = {x "_osc", "Oscillator", PAGE_VOICES, 0, 4, 0, 1, 0, "", osc_waves},                         \
    [P_##X##_OSC_MIX] = {x "_osc_mix", "Osc Mix", PAGE_VOICES, 0, 1, 0, 0.02f, 0, "", NULL},                     \
    CELL_PARAMS(X, x, GATE, "gate", "Rhythm", 1, steps, pulses),                                                  \
    [P_##X##_RATE] = {x "_rate", "Rate", PAGE_RHYTHM, 0, 14, 3, 1, 0, "", rates},                                \
    [P_##X##_LENGTH] = {x "_length", "Gate Length", PAGE_RHYTHM, 0.05f, 1, length, 0.05f, 0, "", NULL},          \
    [P_##X##_ATTACK] = {x "_attack", "Gate Attack", PAGE_RHYTHM, 0, 1, 0, 0.05f, 0, "", NULL},                   \
    [P_##X##_RELEASE] = {x "_release", "Gate Release", PAGE_RHYTHM, 0, 1, 0.25f, 0.05f, 0, "", NULL},            \
    [P_##X##_CHANCE] = {x "_chance", "Chance", PAGE_RHYTHM, 0, 1, 1, 0.05f, 0, "", NULL},                        \
    [P_##X##_STUT_ON] = {x "_stut_on", "Stutter", PAGE_RHYTHM, 0, 1, 0, 1, 0, "", offon},                         \
    [P_##X##_STUT_STEPS] = {x "_stut_steps", "Stutter Steps", PAGE_RHYTHM, 1, 16, 8, 1, 0, "int", NULL},          \
    [P_##X##_STUT_PULSES] = {x "_stut_pulses", "Stutter Pulses", PAGE_RHYTHM, 0, 16, 1, 1, 0, "int", NULL},       \
    [P_##X##_STUT_ROT] = {x "_stut_rot", "Stutter Rotate", PAGE_RHYTHM, 0, 15, 3, 1, 0, "int", NULL},             \
    [P_##X##_STUT_RATCHET] = {x "_ratchet", "Ratchets", PAGE_RHYTHM, 2, 8, 3, 1, 0, "int", NULL},                 \
    [P_##X##_STUT_MODE] = {x "_stut_mode", "Stutter Mode", PAGE_RHYTHM, 0, 5, 0, 1, 0, "", stut_modes},          \
    [P_##X##_STUT_CHANCE] = {x "_stut_chance", "Stutter Chance", PAGE_RHYTHM, 0, 1, 1, 0.05f, 0, "", NULL}

const ParamInfo param_info[P_COUNT] = {
    /* A plays the radio as it is, pulsing gently; B slices it into a five-against-eight rhythm. */
    VOICE_PARAMS(A, "a", OSC_LIVE, 250, 0.0f, 1.0f, -0.25f, 8, 5, 0.85f),
    VOICE_PARAMS(B, "b", OSC_LOOP, 300, 0.35f, 0.85f, 0.25f, 5, 3, 0.5f),

    [P_BPM]    = {"bpm", "Tempo (SELECT)", PAGE_RHYTHM, 20, 300, 80, 1, 0, "bpm", NULL},
    [P_SYNC]   = {"sync", "Wi-Fi Sync", PAGE_RHYTHM, 0, 3, 1, 1, 0, "", sync_modes},
    [P_VOCODE]     = {"vocode", "Vocode", PAGE_ENGINE, 0, 2, 0, 1, 0, "", vocodes},
    [P_VOCODE_AMT] = {"vocode_amt", "Vocode Amount", PAGE_ENGINE, 0, 1, 0.8f, 0.05f, 0, "", NULL},
    [P_BANDS]      = {"bands", "Vocode Bands", PAGE_ENGINE, 0, 4, 2, 1, 0, "", band_names},
    [P_FM]         = {"fm", "FM", PAGE_ENGINE, 0, 1, 0, 0.02f, 0, "", NULL},
    [P_FM_DIR]     = {"fm_dir", "FM Direction", PAGE_ENGINE, 0, 1, 0, 1, 0, "", fm_dirs},
    [P_RING]       = {"ring", "Ring Mod", PAGE_ENGINE, 0, 1, 0, 0.05f, 0, "", NULL},
    [P_MORPH]      = {"morph", "Morph A>B", PAGE_ENGINE, 0, 1, 0, 0.04f, 0, "morph", NULL},
    [P_CONVERSE]   = {"converse", "Converse", PAGE_ENGINE, 0, 1, 0, 0.05f, 0, "", NULL},
    [P_ROOT]   = {"root", "Pitch Root", PAGE_ENGINE, 0, 11, 0, 1, 0, "", roots},
    [P_TONE]   = {"tone", "Filter", PAGE_ENGINE, -1, 1, 0, 0.04f, 0, "tone", NULL},
    [P_RESO]   = {"reso", "Resonance", PAGE_ENGINE, 0, 1, 0.2f, 0.05f, 0, "", NULL},
    [P_LOW]    = {"low", "Bass", PAGE_ENGINE, -12, 12, 0, 1, 0, "dB", NULL},
    [P_HIGH]   = {"high", "Treble", PAGE_ENGINE, -12, 12, 0, 1, 0, "dB", NULL},
    [P_DRIVE]  = {"drive", "Drive", PAGE_ENGINE, 0, 1, 0, 0.04f, 0, "", NULL},
    [P_FOLD]   = {"fold", "Fold", PAGE_ENGINE, 0, 1, 0, 0.04f, 0, "", NULL},
    [P_CRUSH]  = {"crush", "Crush", PAGE_ENGINE, 0, 1, 0, 0.04f, 0, "", NULL},
    [P_ECHO]   = {"echo", "Echo", PAGE_ENGINE, 0, 1, 0.15f, 0.05f, 0, "", NULL},
    [P_SPACE]  = {"space", "Space", PAGE_ENGINE, 0, 1, 0.25f, 0.05f, 0, "", NULL},
    [P_WIDTH]  = {"width", "Stereo Width", PAGE_ENGINE, 0, 2, 1, 0.05f, 0, "", NULL},
    [P_VOLUME] = {"volume", "Volume", PAGE_MIX, -24, 12, 0, 1, 0, "dB", NULL},
    [P_CROSS_LEVEL] = {"cross_level", "Cross Level", PAGE_MIX, 0, 2, 1, 0.02f, 0, "lvl", NULL},
    [P_SHAPE_LEVEL] = {"shape_level", "Shape Level", PAGE_MIX, 0, 2, 1, 0.02f, 0, "lvl", NULL},
    [P_ECHO_LEVEL]  = {"echo_level", "Echo Level", PAGE_MIX, 0, 2, 1, 0.02f, 0, "lvl", NULL},
    [P_SPACE_LEVEL] = {"space_level", "Space Level", PAGE_MIX, 0, 2, 1, 0.02f, 0, "lvl", NULL},
    [P_LIMIT]       = {"limit", "Limiter", PAGE_MIX, 0, 1, 0, 0.02f, 0, "limit", NULL},
    [P_X_GATE_ON]     = {"x_gate_on", "Rhythm", PAGE_RHYTHM, 0, 1, 0, 1, 0, "", offon},
    [P_X_GATE_STEPS]  = {"x_gate_steps", "Rhythm Steps", PAGE_RHYTHM, 1, 16, 8, 1, 0, "int", NULL},
    [P_X_GATE_PULSES] = {"x_gate_pulses", "Rhythm Pulses", PAGE_RHYTHM, 0, 16, 3, 1, 0, "int", NULL},
    [P_X_GATE_ROT]    = {"x_gate_rot", "Rhythm Rotate", PAGE_RHYTHM, 0, 15, 0, 1, 0, "int", NULL},
    [P_X_RATE]        = {"x_rate", "Rate", PAGE_RHYTHM, 0, 14, 3, 1, 0, "", rates},
    [P_X_LENGTH]      = {"x_length", "Gate Length", PAGE_RHYTHM, 0.05f, 1, 0.6f, 0.05f, 0, "", NULL},
    [P_X_ATTACK]      = {"x_attack", "Gate Attack", PAGE_RHYTHM, 0, 1, 0.1f, 0.05f, 0, "", NULL},
    [P_X_RELEASE]     = {"x_release", "Gate Release", PAGE_RHYTHM, 0, 1, 0.3f, 0.05f, 0, "", NULL},
    [P_X_CHANCE]      = {"x_chance", "Chance", PAGE_RHYTHM, 0, 1, 1, 0.05f, 0, "", NULL},
    [P_X_STUT_ON]      = {"x_stut_on", "Stutter", PAGE_RHYTHM, 0, 1, 0, 1, 0, "", offon},
    [P_X_STUT_STEPS]   = {"x_stut_steps", "Stutter Steps", PAGE_RHYTHM, 1, 16, 8, 1, 0, "int", NULL},
    [P_X_STUT_PULSES]  = {"x_stut_pulses", "Stutter Pulses", PAGE_RHYTHM, 0, 16, 1, 1, 0, "int", NULL},
    [P_X_STUT_ROT]     = {"x_stut_rot", "Stutter Rotate", PAGE_RHYTHM, 0, 15, 7, 1, 0, "int", NULL},
    [P_X_STUT_RATCHET] = {"x_ratchet", "Ratchets", PAGE_RHYTHM, 2, 8, 4, 1, 0, "int", NULL},
    [P_X_STUT_MODE]    = {"x_stut_mode", "Stutter Mode", PAGE_RHYTHM, 0, 5, 0, 1, 0, "", stut_modes},
    [P_X_STUT_CHANCE]  = {"x_stut_chance", "Stutter Chance", PAGE_RHYTHM, 0, 1, 1, 0.05f, 0, "", NULL},
};

static int osc_of(int id) { return id >= P_A_STATION && id <= P_B_STUT_CHANCE ? (id - P_A_STATION) / OSC_STRIDE : -1; }
static int osc_field(int id) { return (id - P_A_STATION) % OSC_STRIDE + P_A_STATION; }

const char *param_section(int id)
{
    int o = osc_of(id);
    if (o >= 0) {
        if (osc_field(id) >= P_A_STUT_ON) return o ? "STUTTER B" : "STUTTER A";
        if (osc_field(id) >= P_A_GATE_ON) return o ? "RHYTHM B" : "RHYTHM A";
        return o ? "VOICE B" : "VOICE A";
    }
    if (id >= P_X_STUT_ON) return "STUTTER CROSS";
    if (id >= P_X_GATE_ON) return "RHYTHM CROSS";
    if (id <= P_SYNC) return "CLOCK";
    if (id <= P_CONVERSE) return "CROSS";
    if (id <= P_ROOT) return "KEY";
    if (id <= P_CRUSH) return "SHAPE";
    if (id <= P_WIDTH) return "SPACE";
    if (id == P_VOLUME) return "OUTPUT";
    return "MIX";
}

const char *param_label(int id, const float *p)
{
    int o = osc_of(id);
    if (o < 0) return param_info[id].label;
    int mode = (int)p[P_A_MODE + o * OSC_STRIDE];
    int f = osc_field(id);
    static const char *const size_l[] = {"Size (unused)", "Loop Length", "Cycle Size", "Grain Size"};
    static const char *const tex_l[] = {"Crush", "Auto Grab", "Scan Rate", "Density"};
    if (f == P_A_SIZE) return size_l[mode];
    if (f == P_A_TEXTURE) return tex_l[mode];
    if (f == P_A_DIR) return mode == OSC_LIVE ? "Direction (unused)" : mode == OSC_GRAIN ? "Grain Direction" : "Direction";
    return param_info[id].label;
}

float param_clamp(int id, float v)
{
    const ParamInfo *p = &param_info[id];
    if (v != v) v = p->def; /* NaN from a corrupt settings file */
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    if (p->choices || !strcmp(p->unit, "station") || !strcmp(p->unit, "int")) v = floorf(v + 0.5f);
    return v;
}

float param_adjust(int id, float v, int dir, int fine)
{
    const ParamInfo *p = &param_info[id];
    if (p->choices) {
        int n = (int)p->max + 1;
        return (float)(((int)(v + 0.5f) + dir + n) % n); /* wrap through choices */
    }
    if (p->log) {
        float st = fine ? powf(p->step, 0.25f) : p->step;
        v = dir > 0 ? v * st : v / st;
    } else {
        float step = (fine && strcmp(p->unit, "int") && strcmp(p->unit, "station")) ? p->step * 0.1f : p->step;
        v += dir * step;
        if (fabsf(v) < p->step * 0.05f) v = 0; /* land exactly on zero */
    }
    return param_clamp(id, v);
}

static float loop_regrab_seconds(float t) { return t <= 0.001f ? 0 : 8.0f * powf(0.03f, t); }
static float wave_scan_hz(float t) { return t <= 0.001f ? 0 : 0.05f * powf(600.0f, t); }
static float grain_density(float t) { return 1.0f + t * 15.0f; }
static float tone_cutoff(float tone) /* < 0: low-pass from 20 kHz down to 150 Hz; > 0: high-pass up to 3 kHz */
{
    return tone < 0 ? 20000.0f * powf(150.0f / 20000.0f, -tone) : 20.0f * powf(3000.0f / 20.0f, tone);
}

const char *note_name(int n, char *buf, int len)
{
    snprintf(buf, len, "%s%d", roots[((n % 12) + 12) % 12], n / 12 - 1);
    return buf;
}

void param_format_ctx(int id, const float *params, char *buf, int n)
{
    const ParamInfo *p = &param_info[id];
    float v = params[id];
    int o = osc_of(id);
    if (o >= 0 && osc_field(id) == P_A_TEXTURE) {
        int mode = (int)params[P_A_MODE + o * OSC_STRIDE];
        if (mode == OSC_LIVE) { snprintf(buf, n, v < 0.01f ? "off" : "1/%d", 1 + (int)(v * v * 31)); return; }
        if (mode == OSC_LOOP) { float s = loop_regrab_seconds(v); if (s == 0) snprintf(buf, n, "hold"); else snprintf(buf, n, "every %.2f s", s); return; }
        if (mode == OSC_WAVE) { float h = wave_scan_hz(v); if (h == 0) snprintf(buf, n, "hold"); else snprintf(buf, n, "%.2f Hz", h); return; }
        snprintf(buf, n, "%.1f grains", grain_density(v));
        return;
    }
    if (p->choices) {
        snprintf(buf, n, "%s", p->choices[(int)(v + 0.5f)]);
    } else if (!strcmp(p->unit, "station")) {
        snprintf(buf, n, v < 0 ? "-- off --" : "#%d", (int)v + 1);
    } else if (!strcmp(p->unit, "int")) {
        snprintf(buf, n, "%.0f", v);
    } else if (!strcmp(p->unit, "tone")) {
        if (fabsf(v) < 0.02f) snprintf(buf, n, "open");
        else {
            float hz = tone_cutoff(v);
            if (hz >= 1000) snprintf(buf, n, "%s %.1f kHz", v < 0 ? "dark" : "thin", hz / 1000);
            else snprintf(buf, n, "%s %.0f Hz", v < 0 ? "dark" : "thin", hz);
        }
    } else if (!strcmp(p->unit, "morph")) {
        if (v < 0.01f) snprintf(buf, n, "off");
        else snprintf(buf, n, "%.0f%% A / %.0f%% B", (1 - v) * 100, v * 100);
    } else if (!strcmp(p->unit, "pan")) {
        if (fabsf(v) < 0.01f) snprintf(buf, n, "centre");
        else snprintf(buf, n, "%s %.0f%%", v < 0 ? "left" : "right", fabsf(v) * 100);
    } else if (!strcmp(p->unit, "ms")) {
        snprintf(buf, n, v < 10 ? "%.1f ms" : "%.0f ms", v);
    } else if (!strcmp(p->unit, "limit")) { /* limiter threshold, 0 .. -18 dB */
        if (v < 0.01f) snprintf(buf, n, "off");
        else snprintf(buf, n, "%.1f dB", -18.0f * v);
    } else if (!strcmp(p->unit, "lvl")) { /* a fader: gain shown in dB */
        if (v < 0.001f) snprintf(buf, n, "off");
        else snprintf(buf, n, "%+.1f dB", 20.0f * log10f(v));
    } else if (!strcmp(p->unit, "dB")) {
        snprintf(buf, n, "%+.0f dB", v);
    } else if (!strcmp(p->unit, "st")) {
        snprintf(buf, n, "%+.0f st", v);
    } else if (!strcmp(p->unit, "bpm")) {
        snprintf(buf, n, "%.0f BPM", v);
    } else {
        snprintf(buf, n, "%.0f%%", v * 100);
    }
}

/* ================================================================== engine */

#define CAP_N (1 << 18)     /* live capture per voice (frames): 5.4 s @ 48 kHz */
#define SNAP_N (1 << 17)    /* grabbed loop / wavetable (frames): 2.73 s @ 48 kHz, so Size tops out at 2700 ms */
#define DLY_N (1 << 17)
#define STUT_N (1 << 17)    /* stutter recording per voice (frames): 2.7 s @ 48 kHz, longer than any step */
#define REV_N 8192
#define REV_LINES 5
#define AP_N 1024
#define MAX_GRAINS 24
#define WIN_N 1024
#define VAD_HIST 50
#define TAP_LEN 2048
#define BLOCK 16
#define GRAIN_SPREAD_S 0.6f
#define BANDS 34            /* most vocoder bands */

typedef struct { float ic1, ic2; } Svf;
typedef struct { float a1, a2, a3; } SvfCoef;

typedef struct {
    double pos, rate;
    int len, age;
    float gl, gr;
} Grain;

/* voice sense: speech vs music from low-energy-frame ratio and zero-crossing spread */
typedef struct {
    int flen, fcount, zc;
    float acc, prev;
    float e[VAD_HIST], z[VAD_HIST];
    int hidx, filled;
    float speech;
    int state;
} Vad;

/* stutter: a sound is recorded; on a step the stutter rhythm marks, its first slice repeats (ratchets) */
typedef struct {
    float *buf;
    unsigned w, mark;
    int n, rev, idx, mode;
    float seg, t, flash;
    /* the ratchet now playing: where it reads from, how fast, which way, and whether it is dropped */
    int k, k_mute, k_rev, pitch_dir;
    float k_off, k_rate;
    /* SCATTER: a latched sample */
    float *latch;
    int latch_len, latched;
} Stut;

typedef struct {
    float *cap;           /* stereo ring of the (focus-filtered) live stream */
    unsigned cap_w;
    float *snap[2];       /* double-buffered grabs (stereo) */
    int snap_len[2];
    int cur;
    float morph, morph_step;
    double phase;
    int bounce;           /* DIR_BOTH: playing the loop backwards this time round */
    Stut st;              /* stutter of this voice's sound */
    float timer;
    int want_grab, last_mode;
    int snap_silent;      /* the last grab caught silence (stream not started yet): try again soon */
    Grain grains[MAX_GRAINS];
    float grain_timer;
    float hold[2], dec_ph;
    float out[2];
    float focus_gain;
    Vad vad;
    double osc_ph;        /* secondary oscillator */
    float talk_hp, talk_lp, talk_env, talk_peak; /* converse: how much this voice is talking right now */
    float duck;           /* converse gain */
    /* rhythm */
    long step_abs;
    int cell_idx[NCELL], cell_hit[NCELL];
    float cell_flash[NCELL];
    float gate;
    float level;          /* display follower */
} Osc;

struct Synth {
    float fs;
    float p[P_COUNT];
    Osc osc[2];
    unsigned rng;

    int notes[8], n_notes, last_note;
    float pitch;

    double beat_pos, clock_rate;
    int running;

    Svf filt[2];
    /* cross-modulation filterbank (vocode and morph) */
    SvfCoef bank_c[BANDS];
    Svf bank_a[BANDS], bank_b[BANDS];
    float env_a[BANDS], env_b[BANDS];
    int nbands;
    /* shaping state: bass / treble split, crush hold */
    float lo_lp[2], hi_lp[2], crush_hold[2], crush_ph;
    float *dly;
    unsigned dly_w;
    float dly_time_s, dly_lp[2];
    float *rev[REV_LINES];
    unsigned rev_w;
    float rev_lp[REV_LINES];
    float *ap[4];
    unsigned ap_w;
    double rev_mod;

    float lev_env, lev_gain;   /* output leveller */
    int floor_holder;          /* converse: voice with the floor */
    float floor_quiet;         /* seconds the holder has been quiet */
    float s_lvl[2], s_vol;
    float s_fx[4];             /* smoothed effect return levels: cross, shape, echo, space */
    float meter[MIX_COUNT], meter_fall;
    float lim_env, lim_rel;    /* master limiter: peak envelope and its release coefficient */
    /* the cross rhythm */
    long x_step_abs;
    int x_idx, x_hit;
    float x_flash, x_gate;
    Stut xst;             /* the cross stutter */
    float win[WIN_N + 1];
    float tap[TAP_COUNT][TAP_LEN];
    unsigned tap_w;
};

static inline float fast_tanh(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float db2lin(float db) { return powf(10.0f, db / 20.0f); }

static float rnd01(Synth *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) / 16777216.0f;
}

/* vocoder / morph filterbank: n bands from 120 Hz to 7 kHz, narrower as there are more of them */
static void bank_setup(Synth *s, int n)
{
    float q = 0.45f * n;
    for (int b = 0; b < n; b++) {
        float fc = 120.0f * powf(7000.0f / 120.0f, (float)b / (n - 1));
        if (fc > s->fs * 0.45f) fc = s->fs * 0.45f;
        float g = tanf((float)M_PI * fc / s->fs), k = 1.0f / q;
        s->bank_c[b].a1 = 1.0f / (1.0f + g * (g + k));
        s->bank_c[b].a2 = g * s->bank_c[b].a1;
        s->bank_c[b].a3 = g * s->bank_c[b].a2;
    }
    memset(s->bank_a, 0, sizeof s->bank_a);
    memset(s->bank_b, 0, sizeof s->bank_b);
    s->nbands = n;
}

Synth *synth_create(int sample_rate)
{
    Synth *s = calloc(1, sizeof(Synth));
    if (!s) return NULL;
    s->fs = (float)sample_rate;
    s->dly = calloc(DLY_N * 2, sizeof(float));
    int ok = s->dly != NULL;
    for (int i = 0; i < REV_LINES; i++) ok = ok && (s->rev[i] = calloc(REV_N, sizeof(float)));
    for (int i = 0; i < 4; i++) ok = ok && (s->ap[i] = calloc(AP_N, sizeof(float)));
    for (int o = 0; o < 2; o++) {
        Osc *os = &s->osc[o];
        os->cap = calloc(CAP_N * 2, sizeof(float));
        os->snap[0] = calloc(SNAP_N * 2, sizeof(float));
        os->snap[1] = calloc(SNAP_N * 2, sizeof(float));
        os->st.buf = calloc(STUT_N * 2, sizeof(float));
        os->st.latch = calloc(STUT_N, sizeof(float)); /* STUT_N / 2 stereo frames */
        os->last_mode = -1;
        os->focus_gain = 1;
        os->gate = 1;
        os->duck = 1;
        os->step_abs = -1;
        os->vad.flen = sample_rate / 50; /* 20 ms frames */
        os->vad.speech = 0.5f;
        ok = ok && os->cap && os->snap[0] && os->snap[1] && os->st.buf && os->st.latch;
    }
    ok = ok && (s->xst.buf = calloc(STUT_N * 2, sizeof(float))) && (s->xst.latch = calloc(STUT_N, sizeof(float)));
    if (!ok) { synth_destroy(s); return NULL; }
    for (int i = 0; i < P_COUNT; i++) s->p[i] = param_info[i].def;
    for (int i = 0; i <= WIN_N; i++) { float w = sinf((float)M_PI * i / WIN_N); s->win[i] = w * w; }
    s->rng = 0x5eed5u;
    s->last_note = 60;
    s->pitch = 60;
    s->clock_rate = 1.0;
    s->running = 1;
    s->lev_gain = 1.0f;
    s->lev_env = 0.04f;
    s->s_vol = 1.0f;
    for (int i = 0; i < 4; i++) s->s_fx[i] = 1.0f;
    s->meter_fall = expf(-1.0f / (0.3f * s->fs));
    s->lim_rel = expf(-1.0f / (0.08f * s->fs));
    s->x_step_abs = -1;
    s->x_gate = 1;
    s->dly_time_s = 0.3f * s->fs;
    bank_setup(s, 13);
    return s;
}

void synth_destroy(Synth *s)
{
    if (!s) return;
    for (int o = 0; o < 2; o++) {
        free(s->osc[o].cap);
        free(s->osc[o].snap[0]);
        free(s->osc[o].snap[1]);
        free(s->osc[o].st.buf);
        free(s->osc[o].st.latch);
    }
    for (int i = 0; i < REV_LINES; i++) free(s->rev[i]);
    for (int i = 0; i < 4; i++) free(s->ap[i]);
    free(s->dly);
    free(s->xst.buf);
    free(s->xst.latch);
    free(s);
}

void synth_set_param(Synth *s, int id, float v) { s->p[id] = param_clamp(id, v); }
float synth_get_param(const Synth *s, int id) { return s->p[id]; }
const float *synth_params(const Synth *s) { return s->p; }
int synth_current_note(const Synth *s) { return s->last_note; }
int synth_cell_step(const Synth *s, int o, int c) { return s->osc[o].cell_idx[c]; }
float synth_cell_flash(const Synth *s, int o, int c) { return s->osc[o].cell_flash[c]; }
float synth_gate_value(const Synth *s, int o) { return s->osc[o].gate; }
float synth_speechness(const Synth *s, int o) { return s->osc[o].vad.speech; }
int synth_voice_state(const Synth *s, int o) { return s->osc[o].vad.state; }
float synth_osc_level(const Synth *s, int o) { return s->osc[o].level; }
float synth_leveller_gain(const Synth *s) { return s->lev_gain; }
int synth_seq_step(const Synth *s, int q)
{
    if (q == SEQ_SX) return s->xst.idx;
    return q == SEQ_X ? s->x_idx : q >= SEQ_SA ? s->osc[q - SEQ_SA].st.idx : s->osc[q].cell_idx[CELL_GATE];
}
float synth_seq_flash(const Synth *s, int q)
{
    if (q == SEQ_SX) return s->xst.flash;
    return q == SEQ_X ? s->x_flash : q >= SEQ_SA ? s->osc[q - SEQ_SA].st.flash : s->osc[q].cell_flash[CELL_GATE];
}
int synth_stutter_active(const Synth *s, int v) { return v == SEQ_X ? s->xst.n : s->osc[v].st.n; }
float synth_cross_gate(const Synth *s) { return s->x_gate; }
float synth_mix_meter(const Synth *s, int ch) { return ch >= 0 && ch < MIX_COUNT ? s->meter[ch] : 0; }
int synth_floor(const Synth *s) { return s->p[P_CONVERSE] > 0.01f ? s->floor_holder : -1; }

void synth_tap_read(const Synth *s, int tap, float *dst, int n)
{
    if (n > TAP_LEN) n = TAP_LEN;
    unsigned start = s->tap_w - (unsigned)n;
    for (int i = 0; i < n; i++) dst[i] = s->tap[tap][(start + i) & (TAP_LEN - 1)];
}

void synth_note_on(Synth *s, int note)
{
    for (int i = 0; i < s->n_notes; i++)
        if (s->notes[i] == note) return;
    if (s->n_notes < 8) s->notes[s->n_notes++] = note;
    s->last_note = note;
}

void synth_note_off(Synth *s, int note)
{
    int j = 0;
    for (int i = 0; i < s->n_notes; i++)
        if (s->notes[i] != note) s->notes[j++] = s->notes[i];
    s->n_notes = j;
    if (j > 0) s->last_note = s->notes[j - 1]; /* the pitch stays on the last note when keys are released */
}

void synth_grab(Synth *s, int osc)
{
    for (int o = 0; o < 2; o++)
        if (osc < 0 || osc == o) { s->osc[o].want_grab = 1; s->osc[o].st.latched = 0; } /* SCATTER latches anew */
    if (osc < 0) s->xst.latched = 0;
}

void synth_tap_reset(Synth *s)
{
    s->beat_pos = 0;
    for (int o = 0; o < 2; o++) s->osc[o].step_abs = -1;
    s->x_step_abs = -1;
}

float synth_beat_phase(const Synth *s) { return (float)(s->beat_pos - floor(s->beat_pos)); }
double synth_position(const Synth *s) { return s->beat_pos * 4.0; }
void synth_set_position(Synth *s, double steps) { s->beat_pos = steps < 0 ? 0 : steps / 4.0; }
void synth_set_clock_rate(Synth *s, double r) { s->clock_rate = r < 0.9 ? 0.9 : r > 1.1 ? 1.1 : r; }
void synth_set_running(Synth *s, int on) { s->running = on; }
int synth_running(const Synth *s) { return s->running; }

/* ------------------------------------------------------------------ dice */

static float rand_in(Synth *s, float lo, float hi, int lg)
{
    float r = rnd01(s);
    if (lg && lo > 0) return lo * powf(hi / lo, r);
    return lo + (hi - lo) * r;
}

static void dice_range(int id, float *lo, float *hi)
{
    const ParamInfo *p = &param_info[id];
    *lo = p->min;
    *hi = p->max;
    int o = osc_of(id);
    int f = o >= 0 ? osc_field(id) : id;
    switch (f) {
    case P_A_TUNE: *lo = -12; *hi = 12; break;
    case P_A_SIZE: *lo = 2; *hi = 600; break;
    case P_A_LEVEL: *lo = 0.6f; *hi = 1; break;
    case P_A_PAN: *lo = -0.7f; *hi = 0.7f; break;
    case P_A_GATE_STEPS: case P_X_GATE_STEPS: case P_A_STUT_STEPS: case P_X_STUT_STEPS: *lo = 3; *hi = 16; break;
    case P_A_STUT_RATCHET: case P_X_STUT_RATCHET: *lo = 2; *hi = 4; break;
    case P_A_STUT_CHANCE: case P_X_STUT_CHANCE: *lo = 0.4f; *hi = 1; break;
    case P_A_ATTACK: case P_X_ATTACK: *lo = 0; *hi = 0.5f; break;
    case P_A_RELEASE: case P_X_RELEASE: *lo = 0; *hi = 0.6f; break;
    case P_A_RATE: case P_X_RATE: *lo = 1; *hi = 11; break;
    case P_A_LENGTH: case P_X_LENGTH: *lo = 0.2f; *hi = 1; break;
    case P_A_CHANCE: case P_X_CHANCE: *lo = 0.5f; *hi = 1; break;
    case P_TONE: *lo = -0.7f; *hi = 0.5f; break;
    case P_RESO: *lo = 0; *hi = 0.7f; break;
    case P_FOLD: *lo = 0; *hi = 0.6f; break;
    case P_DRIVE: *lo = 0; *hi = 0.5f; break;
    case P_CRUSH: *lo = 0; *hi = 0.4f; break;
    case P_LOW: case P_HIGH: *lo = -6; *hi = 6; break;
    case P_WIDTH: *lo = 0.6f; *hi = 1.6f; break;
    case P_ECHO: *lo = 0; *hi = 0.5f; break;
    case P_SPACE: *lo = 0; *hi = 0.7f; break;
    case P_A_OSC_MIX: *lo = 0; *hi = 0.4f; break;
    case P_VOCODE_AMT: *lo = 0.4f; *hi = 1; break;
    case P_FM: *lo = 0; *hi = 0.35f; break;
    case P_RING: *lo = 0; *hi = 0.5f; break;
    case P_CONVERSE: *lo = 0.5f; *hi = 1; break;
    }
}

static int dice_excluded(int id)
{
    int o = osc_of(id);
    int f = o >= 0 ? osc_field(id) : id;
    return f == P_A_STATION || f == P_A_FOCUS || f == P_A_DRIVE || id == P_VOLUME || id == P_BPM || id == P_SYNC || id == P_ROOT ||
           (id >= P_CROSS_LEVEL && id <= P_LIMIT); /* the effect returns stay where the player set them */
}

void synth_randomize(Synth *s, const int *ids, int n, float amount)
{
    for (int i = 0; i < n; i++) {
        int id = ids[i];
        if (dice_excluded(id)) continue;
        const ParamInfo *p = &param_info[id];
        float lo, hi;
        dice_range(id, &lo, &hi);
        float v = s->p[id];
        if (p->choices || !strcmp(p->unit, "int")) {
            if (amount < 0.5f) continue; /* gentle mutation leaves switches alone */
            v = floorf(rand_in(s, lo, hi + 0.999f, 0));
        } else if (amount >= 0.99f) {
            v = rand_in(s, lo, hi, p->log);
        } else {
            if (p->log && v > 0) v *= powf(hi / lo, (rnd01(s) * 2 - 1) * amount * 0.5f);
            else v += (hi - lo) * (rnd01(s) * 2 - 1) * amount * 0.5f;
            if (v < lo) v = lo;
            if (v > hi) v = hi;
        }
        s->p[id] = param_clamp(id, v);
    }
    for (int q = 0; q < SEQ_COUNT; q++) { /* keep pulses <= steps and rotation inside the pattern */
        float *st = &s->p[SEQ_PARAM(q, 1)];
        if (st[1] > st[0]) st[1] = floorf(1 + rnd01(s) * (st[0] - 1));
        if (st[2] >= st[0]) st[2] = floorf(rnd01(s) * st[0]);
    }
}

void synth_dice(Synth *s, int which)
{
    int ids[P_COUNT], n = 0;
    for (int i = 0; i < P_COUNT; i++) {
        int pg = param_info[i].page;
        int o = osc_of(i);
        int rhythm = (o >= 0 && osc_field(i) >= P_A_GATE_ON) || (i > P_X_GATE_ON && i <= P_X_STUT_CHANCE && i != P_X_STUT_ON); /* cross on/offs stay */
        if (which == DICE_SOUND && (pg == PAGE_VOICES || pg == PAGE_ENGINE || pg == PAGE_MIX)) ids[n++] = i;
        if (which == DICE_RHYTHM && rhythm) ids[n++] = i;
        if (which == DICE_MUTATE && !param_info[i].choices) ids[n++] = i;
    }
    synth_randomize(s, ids, n, which == DICE_MUTATE ? 0.12f : 1.0f);
    if (which == DICE_SOUND) {
        static const float lo[] = {50, 80, 1, 15}, hi[] = {500, 1500, 25, 350};
        static const float tunes[] = {-12, -7, -5, 0, 0, 0, 5, 7, 12};
        for (int o = 0; o < 2; o++) {
            int m = (int)s->p[P_A_MODE + o * OSC_STRIDE];
            s->p[P_A_SIZE + o * OSC_STRIDE] = rand_in(s, lo[m], hi[m], 1);
            s->p[P_A_TUNE + o * OSC_STRIDE] = tunes[(int)(rnd01(s) * 9) % 9];
            if (m == OSC_LIVE && rnd01(s) < 0.7f) s->p[P_A_TEXTURE + o * OSC_STRIDE] = 0;
            if (rnd01(s) < 0.7f) s->p[P_A_DIR + o * OSC_STRIDE] = DIR_FORWARD; /* reversing stays occasional */
        }
        if (rnd01(s) < 0.4f) s->p[P_TONE] = 0;
        if (rnd01(s) < 0.5f) s->p[P_FOLD] = 0;
        for (int o = 0; o < 2; o++)
            if (rnd01(s) < 0.65f) s->p[P_A_OSC + o * OSC_STRIDE] = 0; /* oscillators stay occasional */
        if (rnd01(s) < 0.6f) s->p[P_VOCODE] = VOC_OFF;
        if (rnd01(s) < 0.6f) s->p[P_FM] = 0;
        if (rnd01(s) < 0.6f) s->p[P_RING] = 0;
        if (rnd01(s) < 0.6f) s->p[P_MORPH] = 0;
        if (rnd01(s) < 0.7f) s->p[P_CONVERSE] = 0;
    }
    if (which == DICE_RHYTHM)
        for (int o = 0; o < 2; o++) {
            s->p[CELL_PARAM(o, CELL_GATE, 0)] = rnd01(s) < 0.85f ? 1 : 0;
            s->p[STUT_PARAM(o, 0)] = rnd01(s) < 0.4f ? 1 : 0;   /* stutters stay occasional */
        }
    if (which == DICE_RHYTHM)
        for (int v = 0; v < 3; v++) { /* a few ratcheted steps, not most of them */
            int vv = v == 2 ? SEQ_X : v;
            if (s->p[STUT_PARAM(vv, 2)] > 3) s->p[STUT_PARAM(vv, 2)] = 1 + floorf(rnd01(s) * 3);
        }
    if (which == DICE_SOUND) {
        if (rnd01(s) < 0.6f) s->p[P_DRIVE] = 0;
        if (rnd01(s) < 0.7f) s->p[P_CRUSH] = 0;
    }
}

/* ------------------------------------------------------------------ voice engines */

static inline float cap_get(const Osc *o, long back, int c)
{
    unsigned i = (o->cap_w - 1u - (unsigned)back) & (CAP_N - 1);
    return o->cap[i * 2 + c];
}

/* Grab the latest `len` frames, bake a crossfade into the seam so it loops cleanly, optionally normalise. */
static void osc_grab(Osc *o, int len, int normalise)
{
    if (len < 8) len = 8;
    if (len > SNAP_N) len = SNAP_N;
    if (o->morph > 0) { o->cur = !o->cur; o->morph = 0; }
    int dst = !o->cur;
    float *sn = o->snap[dst];
    int xf = len / 4;
    if (xf > 2048) xf = 2048;
    double sum[2] = {0, 0};
    float loudest = 0;
    for (int i = 0; i < len; i++) {
        long b0 = (long)len - 1 - i;
        for (int c = 0; c < 2; c++) {
            float v = cap_get(o, b0, c);
            if (i >= len - xf) {
                float t = (float)(i - (len - xf)) / xf;
                v = (1 - t) * v + t * cap_get(o, b0 + len, c);
            }
            sn[i * 2 + c] = v;
            sum[c] += v;
            if (fabsf(v) > loudest) loudest = fabsf(v);
        }
    }
    o->snap_silent = loudest < 0.002f;
    if (normalise) {
        float peak = 0;
        for (int c = 0; c < 2; c++) {
            float mean = (float)(sum[c] / len);
            for (int i = 0; i < len; i++) {
                sn[i * 2 + c] -= mean;
                float a = fabsf(sn[i * 2 + c]);
                if (a > peak) peak = a;
            }
        }
        float g = peak > 1e-4f ? 0.7f / peak : 0;
        if (g > 12) g = 12;
        for (int i = 0; i < len * 2; i++) sn[i] *= g;
    }
    o->snap_len[dst] = len;
    o->morph = 0.000001f;
}

static inline float snap_read(const Osc *o, int which, double phase, int c)
{
    int len = o->snap_len[which];
    if (len <= 0) return 0;
    double x = phase * len;
    int i0 = (int)x;
    float fr = (float)(x - i0);
    if (i0 >= len) i0 -= len;
    int i1 = i0 + 1 == len ? 0 : i0 + 1;
    const float *sn = o->snap[which];
    return sn[i0 * 2 + c] + fr * (sn[i1 * 2 + c] - sn[i0 * 2 + c]);
}

static void osc_control(Synth *s, int idx, int n)
{
    Osc *o = &s->osc[idx];
    const float *p = s->p + idx * OSC_STRIDE;
    int mode = (int)p[P_A_MODE];
    float tex = p[P_A_TEXTURE];
    if (mode != o->last_mode) { o->want_grab = 1; o->last_mode = mode; }
    if (mode == OSC_LOOP || mode == OSC_WAVE) {
        float interval = mode == OSC_LOOP ? loop_regrab_seconds(tex) : (wave_scan_hz(tex) > 0 ? 1.0f / wave_scan_hz(tex) : 0);
        if (interval > 0 || o->snap_silent) {
            o->timer -= n / s->fs;
            if (o->timer <= 0) o->want_grab = 1;
        }
        if (o->want_grab) {
            osc_grab(o, (int)(p[P_A_SIZE] * 0.001f * s->fs), mode == OSC_WAVE);
            o->want_grab = 0;
            o->timer = interval > 0 ? interval : 1e9f;
            if (o->snap_silent && o->timer > 0.5f) o->timer = 0.5f; /* caught silence: look again shortly */
            float mt = mode == OSC_LOOP ? 0.015f : (interval > 0 ? interval : 0.02f);
            if (mt < 0.005f) mt = 0.005f;
            o->morph_step = 1.0f / (mt * s->fs);
        }
    } else {
        o->want_grab = 0;
    }
}

/* polyBLEP correction for the oscillator's saw and square */
static inline float blep(double t, double dt)
{
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0;
}

static void osc_tick(Synth *s, int idx, float ratio, float freq)
{
    Osc *o = &s->osc[idx];
    const float *p = s->p + idx * OSC_STRIDE;
    switch ((int)p[P_A_MODE]) {
    case OSC_LIVE: {
        float l = cap_get(o, 0, 0) * o->focus_gain, r = cap_get(o, 0, 1) * o->focus_gain;
        float tex = p[P_A_TEXTURE];
        if (tex > 0.01f) { /* sample-rate crush */
            o->dec_ph += 1.0f;
            if (o->dec_ph >= 1.0f + tex * tex * 31.0f) { o->dec_ph = 0; o->hold[0] = l; o->hold[1] = r; }
            l = o->hold[0];
            r = o->hold[1];
        }
        o->out[0] = l;
        o->out[1] = r;
        break;
    }
    case OSC_LOOP:
    case OSC_WAVE: {
        int a = o->cur, b = !o->cur;
        int len = o->snap_len[a] > 0 ? o->snap_len[a] : o->snap_len[b];
        if (len <= 0) { o->out[0] = o->out[1] = 0; break; }
        int dir = (int)p[P_A_DIR];
        double rp = dir == DIR_REVERSE || (dir == DIR_BOTH && o->bounce) ? 1.0 - o->phase : o->phase;
        if (rp >= 1.0) rp = 0.999999;
        for (int c = 0; c < 2; c++) {
            float v = snap_read(o, a, rp, c);
            if (o->morph > 0) v += o->morph * (snap_read(o, b, rp, c) - v);
            o->out[c] = v;
        }
        double inc = (int)p[P_A_MODE] == OSC_LOOP ? ratio / (double)len : freq / s->fs;
        if (inc > 0.5) inc = 0.5;
        o->phase += inc;
        if (o->phase >= 1.0) { o->phase -= floor(o->phase); o->bounce = !o->bounce; }
        if (o->morph > 0) {
            o->morph += o->morph_step;
            if (o->morph >= 1.0f) { o->morph = 0; o->cur = b; }
        }
        break;
    }
    case OSC_GRAIN: {
        int glen = (int)(p[P_A_SIZE] * 0.001f * s->fs);
        if (glen < 32) glen = 32;
        if (glen > CAP_N / 8) glen = CAP_N / 8;
        float dens = grain_density(p[P_A_TEXTURE]);
        o->grain_timer -= 1.0f;
        if (o->grain_timer <= 0) {
            o->grain_timer += glen / dens;
            for (int g = 0; g < MAX_GRAINS; g++) {
                Grain *gr = &o->grains[g];
                if (gr->len) continue;
                int gdir = (int)p[P_A_DIR];
                int rev = gdir == DIR_REVERSE || (gdir == DIR_BOTH && rnd01(s) < 0.5f);
                double back = rnd01(s) * GRAIN_SPREAD_S * s->fs;
                if (rev) { /* start just behind the newest audio and read backwards, away from it */
                    gr->pos = (double)o->cap_w - 4 - back;
                    gr->rate = -ratio;
                } else {
                    double lead = glen * (ratio > 1 ? ratio : 1) + 4;
                    gr->pos = (double)o->cap_w - lead - back;
                    gr->rate = ratio;
                }
                gr->len = glen;
                gr->age = 0;
                float pan = (rnd01(s) - 0.5f) * 0.6f;
                gr->gl = 0.75f - pan * 0.5f;
                gr->gr = 0.75f + pan * 0.5f;
                break;
            }
        }
        float l = 0, r = 0;
        for (int g = 0; g < MAX_GRAINS; g++) {
            Grain *gr = &o->grains[g];
            if (!gr->len) continue;
            float w = s->win[gr->age * WIN_N / gr->len];
            double ip = floor(gr->pos);
            float fr = (float)(gr->pos - ip);
            unsigned i0 = (unsigned)(long long)ip & (CAP_N - 1), i1 = (i0 + 1) & (CAP_N - 1);
            float m0 = o->cap[i0 * 2] + o->cap[i0 * 2 + 1];
            float m1 = o->cap[i1 * 2] + o->cap[i1 * 2 + 1];
            float v = (m0 + fr * (m1 - m0)) * w;
            l += v * gr->gl;
            r += v * gr->gr;
            gr->pos += gr->rate;
            if (++gr->age >= gr->len) gr->len = 0;
        }
        float norm = 0.8f / sqrtf(dens);
        o->out[0] = l * norm;
        o->out[1] = r * norm;
        break;
    }
    }

    /* secondary oscillator at the voice's pitch: a clean tone or a carrier for ring / vocode / FM */
    int wave = (int)p[P_A_OSC];
    float mix = p[P_A_OSC_MIX];
    if (wave && mix > 0.001f) {
        double dt = freq / s->fs;
        if (dt > 0.45) dt = 0.45;
        double t = o->osc_ph;
        float v;
        switch (wave) {
        case 1: v = sinf(2.0f * (float)M_PI * (float)t); break;
        case 2: v = t < 0.5 ? (float)(4 * t - 1) : (float)(3 - 4 * t); break;
        case 3: v = (float)(2 * t - 1) - blep(t, dt); break;
        default: {
            double t2 = t + 0.5 >= 1 ? t - 0.5 : t + 0.5;
            v = (t < 0.5 ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
        }
        }
        o->osc_ph += dt;
        if (o->osc_ph >= 1.0) o->osc_ph -= 1.0;
        v *= 0.5f;
        o->out[0] += mix * (v - o->out[0]);
        o->out[1] += mix * (v - o->out[1]);
    }
}

/* ------------------------------------------------------------------ voice sense */

static void vad_tick(Vad *v, float m)
{
    v->acc += m * m;
    if ((m >= 0) != (v->prev >= 0)) v->zc++;
    v->prev = m;
    if (++v->fcount < v->flen) return;
    v->e[v->hidx] = sqrtf(v->acc / v->flen);
    v->z[v->hidx] = (float)v->zc / v->flen;
    v->hidx = (v->hidx + 1) % VAD_HIST;
    if (v->filled < VAD_HIST) v->filled++;
    v->fcount = 0;
    v->acc = 0;
    v->zc = 0;
    if (v->filled < VAD_HIST) return;
    float em = 0, zm = 0;
    for (int i = 0; i < VAD_HIST; i++) { em += v->e[i]; zm += v->z[i]; }
    em /= VAD_HIST;
    zm /= VAD_HIST;
    if (em < 0.004f) { v->state = VS_SILENT; return; }
    int low = 0, hz = 0;
    for (int i = 0; i < VAD_HIST; i++) {
        if (v->e[i] < 0.5f * em) low++;
        if (v->z[i] > 1.5f * zm) hz++;
    }
    /* speech has many quiet gaps between syllables and bursts of fricatives; music is denser */
    float a = ((float)low / VAD_HIST - 0.12f) / 0.30f, b = ((float)hz / VAD_HIST - 0.04f) / 0.20f;
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    b = b < 0 ? 0 : b > 1 ? 1 : b;
    v->speech += (0.6f * a + 0.4f * b - v->speech) * 0.03f;
    if (v->speech > 0.55f) v->state = VS_VOICE;
    else if (v->speech < 0.42f) v->state = VS_MUSIC;
    else if (v->state == VS_SILENT) v->state = VS_MUSIC;
}

/* ------------------------------------------------------------------ rhythm */

/* A Euclidean rhythm's gate at this beat: attack, hold for Length of the step, release. Sets *new_hit on the
 * first sample of a step that has a pulse. Rhythm off = always open. */
static float euclid_gate(Synth *s, int q, double beat, long *step_abs, int *idx, int *hit, float *flash, int *new_hit)
{
    const float *p = s->p;
    float step_beats = rate_beats[(int)p[SEQ_SHAPE(q, RATE)]];
    float step_s = step_beats * 60.0f / p[P_BPM];
    double sp = beat / step_beats;
    long step = (long)floor(sp);
    int on = (int)p[SEQ_PARAM(q, 0)];
    *new_hit = 0;
    if (step != *step_abs) {
        *step_abs = step;
        int steps = (int)p[SEQ_PARAM(q, 1)];
        *idx = (int)(step % steps);
        *hit = on && euclid_hit(steps, (int)p[SEQ_PARAM(q, 2)], (int)p[SEQ_PARAM(q, 3)], *idx) &&
               rnd01(s) < p[SEQ_SHAPE(q, CHANCE)];
        if (*hit) { *flash = 1; *new_hit = 1; }
    }
    if (!on) return 1.0f;
    if (!s->running || !*hit) return 0.0f; /* stopped: a rhythm that is on stays closed */
    float t = (float)(sp - step), len = p[SEQ_SHAPE(q, LENGTH)];
    float att = p[SEQ_SHAPE(q, ATTACK)] * len, min_att = 0.004f / step_s; /* never faster than 4 ms */
    if (att < min_att) att = min_att;
    if (att > len) att = len;
    float rel = p[SEQ_SHAPE(q, RELEASE)] * (1.0f - len) + min_att;
    float g;
    if (t < att) g = t / att;
    else if (t < len) g = 1;
    else if (t < len + rel) g = 1 - (t - len) / rel;
    else g = 0;
    return g * g * (3 - 2 * g);
}

/* A new step for a stutter rhythm (v: voice 0, 1 or SEQ_X): does this step ratchet? */
static void stut_step(Synth *s, Stut *st, int v, long step_abs, int rate)
{
    const float *p = s->p;
    int steps = (int)p[STUT_FIELD(v, STEPS)];
    st->idx = (int)(step_abs % steps);
    st->n = 0;
    if (!(int)p[STUT_FIELD(v, ON)] || !s->running) return;
    if (!euclid_hit(steps, (int)p[STUT_FIELD(v, PULSES)], (int)p[STUT_FIELD(v, ROT)], st->idx)) return;
    if (rnd01(s) >= p[STUT_FIELD(v, CHANCE)]) return;
    int n = (int)p[STUT_FIELD(v, RATCHET)], mode = (int)p[STUT_FIELD(v, MODE)];
    if (mode == STUT_RANDOM) n = 2 + (int)(rnd01(s) * (n - 1) + 0.5f);
    float step_s = rate_beats[rate] * 60.0f / p[P_BPM];
    st->n = n;
    st->mode = mode;
    st->rev = mode == STUT_REVERSE || (mode == STUT_RANDOM && rnd01(s) < 0.3f);
    st->pitch_dir = rnd01(s) < 0.7f ? 1 : -1; /* PITCH: mostly rising */
    st->seg = step_s * s->fs / n;
    if (st->seg > STUT_N / 2) st->seg = STUT_N / 2;
    if (st->seg < 32) st->seg = 32;
    st->mark = st->w;
    st->t = 0;
    st->k = -1;
    st->flash = 1;
    if (mode != STUT_SCATTER) st->latched = 0;
    else if (!st->latched) { /* latch the last step's sound, up to ~1.4 s */
        int len = (int)(step_s * s->fs);
        if (len > STUT_N / 2) len = STUT_N / 2;
        if (len < 64) len = 64;
        for (int i = 0; i < len; i++) {
            unsigned r = (st->w - (unsigned)len + (unsigned)i) & (STUT_N - 1);
            st->latch[i * 2] = st->buf[r * 2];
            st->latch[i * 2 + 1] = st->buf[r * 2 + 1];
        }
        st->latch_len = len;
        st->latched = 1;
    }
}

/* choose how ratchet k plays */
static void stut_ratchet(Synth *s, Stut *st, int k)
{
    st->k = k;
    st->k_off = 0;
    st->k_rate = 1;
    st->k_mute = 0;
    st->k_rev = st->rev && (k & 1);
    switch (st->mode) {
    case STUT_PITCH: /* climbs (or falls) an octave over the step */
        st->k_rate = exp2f(st->pitch_dir * (float)k / st->n);
        break;
    case STUT_DROPOUT: /* some ratchets fall silent, and now and then the whole first one */
        st->k_mute = k > 0 ? rnd01(s) < 0.45f : rnd01(s) < 0.15f;
        break;
    case STUT_SCATTER: { /* a fragment of the latched sample from anywhere, either way, at an octave */
        static const float rates[] = {0.5f, 1, 1, 1, 2};
        st->k_rate = rates[(int)(rnd01(s) * 5) % 5];
        st->k_rev = rnd01(s) < 0.35f;
        float room = st->latch_len - st->seg * st->k_rate;
        st->k_off = room > 0 ? rnd01(s) * room : 0;
        break;
    }
    }
}

/* record a sound, and on a stutter step replace it with ratchets of the step's first slice */
static inline void stutter_tick(Synth *s, Stut *st, float *x)
{
    unsigned w = st->w & (STUT_N - 1);
    st->buf[w * 2] = x[0];
    st->buf[w * 2 + 1] = x[1];
    st->w++;
    if (!st->n) return;
    if (!s->running) { st->n = 0; return; }
    float seg = st->seg, t = st->t;
    int k = (int)(t / seg);
    float pos = t - k * seg;
    if (k >= st->n) { st->n = 0; return; } /* the step ran long (tempo change): let go */
    if (k != st->k) stut_ratchet(s, st, k);
    float fade = 0.002f * s->fs, e = 1.0f; /* 2 ms edges on every ratchet */
    if (pos < fade && (k > 0 || st->mode == STUT_SCATTER)) e = pos / fade;
    if (seg - pos < fade) e = fminf(e, (seg - pos) / fade);
    if (st->k_mute) e = 0;
    if (st->mode == STUT_SCATTER && st->latched) { /* play from the latched sample */
        float rp = st->k_rev ? seg - 1 - pos : pos;
        float fp = st->k_off + rp * st->k_rate;
        int len = st->latch_len;
        int i0 = (int)fp % len, i1 = (i0 + 1) % len;
        float fr = fp - floorf(fp);
        for (int c = 0; c < 2; c++) x[c] = st->latch[i0 * 2 + c] + fr * (st->latch[i1 * 2 + c] - st->latch[i0 * 2 + c]);
    } else if (k > 0 || st->k_rate != 1.0f) { /* replay the step's first slice */
        float rp = st->k_rev ? seg - 1 - pos : pos;
        float fp = fmodf(rp * st->k_rate, seg); /* a faster ratchet loops inside its slice */
        if (k == 0 && fp >= pos) fp = pos;      /* the first ratchet can only play what has been heard */
        unsigned r0 = (st->mark + (unsigned)fp) & (STUT_N - 1), r1 = (r0 + 1) & (STUT_N - 1);
        float fr = fp - floorf(fp);
        for (int c = 0; c < 2; c++) x[c] = st->buf[r0 * 2 + c] + fr * (st->buf[r1 * 2 + c] - st->buf[r0 * 2 + c]);
    }
    x[0] *= e;
    x[1] *= e;
    st->t = t + 1;
}

/* Each voice's rhythm opens it on every pulse. A LOOP voice also restarts its slice on every pulse.
 * On a step its stutter rhythm marks, the voice opens for the whole step and its first slice ratchets. */
static float rhythm_eval(Synth *s, int idx, double beat)
{
    Osc *o = &s->osc[idx];
    long before = o->step_abs;
    int nh;
    float g = euclid_gate(s, idx, beat, &o->step_abs, &o->cell_idx[CELL_GATE], &o->cell_hit[CELL_GATE],
                          &o->cell_flash[CELL_GATE], &nh);
    if (o->step_abs != before) stut_step(s, &o->st, idx, o->step_abs, (int)s->p[P_A_RATE + idx * OSC_STRIDE]);
    if (nh) {
        int mode = (int)s->p[P_A_MODE + idx * OSC_STRIDE];
        if (mode == OSC_LOOP) { o->phase = 0; o->bounce = 0; }
        if (mode == OSC_GRAIN) o->grain_timer = 0;
    }
    return o->st.n ? 1.0f : g;
}

/* ------------------------------------------------------------------ reverb */

static const float rev_ms[REV_LINES] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f};
static const float ap_ms[4] = {4.7f, 6.1f, 3.9f, 5.3f};

static void reverb_gains(float size, float decay, float *g)
{
    float scale = 0.4f + size * 2.4f;
    for (int i = 0; i < REV_LINES; i++) g[i] = powf(10.0f, -3.0f * rev_ms[i] * scale * 0.001f / decay);
}

static void reverb_tick(Synth *s, float inL, float inR, float *outL, float *outR, float size, const float *gains)
{
    float x[2] = {inL, inR};
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 2; k++) {
            float *buf = s->ap[c * 2 + k];
            int d = (int)(ap_ms[c * 2 + k] * 0.001f * s->fs);
            if (d >= AP_N) d = AP_N - 1;
            float z = buf[(s->ap_w - d) & (AP_N - 1)];
            float y = z - 0.6f * x[c];
            buf[s->ap_w & (AP_N - 1)] = x[c] + 0.6f * y;
            x[c] = y;
        }
    s->ap_w++;
    float scale = 0.4f + size * 2.4f;
    s->rev_mod += 0.37 / s->fs;
    if (s->rev_mod > 1) s->rev_mod -= 1;
    float wob = sinf(2.0f * (float)M_PI * (float)s->rev_mod) * 0.0006f * s->fs;
    float d[REV_LINES], sum = 0;
    for (int i = 0; i < REV_LINES; i++) {
        float len = rev_ms[i] * scale * 0.001f * s->fs + (i == 0 ? wob : i == 3 ? -wob : 0);
        if (len > REV_N - 4) len = REV_N - 4;
        float rp = (float)s->rev_w - len;
        while (rp < 0) rp += REV_N;
        unsigned r0 = (unsigned)rp;
        float fr = rp - r0;
        float v = s->rev[i][r0 & (REV_N - 1)] + fr * (s->rev[i][(r0 + 1) & (REV_N - 1)] - s->rev[i][r0 & (REV_N - 1)]);
        s->rev_lp[i] += (v - s->rev_lp[i]) * 0.6f;
        d[i] = s->rev_lp[i];
        sum += d[i];
    }
    float hh = sum * (2.0f / REV_LINES);
    for (int i = 0; i < REV_LINES; i++)
        s->rev[i][s->rev_w & (REV_N - 1)] = ((i & 1) ? x[1] : x[0]) * 0.5f + (d[i] - hh) * gains[i];
    s->rev_w++;
    *outL = 0.6f * (d[0] - d[1] + d[2] + 0.5f * d[4]);
    *outR = 0.6f * (d[3] - d[2] + d[1] - 0.5f * d[4]);
}

/* ------------------------------------------------------------------ main process */

float synth_cross_amount(const Synth *s)
{
    float a = (int)s->p[P_VOCODE] ? s->p[P_VOCODE_AMT] : 0;
    if (s->p[P_FM] * 2 > a) a = s->p[P_FM] * 2;
    if (s->p[P_RING] > a) a = s->p[P_RING];
    if (s->p[P_MORPH] > 0.01f && 0.5f + 0.5f * s->p[P_MORPH] > a) a = 0.5f + 0.5f * s->p[P_MORPH];
    if (s->p[P_CONVERSE] > a) a = s->p[P_CONVERSE];
    return a > 1 ? 1 : a;
}

static inline float band_bp(Svf *f, float x, const SvfCoef *c)
{
    float v3 = x - f->ic2;
    float v1 = c->a1 * f->ic1 + c->a2 * v3;
    float v2 = f->ic2 + c->a2 * f->ic1 + c->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    return v1;
}

float synth_fold_preview(float x, float fold)
{
    if (fold < 0.01f) return x;
    float y = sinf(x * (1.0f + fold * 7.0f) * (float)M_PI * 0.5f);
    float mix = fold * 2.5f > 1 ? 1 : fold * 2.5f;
    return x + mix * (y - x);
}

static inline void meter_hit(float *m, float v, float fall)
{
    v = fabsf(v);
    *m = v > *m ? v : *m * fall;
}

void synth_process(Synth *s, const float *inA, const float *inB, float *out, int frames)
{
    const float fs = s->fs;
    const float *in[2] = {inA, inB};
    const double beat_inc = s->running ? s->p[P_BPM] / 60.0 / fs * s->clock_rate : 0.0;

    for (int b0 = 0; b0 < frames; b0 += BLOCK) {
        int n = frames - b0 < BLOCK ? frames - b0 : BLOCK;
        double beat0 = s->beat_pos;
        s->beat_pos += beat_inc * n;
        osc_control(s, 0, n);
        osc_control(s, 1, n);

        s->pitch += ((float)s->last_note - s->pitch) * (1.0f - expf(-n / (0.02f * fs))); /* short glide */
        float ratio[2], freq[2];
        for (int o = 0; o < 2; o++) {
            float semi = s->pitch + s->p[P_A_TUNE + o * OSC_STRIDE];
            ratio[o] = exp2f((semi - 60.0f) / 12.0f); /* C4 plays a slice at its own speed */
            freq[o] = 440.0f * exp2f((semi - 69.0f) / 12.0f);
            float t = s->p[P_A_LEVEL + o * OSC_STRIDE];
            s->s_lvl[o] += (t - s->s_lvl[o]) * 0.15f;
            for (int c = 0; c < NCELL; c++) s->osc[o].cell_flash[c] *= expf(-n / (0.15f * fs));
        }
        s->x_flash *= expf(-n / (0.15f * fs));
        for (int o = 0; o < 2; o++) s->osc[o].st.flash *= expf(-n / (0.15f * fs));
        s->xst.flash *= expf(-n / (0.15f * fs));
        s->s_vol += (db2lin(s->p[P_VOLUME]) - s->s_vol) * 0.15f;
        for (int f = 0; f < 4; f++) s->s_fx[f] += (s->p[P_CROSS_LEVEL + f] - s->s_fx[f]) * 0.15f;
        const float cross_lvl = s->s_fx[0], shape_lvl = s->s_fx[1], echo_lvl = s->s_fx[2], space_lvl = s->s_fx[3];
        float *mt = s->meter;
        const float fall = s->meter_fall;
        /* master limiter: threshold 0 .. -18 dB, made up to a -1 dB ceiling */
        const float limit = s->p[P_LIMIT], lim_thr = db2lin(-18.0f * limit), lim_makeup = 0.89f / lim_thr;

        /* filter: one knob from dark (low-pass) through open to thin (high-pass) */
        float tone = s->p[P_TONE];
        int fmode = tone < -0.02f ? 1 : tone > 0.02f ? 2 : 0;
        float g = tanf((float)M_PI * fminf(tone_cutoff(tone), fs * 0.45f) / fs);
        float k = 2.0f - 1.9f * s->p[P_RESO];
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        float fold = s->p[P_FOLD];
        float g_low = db2lin(s->p[P_LOW]) - 1.0f, g_high = db2lin(s->p[P_HIGH]) - 1.0f;
        int eq_on = fabsf(s->p[P_LOW]) > 0.1f || fabsf(s->p[P_HIGH]) > 0.1f;
        float drive = s->p[P_DRIVE], dgain = 1.0f + drive * 9.0f, dcomp = 1.0f / (1.0f + drive * 2.0f);
        float crush = s->p[P_CRUSH], cq = exp2f(15.0f - crush * 12.0f), chold = 1.0f + crush * crush * 24.0f;
        float width = s->p[P_WIDTH];
        const float k_lo = 1.0f - expf(-2.0f * (float)M_PI * 250.0f / fs), k_hi = 1.0f - expf(-2.0f * (float)M_PI * 3000.0f / fs);
        float echo = s->p[P_ECHO], space = s->p[P_SPACE];
        float dtarget = 0.75f * 60.0f / s->p[P_BPM] * fs; /* dotted eighth */
        if (dtarget > DLY_N - 4) dtarget = DLY_N - 4;
        float dfb = 0.25f + 0.45f * echo;
        float rsize = 0.6f + 0.4f * space, rgains[REV_LINES];
        if (space > 0.01f) reverb_gains(rsize, 3.0f + 15.0f * space, rgains);
        int focus[2] = {(int)s->p[P_A_FOCUS], (int)s->p[P_B_FOCUS]};
        float fm = s->p[P_FM], ring = s->p[P_RING], morph = s->p[P_MORPH];
        int fm_dir = (int)s->p[P_FM_DIR], vocode = (int)s->p[P_VOCODE];
        float vamt = s->p[P_VOCODE_AMT];
        int bank_on = vocode != VOC_OFF || morph > 0.01f;
        int nb = band_counts[(int)s->p[P_BANDS]];
        if (nb != s->nbands) bank_setup(s, nb);
        float g_voc[BANDS], g_ma[BANDS], g_mb[BANDS];
        if (bank_on)
            for (int b = 0; b < nb; b++) {
                float na = s->env_a[b] + 1e-3f, nb = s->env_b[b] + 1e-3f;
                g_voc[b] = vocode == VOC_A_TO_B ? s->env_a[b] / nb : s->env_b[b] / na;
                if (morph > 0.01f) { /* both voices steered toward an envelope between theirs */
                    float target = expf((1 - morph) * logf(na) + morph * logf(nb));
                    g_ma[b] = (1 - morph) * target / na;
                    g_mb[b] = morph * target / nb;
                }
            }

        /* converse: whoever is talking holds the floor; when they pause and the other is talking, it passes */
        float converse = s->p[P_CONVERSE];
        if (converse > 0.01f) {
            int h = s->floor_holder, o2 = !h;
            int talk_h = s->osc[h].talk_env > 0.35f * s->osc[h].talk_peak && s->osc[h].talk_env > 0.003f;
            int talk_o = s->osc[o2].talk_env > 0.35f * s->osc[o2].talk_peak && s->osc[o2].talk_env > 0.003f;
            s->floor_quiet = talk_h ? 0 : s->floor_quiet + n / fs;
            if (talk_o && s->floor_quiet > 0.25f) { s->floor_holder = o2; s->floor_quiet = 0; }
        }
        float duck_target[2];
        for (int o = 0; o < 2; o++)
            duck_target[o] = converse > 0.01f && o != s->floor_holder ? 1.0f - 0.95f * converse : 1.0f;
        float fstep = 1.0f / (0.15f * fs);

        for (int i = b0; i < b0 + n; i++) {
            double beat = beat0 + beat_inc * (i - b0);
            unsigned tw = s->tap_w & (TAP_LEN - 1);
            for (int o = 0; o < 2; o++) { /* voice sense + focus-filtered capture */
                Osc *os = &s->osc[o];
                float l = in[o][i * 2], r = in[o][i * 2 + 1];
                vad_tick(&os->vad, 0.5f * (l + r));
                int match = focus[o] == FOCUS_ALL || (focus[o] == FOCUS_VOICE && os->vad.state == VS_VOICE) ||
                            (focus[o] == FOCUS_MUSIC && os->vad.state == VS_MUSIC);
                os->focus_gain += match ? fstep : -fstep;
                os->focus_gain = os->focus_gain > 1 ? 1 : os->focus_gain < 0 ? 0 : os->focus_gain;
                if (match || os->focus_gain > 0) {
                    os->cap[os->cap_w * 2] = l;
                    os->cap[os->cap_w * 2 + 1] = r;
                    os->cap_w = (os->cap_w + 1) & (CAP_N - 1);
                }
                s->tap[o ? TAP_RAW_B : TAP_RAW_A][tw] = 0.5f * (l + r);
            }
            s->tap[TAP_STREAM][tw] = 0.5f * (s->tap[TAP_RAW_A][tw] + s->tap[TAP_RAW_B][tw]);

            int xnh;
            long xbefore = s->x_step_abs;
            float xg = euclid_gate(s, SEQ_X, beat, &s->x_step_abs, &s->x_idx, &s->x_hit, &s->x_flash, &xnh);
            if (s->x_step_abs != xbefore) stut_step(s, &s->xst, SEQ_X, s->x_step_abs, (int)s->p[P_X_RATE]);
            s->x_gate += (xg - s->x_gate) * 0.04f;
            const float xgate = s->x_gate;
            float x[2] = {0, 0}, vm[2] = {0, 0};
            /* the modulating voice runs first so it can bend the other one's pitch (FM) */
            int first = fm_dir == FM_B_TO_A ? 1 : 0;
            for (int k2 = 0; k2 < 2; k2++) {
                int o = k2 == 0 ? first : !first;
                Osc *os = &s->osc[o];
                float fmr = 1.0f;
                if (k2 == 1 && fm > 0.001f) fmr = exp2f(fm * xgate * 4.0f * vm[first] * 2.0f);
                osc_tick(s, o, ratio[o] * fmr, freq[o] * fmr);
                float gg = rhythm_eval(s, o, beat);
                stutter_tick(s, &os->st, os->out);
                os->gate += (gg - os->gate) * 0.04f;
                float vl = os->out[0] * os->gate, vr = os->out[1] * os->gate;
                /* speech-band activity of this voice (before ducking), for converse */
                float mono = 0.5f * (vl + vr);
                os->talk_hp += (mono - os->talk_hp) * 0.03f;           /* removes rumble below ~250 Hz */
                os->talk_lp += ((mono - os->talk_hp) - os->talk_lp) * 0.35f; /* and hiss above ~3.5 kHz */
                float ta = fabsf(os->talk_lp);
                os->talk_env += (ta - os->talk_env) * (ta > os->talk_env ? 0.01f : 0.0003f);
                os->talk_peak = os->talk_env > os->talk_peak ? os->talk_env : os->talk_peak * 0.99999f;
                os->duck += (duck_target[o] - os->duck) * 0.0005f; /* ~40 ms */
                vl *= os->duck;
                vr *= os->duck;
                float vdrive = s->p[P_A_DRIVE + o * OSC_STRIDE];
                if (vdrive > 0.01f) { /* the voice's own drive: warm saturation, roughly level-matched */
                    float dg = 1.0f + vdrive * 9.0f, dc = 1.0f / (1.0f + vdrive * 2.5f);
                    vl = fast_tanh(vl * dg) * dc;
                    vr = fast_tanh(vr * dg) * dc;
                }
                vm[o] = 0.5f * (vl + vr) * s->s_lvl[o];
                s->tap[o ? TAP_B : TAP_A][tw] = 0.5f * (vl + vr);
                os->level += (fabsf(vl) + fabsf(vr) - os->level) * 0.001f;
                meter_hit(&mt[o ? MIX_B : MIX_A], fmaxf(fabsf(vl), fabsf(vr)) * s->s_lvl[o], fall);
                float pan = s->p[P_A_PAN + o * OSC_STRIDE];
                x[0] += vl * s->s_lvl[o] * (1.0f - (pan > 0 ? pan : 0));
                x[1] += vr * s->s_lvl[o] * (1.0f + (pan < 0 ? pan : 0));
            }
            s->tap[TAP_VOICES][tw] = 0.5f * (x[0] + x[1]);

            /* cross-modulation between the two voices */
            float am = vm[0], bm = vm[1];
            const float dry[2] = {x[0], x[1]};
            if (ring > 0.001f) {
                float rm = am * bm * 6.0f * ring;
                x[0] += rm;
                x[1] += rm;
            }
            if (bank_on) {
                float voc = 0, mor = 0;
                for (int b = 0; b < nb; b++) {
                    float ba = band_bp(&s->bank_a[b], am, &s->bank_c[b]);
                    float bb = band_bp(&s->bank_b[b], bm, &s->bank_c[b]);
                    float ea = fabsf(ba), eb = fabsf(bb);
                    s->env_a[b] += (ea - s->env_a[b]) * (ea > s->env_a[b] ? 0.01f : 0.0015f);
                    s->env_b[b] += (eb - s->env_b[b]) * (eb > s->env_b[b] ? 0.01f : 0.0015f);
                    if (vocode == VOC_A_TO_B) voc += bb * g_voc[b]; /* B's sound with A's spectrum */
                    else if (vocode == VOC_B_TO_A) voc += ba * g_voc[b];
                    if (morph > 0.01f) mor += ba * g_ma[b] + bb * g_mb[b];
                }
                if (vocode != VOC_OFF) {
                    float h = fast_tanh(voc * 1.5f) * 0.7f;
                    x[0] += vamt * (h - x[0]);
                    x[1] += vamt * (h - x[1]);
                }
                if (morph > 0.01f) {
                    float mm = morph * 5 > 1 ? 1 : morph * 5;
                    float h = mor * 1.2f;
                    x[0] += mm * (h - x[0]);
                    x[1] += mm * (h - x[1]);
                }
            }
            for (int c = 0; c < 2; c++) x[c] = dry[c] + cross_lvl * xgate * (x[c] - dry[c]); /* cross return level, opened by the cross rhythm */
            meter_hit(&mt[MIX_CROSS], fmaxf(fabsf(x[0] - dry[0]), fabsf(x[1] - dry[1])), fall);
            stutter_tick(s, &s->xst, x); /* the cross stutter ratchets the mix from here on */
            s->tap[TAP_CROSS][tw] = 0.5f * (x[0] + x[1]);

            float tf = 0, tfo = 0, pre[2];
            for (int c = 0; c < 2; c++) {
                float v = x[c];
                if (fmode) {
                    Svf *f = &s->filt[c];
                    float v3 = v - f->ic2;
                    float v1 = a1 * f->ic1 + a2 * v3;
                    float v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
                    f->ic1 = 2 * v1 - f->ic1;
                    f->ic2 = 2 * v2 - f->ic2;
                    v = fmode == 1 ? v2 : v - k * v1 - v2;
                }
                if (eq_on) { /* bass and treble shelves around 250 Hz and 3 kHz */
                    s->lo_lp[c] += (v - s->lo_lp[c]) * k_lo;
                    s->hi_lp[c] += (v - s->hi_lp[c]) * k_hi;
                    v += s->lo_lp[c] * g_low + (v - s->hi_lp[c]) * g_high;
                }
                tf += v;
                pre[c] = v;
                if (drive > 0.01f) v = fast_tanh(v * dgain) * dcomp; /* warm saturation */
                v = synth_fold_preview(v, fold);
                tfo += v;
                x[c] = v;
            }
            s->tap[TAP_FILTER][tw] = 0.5f * tf;
            if (crush > 0.01f) { /* fewer bits and a lower sample rate */
                s->crush_ph += 1.0f;
                if (s->crush_ph >= chold) {
                    s->crush_ph -= chold;
                    for (int c = 0; c < 2; c++) s->crush_hold[c] = floorf(x[c] * cq + 0.5f) / cq;
                }
                x[0] = s->crush_hold[0];
                x[1] = s->crush_hold[1];
                tfo = x[0] + x[1];
            }
            if (fabsf(shape_lvl - 1.0f) > 0.001f) { /* shape return level: blend the dry signal back in */
                for (int c = 0; c < 2; c++) x[c] = pre[c] + shape_lvl * (x[c] - pre[c]);
                tfo = x[0] + x[1];
            }
            meter_hit(&mt[MIX_SHAPE], fmaxf(fabsf(x[0] - pre[0]), fabsf(x[1] - pre[1])), fall);
            s->tap[TAP_FOLD][tw] = 0.5f * tfo;

            /* echo send: ping-pong at a dotted eighth */
            s->dly_time_s += (dtarget - s->dly_time_s) * 0.0005f;
            float rp = (float)s->dly_w - s->dly_time_s;
            while (rp < 0) rp += DLY_N;
            unsigned r0 = (unsigned)rp;
            float fr = rp - r0;
            unsigned r1 = (r0 + 1) & (DLY_N - 1);
            r0 &= DLY_N - 1;
            float d[2];
            for (int c = 0; c < 2; c++) d[c] = s->dly[r0 * 2 + c] + fr * (s->dly[r1 * 2 + c] - s->dly[r0 * 2 + c]);
            for (int c = 0; c < 2; c++) {
                s->dly_lp[c] += (d[1 - c] - s->dly_lp[c]) * 0.6f;
                s->dly[s->dly_w * 2 + c] = fast_tanh(x[c] * (echo > 0.01f ? 1.0f : 0.0f) + s->dly_lp[c] * dfb);
            }
            s->dly_w = (s->dly_w + 1) & (DLY_N - 1);
            float el = echo * echo_lvl;
            x[0] += d[0] * el;
            x[1] += d[1] * el;
            meter_hit(&mt[MIX_ECHO], fmaxf(fabsf(d[0]), fabsf(d[1])) * el, fall);
            s->tap[TAP_ECHO][tw] = 0.5f * (x[0] + x[1]);

            /* space send */
            if (space > 0.01f) {
                float wl, wr;
                reverb_tick(s, x[0], x[1], &wl, &wr, rsize, rgains);
                float sl = space * 1.2f * space_lvl;
                x[0] += wl * sl;
                x[1] += wr * sl;
                meter_hit(&mt[MIX_SPACE], fmaxf(fabsf(wl), fabsf(wr)) * sl, fall);
            } else {
                mt[MIX_SPACE] *= fall;
            }

            if (fabsf(width - 1.0f) > 0.01f) { /* stereo width: 0 mono .. 2 extra wide */
                float mid = 0.5f * (x[0] + x[1]), side = 0.5f * (x[0] - x[1]) * width;
                x[0] = mid + side;
                x[1] = mid - side;
            }

            /* leveller: brings the mix to a steady loudness (-6 .. +12 dB), slow enough not to pump */
            float e = 0.5f * (x[0] * x[0] + x[1] * x[1]);
            s->lev_env += (e - s->lev_env) * 0.0005f;
            if (s->lev_env > 1e-6f) {
                float target = 0.22f / sqrtf(s->lev_env);
                target = target < 0.5f ? 0.5f : target > 4.0f ? 4.0f : target;
                float coef = target < s->lev_gain ? 0.0002f : 0.00002f; /* down in ~0.1 s, up in ~1 s */
                s->lev_gain += (target - s->lev_gain) * coef;
            }
            for (int c = 0; c < 2; c++) x[c] *= s->lev_gain;
            if (limit > 0.01f) { /* instant attack, ~80 ms release */
                float pk = fmaxf(fabsf(x[0]), fabsf(x[1]));
                s->lim_env = pk > s->lim_env ? pk : s->lim_env * s->lim_rel;
                float lg = s->lim_env > lim_thr ? lim_thr / s->lim_env : 1.0f;
                mt[MIX_LIMIT] = 1.0f - lg > mt[MIX_LIMIT] ? 1.0f - lg : mt[MIX_LIMIT] * fall;
                x[0] *= lg * lim_makeup;
                x[1] *= lg * lim_makeup;
            } else {
                s->lim_env = 0;
                mt[MIX_LIMIT] *= fall;
            }
            out[i * 2] = fast_tanh(x[0] * s->s_vol);
            out[i * 2 + 1] = fast_tanh(x[1] * s->s_vol);
            meter_hit(&mt[MIX_OUT], fmaxf(fabsf(out[i * 2]), fabsf(out[i * 2 + 1])), fall);
            s->tap[TAP_OUT][tw] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
            s->tap_w++;
        }
    }
}

void synth_tap_features(const Synth *s, int tap, float *rms, float *bright, float *crest)
{
    const float *t = s->tap[tap];
    unsigned end = s->tap_w;
    double e = 0, d = 0;
    float pk = 0, prev = t[(end - 1025) & (TAP_LEN - 1)];
    for (unsigned i = end - 1024; i != end; i++) {
        float v = t[i & (TAP_LEN - 1)];
        e += v * v;
        d += (v - prev) * (v - prev);
        prev = v;
        if (fabsf(v) > pk) pk = fabsf(v);
    }
    float r = (float)sqrt(e / 1024.0);
    *rms = r;
    *bright = r > 1e-6f ? (float)sqrt(d / 1024.0) / r : 0;
    *crest = r > 1e-6f ? pk / r : 1.4f;
}
