/* RadioScan synth engine — internet radio streams as oscillators.
 *
 *  stream A -> [voice sense] -> OSC A (LIVE | LOOP | WAVE | GRAIN) (+ tone) -> rhythmic gate A --\
 *                                                                                       HYBRID (vocoder / follow / cross-fold)
 *  stream B -> [voice sense] -> OSC B (LIVE | LOOP | WAVE | GRAIN) (+ tone) -> rhythmic gate B --/  + ring mod, B>A FM, drive
 *        -> FILTER 1 (state-variable) -> WAVEFOLDER -> FILTER 2 (ladder) -> VCA
 *        -> DELAY -> REVERB (5-line FDN) -> MASTER
 *
 *  Modulation: 5 Euclidean rings, LFO, ADSR, 5 random-walk drifters, tempo clock + arpeggiator.
 *  "Voice sense" classifies each stream as speech or music so an oscillator can focus on either.
 */
#ifndef DSP_H
#define DSP_H

#define OSC_FIELDS(X)                                                                                         \
    P_##X##_STATION, P_##X##_MODE, P_##X##_SOURCE, P_##X##_TUNE, P_##X##_FINE, P_##X##_SIZE, P_##X##_TEXTURE,  \
    P_##X##_MOTION, P_##X##_FOCUS, P_##X##_POOL, P_##X##_AUTOSCAN, P_##X##_LEVEL, P_##X##_PAN,                \
    P_##X##_TONE, P_##X##_TONE_MIX,                                                                           \
    P_##X##_GATE, P_##X##_G_STEPS, P_##X##_G_PULSES, P_##X##_G_ROTATE, P_##X##_G_RATE, P_##X##_G_ATTACK,       \
    P_##X##_G_LENGTH, P_##X##_G_RELEASE, P_##X##_G_CURVE, P_##X##_G_CHANCE

#define RING_FIELDS(N)                                                                                        \
    P_E##N##_STEPS, P_E##N##_PULSES, P_E##N##_ROTATE, P_E##N##_RATE, P_E##N##_DEST, P_E##N##_DEPTH,            \
    P_E##N##_CHANCE, P_E##N##_DECAY

enum {
    /* VOICES: OSC A and OSC B share a layout (OSC B id = OSC A id + OSC_STRIDE) */
    OSC_FIELDS(A),
    OSC_FIELDS(B),
    /* SHAPE */
    P_HYBRID, P_HYBRID_AMT, P_RING, P_FM, P_DRIVE,
    P_F1_MODE, P_F1_CUTOFF, P_F1_RES, P_F1_KEYTRACK,
    P_FOLD_TYPE, P_FOLD_AMOUNT, P_FOLD_BIAS, P_FOLD_MIX,
    P_F2_MODE, P_F2_CUTOFF, P_F2_RES, P_F2_DRIVE, P_F2_KEYTRACK,
    /* MOTION: 5 Euclidean rings (ring n id = ring 1 id + n * RING_STRIDE), clock, chance */
    RING_FIELDS(1), RING_FIELDS(2), RING_FIELDS(3), RING_FIELDS(4), RING_FIELDS(5),
    P_BPM, P_ROOT, P_SCALE, P_GLIDE, P_ARP_MODE, P_ARP_RATE, P_ARP_OCT, P_ARP_GATE, P_SYNC,
    P_DRIFT, P_DRIFT_SPEED, P_REVERSE, P_OCT_JUMP, P_SCATTER,
    /* SPACE */
    P_ENV_AMP, P_ENV_ATTACK, P_ENV_DECAY, P_ENV_SUSTAIN, P_ENV_RELEASE, P_ENV_TO_F1, P_ENV_TO_F2, P_ENV_TO_FOLD,
    P_LFO_SHAPE, P_LFO_SYNC, P_LFO_RATE, P_LFO_TO_F1, P_LFO_TO_FOLD, P_LFO_TO_F2, P_LFO_TO_PITCH, P_LFO_TO_AMP,
    P_DLY_SYNC, P_DLY_TIME, P_DLY_FEEDBACK, P_DLY_MIX,
    P_REV_SIZE, P_REV_DECAY, P_REV_DAMP, P_REV_MIX, P_REV_FREEZE,
    P_MASTER,
    P_COUNT
};

#define OSC_STRIDE (P_B_STATION - P_A_STATION)
#define RING_STRIDE (P_E2_STEPS - P_E1_STEPS)
#define NUM_RINGS 5
#define NUM_DRIFT 5

