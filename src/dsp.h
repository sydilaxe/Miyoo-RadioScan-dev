/* RadioScan synth engine — two internet radio streams, each played through a voice and opened
 * and closed by its own Euclidean rhythm.
 *
 *  stream A -> [voice sense] -> VOICE A (LIVE | LOOP | WAVE | GRAIN) (+ osc) -> RHYTHM A --\
 *                                                       FM between the voices, either way    CROSS: ring, vocode, morph
 *  stream B -> [voice sense] -> VOICE B (LIVE | LOOP | WAVE | GRAIN) (+ osc) -> RHYTHM B --/   -> FILTER -> FOLD
 *                                                                                             -> ECHO -> SPACE
 *                                                                                             -> VOLUME -> LEVELLER
 *  The tempo clock (tap tempo, Wi-Fi sync) drives both rhythms.
 *  "Voice sense" classifies each stream as speech or music so a voice can focus on either.
 */
#ifndef DSP_H
#define DSP_H

#define VOICE_FIELDS(X)                                                                                       \
    P_##X##_STATION, P_##X##_MODE, P_##X##_TUNE, P_##X##_SIZE, P_##X##_TEXTURE, P_##X##_DIR, P_##X##_FOCUS, P_##X##_LEVEL, \
    P_##X##_PAN, P_##X##_DRIVE, P_##X##_OSC, P_##X##_OSC_MIX,                                                                             \
    P_##X##_GATE_ON, P_##X##_GATE_STEPS, P_##X##_GATE_PULSES, P_##X##_GATE_ROT,                                 \
    P_##X##_RATE, P_##X##_LENGTH, P_##X##_ATTACK, P_##X##_RELEASE, P_##X##_CHANCE

enum {
    VOICE_FIELDS(A), /* VOICE B id = VOICE A id + OSC_STRIDE */
    VOICE_FIELDS(B),
    P_BPM, P_SYNC,
    P_VOCODE, P_VOCODE_AMT, P_BANDS, P_FM, P_FM_DIR, P_RING, P_MORPH, P_CONVERSE,
    P_ROOT,
    P_TONE, P_RESO, P_LOW, P_HIGH, P_DRIVE, P_FOLD, P_CRUSH,
    P_ECHO, P_SPACE, P_WIDTH, P_VOLUME,
    P_CROSS_LEVEL, P_SHAPE_LEVEL, P_ECHO_LEVEL, P_SPACE_LEVEL, /* MIX page: how loud each effect comes back */
    P_LIMIT,                                                   /* MIX page: master limiter */
    /* the cross rhythm: when on, cross-modulation (morph, vocode, ring, FM) comes in only on its pulses */
    P_X_GATE_ON, P_X_GATE_STEPS, P_X_GATE_PULSES, P_X_GATE_ROT,
    P_X_RATE, P_X_LENGTH, P_X_ATTACK, P_X_RELEASE, P_X_CHANCE,
    P_COUNT
};

#define OSC_STRIDE (P_B_STATION - P_A_STATION)
/* each voice has one Euclidean rhythm (its gate) that opens and closes it */
enum { CELL_GATE, NCELL };
#define CELL_STRIDE 4
#define CELL_PARAM(osc, cell, field) (P_A_GATE_ON + (osc) * OSC_STRIDE + (cell) * CELL_STRIDE + (field)) /* field 0 on, 1 steps, 2 pulses, 3 rotate */
/* the three rhythms: voice A, voice B and the cross rhythm */
enum { SEQ_A, SEQ_B, SEQ_X, SEQ_COUNT };
#define SEQ_PARAM(seq, field) ((seq) == SEQ_X ? P_X_GATE_ON + (field) : CELL_PARAM(seq, CELL_GATE, field))
#define SEQ_SHAPE(seq, p) ((seq) == SEQ_X ? P_X_##p : P_A_##p + (seq) * OSC_STRIDE) /* RATE, LENGTH, ATTACK, RELEASE, CHANCE */

enum { OSC_LIVE, OSC_LOOP, OSC_WAVE, OSC_GRAIN };
enum { FOCUS_ALL, FOCUS_VOICE, FOCUS_MUSIC };
enum { DIR_FORWARD, DIR_REVERSE, DIR_BOTH }; /* BOTH: loops ping-pong, grains go either way */
enum { CAT_ANY, CAT_VOICE, CAT_MUSIC, CAT_SOUND };
enum { VS_SILENT, VS_MUSIC, VS_VOICE };
enum { PAGE_VOICES, PAGE_RHYTHM, PAGE_ENGINE, PAGE_MIX, PAGE_COUNT };
enum { VOC_OFF, VOC_A_TO_B, VOC_B_TO_A };
enum { FM_B_TO_A, FM_A_TO_B };

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
const char *param_label(int id, const float *params);  /* engine-aware labels */
const char *param_section(int id);                     /* group heading */
int euclid_hit(int steps, int pulses, int rotate, int step);

