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
static const char *const sources[] = {"OWN STREAM", "OTHER STREAM", "BOTH STREAMS", NULL};
static const char *const focuses[] = {"ALL SOUND", "VOICES ONLY", "MUSIC ONLY", NULL};
static const char *const tones[] = {"OFF", "SINE", "TRIANGLE", "SAW", "SQUARE", NULL};
const char *const category_names[] = {"ANY", "VOICE", "MUSIC", "SOUND", NULL};
static const char *const autoscan_names[] = {"OFF", "10 s", "30 s", "1 min", "5 min", NULL};
const float autoscan_seconds[] = {0, 10, 30, 60, 300};
static const char *const roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", NULL};
static const char *const scales[] = {"PENTATONIC", "MINOR PENT", "HIRAJOSHI", "IN SEN", "YO", "PELOG",
                                     "EGYPTIAN", "MAJOR", "MINOR", "DORIAN", "CHROMATIC", NULL};
static const int scale_tab[][SCALE_LEN] = {
    {0, 2, 4, 7, 9, 12, 14, 16}, {0, 3, 5, 7, 10, 12, 15, 17}, {0, 2, 3, 7, 8, 12, 14, 15},
    {0, 1, 5, 7, 10, 12, 13, 17}, {0, 2, 5, 7, 9, 12, 14, 17}, {0, 1, 3, 7, 8, 12, 13, 15},
    {0, 2, 5, 7, 10, 12, 14, 17}, {0, 2, 4, 5, 7, 9, 11, 12}, {0, 2, 3, 5, 7, 8, 10, 12},
    {0, 2, 3, 5, 7, 9, 10, 12}, {0, 1, 2, 3, 4, 5, 6, 7},
};
static const char *const hybrids[] = {"OFF", "VOCODE A>B", "VOCODE B>A", "A SHAPES B", "CROSS-FOLD", NULL};
static const char *const f1_modes[] = {"OFF", "LOWPASS", "BANDPASS", "HIGHPASS", "NOTCH", NULL};
static const char *const f2_modes[] = {"OFF", "LP 24dB", "LP 12dB", "BANDPASS", "HIGHPASS", NULL};
static const char *const fold_types[] = {"OFF", "SINE", "TRIANGLE", "MULTI", NULL};
static const char *const lfo_shapes[] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM", NULL};
static const char *const env_amps[] = {"DRONE", "GATED", NULL};
static const char *const arp_modes[] = {"OFF", "UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED", NULL};
static const char *const offon[] = {"OFF", "ON", NULL};
static const char *const sync_modes[] = {"OFF", "AUTO", "LEAD", "FOLLOW", NULL};
static const char *const dests[] = {"OFF", "NOTE", "FILTER 1", "FILTER 2", "FOLD", "PITCH", "LEVEL A", "LEVEL B",
                                    "A<>B", "RING MOD", "REVERB", "DELAY", "GRAB A", "GRAB B", "HOP A", "HOP B", NULL};
/* note values; the five-based ones (5/16, 5/8, 5 bars) are deliberate */
static const char *const rates[] = {"1/32", "1/16", "1/8T", "1/8", "1/8.", "5/16", "1/4T", "1/4", "1/4.",
                                    "5/8", "1/2", "1 BAR", "2 BARS", "4 BARS", "5 BARS", NULL};
static const float rate_beats[] = {0.125f, 0.25f, 1.0f / 3, 0.5f, 0.75f, 1.25f, 2.0f / 3, 1, 1.5f, 2.5f, 2, 4, 8, 16, 20};
static const char *const syncs[] = {"FREE", "1/32", "1/16", "1/8T", "1/8", "1/8.", "5/16", "1/4T", "1/4", "1/4.",
                                    "5/8", "1/2", "1 BAR", "2 BARS", "4 BARS", "5 BARS", NULL};
#define SYNC_BEATS(i) ((i) > 0 ? rate_beats[(i) - 1] : 0)

const char *const page_names[PAGE_COUNT] = {"VOICES", "SHAPE", "MOTION", "SPACE"};

const int *scale_intervals(int scale)
{
    if (scale < 0 || scale > 10) scale = 0;
    return scale_tab[scale];
}

int euclid_hit(int steps, int pulses, int rotate, int step)
{
    if (steps <= 0 || pulses <= 0) return 0;
    if (pulses >= steps) return 1;
    int j = ((step - rotate) % steps + steps) % steps;
    return (j * pulses) % steps < pulses;
}