enum { OSC_LIVE, OSC_LOOP, OSC_WAVE, OSC_GRAIN };
enum { SRC_SELF, SRC_OTHER, SRC_BOTH };
enum { FOCUS_ALL, FOCUS_VOICE, FOCUS_MUSIC };
enum { CAT_ANY, CAT_VOICE, CAT_MUSIC, CAT_SOUND };
enum { VS_SILENT, VS_MUSIC, VS_VOICE };
enum { HY_OFF, HY_VOCODE_AB, HY_VOCODE_BA, HY_FOLLOW, HY_CROSSFOLD };
enum {
    DEST_OFF, DEST_NOTE, DEST_F1, DEST_F2, DEST_FOLD, DEST_PITCH, DEST_LEVEL_A, DEST_LEVEL_B,
    DEST_XFADE, DEST_RING, DEST_REVERB, DEST_DELAY, DEST_GRAB_A, DEST_GRAB_B, DEST_HOP_A, DEST_HOP_B, DEST_COUNT
};

enum { PAGE_VOICES, PAGE_SHAPE, PAGE_MOTION, PAGE_SPACE, PAGE_COUNT };

typedef struct {
    const char *key;      /* settings-file key */
    const char *label;    /* on-screen name */
    int page;
    float min, max, def;
    float step;           /* linear step, or multiplicative step when log=1 */
    int log;
    const char *unit;
    const char *const *choices; /* non-NULL: enumerated parameter */
} ParamInfo;

extern const ParamInfo param_info[P_COUNT];
extern const char *const page_names[PAGE_COUNT];
extern const char *const category_names[];
extern const float autoscan_seconds[];
const char *param_label(int id, const float *params);  /* engine-aware labels */
const char *param_section(int id);                     /* group heading */
#define SCALE_LEN 8
const int *scale_intervals(int scale);                 /* 8 semitone offsets for the 8 note keys */
int euclid_hit(int steps, int pulses, int rotate, int step);

typedef struct Synth Synth;

Synth *synth_create(int sample_rate);
void synth_destroy(Synth *s);
void synth_set_param(Synth *s, int id, float v);
float synth_get_param(const Synth *s, int id);
const float *synth_params(const Synth *s);

void synth_note_on(Synth *s, int midi_note);
void synth_note_off(Synth *s, int midi_note);
void synth_all_notes_off(Synth *s);
void synth_set_base_note(Synth *s, int midi_note);     /* lowest note used by generative NOTE rings */
void synth_grab(Synth *s, int osc);                    /* osc: 0, 1 or -1 for both */
void synth_tap_reset(Synth *s);                        /* restart the bar on a tap */
float synth_beat_phase(const Synth *s);
/* clock control for Wi-Fi sync (main thread; hold the audio lock around calls) */
double synth_position(const Synth *s);                 /* 16th notes since the downbeat */
void synth_set_position(Synth *s, double steps);
void synth_set_clock_rate(Synth *s, double rate);      /* 0.9 .. 1.1 speed trim */
void synth_set_running(Synth *s, int on);               /* clock transport: rings, gates, arp */
int synth_running(const Synth *s);
int synth_take_hop(Synth *s, int osc);                 /* 1 if a ring asked this oscillator to hop station */

/* Dice: randomise parameters within musical ranges. amount 1 = anything goes, small = gentle mutation. */
void synth_randomize(Synth *s, const int *ids, int n, float amount);
enum { DICE_SOUND, DICE_MOTION, DICE_MUTATE };
void synth_dice(Synth *s, int which);

/* inA / inB: interleaved stereo stream audio; out: interleaved stereo. */
void synth_process(Synth *s, const float *inA, const float *inB, float *out, int frames);

/* display helpers */
float synth_lfo_value(const Synth *s);
float synth_env_value(const Synth *s);
int synth_current_note(const Synth *s);
int synth_gate_on(const Synth *s);
int synth_ring_step(const Synth *s, int ring);
float synth_ring_flash(const Synth *s, int ring);
int synth_gate_step(const Synth *s, int osc);
float synth_gate_value(const Synth *s, int osc);
float synth_drift_value(const Synth *s, int i);
float synth_speechness(const Synth *s, int osc);       /* 0 music .. 1 speech */
int synth_voice_state(const Synth *s, int osc);        /* VS_* */
float synth_osc_level(const Synth *s, int osc);
int synth_osc_snapshot(Synth *s, int osc, float *dst, int n);

float param_clamp(int id, float v);
float param_adjust(int id, float v, int dir, int fine);
void param_format_ctx(int id, const float *params, char *buf, int n);
const char *note_name(int midi_note, char *buf, int n);

#endif