typedef struct Synth Synth;

Synth *synth_create(int sample_rate);
void synth_destroy(Synth *s);
void synth_set_param(Synth *s, int id, float v);
float synth_get_param(const Synth *s, int id);
const float *synth_params(const Synth *s);

/* pitch for LOOP / WAVE / GRAIN voices (LIVE plays the radio as is); note 60 plays slices at their own speed */
void synth_note_on(Synth *s, int midi_note);
void synth_note_off(Synth *s, int midi_note);
void synth_grab(Synth *s, int osc);                    /* fresh slice from the stream; osc 0, 1 or -1 */

/* clock: tap tempo and Wi-Fi sync (main thread; hold the audio lock around position calls) */
void synth_tap_reset(Synth *s);
float synth_beat_phase(const Synth *s);
double synth_position(const Synth *s);                 /* 16th notes since the downbeat */
void synth_set_position(Synth *s, double steps);
void synth_set_clock_rate(Synth *s, double rate);      /* 0.9 .. 1.1 speed trim */
void synth_set_running(Synth *s, int on);
int synth_running(const Synth *s);

/* dice: randomise parameters within musical ranges (amount 1 = anything goes, small = gentle) */
void synth_randomize(Synth *s, const int *ids, int n, float amount);
enum { DICE_SOUND, DICE_RHYTHM, DICE_MUTATE };
void synth_dice(Synth *s, int which);

/* inA / inB: interleaved stereo stream audio; out: interleaved stereo */
void synth_process(Synth *s, const float *inA, const float *inB, float *out, int frames);

/* display */
int synth_current_note(const Synth *s);
int synth_cell_step(const Synth *s, int osc, int cell);    /* current step of a rhythm cell */
float synth_cell_flash(const Synth *s, int osc, int cell);  /* 1 on a hit, fading */
int synth_seq_step(const Synth *s, int seq);           /* current step of SEQ_A, SEQ_B or SEQ_X */
float synth_seq_flash(const Synth *s, int seq);
float synth_cross_gate(const Synth *s);                /* how open the cross rhythm is now, 0..1 */
float synth_gate_value(const Synth *s, int osc);       /* how open the voice is now, 0..1 */
float synth_speechness(const Synth *s, int osc);       /* 0 music .. 1 speech */
int synth_voice_state(const Synth *s, int osc);        /* VS_* */
float synth_osc_level(const Synth *s, int osc);
float synth_leveller_gain(const Synth *s);
int synth_floor(const Synth *s);                       /* converse: which voice holds the floor (0, 1), -1 off */
/* copy the latest n samples (n <= 2048) of a tap, oldest first */
void synth_tap_read(const Synth *s, int tap, float *dst, int n);

enum { TAP_STREAM, TAP_VOICES, TAP_CROSS, TAP_FILTER, TAP_FOLD, TAP_ECHO, TAP_OUT, TAP_A, TAP_B, TAP_RAW_A, TAP_RAW_B, TAP_COUNT };
/* level (RMS), brightness (0 dark .. ~1.4 noise) and crest factor (peak / RMS) over the last ~20 ms */
void synth_tap_features(const Synth *s, int tap, float *rms, float *bright, float *crest);
float synth_fold_preview(float x, float fold);         /* the folder's shape at a fold amount */
float synth_cross_amount(const Synth *s);              /* 0..1: how much cross-modulation is on */
/* MIX page meters: peak level (0..~1) of each voice after its drive and fader, each effect's return and the
 * output; MIX_LIMIT is the limiter's gain reduction (0 none .. 1 full) */
enum { MIX_A, MIX_B, MIX_CROSS, MIX_SHAPE, MIX_ECHO, MIX_SPACE, MIX_LIMIT, MIX_OUT, MIX_COUNT };
float synth_mix_meter(const Synth *s, int ch);

float param_clamp(int id, float v);
float param_adjust(int id, float v, int dir, int fine);
void param_format_ctx(int id, const float *params, char *buf, int n);
const char *note_name(int midi_note, char *buf, int n);

#endif