#define OSC_PARAMS(X, x, mode, size, tex, mot, lvl, pan, gsteps, gpulses, grate)                             \
    [P_##X##_STATION] = {x "_station", "Station", PAGE_VOICES, -1, 511, -1, 1, 0, "station", NULL},              \
    [P_##X##_MODE] = {x "_engine", "Engine", PAGE_VOICES, 0, 3, mode, 1, 0, "", osc_modes},                      \
    [P_##X##_SOURCE] = {x "_source", "Source", PAGE_VOICES, 0, 2, 0, 1, 0, "", sources},                         \
    [P_##X##_TUNE] = {x "_tune", "Tune", PAGE_VOICES, -24, 24, 0, 1, 0, "st", NULL},                             \
    [P_##X##_FINE] = {x "_fine", "Fine", PAGE_VOICES, -100, 100, 0, 1, 0, "ct", NULL},                           \
    [P_##X##_SIZE] = {x "_size", "Size", PAGE_VOICES, 1, 2000, size, 1.08f, 1, "ms", NULL},                      \
    [P_##X##_TEXTURE] = {x "_texture", "Texture", PAGE_VOICES, 0, 1, tex, 0.02f, 0, "", NULL},                   \
    [P_##X##_MOTION] = {x "_motion", "Motion", PAGE_VOICES, 0, 1, mot, 0.02f, 0, "", NULL},                      \
    [P_##X##_FOCUS] = {x "_focus", "Focus", PAGE_VOICES, 0, 2, 0, 1, 0, "", focuses},                            \
    [P_##X##_POOL] = {x "_pool", "Station Pool", PAGE_VOICES, 0, 3, 0, 1, 0, "", category_names},                \
    [P_##X##_AUTOSCAN] = {x "_autoscan", "Auto-Scan", PAGE_VOICES, 0, 4, 0, 1, 0, "", autoscan_names},           \
    [P_##X##_LEVEL] = {x "_level", "Level", PAGE_VOICES, 0, 1, lvl, 0.02f, 0, "", NULL},                         \
    [P_##X##_PAN] = {x "_pan", "Pan", PAGE_VOICES, -1, 1, pan, 0.05f, 0, "pan", NULL},                           \
    [P_##X##_TONE] = {x "_tone", "Tone Osc", PAGE_VOICES, 0, 4, 0, 1, 0, "", tones},                             \
    [P_##X##_TONE_MIX] = {x "_tone_mix", "Tone Mix", PAGE_VOICES, 0, 1, 0, 0.02f, 0, "", NULL},                  \
    [P_##X##_GATE] = {x "_gate", "Gate Depth", PAGE_VOICES, 0, 1, 0, 0.05f, 0, "", NULL},                        \
    [P_##X##_G_STEPS] = {x "_g_steps", "Steps", PAGE_VOICES, 1, 16, gsteps, 1, 0, "int", NULL},                  \
    [P_##X##_G_PULSES] = {x "_g_pulses", "Pulses", PAGE_VOICES, 0, 16, gpulses, 1, 0, "int", NULL},              \
    [P_##X##_G_ROTATE] = {x "_g_rotate", "Rotate", PAGE_VOICES, 0, 15, 0, 1, 0, "int", NULL},                    \
    [P_##X##_G_RATE] = {x "_g_rate", "Rate", PAGE_VOICES, 0, 14, grate, 1, 0, "", rates},                        \
    [P_##X##_G_ATTACK] = {x "_g_attack", "Attack", PAGE_VOICES, 0, 1, 0.15f, 0.02f, 0, "", NULL},                \
    [P_##X##_G_LENGTH] = {x "_g_length", "Length", PAGE_VOICES, 0, 1, 0.5f, 0.02f, 0, "", NULL},                 \
    [P_##X##_G_RELEASE] = {x "_g_release", "Release", PAGE_VOICES, 0, 1, 0.35f, 0.02f, 0, "", NULL},             \
    [P_##X##_G_CURVE] = {x "_g_curve", "Curve", PAGE_VOICES, -1, 1, 0, 0.05f, 0, "curve", NULL},                 \
    [P_##X##_G_CHANCE] = {x "_g_chance", "Chance", PAGE_VOICES, 0, 1, 1, 0.05f, 0, "", NULL}

#define RING_PARAMS(N, steps, pulses, rate, dest, depth, chance, decay)                                     \
    [P_E##N##_STEPS] = {"e" #N "_steps", "Steps", PAGE_MOTION, 1, 16, steps, 1, 0, "int", NULL},                 \
    [P_E##N##_PULSES] = {"e" #N "_pulses", "Pulses", PAGE_MOTION, 0, 16, pulses, 1, 0, "int", NULL},             \
    [P_E##N##_ROTATE] = {"e" #N "_rotate", "Rotate", PAGE_MOTION, 0, 15, 0, 1, 0, "int", NULL},                  \
    [P_E##N##_RATE] = {"e" #N "_rate", "Rate", PAGE_MOTION, 0, 14, rate, 1, 0, "", rates},                       \
    [P_E##N##_DEST] = {"e" #N "_dest", "Destination", PAGE_MOTION, 0, DEST_COUNT - 1, dest, 1, 0, "", dests},    \
    [P_E##N##_DEPTH] = {"e" #N "_depth", "Depth", PAGE_MOTION, -1, 1, depth, 0.05f, 0, "", NULL},                \
    [P_E##N##_CHANCE] = {"e" #N "_chance", "Chance", PAGE_MOTION, 0, 1, chance, 0.05f, 0, "", NULL},             \
    [P_E##N##_DECAY] = {"e" #N "_decay", "Decay", PAGE_MOTION, 0.05f, 16, decay, 1.15f, 1, "steps", NULL}

const ParamInfo param_info[P_COUNT] = {
    /* OSC A: a granular cloud. OSC B: a five-millisecond wavetable that rescans slowly. */
    OSC_PARAMS(A, "a", OSC_GRAIN, 120, 0.35f, 0.3f, 0.6f, -0.25f, 8, 5, 3),
    OSC_PARAMS(B, "b", OSC_WAVE, 5, 0.25f, 0.0f, 0.35f, 0.25f, 5, 3, 1),

    [P_HYBRID]        = {"hybrid", "Hybrid", PAGE_SHAPE, 0, 4, 0, 1, 0, "", hybrids},
    [P_HYBRID_AMT]    = {"hybrid_amt", "Hybrid Amount", PAGE_SHAPE, 0, 1, 0.7f, 0.02f, 0, "", NULL},
    [P_RING]          = {"ring", "Ring Mod A*B", PAGE_SHAPE, 0, 1, 0, 0.02f, 0, "", NULL},
    [P_FM]            = {"fm", "B > A FM", PAGE_SHAPE, 0, 1, 0, 0.01f, 0, "", NULL},
    [P_DRIVE]         = {"drive", "Drive", PAGE_SHAPE, -12, 24, 0, 1, 0, "dB", NULL},
    [P_F1_MODE]       = {"f1_mode", "Mode", PAGE_SHAPE, 0, 4, 0, 1, 0, "", f1_modes},
    [P_F1_CUTOFF]     = {"f1_cutoff", "Cutoff", PAGE_SHAPE, 20, 20000, 1200, 1.06f, 1, "Hz", NULL},
    [P_F1_RES]        = {"f1_res", "Resonance", PAGE_SHAPE, 0, 1, 0.2f, 0.02f, 0, "", NULL},
    [P_F1_KEYTRACK]   = {"f1_kt", "Key Track", PAGE_SHAPE, 0, 1, 0, 0.05f, 0, "", NULL},
    [P_FOLD_TYPE]     = {"fold_type", "Type", PAGE_SHAPE, 0, 3, 1, 1, 0, "", fold_types},
    [P_FOLD_AMOUNT]   = {"fold_amount", "Fold Amount", PAGE_SHAPE, 0.5f, 30, 1.2f, 1.06f, 1, "x", NULL},
    [P_FOLD_BIAS]     = {"fold_bias", "Symmetry", PAGE_SHAPE, -1, 1, 0, 0.02f, 0, "", NULL},
    [P_FOLD_MIX]      = {"fold_mix", "Dry/Wet", PAGE_SHAPE, 0, 1, 0.5f, 0.02f, 0, "", NULL},
    [P_F2_MODE]       = {"f2_mode", "Mode", PAGE_SHAPE, 0, 4, 1, 1, 0, "", f2_modes},
    [P_F2_CUTOFF]     = {"f2_cutoff", "Cutoff", PAGE_SHAPE, 20, 16000, 1800, 1.06f, 1, "Hz", NULL},
    [P_F2_RES]        = {"f2_res", "Resonance", PAGE_SHAPE, 0, 1.05f, 0.2f, 0.02f, 0, "", NULL},
    [P_F2_DRIVE]      = {"f2_drive", "Drive", PAGE_SHAPE, 0, 24, 0, 1, 0, "dB", NULL},
    [P_F2_KEYTRACK]   = {"f2_kt", "Key Track", PAGE_SHAPE, 0, 1, 0.3f, 0.05f, 0, "", NULL},

    /* five rings, five-based patterns */
    RING_PARAMS(1, 8, 5, 3, DEST_F2, 0.3f, 1.0f, 2.0f),
    RING_PARAMS(2, 5, 2, 7, DEST_GRAB_B, 1.0f, 0.5f, 1.0f),
    RING_PARAMS(3, 13, 5, 3, DEST_NOTE, 1.0f, 0.6f, 3.0f),
    RING_PARAMS(4, 16, 5, 1, DEST_FOLD, 0.25f, 0.8f, 1.5f),
    RING_PARAMS(5, 5, 3, 10, DEST_REVERB, 0.3f, 1.0f, 1.0f),

    [P_BPM]           = {"bpm", "Tempo (SELECT)", PAGE_MOTION, 20, 300, 55, 1, 0, "bpm", NULL},
    [P_ROOT]          = {"root", "Key Root", PAGE_MOTION, 0, 11, 0, 1, 0, "", roots},
    [P_SCALE]         = {"scale", "Scale", PAGE_MOTION, 0, 10, 0, 1, 0, "", scales},
    [P_GLIDE]         = {"glide", "Glide", PAGE_MOTION, 0, 3, 0.08f, 0.01f, 0, "s", NULL},
    [P_ARP_MODE]      = {"arp_mode", "Arpeggiator", PAGE_MOTION, 0, 5, 0, 1, 0, "", arp_modes},
    [P_ARP_RATE]      = {"arp_rate", "Arp Rate", PAGE_MOTION, 0, 14, 3, 1, 0, "", rates},
    [P_ARP_OCT]       = {"arp_oct", "Arp Octaves", PAGE_MOTION, 1, 4, 1, 1, 0, "int", NULL},
    [P_ARP_GATE]      = {"arp_gate", "Arp Gate", PAGE_MOTION, 0.05f, 1, 0.5f, 0.05f, 0, "", NULL},
    [P_SYNC]          = {"sync", "Wi-Fi Sync", PAGE_MOTION, 0, 3, 1, 1, 0, "", sync_modes},
    [P_DRIFT]         = {"drift", "Drift", PAGE_MOTION, 0, 1, 0.25f, 0.02f, 0, "", NULL},
    [P_DRIFT_SPEED]   = {"drift_speed", "Drift Speed", PAGE_MOTION, 0.005f, 2, 0.05f, 1.12f, 1, "Hz", NULL},
    [P_REVERSE]       = {"reverse", "Reverse Chance", PAGE_MOTION, 0, 1, 0, 0.05f, 0, "", NULL},
    [P_OCT_JUMP]      = {"oct_jump", "Octave Jumps", PAGE_MOTION, 0, 1, 0, 0.05f, 0, "", NULL},
    [P_SCATTER]       = {"scatter", "Pitch Scatter", PAGE_MOTION, 0, 12, 0, 0.1f, 0, "st", NULL},

    [P_ENV_AMP]       = {"env_amp", "VCA", PAGE_SPACE, 0, 1, 0, 1, 0, "", env_amps},
    [P_ENV_ATTACK]    = {"env_a", "Attack", PAGE_SPACE, 0.001f, 10, 0.4f, 1.15f, 1, "s", NULL},
    [P_ENV_DECAY]     = {"env_d", "Decay", PAGE_SPACE, 0.005f, 10, 1.0f, 1.15f, 1, "s", NULL},
    [P_ENV_SUSTAIN]   = {"env_s", "Sustain", PAGE_SPACE, 0, 1, 0.7f, 0.02f, 0, "", NULL},
    [P_ENV_RELEASE]   = {"env_r", "Release", PAGE_SPACE, 0.005f, 15, 3.0f, 1.15f, 1, "s", NULL},
    [P_ENV_TO_F1]     = {"env_f1", "> Filter 1", PAGE_SPACE, -5, 5, 0, 0.1f, 0, "oct", NULL},
    [P_ENV_TO_F2]     = {"env_f2", "> Filter 2", PAGE_SPACE, -5, 5, 1, 0.1f, 0, "oct", NULL},
    [P_ENV_TO_FOLD]   = {"env_fold", "> Fold", PAGE_SPACE, -1, 1, 0, 0.02f, 0, "", NULL},
    [P_LFO_SHAPE]     = {"lfo_shape", "Shape", PAGE_SPACE, 0, 4, 0, 1, 0, "", lfo_shapes},
    [P_LFO_SYNC]      = {"lfo_sync", "Tempo Sync", PAGE_SPACE, 0, 15, 0, 1, 0, "", syncs},
    [P_LFO_RATE]      = {"lfo_rate", "Rate (free)", PAGE_SPACE, 0.005f, 30, 0.05f, 1.08f, 1, "Hz", NULL},
    [P_LFO_TO_F1]     = {"lfo_f1", "> Filter 1", PAGE_SPACE, -5, 5, 0, 0.1f, 0, "oct", NULL},
    [P_LFO_TO_FOLD]   = {"lfo_fold", "> Fold", PAGE_SPACE, -1, 1, 0, 0.02f, 0, "", NULL},
    [P_LFO_TO_F2]     = {"lfo_f2", "> Filter 2", PAGE_SPACE, -5, 5, 0.4f, 0.1f, 0, "oct", NULL},
    [P_LFO_TO_PITCH]  = {"lfo_pitch", "> Pitch", PAGE_SPACE, -12, 12, 0, 0.1f, 0, "st", NULL},
    [P_LFO_TO_AMP]    = {"lfo_amp", "> Amp (breath)", PAGE_SPACE, 0, 1, 0, 0.02f, 0, "", NULL},
    [P_DLY_SYNC]      = {"dly_sync", "Delay Sync", PAGE_SPACE, 0, 15, 6, 1, 0, "", syncs},
    [P_DLY_TIME]      = {"dly_time", "Delay (free)", PAGE_SPACE, 10, 2500, 375, 1.05f, 1, "ms", NULL},
    [P_DLY_FEEDBACK]  = {"dly_fb", "Feedback", PAGE_SPACE, 0, 0.95f, 0.45f, 0.02f, 0, "", NULL},
    [P_DLY_MIX]       = {"dly_mix", "Delay Mix", PAGE_SPACE, 0, 1, 0.18f, 0.02f, 0, "", NULL},
    [P_REV_SIZE]      = {"rev_size", "Size", PAGE_SPACE, 0, 1, 0.7f, 0.02f, 0, "", NULL},
    [P_REV_DECAY]     = {"rev_decay", "Decay", PAGE_SPACE, 0.3f, 60, 12, 1.1f, 1, "s", NULL},
    [P_REV_DAMP]      = {"rev_damp", "Damping", PAGE_SPACE, 0, 1, 0.4f, 0.02f, 0, "", NULL},
    [P_REV_MIX]       = {"rev_mix", "Reverb Mix", PAGE_SPACE, 0, 1, 0.45f, 0.02f, 0, "", NULL},
    [P_REV_FREEZE]    = {"rev_freeze", "Freeze", PAGE_SPACE, 0, 1, 0, 1, 0, "", offon},
    [P_MASTER]        = {"master", "Master", PAGE_SPACE, -60, 6, -9, 1, 0, "dB", NULL},
};

static int osc_of(int id) { return id >= P_A_STATION && id <= P_B_G_CHANCE ? (id - P_A_STATION) / OSC_STRIDE : -1; }
static int osc_field(int id) { return (id - P_A_STATION) % OSC_STRIDE + P_A_STATION; }

const char *param_section(int id)
{
    static const char *const ring_names[] = {"RING 1", "RING 2", "RING 3", "RING 4", "RING 5"};
    int o = osc_of(id);
    if (o >= 0) {
        int f = osc_field(id);
        if (f >= P_A_GATE) return o ? "GATE B" : "GATE A";
        return o ? "OSC B" : "OSC A";
    }
    if (id <= P_DRIVE) return "HYBRID";
    if (id <= P_F1_KEYTRACK) return "FILTER 1";
    if (id <= P_FOLD_MIX) return "FOLDER";
    if (id <= P_F2_KEYTRACK) return "FILTER 2";
    if (id <= P_E5_DECAY) return ring_names[(id - P_E1_STEPS) / RING_STRIDE];
    if (id <= P_SYNC) return "CLOCK";
    if (id <= P_SCATTER) return "CHANCE";
    if (id <= P_ENV_TO_FOLD) return "VCA";
    if (id <= P_LFO_TO_AMP) return "LFO";
    if (id <= P_DLY_MIX) return "DELAY";
    if (id <= P_REV_FREEZE) return "REVERB";
    return "OUTPUT";
}

const char *param_label(int id, const float *p)
{
    int o = osc_of(id);
    if (o < 0) return param_info[id].label;
    int mode = (int)p[P_A_MODE + o * OSC_STRIDE];
    int f = osc_field(id);
    static const char *const size_l[] = {"Size (unused)", "Loop Length", "Cycle Size", "Grain Size"};
    static const char *const tex_l[] = {"Decimate", "Auto Grab", "Scan Rate", "Density"};
    static const char *const mot_l[] = {"Bit Crush", "Position", "Position", "Spread"};
    if (f == P_A_SIZE) return size_l[mode];
    if (f == P_A_TEXTURE) return tex_l[mode];
    if (f == P_A_MOTION) return mot_l[mode];
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

/* engine-specific texture mappings (shared by DSP and display) */
static float loop_regrab_seconds(float t) { return t <= 0.001f ? 0 : 8.0f * powf(0.03f, t); }
static float wave_scan_hz(float t) { return t <= 0.001f ? 0 : 0.05f * powf(600.0f, t); }
static float grain_density(float t) { return 1.0f + t * 15.0f; }
#define POSITION_SECONDS 3.0f

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
    if (o >= 0) {
        int mode = (int)params[P_A_MODE + o * OSC_STRIDE];
        int f = osc_field(id);
        if (f == P_A_TEXTURE) {
            if (mode == OSC_LIVE) { snprintf(buf, n, v < 0.01f ? "off" : "1/%d", 1 + (int)(v * v * 31)); return; }
            if (mode == OSC_LOOP) { float s = loop_regrab_seconds(v); if (s == 0) snprintf(buf, n, "hold"); else snprintf(buf, n, "every %.2f s", s); return; }
            if (mode == OSC_WAVE) { float h = wave_scan_hz(v); if (h == 0) snprintf(buf, n, "hold"); else snprintf(buf, n, "%.2f Hz", h); return; }
            snprintf(buf, n, "%.1f grains", grain_density(v));
            return;
        }
        if (f == P_A_MOTION) {
            if (mode == OSC_LIVE) { snprintf(buf, n, v < 0.01f ? "off" : "%d bit", 16 - (int)(v * 14)); return; }
            snprintf(buf, n, "%.2f s back", v * POSITION_SECONDS);
            return;
        }
        if (f == P_A_GATE && v < 0.001f) { snprintf(buf, n, "off"); return; }
    }
    if (p->choices) {
        snprintf(buf, n, "%s", p->choices[(int)(v + 0.5f)]);
    } else if (!strcmp(p->unit, "station")) {
        snprintf(buf, n, v < 0 ? "-- off --" : "#%d", (int)v + 1);
    } else if (!strcmp(p->unit, "int")) {
        snprintf(buf, n, "%.0f", v);
    } else if (!strcmp(p->unit, "steps")) {
        snprintf(buf, n, "%.2f steps", v);
    } else if (!strcmp(p->unit, "curve")) {
        snprintf(buf, n, fabsf(v) < 0.01f ? "linear" : v < 0 ? "soft %.0f%%" : "sharp %.0f%%", fabsf(v) * 100);
    } else if (!strcmp(p->unit, "Hz") && v >= 1000) {
        snprintf(buf, n, "%.2f kHz", v / 1000.0f);
    } else if (!strcmp(p->unit, "Hz")) {
        snprintf(buf, n, v < 0.1f ? "%.3f Hz" : v < 10 ? "%.2f Hz" : "%.0f Hz", v);
    } else if (!strcmp(p->unit, "s")) {
        if (v < 1) snprintf(buf, n, "%.0f ms", v * 1000);
        else snprintf(buf, n, "%.2f s", v);
    } else if (!strcmp(p->unit, "ms")) {
        snprintf(buf, n, v < 10 ? "%.1f ms" : "%.0f ms", v);
    } else if (!strcmp(p->unit, "dB") || !strcmp(p->unit, "ct")) {
        snprintf(buf, n, "%+.0f %s", v, p->unit);
    } else if (!strcmp(p->unit, "st")) {
        snprintf(buf, n, fabsf(v - roundf(v)) < 0.01f ? "%+.0f st" : "%+.1f st", v);
    } else if (!strcmp(p->unit, "bpm")) {
        snprintf(buf, n, "%.0f BPM", v);
    } else if (!strcmp(p->unit, "oct")) {
        snprintf(buf, n, "%+.1f oct", v);
    } else if (!strcmp(p->unit, "x")) {
        snprintf(buf, n, "%.2fx", v);
    } else if (!strcmp(p->unit, "pan")) {
        if (fabsf(v) < 0.01f) snprintf(buf, n, "centre");
        else snprintf(buf, n, "%c%.0f", v < 0 ? 'L' : 'R', fabsf(v) * 100);
    } else if (p->min < 0) {
        snprintf(buf, n, "%+.0f%%", v * 100);
    } else {
        snprintf(buf, n, "%.0f%%", v * 100);
    }
}

/* ================================================================== engine */

#define CAP_N (1 << 18)     /* live capture per oscillator (frames): 5.4 s @ 48 kHz */
#define SNAP_N (1 << 17)    /* grabbed loop / wavetable (frames) */
#define DLY_N (1 << 17)
#define REV_N 8192
#define REV_LINES 5
#define AP_N 1024
#define MAX_GRAINS 24
#define WIN_N 1024
#define VOC_BANDS 15
#define VAD_HIST 50
#define BLOCK 16

typedef struct { float ic1, ic2; } Svf;
typedef struct { float y[4], x[4]; } Ladder;
typedef struct { float g, k, a1, a2, a3; } SvfCoef;

typedef struct {
    double pos, rate;
    int len, age, src;
    float gl, gr;
} Grain;

/* voice sense: speech vs music from low-energy-frame ratio and zero-crossing spread */
typedef struct {
    int flen, fcount, zc;
    float acc, prev;
    float e[VAD_HIST], z[VAD_HIST];
    int hidx, filled;
    float speech;   /* smoothed 0..1 */
    int state;      /* VS_* */
} Vad;

typedef struct {
    float *cap;           /* stereo ring of the (focus-filtered) live stream */
    unsigned cap_w;
    float *snap[2];       /* double-buffered grabs (stereo) */
    int snap_len[2];
    int cur;
    float morph, morph_step;
    double phase;
    float timer;
    int want_grab, last_mode;
    Grain grains[MAX_GRAINS];
    float grain_timer;
    float hold[2], dec_ph;
    float out[2];
    float focus_gain;
    Vad vad;
    double tone_ph;       /* simple oscillator mixed into the voice */
    /* rhythmic gate */
    long gate_step_abs;
    int gate_hit, gate_idx;
    float gate_gain, gate_shape;
    float level;          /* display follower */
} Osc;

typedef struct {
    long last_step;
    int idx;
    float env, flash;
} Ring;

struct Synth {
    float fs;
    float p[P_COUNT];
    Osc osc[2];

    Svf f1[2];
    Ladder f2[2];
    float dc_x[2], dc_y[2];

    /* hybrid */
    SvfCoef voc_c[VOC_BANDS];
    Svf voc_m[VOC_BANDS], voc_k[VOC_BANDS];
    float voc_env[VOC_BANDS];
    float follow;

    /* modulation */
    double lfo_phase;
    float lfo_val, lfo_sh;
    unsigned rng;
    Ring ring[NUM_RINGS];
    float drift[NUM_DRIFT], drift_target[NUM_DRIFT], drift_timer[NUM_DRIFT];
    volatile int hop_req[2];

    /* notes */
    int notes[8], n_notes;
    int last_note, base_note;
    float pitch;
    int gate, env_stage;
    float env;
    double ring_note_off;
    int ring_note_on;
    double beat_pos;
    double clock_rate;    /* sync speed trim */
    int running;          /* clock transport */
    double arp_next, arp_off;
    int arp_idx, arp_dir, arp_active;

    /* delay */
    float *dly;
    unsigned dly_w;
    float dly_time_s;
    float dly_lp[2];

    /* reverb: 5-line feedback delay network with input diffusion */
    float *rev[REV_LINES];
    unsigned rev_w;
    float rev_lp[REV_LINES];
    float *ap[4];
    unsigned ap_w;
    double rev_mod;

    float s_f1, s_f2, s_fold, s_drive, s_master, s_lvl[2];
    float win[WIN_N + 1];

    /* display taps (mono rings written by the audio thread, read by the UI) */
    float tap[TAP_COUNT][2048];
    unsigned tap_w;
    SynthLive live;
};

static inline float fast_tanh(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float db2lin(float db) { return powf(10.0f, db / 20.0f); }

static inline float tri_fold(float x)
{
    float t = 0.25f * x + 0.25f;
    return 4.0f * fabsf(t - floorf(t + 0.5f)) - 1.0f;
}

static float rnd01(Synth *s)
{
    s->rng = s->rng * 1664525u + 1013904223u;
    return (float)(s->rng >> 8) / 16777216.0f;
}

static SvfCoef svf_coef(float fc, float q, float fs)
{
    SvfCoef c;
    if (fc > fs * 0.45f) fc = fs * 0.45f;
    c.g = tanf((float)M_PI * fc / fs);
    c.k = 1.0f / q;
    c.a1 = 1.0f / (1.0f + c.g * (c.g + c.k));
    c.a2 = c.g * c.a1;
    c.a3 = c.g * c.a2;
    return c;
}

static inline float svf_bp(Svf *f, float x, const SvfCoef *c)
{
    float v3 = x - f->ic2;
    float v1 = c->a1 * f->ic1 + c->a2 * v3;
    float v2 = f->ic2 + c->a2 * f->ic1 + c->a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    return v1;
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
        s->osc[o].cap = calloc(CAP_N * 2, sizeof(float));
        s->osc[o].snap[0] = calloc(SNAP_N * 2, sizeof(float));
        s->osc[o].snap[1] = calloc(SNAP_N * 2, sizeof(float));
        s->osc[o].last_mode = -1;
        s->osc[o].focus_gain = 1;
        s->osc[o].gate_gain = 1;
        s->osc[o].gate_step_abs = -1;
        s->osc[o].vad.flen = sample_rate / 50; /* 20 ms frames */
        s->osc[o].vad.speech = 0.5f;
        ok = ok && s->osc[o].cap && s->osc[o].snap[0] && s->osc[o].snap[1];
    }
    if (!ok) { synth_destroy(s); return NULL; }
    for (int i = 0; i < P_COUNT; i++) s->p[i] = param_info[i].def;
    for (int i = 0; i <= WIN_N; i++) { float w = sinf((float)M_PI * i / WIN_N); s->win[i] = w * w; }
    for (int b = 0; b < VOC_BANDS; b++) {
        float f = 120.0f * powf(7000.0f / 120.0f, (float)b / (VOC_BANDS - 1));
        s->voc_c[b] = svf_coef(f, 7.0f, s->fs);
    }
    for (int r = 0; r < NUM_RINGS; r++) s->ring[r].last_step = -1;
    s->rng = 0x5eed5u;
    s->clock_rate = 1.0;
    s->running = 1;
    s->last_note = s->base_note = 48;
    s->pitch = 48;
    s->s_f1 = s->p[P_F1_CUTOFF];
    s->s_f2 = s->p[P_F2_CUTOFF];
    s->s_fold = s->p[P_FOLD_AMOUNT];
    s->s_drive = db2lin(s->p[P_DRIVE]);
    s->s_master = db2lin(s->p[P_MASTER]);
    s->dly_time_s = 0.3f * s->fs;
    return s;
}

void synth_destroy(Synth *s)
{
    if (!s) return;
    for (int o = 0; o < 2; o++) {
        free(s->osc[o].cap);
        free(s->osc[o].snap[0]);
        free(s->osc[o].snap[1]);
    }
    for (int i = 0; i < REV_LINES; i++) free(s->rev[i]);
    for (int i = 0; i < 4; i++) free(s->ap[i]);
    free(s->dly);
    free(s);
}

void synth_set_param(Synth *s, int id, float v) { s->p[id] = param_clamp(id, v); }
float synth_get_param(const Synth *s, int id) { return s->p[id]; }
const float *synth_params(const Synth *s) { return s->p; }
float synth_lfo_value(const Synth *s) { return s->lfo_val; }
float synth_env_value(const Synth *s) { return s->env; }
int synth_current_note(const Synth *s) { return s->last_note; }
int synth_gate_on(const Synth *s) { return s->gate; }
int synth_ring_step(const Synth *s, int r) { return s->ring[r].idx; }
float synth_ring_flash(const Synth *s, int r) { return s->ring[r].flash; }
int synth_gate_step(const Synth *s, int o) { return s->osc[o].gate_idx; }
float synth_gate_value(const Synth *s, int o) { return s->osc[o].gate_gain; }
float synth_drift_value(const Synth *s, int i) { return s->drift[i]; }
float synth_speechness(const Synth *s, int o) { return s->osc[o].vad.speech; }
int synth_voice_state(const Synth *s, int o) { return s->osc[o].vad.state; }
float synth_osc_level(const Synth *s, int o) { return s->osc[o].level; }
void synth_set_base_note(Synth *s, int n) { s->base_note = n; }

int synth_take_hop(Synth *s, int o)
{
    return __atomic_exchange_n(&s->hop_req[o], 0, __ATOMIC_ACQ_REL);
}

void synth_tap_reset(Synth *s)
{
    s->beat_pos = 0;
    s->arp_next = 0;
    s->lfo_phase = 0;
    for (int r = 0; r < NUM_RINGS; r++) s->ring[r].last_step = -1;
    for (int o = 0; o < 2; o++) s->osc[o].gate_step_abs = -1;
}

float synth_beat_phase(const Synth *s) { return (float)(s->beat_pos - floor(s->beat_pos)); }

double synth_position(const Synth *s) { return s->beat_pos * 4.0; }

void synth_set_position(Synth *s, double steps)
{
    if (steps < 0) steps = 0;
    s->beat_pos = steps / 4.0;
    s->arp_next = s->beat_pos;
}

void synth_set_clock_rate(Synth *s, double r) { s->clock_rate = r < 0.9 ? 0.9 : r > 1.1 ? 1.1 : r; }
void synth_set_running(Synth *s, int on) { s->running = on; }
int synth_running(const Synth *s) { return s->running; }

void synth_grab(Synth *s, int osc)
{
    for (int o = 0; o < 2; o++)
        if (osc < 0 || osc == o) s->osc[o].want_grab = 1;
}

static void trigger_note(Synth *s, int note)
{
    s->last_note = note;
    s->gate = 1;
    s->env_stage = 1;
}

void synth_note_on(Synth *s, int note)
{
    for (int i = 0; i < s->n_notes; i++)
        if (s->notes[i] == note) return;
    if (s->n_notes < 8) s->notes[s->n_notes++] = note;
    s->ring_note_on = 0;
    if ((int)s->p[P_ARP_MODE]) {
        if (s->n_notes == 1) { s->arp_next = s->beat_pos; s->arp_idx = -1; s->arp_dir = 1; }
        return;
    }
    trigger_note(s, note);
}

void synth_note_off(Synth *s, int note)
{
    int j = 0;
    for (int i = 0; i < s->n_notes; i++)
        if (s->notes[i] != note) s->notes[j++] = s->notes[i];
    s->n_notes = j;
    if ((int)s->p[P_ARP_MODE]) return; /* the arpeggiator owns the gate */
    if (j > 0) s->last_note = s->notes[j - 1];
    else if (s->gate) { s->gate = 0; s->env_stage = 4; }
}

void synth_all_notes_off(Synth *s)
{
    s->n_notes = 0;
    if (s->gate) { s->gate = 0; s->env_stage = 4; }
}

/* ------------------------------------------------------------------ dice */

static float rand_in(Synth *s, float lo, float hi, int lg)
{
    float r = rnd01(s);
    if (lg && lo > 0) return lo * powf(hi / lo, r);
    return lo + (hi - lo) * r;
}

/* musical limits for randomisation, so the dice never produce something painful */
static void dice_range(int id, float *lo, float *hi)
{
    const ParamInfo *p = &param_info[id];
    *lo = p->min;
    *hi = p->max;
    int o = osc_of(id);
    int f = o >= 0 ? osc_field(id) : id;
    switch (f) {
    case P_A_TUNE: *lo = -12; *hi = 12; break;
    case P_A_FINE: *lo = -15; *hi = 15; break;
    case P_A_SIZE: *lo = 2; *hi = 600; break;
    case P_A_LEVEL: *lo = 0.25f; *hi = 0.8f; break;
    case P_A_TONE_MIX: *lo = 0; *hi = 0.35f; break;
    case P_A_GATE: *lo = 0; *hi = 0.9f; break;
    case P_F1_CUTOFF: *lo = 150; *hi = 7000; break;
    case P_F2_CUTOFF: *lo = 250; *hi = 6000; break;
    case P_F1_RES: case P_F2_RES: *lo = 0; *hi = 0.8f; break;
    case P_FOLD_AMOUNT: *lo = 0.7f; *hi = 6; break;
    case P_FOLD_BIAS: *lo = -0.5f; *hi = 0.5f; break;
    case P_DRIVE: *lo = -6; *hi = 9; break;
    case P_F2_DRIVE: *lo = 0; *hi = 9; break;
    case P_RING: *lo = 0; *hi = 0.5f; break;
    case P_FM: *lo = 0; *hi = 0.3f; break;
    case P_DLY_FEEDBACK: *lo = 0.1f; *hi = 0.8f; break;
    case P_REV_DECAY: *lo = 2; *hi = 30; break;
    case P_LFO_RATE: *lo = 0.01f; *hi = 2; break;
    case P_DRIFT_SPEED: *lo = 0.01f; *hi = 0.5f; break;
    case P_SCATTER: *lo = 0; *hi = 5; break;
    default:
        if (id >= P_E1_STEPS && id <= P_E5_DECAY) {
            int rf = (id - P_E1_STEPS) % RING_STRIDE + P_E1_STEPS;
            if (rf == P_E1_STEPS) { *lo = 3; *hi = 16; }
            if (rf == P_E1_RATE) { *lo = 1; *hi = 12; }
            if (rf == P_E1_DEPTH) { *lo = -0.7f; *hi = 0.7f; }
            if (rf == P_E1_CHANCE) { *lo = 0.35f; *hi = 1; }
            if (rf == P_E1_DECAY) { *lo = 0.3f; *hi = 4; }
        }
    }
}

static int dice_excluded(int id)
{
    int o = osc_of(id);
    int f = o >= 0 ? osc_field(id) : id;
    return f == P_A_STATION || f == P_A_POOL || f == P_A_AUTOSCAN || f == P_A_FOCUS || id == P_MASTER ||
           id == P_BPM || id == P_ROOT || id == P_REV_FREEZE || id == P_ENV_AMP || id == P_ARP_MODE ||
           id == P_SYNC;
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
            if (amount < 0.5f && rnd01(s) > amount) continue; /* gentle mode rarely flips switches */
            v = floorf(rand_in(s, lo, hi + 0.999f, 0));
            if (id >= P_E1_STEPS && id <= P_E5_DECAY && (id - P_E1_STEPS) % RING_STRIDE == P_E1_DEST - P_E1_STEPS &&
                (v == DEST_HOP_A || v == DEST_HOP_B) && rnd01(s) < 0.8f)
                v = DEST_F2; /* station hops stay rare surprises */
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
    /* keep pulses <= steps */
    for (int r = 0; r < NUM_RINGS; r++) {
        float *st = &s->p[P_E1_STEPS + r * RING_STRIDE];
        if (st[1] > st[0]) st[1] = floorf(1 + rnd01(s) * (st[0] - 1));
    }
    for (int o = 0; o < 2; o++) {
        float *g = &s->p[P_A_G_STEPS + o * OSC_STRIDE];
        if (g[1] > g[0]) g[1] = floorf(1 + rnd01(s) * (g[0] - 1));
    }
}

void synth_dice(Synth *s, int which)
{
    int ids[P_COUNT], n = 0;
    for (int i = 0; i < P_COUNT; i++) {
        int pg = param_info[i].page;
        int o = osc_of(i);
        int gate = o >= 0 && osc_field(i) >= P_A_GATE;
        if (which == DICE_SOUND && ((pg == PAGE_VOICES && !gate) || pg == PAGE_SHAPE)) ids[n++] = i;
        if (which == DICE_MOTION && ((i >= P_E1_STEPS && i <= P_E5_DECAY) || gate || (i >= P_DRIFT && i <= P_SCATTER) ||
                                     (i >= P_LFO_SHAPE && i <= P_LFO_TO_AMP)))
            ids[n++] = i;
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
            if (rnd01(s) < 0.6f) s->p[P_A_TONE + o * OSC_STRIDE] = 0; /* streams stay the main source */
        }
        if (rnd01(s) < 0.6f) s->p[P_RING] = 0;
        if (rnd01(s) < 0.7f) s->p[P_FM] = 0;
        if (rnd01(s) < 0.4f) s->p[P_HYBRID] = HY_OFF;
        if ((int)s->p[P_F2_MODE] == 0 && rnd01(s) < 0.7f) s->p[P_F2_MODE] = 1;
    }
    if (which == DICE_MOTION)
        for (int o = 0; o < 2; o++)
            if (rnd01(s) < 0.5f) s->p[P_A_GATE + o * OSC_STRIDE] = 0;
}

/* ------------------------------------------------------------------ oscillator engines */

static inline float cap_get(const Osc *o, long back, int c)
{
    unsigned i = (o->cap_w - 1u - (unsigned)back) & (CAP_N - 1);
    return o->cap[i * 2 + c];
}

/* Grab `len` frames ending `back` frames before the write head from the chosen source(s),
 * bake a crossfade into the seam so it loops cleanly, optionally normalise, maybe reverse. */
static void osc_grab(Synth *s, int idx, int len, int back, int normalise)
{
    Osc *o = &s->osc[idx];
    int src = (int)s->p[P_A_SOURCE + idx * OSC_STRIDE];
    const Osc *ca = src == SRC_OTHER ? &s->osc[!idx] : o;
    const Osc *cb = src == SRC_BOTH ? &s->osc[!idx] : NULL;
    if (len < 8) len = 8;
    if (len > SNAP_N) len = SNAP_N;
    if (back + len * 2 > CAP_N - 16) back = CAP_N - 16 - len * 2;
    if (back < 0) back = 0;
    if (o->morph > 0) { o->cur = !o->cur; o->morph = 0; }
    int dst = !o->cur;
    float *sn = o->snap[dst];
    int xf = len / 4;
    if (xf > 2048) xf = 2048;
    double sum[2] = {0, 0};
    for (int i = 0; i < len; i++) {
        long b0 = (long)back + len - 1 - i; /* frames back from head for sample i */
        for (int c = 0; c < 2; c++) {
            float v = cap_get(ca, b0, c);
            if (cb) v = 0.5f * (v + cap_get(cb, b0, c));
            if (i >= len - xf) {
                float t = (float)(i - (len - xf)) / xf;
                float w = cap_get(ca, b0 + len, c);
                if (cb) w = 0.5f * (w + cap_get(cb, b0 + len, c));
                v = (1 - t) * v + t * w;
            }
            sn[i * 2 + c] = v;
            sum[c] += v;
        }
    }
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
    if (rnd01(s) < s->p[P_REVERSE]) /* chance: play this grab backwards */
        for (int i = 0; i < len / 2; i++)
            for (int c = 0; c < 2; c++) {
                float t = sn[i * 2 + c];
                sn[i * 2 + c] = sn[(len - 1 - i) * 2 + c];
                sn[(len - 1 - i) * 2 + c] = t;
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
    int back = (int)(p[P_A_MOTION] * POSITION_SECONDS * s->fs);
    float dt = n / s->fs;

    if (mode != o->last_mode) { o->want_grab = 1; o->last_mode = mode; }
    if (mode == OSC_LOOP || mode == OSC_WAVE) {
        float interval = mode == OSC_LOOP ? loop_regrab_seconds(tex)
                                          : (wave_scan_hz(tex) > 0 ? 1.0f / wave_scan_hz(tex) : 0);
        if (interval > 0) {
            o->timer -= dt;
            if (o->timer <= 0) o->want_grab = 1;
        }
        if (o->want_grab) {
            osc_grab(s, idx, (int)(p[P_A_SIZE] * 0.001f * s->fs), back, mode == OSC_WAVE);
            o->want_grab = 0;
            o->timer = interval > 0 ? interval : 1e9f;
            float mt = mode == OSC_LOOP ? 0.015f : (interval > 0 ? interval : 0.02f);
            if (mt < 0.005f) mt = 0.005f;
            o->morph_step = 1.0f / (mt * s->fs);
        }
    } else {
        o->want_grab = 0;
    }
}

/* polyBLEP correction for the naive saw / square */
static inline float blep(double t, double dt)
{
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0;
}

static float tone_tick(Osc *o, int wave, float freq, float fs)
{
    double dt = freq / fs;
    if (dt > 0.45) dt = 0.45;
    double t = o->tone_ph;
    float v;
    switch (wave) {
    case 1: v = sinf(2.0f * (float)M_PI * (float)t); break;
    case 2: v = t < 0.5 ? (float)(4 * t - 1) : (float)(3 - 4 * t); break;
    case 3: v = (float)(2 * t - 1) - blep(t, dt); break;
    default: {
        double t2 = t + 0.5;
        if (t2 >= 1) t2 -= 1;
        v = (t < 0.5 ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
        break;
    }
    }
    o->tone_ph += dt;
    if (o->tone_ph >= 1.0) o->tone_ph -= 1.0;
    return v * 0.5f;
}

static void osc_tick(Synth *s, int idx, float ratio, float freq)
{
    Osc *o = &s->osc[idx];
    const float *p = s->p + idx * OSC_STRIDE;
    int mode = (int)p[P_A_MODE];
    int src = (int)p[P_A_SOURCE];
    const Osc *so = src == SRC_OTHER ? &s->osc[!idx] : o;

    switch (mode) {
    case OSC_LIVE: {
        float l = cap_get(so, 0, 0), r = cap_get(so, 0, 1);
        if (src == SRC_BOTH) { l = 0.5f * (l + cap_get(&s->osc[!idx], 0, 0)); r = 0.5f * (r + cap_get(&s->osc[!idx], 0, 1)); }
        l *= so->focus_gain;
        r *= so->focus_gain;
        float tex = p[P_A_TEXTURE], mot = p[P_A_MOTION];
        if (tex > 0.01f) {
            o->dec_ph += 1.0f;
            if (o->dec_ph >= 1.0f + tex * tex * 31.0f) { o->dec_ph = 0; o->hold[0] = l; o->hold[1] = r; }
            l = o->hold[0]; r = o->hold[1];
        }
        if (mot > 0.01f) {
            float q = exp2f(15.0f - mot * 14.0f);
            l = floorf(l * q + 0.5f) / q;
            r = floorf(r * q + 0.5f) / q;
        }
        o->out[0] = l; o->out[1] = r;
        break;
    }
    case OSC_LOOP:
    case OSC_WAVE: {
        int a = o->cur, b = !o->cur;
        int len = o->snap_len[a] > 0 ? o->snap_len[a] : o->snap_len[b];
        if (len <= 0) { o->out[0] = o->out[1] = 0; break; }
        for (int c = 0; c < 2; c++) {
            float v = snap_read(o, a, o->phase, c);
            if (o->morph > 0) v += o->morph * (snap_read(o, b, o->phase, c) - v);
            o->out[c] = v;
        }
        double inc = mode == OSC_LOOP ? ratio / (double)len : freq / s->fs;
        if (inc > 0.5) inc = 0.5;
        o->phase += inc;
        if (o->phase >= 1.0) o->phase -= floor(o->phase);
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
                float rate = ratio;
                if (s->p[P_SCATTER] > 0) rate *= exp2f((rnd01(s) * 2 - 1) * s->p[P_SCATTER] / 12.0f);
                if (rnd01(s) < s->p[P_OCT_JUMP]) rate *= rnd01(s) < 0.5f ? 0.5f : 2.0f;
                if (rnd01(s) < s->p[P_REVERSE]) rate = -rate;
                float spread = p[P_A_MOTION] * POSITION_SECONDS * s->fs;
                double lead = glen * (rate > 1 ? rate : 1) + 4;
                gr->src = src == SRC_BOTH ? (rnd01(s) < 0.5f ? idx : !idx) : (src == SRC_OTHER ? !idx : idx);
                gr->pos = (double)s->osc[gr->src].cap_w - lead - rnd01(s) * spread;
                gr->rate = rate;
                gr->len = glen;
                gr->age = 0;
                float pan = (rnd01(s) - 0.5f) * (0.3f + p[P_A_MOTION]);
                gr->gl = 0.75f - pan * 0.5f;
                gr->gr = 0.75f + pan * 0.5f;
                break;
            }
        }
        float l = 0, r = 0;
        for (int g = 0; g < MAX_GRAINS; g++) {
            Grain *gr = &o->grains[g];
            if (!gr->len) continue;
            const float *cap = s->osc[gr->src].cap;
            float w = s->win[gr->age * WIN_N / gr->len];
            double ip = floor(gr->pos);
            float fr = (float)(gr->pos - ip);
            unsigned i0 = (unsigned)(long long)ip & (CAP_N - 1), i1 = (i0 + 1) & (CAP_N - 1);
            float m0 = cap[i0 * 2] + cap[i0 * 2 + 1];
            float m1 = cap[i1 * 2] + cap[i1 * 2 + 1];
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

    /* optional simple oscillator at the voice's pitch: a clean carrier for vocoding / ring mod */
    int tw = (int)p[P_A_TONE];
    float tm = p[P_A_TONE_MIX];
    if (tw && tm > 0.001f) {
        float t = tone_tick(o, tw, freq, s->fs);
        o->out[0] = o->out[0] * (1.0f - tm) + t * tm;
        o->out[1] = o->out[1] * (1.0f - tm) + t * tm;
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
    /* speech: many quiet gaps between syllables (low-energy ratio) and bursts of
     * fricatives (high zero-crossing ratio); music is denser and steadier */
    float ler = (float)low / VAD_HIST, hzcrr = (float)hz / VAD_HIST;
    float a = (ler - 0.12f) / 0.30f, b = (hzcrr - 0.04f) / 0.20f;
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    if (b < 0) b = 0;
    if (b > 1) b = 1;
    float score = 0.6f * a + 0.4f * b;
    v->speech += (score - v->speech) * 0.03f;
    if (v->speech > 0.55f) v->state = VS_VOICE;
    else if (v->speech < 0.42f) v->state = VS_MUSIC;
    else if (v->state == VS_SILENT) v->state = VS_MUSIC;
}

/* ------------------------------------------------------------------ modulation */

static void lfo_tick(Synth *s, int n)
{
    int div = (int)s->p[P_LFO_SYNC];
    double rate = div ? s->p[P_BPM] / 60.0 / SYNC_BEATS(div) : s->p[P_LFO_RATE];
    double ph = s->lfo_phase + rate / s->fs * n;
    if (ph >= 1.0) { ph -= floor(ph); s->lfo_sh = rnd01(s) * 2 - 1; }
    s->lfo_phase = ph;
    float x = (float)ph, v;
    switch ((int)s->p[P_LFO_SHAPE]) {
    case 0: v = sinf(2.0f * (float)M_PI * x); break;
    case 1: v = x < 0.5f ? 4 * x - 1 : 3 - 4 * x; break;
    case 2: v = 2 * x - 1; break;
    case 3: v = x < 0.5f ? 1 : -1; break;
    default: v = s->lfo_sh; break;
    }
    s->lfo_val = v;
}

static void drift_tick(Synth *s, int n)
{
    float dt = n / s->fs, sp = s->p[P_DRIFT_SPEED];
    for (int i = 0; i < NUM_DRIFT; i++) {
        s->drift_timer[i] -= dt * sp * (0.6f + 0.2f * i); /* five walkers at slightly different paces */
        if (s->drift_timer[i] <= 0) { s->drift_timer[i] += 1.0f; s->drift_target[i] = rnd01(s) * 2 - 1; }
        s->drift[i] += (s->drift_target[i] - s->drift[i]) * (1.0f - expf(-dt * sp * 3.0f));
    }
}

static void arp_tick(Synth *s)
{
    int mode = (int)s->p[P_ARP_MODE];
    if (!mode) {
        if (s->arp_active) { s->arp_active = 0; if (!s->n_notes && s->gate) { s->gate = 0; s->env_stage = 4; } }
        return;
    }
    if (s->arp_active && s->beat_pos >= s->arp_off && s->gate) { s->gate = 0; s->env_stage = 4; }
    if (s->n_notes == 0) {
        if (s->arp_active && s->gate) { s->gate = 0; s->env_stage = 4; }
        s->arp_active = 0;
        s->arp_next = s->beat_pos;
        return;
    }
    if (s->beat_pos < s->arp_next) return;
    float step = rate_beats[(int)s->p[P_ARP_RATE]];
    s->arp_next += step;
    if (s->arp_next < s->beat_pos) s->arp_next = s->beat_pos + step;
    s->arp_off = s->beat_pos + step * s->p[P_ARP_GATE];

    int pool[32], np = 0, oct = (int)s->p[P_ARP_OCT];
    int sorted[8];
    memcpy(sorted, s->notes, sizeof(int) * s->n_notes);
    if (mode != 5)
        for (int i = 1; i < s->n_notes; i++)
            for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; j--) { int t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
    for (int o = 0; o < oct; o++)
        for (int i = 0; i < s->n_notes && np < 32; i++) pool[np++] = sorted[i] + 12 * o;

    int idx = s->arp_idx;
    switch (mode) {
    case 1: case 5: idx = (idx + 1) % np; break;
    case 2: idx = idx <= 0 ? np - 1 : idx - 1; break;
    case 3:
        if (np == 1) { idx = 0; break; }
        idx += s->arp_dir;
        if (idx >= np) { idx = np - 2; s->arp_dir = -1; }
        if (idx < 0) { idx = 1; s->arp_dir = 1; }
        break;
    default: idx = (int)(rnd01(s) * np) % np; break;
    }
    if (idx < 0 || idx >= np) idx = 0;
    s->arp_idx = idx;
    int note = pool[idx];
    if (rnd01(s) < s->p[P_OCT_JUMP] * 0.5f) note += 12;
    trigger_note(s, note);
    s->arp_active = 1;
}

typedef struct { float f1, f2, fold, pitch, lvl[2], xfade, ring, rev, dly; } Mods;

static void rings_tick(Synth *s, int n, Mods *m)
{
    memset(m, 0, sizeof *m);
    float spb = 60.0f / s->p[P_BPM]; /* seconds per beat */
    float dt = n / s->fs;
    for (int r = 0; r < NUM_RINGS; r++) {
        const float *p = s->p + r * RING_STRIDE;
        Ring *rg = &s->ring[r];
        int steps = (int)p[P_E1_STEPS];
        float step_beats = rate_beats[(int)p[P_E1_RATE]];
        long step = (long)floor(s->beat_pos / step_beats);
        int dest = (int)p[P_E1_DEST];
        if (step != rg->last_step) {
            rg->last_step = step;
            rg->idx = (int)(step % steps);
            if (euclid_hit(steps, (int)p[P_E1_PULSES], (int)p[P_E1_ROTATE], rg->idx) && rnd01(s) < p[P_E1_CHANCE]) {
                rg->env = 1;
                rg->flash = 1;
                switch (dest) {
                case DEST_NOTE: {
                    if (s->n_notes) break; /* held keys take priority */
                    const int *sc = scale_intervals((int)s->p[P_SCALE]);
                    int note = s->base_note + sc[(int)(rnd01(s) * 5) % 5]; /* five-note pool */
                    if (rnd01(s) < s->p[P_OCT_JUMP]) note += rnd01(s) < 0.5f ? -12 : 12;
                    trigger_note(s, note);
                    s->ring_note_on = 1;
                    s->ring_note_off = s->beat_pos + step_beats * p[P_E1_DECAY] * 0.5;
                    break;
                }
                case DEST_GRAB_A: s->osc[0].want_grab = 1; break;
                case DEST_GRAB_B: s->osc[1].want_grab = 1; break;
                case DEST_HOP_A: s->hop_req[0] = 1; break;
                case DEST_HOP_B: s->hop_req[1] = 1; break;
                }
            }
        }
        float decay_s = p[P_E1_DECAY] * step_beats * spb;
        rg->env *= expf(-dt / (decay_s > 0.005f ? decay_s : 0.005f));
        rg->flash *= expf(-dt / 0.12f);
        float v = rg->env * p[P_E1_DEPTH];
        switch (dest) {
        case DEST_F1: m->f1 += v * 4; break;
        case DEST_F2: m->f2 += v * 4; break;
        case DEST_FOLD: m->fold += v * 2; break;
        case DEST_PITCH: m->pitch += v * 12; break;
        case DEST_LEVEL_A: m->lvl[0] += v; break;
        case DEST_LEVEL_B: m->lvl[1] += v; break;
        case DEST_XFADE: m->xfade += v; break;
        case DEST_RING: m->ring += v; break;
        case DEST_REVERB: m->rev += v; break;
        case DEST_DELAY: m->dly += v * 0.5f; break;
        }
    }
    if (s->ring_note_on && s->beat_pos >= s->ring_note_off && !s->n_notes) {
        s->ring_note_on = 0;
        if (s->gate) { s->gate = 0; s->env_stage = 4; }
    }
}

static void env_tick(Synth *s, int n)
{
    float t;
    switch (s->env_stage) {
    case 1:
        t = s->p[P_ENV_ATTACK] * s->fs;
        s->env += (1.25f - s->env) * (1.0f - expf(-n / t));
        if (s->env >= 1.0f) { s->env = 1.0f; s->env_stage = 2; }
        break;
    case 2:
        t = s->p[P_ENV_DECAY] * s->fs;
        s->env += (s->p[P_ENV_SUSTAIN] - s->env) * (1.0f - expf(-n * 4.0f / t));
        if (fabsf(s->env - s->p[P_ENV_SUSTAIN]) < 0.001f) s->env_stage = 3;
        break;
    case 3: s->env = s->p[P_ENV_SUSTAIN]; break;
    case 4:
        t = s->p[P_ENV_RELEASE] * s->fs;
        s->env += (0.0f - s->env) * (1.0f - expf(-n * 4.0f / t));
        if (s->env < 0.0005f) { s->env = 0; s->env_stage = 0; }
        break;
    default: s->env = 0;
    }
}

/* rhythmic gate: Euclidean pattern, each pulse shaped by attack / length / release / curve */
static inline float gate_curve(float x, float curve)
{
    if (curve >= 0) return powf(x, 1.0f + curve * 4.0f);
    return 1.0f - powf(1.0f - x, 1.0f - curve * 4.0f);
}

static float gate_eval(Synth *s, int idx, double beat)
{
    Osc *o = &s->osc[idx];
    const float *p = s->p + idx * OSC_STRIDE;
    float depth = p[P_A_GATE];
    if (depth <= 0.001f) { o->gate_shape = 1; return 1.0f; }
    float step_beats = rate_beats[(int)p[P_A_G_RATE]];
    double sp = beat / step_beats;
    long step = (long)floor(sp);
    int steps = (int)p[P_A_G_STEPS];
    if (step != o->gate_step_abs) {
        o->gate_step_abs = step;
        o->gate_idx = (int)(step % steps);
        o->gate_hit = euclid_hit(steps, (int)p[P_A_G_PULSES], (int)p[P_A_G_ROTATE], o->gate_idx) &&
                      rnd01(s) < p[P_A_G_CHANCE];
    }
    float g = 0;
    if (o->gate_hit) {
        float t = (float)(sp - step);
        float a = p[P_A_G_ATTACK], l = p[P_A_G_LENGTH], r = p[P_A_G_RELEASE], cv = p[P_A_G_CURVE];
        if (l < a) l = a;
        if (t < a) g = gate_curve(t / a, cv);
        else if (t < l) g = 1;
        else if (r > 0.001f && t < l + r) g = 1.0f - gate_curve((t - l) / r, -cv);
    }
    o->gate_shape = g;
    return 1.0f - depth * (1.0f - g);
}

/* ------------------------------------------------------------------ filters, folder */

static inline float svf_tick(Svf *f, float x, float k, float a1, float a2, float a3, int mode)
{
    float v3 = x - f->ic2;
    float v1 = a1 * f->ic1 + a2 * v3;
    float v2 = f->ic2 + a2 * f->ic1 + a3 * v3;
    f->ic1 = 2 * v1 - f->ic1;
    f->ic2 = 2 * v2 - f->ic2;
    switch (mode) {
    case 1: return v2;
    case 2: return v1 * k;
    case 3: return x - k * v1 - v2;
    default: return x - k * v1;
    }
}

static inline float ladder_tick(Ladder *l, float x, float f, float fb, float drive, int mode)
{
    float in = fast_tanh(x * drive) - fast_tanh(l->y[3]) * fb;
    in *= 0.35013f * (f * f) * (f * f);
    float r = 1.0f - f;
    l->y[0] = in + 0.3f * l->x[0] + r * l->y[0]; l->x[0] = in;
    l->y[1] = l->y[0] + 0.3f * l->x[1] + r * l->y[1]; l->x[1] = l->y[0];
    l->y[2] = l->y[1] + 0.3f * l->x[2] + r * l->y[2]; l->x[2] = l->y[1];
    l->y[3] = l->y[2] + 0.3f * l->x[3] + r * l->y[3]; l->x[3] = l->y[2];
    float comp = (1.0f + fb * 0.5f) / drive;
    switch (mode) {
    case 1: return l->y[3] * comp;
    case 2: return l->y[1] * comp;
    case 3: return (l->y[1] - l->y[3]) * 2.0f * comp;
    default: return x - l->y[3] * comp;
    }
}

static inline float fold(int type, float x)
{
    switch (type) {
    case 1: return sinf(x * (float)M_PI * 0.5f);
    case 2: return tri_fold(x);
    case 3: {
        float y = tri_fold(x);
        y = sinf((y * 1.7f + 0.25f * x) * (float)M_PI * 0.5f);
        return fast_tanh(1.4f * y);
    }
    default: return x;
    }
}

/* ------------------------------------------------------------------ reverb */

static const float rev_ms[REV_LINES] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f};
static const float ap_ms[4] = {4.7f, 6.1f, 3.9f, 5.3f};

static void reverb_tick(Synth *s, float inL, float inR, float *outL, float *outR, float size, float decay,
                        float damp, int freeze)
{
    /* input diffusion: two allpasses per channel */
    float x[2] = {inL, inR};
    if (freeze) x[0] = x[1] = 0;
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
    float wob = sinf(2.0f * (float)M_PI * (float)s->rev_mod) * 0.0006f * s->fs; /* slow shimmer of line lengths */
    float d[REV_LINES], sum = 0;
    for (int i = 0; i < REV_LINES; i++) {
        float len = rev_ms[i] * scale * 0.001f * s->fs + (i == 0 ? wob : i == 3 ? -wob : 0);
        if (len > REV_N - 4) len = REV_N - 4;
        float rp = (float)s->rev_w - len;
        while (rp < 0) rp += REV_N;
        unsigned r0 = (unsigned)rp;
        float fr = rp - r0;
        float v = s->rev[i][r0 & (REV_N - 1)] + fr * (s->rev[i][(r0 + 1) & (REV_N - 1)] - s->rev[i][r0 & (REV_N - 1)]);
        if (!freeze) s->rev_lp[i] += (v - s->rev_lp[i]) * (1.0f - damp * 0.85f);
        else s->rev_lp[i] = v;
        d[i] = s->rev_lp[i];
        sum += d[i];
    }
    float hh = sum * (2.0f / REV_LINES); /* Householder feedback matrix */
    for (int i = 0; i < REV_LINES; i++) {
        float len = rev_ms[i] * scale * 0.001f;
        float g = freeze ? 1.0f : powf(10.0f, -3.0f * len / decay);
        float in = (i & 1) ? x[1] : x[0];
        s->rev[i][s->rev_w & (REV_N - 1)] = in * 0.5f + (d[i] - hh) * g;
    }
    s->rev_w++;
    *outL = 0.6f * (d[0] - d[1] + d[2] + 0.5f * d[4]);
    *outR = 0.6f * (d[3] - d[2] + d[1] - 0.5f * d[4]);
}

/* ------------------------------------------------------------------ main process */

void synth_process(Synth *s, const float *inA, const float *inB, float *out, int frames)
{
    const float fs = s->fs;
    const float sm = 0.15f;
    const float *in[2] = {inA, inB};
    const double beat_inc = s->running ? s->p[P_BPM] / 60.0 / fs * s->clock_rate : 0.0;

    for (int b0 = 0; b0 < frames; b0 += BLOCK) {
        int n = frames - b0 < BLOCK ? frames - b0 : BLOCK;
        double beat0 = s->beat_pos;
        s->beat_pos += beat_inc * n;
        Mods m;
        rings_tick(s, n, &m);
        arp_tick(s);
        lfo_tick(s, n);
        drift_tick(s, n);
        env_tick(s, n);
        osc_control(s, 0, n);
        osc_control(s, 1, n);
        float lfo = s->lfo_val, env = s->env;
        float dr = s->p[P_DRIFT];
        float d0 = s->drift[0] * dr, d1 = s->drift[1] * dr, d2 = s->drift[2] * dr, d3 = s->drift[3] * dr, d4 = s->drift[4] * dr;

        float gt = s->p[P_GLIDE];
        float gcoef = gt < 0.001f ? 1.0f : 1.0f - expf(-n / (gt * 0.3f * fs));
        s->pitch += ((float)s->last_note - s->pitch) * gcoef;
        float note = s->pitch + lfo * s->p[P_LFO_TO_PITCH] + m.pitch + d4 * 0.3f;

        float ratio[2], freq[2];
        for (int o = 0; o < 2; o++) {
            const float *p = s->p + o * OSC_STRIDE;
            float semi = note + p[P_A_TUNE] + p[P_A_FINE] * 0.01f;
            ratio[o] = exp2f((semi - 60.0f) / 12.0f);
            freq[o] = 440.0f * exp2f((semi - 69.0f) / 12.0f);
        }

        s->s_drive += (db2lin(s->p[P_DRIVE]) - s->s_drive) * sm;
        s->s_master += (db2lin(s->p[P_MASTER]) - s->s_master) * sm;
        s->s_f1 += (s->p[P_F1_CUTOFF] - s->s_f1) * sm;
        s->s_f2 += (s->p[P_F2_CUTOFF] - s->s_f2) * sm;
        s->s_fold += (s->p[P_FOLD_AMOUNT] - s->s_fold) * sm;

        float kt = (s->pitch - 60.0f) / 12.0f;
        float c1 = s->s_f1 * exp2f(lfo * s->p[P_LFO_TO_F1] + env * s->p[P_ENV_TO_F1] + kt * s->p[P_F1_KEYTRACK] + m.f1 + d3 * 1.5f);
        float c2 = s->s_f2 * exp2f(lfo * s->p[P_LFO_TO_F2] + env * s->p[P_ENV_TO_F2] + kt * s->p[P_F2_KEYTRACK] + m.f2 + d0 * 1.5f);
        if (c1 > fs * 0.45f) c1 = fs * 0.45f;
        if (c1 < 10) c1 = 10;
        if (c2 < 10) c2 = 10;

        float g = tanf((float)M_PI * c1 / fs);
        float k = 2.0f - 1.96f * s->p[P_F1_RES];
        float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        int f1m = (int)s->p[P_F1_MODE];

        float lf = c2 / (fs * 0.5f) * 1.16f;
        if (lf > 1.0f) lf = 1.0f;
        float lfb = s->p[P_F2_RES] * 4.0f * (1.0f - 0.15f * lf * lf);
        float ldrive = db2lin(s->p[P_F2_DRIVE]);
        int f2m = (int)s->p[P_F2_MODE];

        float famt = s->s_fold * exp2f(m.fold + d1) * (1.0f + lfo * s->p[P_LFO_TO_FOLD] + env * s->p[P_ENV_TO_FOLD]);
        if (famt < 0.05f) famt = 0.05f;
        float fbias = s->p[P_FOLD_BIAS];
        s->live.cutoff1 = c1;
        s->live.cutoff2 = c2;
        s->live.fold_amount = famt;
        s->live.fold_bias = fbias;
        float fmix = s->p[P_FOLD_MIX];
        int ftype = (int)s->p[P_FOLD_TYPE];
        s->live.fold_type = ftype;
        s->live.f1_mode = f1m;
        s->live.f2_mode = f2m;

        float amp = 1.0f - s->p[P_LFO_TO_AMP] * (0.5f - 0.5f * lfo);
        if ((int)s->p[P_ENV_AMP] == 1) amp *= env;

        /* levels with ring/drift modulation and A<>B crossfade */
        float lvl[2];
        for (int o = 0; o < 2; o++) {
            float t = s->p[P_A_LEVEL + o * OSC_STRIDE] * (1.0f + m.lvl[o]) * (1.0f + (o ? d2 : -d2) * 0.5f);
            float xf = o ? -m.xfade : m.xfade;
            if (xf > 0) t *= 1.0f - (xf > 1 ? 1 : xf);
            if (t < 0) t = 0;
            s->s_lvl[o] += (t - s->s_lvl[o]) * sm;
            lvl[o] = s->s_lvl[o];
        }
        float pan[2] = {s->p[P_A_PAN], s->p[P_B_PAN]};
        float ring = s->p[P_RING] + m.ring, fm = s->p[P_FM];
        if (ring < 0) ring = 0;
        int hyb = (int)s->p[P_HYBRID];
        float hamt = s->p[P_HYBRID_AMT];
        int dsync = (int)s->p[P_DLY_SYNC];
        float dtarget = (dsync ? SYNC_BEATS(dsync) * 60.0f / s->p[P_BPM] : s->p[P_DLY_TIME] * 0.001f) * fs;
        if (dtarget > DLY_N - 4) dtarget = DLY_N - 4;
        float dfb = s->p[P_DLY_FEEDBACK] + m.dly, dmix = s->p[P_DLY_MIX];
        if (dfb > 0.97f) dfb = 0.97f;
        if (dfb < 0) dfb = 0;
        float rmix = s->p[P_REV_MIX] + m.rev;
        if (rmix < 0) rmix = 0;
        if (rmix > 1) rmix = 1;
        float rsize = s->p[P_REV_SIZE], rdecay = s->p[P_REV_DECAY], rdamp = s->p[P_REV_DAMP];
        int rfreeze = (int)s->p[P_REV_FREEZE];

        int focus[2] = {(int)s->p[P_A_FOCUS], (int)s->p[P_B_FOCUS]};
        float fstep = 1.0f / (0.15f * fs); /* 150 ms focus fade */

        for (int i = b0; i < b0 + n; i++) {
            double beat = beat0 + beat_inc * (i - b0);
            /* voice sense + focus-filtered capture of both streams */
            for (int o = 0; o < 2; o++) {
                Osc *os = &s->osc[o];
                float l = in[o][i * 2], r = in[o][i * 2 + 1];
                vad_tick(&os->vad, 0.5f * (l + r));
                int match = focus[o] == FOCUS_ALL ||
                            (focus[o] == FOCUS_VOICE && os->vad.state == VS_VOICE) ||
                            (focus[o] == FOCUS_MUSIC && os->vad.state == VS_MUSIC);
                os->focus_gain += match ? fstep : -fstep;
                if (os->focus_gain > 1) os->focus_gain = 1;
                if (os->focus_gain < 0) os->focus_gain = 0;
                if (match || os->focus_gain > 0) { /* only matching material enters the capture buffer */
                    os->cap[os->cap_w * 2] = l;
                    os->cap[os->cap_w * 2 + 1] = r;
                    os->cap_w = (os->cap_w + 1) & (CAP_N - 1);
                }
            }
            /* B first so it can frequency-modulate A */
            osc_tick(s, 1, ratio[1], freq[1]);
            float bm = 0.5f * (s->osc[1].out[0] + s->osc[1].out[1]);
            float fmr = fm > 0 ? exp2f(fm * 4.0f * bm) : 1.0f;
            osc_tick(s, 0, ratio[0] * fmr, freq[0] * fmr);

            float o2[2][2];
            for (int o = 0; o < 2; o++) {
                float gg = gate_eval(s, o, beat);
                Osc *os = &s->osc[o];
                os->gate_gain += (gg - os->gate_gain) * 0.04f; /* ~1 ms de-click */
                o2[o][0] = os->out[0] * os->gate_gain;
                o2[o][1] = os->out[1] * os->gate_gain;
                float lv = fabsf(o2[o][0]) + fabsf(o2[o][1]);
                os->level += (lv - os->level) * 0.001f;
            }
            float am = 0.5f * (o2[0][0] + o2[0][1]);
            bm = 0.5f * (o2[1][0] + o2[1][1]);

            float x[2] = {0, 0};
            for (int o = 0; o < 2; o++) {
                float gl = lvl[o] * (pan[o] > 0 ? 1 - pan[o] : 1);
                float gr = lvl[o] * (pan[o] < 0 ? 1 + pan[o] : 1);
                x[0] += o2[o][0] * gl;
                x[1] += o2[o][1] * gr;
            }
            float rm = am * bm * 4.0f * ring;
            x[0] += rm;
            x[1] += rm;

            /* hybrids: new sounds made from both streams at once */
            if (hyb != HY_OFF) {
                float h[2] = {0, 0};
                if (hyb == HY_VOCODE_AB || hyb == HY_VOCODE_BA) {
                    float mod = hyb == HY_VOCODE_AB ? am : bm, car = hyb == HY_VOCODE_AB ? bm : am;
                    float v = 0;
                    for (int bnd = 0; bnd < VOC_BANDS; bnd++) {
                        float me = fabsf(svf_bp(&s->voc_m[bnd], mod, &s->voc_c[bnd]));
                        float c = svf_bp(&s->voc_k[bnd], car, &s->voc_c[bnd]);
                        float *e = &s->voc_env[bnd];
                        *e += (me - *e) * (me > *e ? 0.02f : 0.002f);
                        v += c * *e;
                    }
                    h[0] = h[1] = fast_tanh(v * 24.0f) * 0.6f;
                } else if (hyb == HY_FOLLOW) {
                    float me = fabsf(am);
                    s->follow += (me - s->follow) * (me > s->follow ? 0.01f : 0.0005f);
                    float gfol = s->follow * 6.0f;
                    if (gfol > 1.5f) gfol = 1.5f;
                    h[0] = o2[1][0] * gfol;
                    h[1] = o2[1][1] * gfol;
                } else { /* CROSS-FOLD: B pushes A through a sine folder */
                    for (int c = 0; c < 2; c++) h[c] = 0.6f * sinf((o2[0][c] * 1.5f + o2[1][c] * 3.0f) * (float)M_PI * 0.5f);
                }
                x[0] = x[0] * (1.0f - hamt) + h[0] * hamt;
                x[1] = x[1] * (1.0f - hamt) + h[1] * hamt;
            }
            x[0] *= s->s_drive;
            x[1] *= s->s_drive;

            unsigned tw = s->tap_w & 2047;
            s->tap[TAP_STREAM][tw] = 0.25f * (in[0][i * 2] + in[0][i * 2 + 1] + in[1][i * 2] + in[1][i * 2 + 1]);
            s->tap[TAP_A][tw] = am;
            s->tap[TAP_B][tw] = bm;
            s->tap[TAP_VOICES][tw] = 0.5f * (x[0] + x[1]);
            float t1 = 0, t2 = 0, t3 = 0;
            for (int c = 0; c < 2; c++) {
                float v = x[c];
                if (f1m) v = svf_tick(&s->f1[c], v, k, a1, a2, a3, f1m);
                t1 += v;
                if (ftype) {
                    float w = fold(ftype, v * famt + fbias);
                    float y = w - s->dc_x[c] + 0.995f * s->dc_y[c];
                    s->dc_x[c] = w; s->dc_y[c] = y;
                    v = v + fmix * (y - v);
                }
                t2 += v;
                if (f2m) v = ladder_tick(&s->f2[c], v, lf, lfb, ldrive, f2m);
                t3 += v;
                x[c] = v * amp;
            }
            s->tap[TAP_FILTER1][tw] = 0.5f * t1;
            s->tap[TAP_FOLD][tw] = 0.5f * t2;
            s->tap[TAP_FILTER2][tw] = 0.5f * t3 * amp;

            /* ping-pong delay */
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
                s->dly[s->dly_w * 2 + c] = fast_tanh(x[c] + s->dly_lp[c] * dfb);
            }
            s->dly_w = (s->dly_w + 1) & (DLY_N - 1);
            for (int c = 0; c < 2; c++) x[c] = x[c] * (1.0f - 0.5f * dmix) + dmix * d[c];

            /* reverb */
            float wl, wr;
            reverb_tick(s, x[0], x[1], &wl, &wr, rsize, rdecay, rdamp, rfreeze);
            x[0] = x[0] * (1.0f - rmix) + wl * rmix * 1.4f;
            x[1] = x[1] * (1.0f - rmix) + wr * rmix * 1.4f;

            out[i * 2] = fast_tanh(x[0] * s->s_master);
            out[i * 2 + 1] = fast_tanh(x[1] * s->s_master);
            s->tap[TAP_OUT][tw] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
            s->tap_w++;
        }
    }
}

int synth_osc_snapshot(Synth *s, int idx, float *dst, int n)
{
    Osc *o = &s->osc[idx];
    int mode = (int)s->p[P_A_MODE + idx * OSC_STRIDE];
    if (mode == OSC_LOOP || mode == OSC_WAVE) {
        int which = o->morph > 0.5f ? !o->cur : o->cur;
        int len = o->snap_len[which];
        if (len <= 0) { memset(dst, 0, sizeof(float) * n); return n; }
        for (int i = 0; i < n; i++) {
            int j = (int)((long long)i * len / n);
            dst[i] = 0.5f * (o->snap[which][j * 2] + o->snap[which][j * 2 + 1]);
        }
        return n;
    }
    int span = (int)(0.04f * s->fs);
    for (int i = 0; i < n; i++) {
        long back = span - (long)((long long)i * span / n);
        dst[i] = 0.5f * (cap_get(o, back, 0) + cap_get(o, back, 1));
    }
    return n;
}

void synth_tap(const Synth *s, int tap, float *dst, int n, int span)
{
    if (span > 1024) span = 1024;
    if (span < n) span = n;
    const float *t = s->tap[tap];
    unsigned end = s->tap_w;
    unsigned start = end - (unsigned)span - 1024; /* look for a trigger in the 1024 samples before the window */
    for (int i = 0; i < 1023; i++) {
        float a = t[(start + i) & 2047], b = t[(start + i + 1) & 2047];
        if (a <= 0 && b > 0) { start += i; goto found; }
    }
    start = end - (unsigned)span;
found:
    for (int i = 0; i < n; i++) dst[i] = t[(start + (unsigned)((long long)i * span / n)) & 2047];
}

void synth_live(const Synth *s, SynthLive *out) { *out = s->live; }

float synth_fold_preview(int type, float x) { return type ? fold(type, x) : x; }
