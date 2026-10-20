/* RadioScan — a synthesizer for the Miyoo Mini Plus whose sound sources are internet radio streams.
 *
 *  PLAY  (home)  three circles: OSC A, the five Euclidean rings, OSC B. Buttons are a scale keyboard.
 *  EDIT          five pages: RADIO, VOICES, SHAPE, MOTION, SPACE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <SDL/SDL.h>

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#include "dsp.h"
#include "font.h"
#include "net.h"
#include "stations.h"
#include "stream.h"
#include "sync.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SCREEN_W 640
#define SCREEN_H 480

/* ================================================================== buttons */
/* Miyoo Mini key codes as delivered by its SDL 1.2 (the same keys work on a desktop keyboard). */
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_X, B_Y, B_L1, B_R1, B_L2, B_R2, B_SELECT, B_START, B_MENU, B_COUNT, B_NONE = -1 };

static int map_key(SDLKey k)
{
    switch (k) {
    case SDLK_UP: return B_UP;
    case SDLK_DOWN: return B_DOWN;
    case SDLK_LEFT: return B_LEFT;
    case SDLK_RIGHT: return B_RIGHT;
    case SDLK_SPACE: return B_A;
    case SDLK_LCTRL: return B_B;
    case SDLK_LSHIFT: return B_X;
    case SDLK_LALT: return B_Y;
    case SDLK_e: return B_L1;
    case SDLK_t: return B_R1;
    case SDLK_TAB: return B_L2;
    case SDLK_BACKSPACE: return B_R2;
    case SDLK_RCTRL: return B_SELECT;
    case SDLK_RETURN: return B_START;
    case SDLK_ESCAPE: return B_MENU;
    default: return B_NONE;
    }
}

/* ================================================================== state */

static Synth *syn;
static Stream *st[2];
static int audio_rate = 48000;
static StationList stations;
static char app_dir[PATH_MAX], stations_path[PATH_MAX + 16], settings_path[PATH_MAX + 16];

enum { SCR_PLAY, SCR_EDIT };
enum { OV_NONE, OV_KEYBOARD, OV_SCANMENU, OV_SCANRESULTS };
enum { KB_ADD_URL, KB_ADD_NAME, KB_SEARCH };
enum { TAB_RADIO, TAB_VOICES, TAB_RHYTHM, TAB_ENGINE, TAB_MIX, TAB_COUNT };
static const char *const tab_names[TAB_COUNT] = {"RADIO", "VOICES", "RHYTHM", "ENGINE", "MIX"};

#define ROW_HEADER (-3)

static struct {
    int screen, overlay;
    int tab;
    int sel[TAB_COUNT], scroll[TAB_COUNT];
    int radio_sel, radio_scroll;
    int mix_sel;              /* MIX page: selected fader */
    int confirm_delete;
    int octave;
    int held[B_COUNT];
    Uint32 held_since[B_COUNT], last_repeat[B_COUNT];
    int l2_used;              /* L2 acted as a modifier while held (otherwise a tap = grab) */
    int cell;                 /* PLAY: selected rhythm, SEQ_A, SEQ_B or SEQ_X (the cross rhythm in the middle) */
    char toast[128];
    Uint32 toast_until;
    Uint32 grab_flash, menu_armed;
    int grab_osc;             /* voice of the last grab: 0, 1 or -1 for both */
    Uint32 taps[8];
    int ntaps;
    int pending_station[2];
    Uint32 pending_at[2];
    int tuned[2];
    int kb_purpose, kb_row, kb_col, kb_shift;
    char kb_text[512], kb_url[512];
    int kb_cat;
    int scan_sel, scan_scroll, scan_menu_sel, scan_menu_scroll, scan_cat;
    StationList scan_results;
    volatile int scan_busy;
    char scan_title[96];
    int dirty_settings;
    int running;
} ui;

/* ================================================================== audio */

static float bufA[4096], bufB[4096], bufO[4096];

/* Treat denormal floats as zero in the audio thread: decaying reverb/filter tails otherwise
 * fall into denormals, which are extremely slow on the Cortex-A7's VFP. */
static void enable_flush_to_zero(void)
{
#if defined(__arm__)
    unsigned fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24); /* FZ */
    __asm__ volatile("vmsr fpscr, %0" : : "r"(fpscr));
#elif defined(__x86_64__) || defined(__SSE__)
    unsigned csr;
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040; /* FTZ | DAZ */
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
#endif
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    static __thread int ftz;
    if (!ftz) { enable_flush_to_zero(); ftz = 1; }
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    while (frames > 0) {
        int n = frames > 2048 ? 2048 : frames;
        stream_read(st[0], bufA, n);
        stream_read(st[1], bufB, n);
        synth_process(syn, bufA, bufB, bufO, n);
        for (int i = 0; i < n; i++) {
            float l = bufO[i * 2], r = bufO[i * 2 + 1];
            out[i * 2] = (int16_t)(l * 32767.0f);
            out[i * 2 + 1] = (int16_t)(r * 32767.0f);
        }
        out += n * 2;
        frames -= n;
    }
}

/* ================================================================== drawing */

static SDL_Surface *video, *screen;
static Uint32 *px;
static int pitch_px;

#define RGB(r, g, b) ((Uint32)(((r) << 16) | ((g) << 8) | (b)))
/* forest palette */
static const Uint32 C_BG = RGB(15, 23, 18),        /* forest floor at dusk */
    C_PANEL = RGB(26, 38, 30),                     /* moss shade */
    C_PANEL2 = RGB(42, 58, 45),                    /* lichen */
    C_SEL = RGB(50, 70, 48),                       /* sunlit moss */
    C_TEXT = RGB(233, 228, 212),                   /* birch bark */
    C_DIM = RGB(138, 152, 126),                    /* sage */
    C_LEAF = RGB(143, 191, 106),                   /* OSC A: new leaf */
    C_AMBER = RGB(217, 164, 65),                   /* OSC B: autumn */
    C_RUST = RGB(196, 98, 62),                     /* berries / warnings */
    C_WATER = RGB(112, 166, 166),                  /* voices: river */
    C_BARK = RGB(140, 106, 76);                    /* sound: bark */
static const Uint32 ring_cols[5] = {RGB(118, 160, 92), RGB(170, 196, 118), RGB(217, 164, 65),
                                            RGB(196, 120, 76), RGB(112, 166, 166)};

static Uint32 map_col(Uint32 c) { return SDL_MapRGB(screen->format, (c >> 16) & 255, (c >> 8) & 255, c & 255); }

static void fill(int x, int y, int w, int h, Uint32 c)
{
    if (w <= 0 || h <= 0) return;
    SDL_Rect r = {(Sint16)x, (Sint16)y, (Uint16)w, (Uint16)h};
    SDL_FillRect(screen, &r, map_col(c));
}

static inline void pset(int x, int y, Uint32 m)
{
    if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) px[y * pitch_px + x] = m;
}

static Uint32 mix_col(Uint32 a, Uint32 b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int r = (int)(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
    int g = (int)(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
    int bl = (int)((a & 255) * (1 - t) + (b & 255) * t);
    return RGB(r, g, bl);
}

static void line(int x0, int y0, int x1, int y1, Uint32 c)
{
    Uint32 m = map_col(c);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        pset(x0, y0, m);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void disc(int cx, int cy, int r, Uint32 c)
{
    for (int dy = -r; dy <= r; dy++) {
        int w = (int)sqrtf((float)(r * r - dy * dy) + 0.5f);
        fill(cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}

static void circle(int cx, int cy, int r, Uint32 c)
{
    Uint32 m = map_col(c);
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pset(cx + x, cy + y, m); pset(cx + y, cy + x, m); pset(cx - y, cy + x, m); pset(cx - x, cy + y, m);
        pset(cx - x, cy - y, m); pset(cx - y, cy - x, m); pset(cx + y, cy - x, m); pset(cx + x, cy - y, m);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

/* arc from fraction a0 to a1 of a full turn, starting at 12 o'clock, clockwise */
static void arc(int cx, int cy, int r, int thick, float a0, float a1, Uint32 c)
{
    Uint32 m = map_col(c);
    if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
    float step = 0.5f / (r * 2.0f * (float)M_PI);
    for (float a = a0; a <= a1; a += step) {
        float s = sinf(a * 2 * (float)M_PI), co = -cosf(a * 2 * (float)M_PI);
        for (int k = 0; k < thick; k++) pset(cx + (int)lroundf(s * (r + k)), cy + (int)lroundf(co * (r + k)), m);
    }
}

static void polar(int cx, int cy, float r, float frac, int *x, int *y)
{
    *x = cx + (int)lroundf(sinf(frac * 2 * (float)M_PI) * r);
    *y = cy - (int)lroundf(cosf(frac * 2 * (float)M_PI) * r);
}

static int text(int x, int y, const char *s, Uint32 col, int max_chars)
{
    Uint32 m = map_col(col);
    int n = 0, len = (int)strlen(s);
    int ellipsis = max_chars > 0 && len > max_chars;
    for (; *s; s++, n++) {
        if (max_chars > 0 && n >= max_chars) break;
        unsigned char ch = (unsigned char)*s;
        if (ellipsis && n == max_chars - 1) ch = '~';
        if (ch < FONT_FIRST || ch > FONT_LAST) ch = '?';
        const uint16_t *g = font_bits[ch - FONT_FIRST];
        for (int r = 0; r < FONT_H; r++) {
            uint16_t bits = g[r];
            if (!bits) continue;
            for (int c = 0; c < FONT_W; c++)
                if (bits & (1 << (15 - c))) pset(x + n * FONT_W + c, y + r, m);
        }
    }
    return n * FONT_W;
}

static void text_right(int xr, int y, const char *s, Uint32 col, int max_chars)
{
    int len = (int)strlen(s);
    if (max_chars > 0 && len > max_chars) len = max_chars;
    text(xr - len * FONT_W, y, s, col, max_chars);
}

static void text_center(int xc, int y, const char *s, Uint32 col, int max_chars)
{
    int len = (int)strlen(s);
    if (max_chars > 0 && len > max_chars) len = max_chars;
    text(xc - len * FONT_W / 2, y, s, col, max_chars);
}

/* round knob: track ring + value arc (bipolar params fill from 12 o'clock) */
static void knob(int cx, int cy, int r, float frac, int bipolar, Uint32 col)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    arc(cx, cy, r, 2, 0.125f, 0.875f, C_PANEL2);
    if (bipolar) arc(cx, cy, r, 2, 0.5f, 0.125f + frac * 0.75f, col);
    else if (frac > 0.002f) arc(cx, cy, r, 2, 0.125f, 0.125f + frac * 0.75f, col);
    int x, y;
    polar(cx, cy, r - 3, 0.625f + frac * 0.75f + 0.5f, &x, &y);
    line(cx, cy, x, y, col);
}

static void toast(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui.toast, sizeof ui.toast, fmt, ap);
    va_end(ap);
    ui.toast_until = SDL_GetTicks() + 2200;
}

/* ================================================================== settings & stations */

/* Settings from an older version only keep their stations: the sound defaults changed. */
#define SETTINGS_VERSION 3
static int settings_were_reset;

static void save_settings(void)
{
    char tmp[PATH_MAX + 32];
    snprintf(tmp, sizeof tmp, "%s.tmp", settings_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "version=%d\n", SETTINGS_VERSION);
    for (int i = 0; i < P_COUNT; i++) fprintf(f, "%s=%g\n", param_info[i].key, synth_get_param(syn, i));
    for (int o = 0; o < 2; o++) {
        int idx = (int)synth_get_param(syn, P_A_STATION + o * OSC_STRIDE);
        if (idx >= 0 && idx < stations.count) fprintf(f, "%c_url=%s\n", 'a' + o, stations.items[idx].url);
    }
    fprintf(f, "octave=%d\n", ui.octave);
    fclose(f);
    rename(tmp, settings_path);
    ui.dirty_settings = 0;
}


static int load_settings(void)
{
    FILE *f = fopen(settings_path, "r");
    if (!f) return -1;
    char line_[1024];
    int version = 0;
    while (fgets(line_, sizeof line_, f))
        if (!strncmp(line_, "version=", 8)) version = atoi(line_ + 8);
    rewind(f);
    settings_were_reset = version < SETTINGS_VERSION;
    while (fgets(line_, sizeof line_, f)) {
        char *eq = strchr(line_, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(line_, "octave")) { ui.octave = atoi(v); continue; }
        if ((line_[0] == 'a' || line_[0] == 'b') && !strcmp(line_ + 1, "_url")) {
            int idx = stations_find_url(&stations, v); /* URL wins over a stale index */
            synth_set_param(syn, P_A_STATION + (line_[0] - 'a') * OSC_STRIDE, (float)idx);
            continue;
        }
        if (settings_were_reset) continue;
        for (int i = 0; i < P_COUNT; i++)
            if (!strcmp(param_info[i].key, line_)) synth_set_param(syn, i, (float)atof(v));
    }
    fclose(f);
    if (ui.octave < -3 || ui.octave > 3) ui.octave = 0;
    return 0;
}

static void save_stations(void)
{
    if (stations_save(&stations, stations_path)) toast("Could not save stations.txt");
}

static int station_param(int o) { return (int)synth_get_param(syn, P_A_STATION + o * OSC_STRIDE); }

static void tune_now(int o, int idx)
{
    synth_set_param(syn, P_A_STATION + o * OSC_STRIDE, (float)idx);
    ui.pending_station[o] = -2;
    ui.tuned[o] = idx;
    if (idx >= 0 && idx < stations.count) stream_play(st[o], stations.items[idx].url);
    else stream_stop(st[o]);
    ui.dirty_settings = 1;
}

static void tune_later(int o, int idx)
{
    synth_set_param(syn, P_A_STATION + o * OSC_STRIDE, (float)idx);
    ui.pending_station[o] = idx;
    ui.pending_at[o] = SDL_GetTicks() + 700; /* debounce while scrolling */
}

/* another station for a voice: random, or the next one in the list */
static int pool_next(int o, int random)
{
    int n = stations.count, cur = station_param(o);
    if (n == 0) return -1;
    if (random && n > 1) {
        int i;
        do { i = rand() % n; } while (i == cur);
        return i;
    }
    return (cur + 1) % n;
}

static int first_of_kind(int cat, int fallback)
{
    for (int i = 0; i < stations.count; i++)
        if (stations.items[i].cat == cat) return i;
    return fallback < stations.count ? fallback : -1;
}

static void delete_station(int idx)
{
    stations_remove(&stations, idx);
    for (int o = 0; o < 2; o++) {
        int cur = station_param(o);
        if (cur == idx) tune_now(o, -1);
        else if (cur > idx) { synth_set_param(syn, P_A_STATION + o * OSC_STRIDE, (float)(cur - 1)); ui.tuned[o] = cur - 1; }
    }
    save_stations();
    ui.dirty_settings = 1;
}

/* ================================================================== directory scan thread */

static int scan_kind;
static char scan_query[256];
static StationList scan_tmp;
static char scan_err[200];
static volatile int scan_done, scan_rc;

static void *scan_thread(void *arg)
{
    (void)arg;
    scan_rc = directory_search(scan_kind, scan_query, &scan_tmp, scan_err, sizeof scan_err);
    scan_done = 1;
    return NULL;
}

static void start_scan(int kind, const char *query, int cat)
{
    if (ui.scan_busy) return;
    scan_kind = kind;
    ui.scan_cat = cat;
    snprintf(scan_query, sizeof scan_query, "%s", query ? query : "");
    if (kind == 2) snprintf(ui.scan_title, sizeof ui.scan_title, "random stations");
    else snprintf(ui.scan_title, sizeof ui.scan_title, "%s: %s", kind == 0 ? "genre" : "search", scan_query);
    scan_done = 0;
    ui.scan_busy = 1;
    ui.scan_results.count = 0;
    pthread_t t;
    if (pthread_create(&t, NULL, scan_thread, NULL) == 0) pthread_detach(t);
    else ui.scan_busy = 0;
}

/* ================================================================== notes, tempo */


/* ================================================================== Wi-Fi sync */

static int sc_mode(void) { return (int)synth_get_param(syn, P_SYNC); }
static float sc_bpm(void) { return synth_get_param(syn, P_BPM); }
static void sc_set_bpm(float b) { synth_set_param(syn, P_BPM, roundf(b * 10.0f) / 10.0f); }
static double sc_position(void) { SDL_LockAudio(); double p = synth_position(syn); SDL_UnlockAudio(); return p; }
static void sc_set_position(double p) { SDL_LockAudio(); synth_set_position(syn, p); SDL_UnlockAudio(); }
static void sc_set_rate(double r) { synth_set_clock_rate(syn, r); }
static int sc_playing(void) { return synth_running(syn); }
static void sc_set_playing(int on) { synth_set_running(syn, on); }
static const SyncClock sync_clock = {sc_mode, sc_bpm, sc_set_bpm, sc_position, sc_set_position,
                                     sc_set_rate, sc_playing, sc_set_playing};

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

static void tap_tempo(void)
{
    Uint32 now = SDL_GetTicks();
    if (sync_status()->following) { toast("following the lead unit's tempo"); return; }
    if (ui.ntaps > 0 && now - ui.taps[ui.ntaps - 1] > 2000) ui.ntaps = 0;
    if (ui.ntaps == 8) { memmove(ui.taps, ui.taps + 1, sizeof(Uint32) * 7); ui.ntaps = 7; }
    ui.taps[ui.ntaps++] = now;
    synth_tap_reset(syn);
    if (ui.ntaps >= 2) {
        float avg = (float)(ui.taps[ui.ntaps - 1] - ui.taps[0]) / (ui.ntaps - 1);
        synth_set_param(syn, P_BPM, roundf(60000.0f / avg));
        ui.dirty_settings = 1;
        toast("tempo %.0f bpm", synth_get_param(syn, P_BPM));
    } else {
        toast("tap again to set the tempo");
    }
}

/* ================================================================== on-screen keyboard */

static const char *const kb_rows[2][4] = {
    {"1234567890-=", "qwertyuiop/:", "asdfghjkl.?&", "zxcvbnm_~%#@"},
    {"!\"$'()*+,;[]", "QWERTYUIOP/:", "ASDFGHJKL.?&", "ZXCVBNM_~%#@"},
};
static const char *const kb_special[] = {"SHIFT", "SPACE", "DEL", "http://", "https://", ".com", "DONE"};
#define KB_SPECIAL_N 7

static void open_keyboard(int purpose, const char *initial)
{
    ui.overlay = OV_KEYBOARD;
    ui.kb_purpose = purpose;
    ui.kb_row = ui.kb_col = ui.kb_shift = 0;
    snprintf(ui.kb_text, sizeof ui.kb_text, "%s", initial ? initial : "");
}

static void kb_append(const char *s)
{
    size_t n = strlen(ui.kb_text);
    snprintf(ui.kb_text + n, sizeof ui.kb_text - n, "%s", s);
}

static void kb_done(void)
{
    if (ui.kb_purpose == KB_ADD_URL) {
        if (strncasecmp(ui.kb_text, "http://", 7) && strncasecmp(ui.kb_text, "https://", 8)) {
            toast("the URL must start with http:// or https://");
            return;
        }
        snprintf(ui.kb_url, sizeof ui.kb_url, "%s", ui.kb_text);
        const char *host = strstr(ui.kb_url, "://") + 3;
        char name[96];
        snprintf(name, sizeof name, "%.*s", (int)strcspn(host, "/:"), host);
        open_keyboard(KB_ADD_NAME, name);
        return;
    }
    if (ui.kb_purpose == KB_ADD_NAME) {
        int idx = stations_add(&stations, ui.kb_text[0] ? ui.kb_text : "custom", ui.kb_url, CAT_ANY);
        ui.overlay = OV_NONE;
        if (idx < 0) { toast("station list is full"); return; }
        save_stations();
        ui.radio_sel = idx;
        toast("added %s  (L2 sets voice/music/sound)", stations.items[idx].name);
        return;
    }
    ui.overlay = OV_SCANRESULTS;
    start_scan(1, ui.kb_text, CAT_ANY);
}

static void kb_press(void)
{
    if (ui.kb_row < 4) {
        char ch[2] = {kb_rows[ui.kb_shift][ui.kb_row][ui.kb_col], 0};
        kb_append(ch);
        return;
    }
    switch (ui.kb_col) {
    case 0: ui.kb_shift = !ui.kb_shift; break;
    case 1: kb_append(" "); break;
    case 2: { size_t n = strlen(ui.kb_text); if (n) ui.kb_text[n - 1] = 0; break; }
    case 3: kb_append("http://"); break;
    case 4: kb_append("https://"); break;
    case 5: kb_append(".com"); break;
    default: kb_done(); break;
    }
}

static void kb_input(int b)
{
    int cols = ui.kb_row < 4 ? 12 : KB_SPECIAL_N;
    switch (b) {
    case B_UP: ui.kb_row = (ui.kb_row + 4) % 5; break;
    case B_DOWN: ui.kb_row = (ui.kb_row + 1) % 5; break;
    case B_LEFT: ui.kb_col = (ui.kb_col + cols - 1) % cols; break;
    case B_RIGHT: ui.kb_col = (ui.kb_col + 1) % cols; break;
    case B_A: kb_press(); break;
    case B_B: { size_t n = strlen(ui.kb_text); if (n) ui.kb_text[n - 1] = 0; break; }
    case B_Y: kb_append(" "); break;
    case B_X: ui.kb_shift = !ui.kb_shift; break;
    case B_START: kb_done(); break;
    case B_MENU: case B_SELECT: ui.overlay = OV_NONE; break;
    }
    cols = ui.kb_row < 4 ? 12 : KB_SPECIAL_N;
    if (ui.kb_col >= cols) ui.kb_col = cols - 1;
}

static void draw_keyboard(void)
{
    fill(20, 56, 600, 384, C_PANEL);
    const char *title = ui.kb_purpose == KB_ADD_URL ? "add station - stream URL" :
                        ui.kb_purpose == KB_ADD_NAME ? "add station - name" : "search the directory by name";
    text(36, 66, title, C_LEAF, 0);
    fill(36, 94, 568, 30, C_BG);
    int len = (int)strlen(ui.kb_text);
    const char *shown = len > 54 ? ui.kb_text + len - 54 : ui.kb_text;
    int w = text(44, 100, shown, C_TEXT, 0);
    if ((SDL_GetTicks() / 400) & 1) fill(44 + w + 1, 100, 2, FONT_H, C_LEAF);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 12; c++) {
            int cx = 61 + c * 47, cy = 162 + r * 48;
            int sel = ui.kb_row == r && ui.kb_col == c;
            disc(cx, cy, 20, sel ? C_LEAF : C_PANEL2);
            char ch[2] = {kb_rows[ui.kb_shift][r][c], 0};
            text(cx - 4, cy - 9, ch, sel ? C_BG : C_TEXT, 0);
        }
    int x = 40;
    for (int c = 0; c < KB_SPECIAL_N; c++) {
        int wbtn = (int)strlen(kb_special[c]) * FONT_W + 20;
        int sel = ui.kb_row == 4 && ui.kb_col == c;
        Uint32 bg = sel ? C_LEAF : (c == 0 && ui.kb_shift) ? C_SEL : C_PANEL2;
        disc(x + 14, 370, 14, bg);
        disc(x + wbtn - 14, 370, 14, bg);
        fill(x + 14, 356, wbtn - 28, 29, bg);
        text(x + 10, 361, kb_special[c], sel ? C_BG : C_TEXT, 0);
        x += wbtn + 6;
    }
    text(36, 404, "A type  B del  X shift  Y space  START done  MENU cancel", C_DIM, 0);
}

/* ================================================================== scan overlays */

static void scan_menu_input(int b)
{
    int n = scan_genre_count + 2;
    switch (b) {
    case B_UP: ui.scan_menu_sel = (ui.scan_menu_sel + n - 1) % n; break;
    case B_DOWN: ui.scan_menu_sel = (ui.scan_menu_sel + 1) % n; break;
    case B_LEFT: ui.scan_menu_sel = ui.scan_menu_sel >= 10 ? ui.scan_menu_sel - 10 : 0; break;
    case B_RIGHT: ui.scan_menu_sel = ui.scan_menu_sel + 10 < n ? ui.scan_menu_sel + 10 : n - 1; break;
    case B_A:
        if (ui.scan_menu_sel == 0) { open_keyboard(KB_SEARCH, ""); break; }
        ui.overlay = OV_SCANRESULTS;
        if (ui.scan_menu_sel == 1) start_scan(2, NULL, CAT_ANY);
        else start_scan(0, scan_genres[ui.scan_menu_sel - 2].tag, scan_genres[ui.scan_menu_sel - 2].cat);
        break;
    case B_B: case B_MENU: case B_R2: ui.overlay = OV_NONE; break;
    }
}

static Uint32 cat_col(int cat)
{
    switch (cat) {
    case CAT_VOICE: return C_WATER;
    case CAT_MUSIC: return C_LEAF;
    case CAT_SOUND: return C_BARK;
    default: return C_PANEL2;
    }
}

static void draw_list_frame(const char *title, const char *hint)
{
    fill(20, 40, 600, 404, C_PANEL);
    text(36, 50, title, C_LEAF, 50);
    text(36, 418, hint, C_DIM, 58);
}

static void draw_scan_menu(void)
{
    draw_list_frame("scan the airwaves  (radio-browser.info)", "A open  B back    river = voices  bark = sound  leaf = music");
    int n = scan_genre_count + 2, rows = 15;
    if (ui.scan_menu_sel < ui.scan_menu_scroll) ui.scan_menu_scroll = ui.scan_menu_sel;
    if (ui.scan_menu_sel >= ui.scan_menu_scroll + rows) ui.scan_menu_scroll = ui.scan_menu_sel - rows + 1;
    for (int r = 0; r < rows && ui.scan_menu_scroll + r < n; r++) {
        int i = ui.scan_menu_scroll + r;
        int y = 76 + r * 22;
        if (i == ui.scan_menu_sel) fill(28, y - 2, 584, 22, C_SEL);
        if (i == 0) text(52, y, "search by name...", C_TEXT, 0);
        else if (i == 1) text(52, y, "random stations", C_TEXT, 0);
        else {
            disc(40, y + 8, 5, cat_col(scan_genres[i - 2].cat));
            text(52, y, scan_genres[i - 2].tag, i == ui.scan_menu_sel ? C_LEAF : C_TEXT, 0);
        }
    }
}

static void scan_results_input(int b)
{
    int n = ui.scan_results.count;
    Station *s = n ? &ui.scan_results.items[ui.scan_sel] : NULL;
    switch (b) {
    case B_UP: if (n) ui.scan_sel = (ui.scan_sel + n - 1) % n; break;
    case B_DOWN: if (n) ui.scan_sel = (ui.scan_sel + 1) % n; break;
    case B_LEFT: ui.scan_sel = ui.scan_sel >= 10 ? ui.scan_sel - 10 : 0; break;
    case B_RIGHT: if (n) ui.scan_sel = ui.scan_sel + 10 < n ? ui.scan_sel + 10 : n - 1; break;
    case B_A: if (s) { stream_play(st[0], s->url); ui.tuned[0] = -1; toast("OSC A hears %s", s->name); } break;
    case B_Y: if (s) { stream_play(st[1], s->url); ui.tuned[1] = -1; toast("OSC B hears %s", s->name); } break;
    case B_X:
        if (s) {
            int cat = ui.scan_cat != CAT_ANY ? ui.scan_cat : s->cat;
            int idx = stations_add(&stations, s->name, s->url, cat);
            if (idx >= 0) { save_stations(); toast("saved %s", s->name); }
            else toast("station list is full");
        }
        break;
    case B_B: case B_MENU: ui.overlay = OV_SCANMENU; break;
    }
}

static void draw_scan_results(void)
{
    draw_list_frame(ui.scan_title, "A try on OSC A   Y try on OSC B   X save   B back");
    if (ui.scan_busy) {
        /* five orbiting dots while the directory answers */
        float t = SDL_GetTicks() / 1000.0f;
        for (int i = 0; i < 5; i++) {
            int x, y;
            polar(320, 220, 30, t * 0.4f + i / 5.0f, &x, &y);
            disc(x, y, 4, ring_cols[i]);
        }
        text_center(320, 270, "listening to the directory...", C_DIM, 0);
        return;
    }
    int n = ui.scan_results.count, rows = 15;
    if (n == 0) { text_center(320, 220, scan_rc < 0 ? scan_err : "no MP3 stations found", C_DIM, 56); return; }
    if (ui.scan_sel < ui.scan_scroll) ui.scan_scroll = ui.scan_sel;
    if (ui.scan_sel >= ui.scan_scroll + rows) ui.scan_scroll = ui.scan_sel - rows + 1;
    for (int r = 0; r < rows && ui.scan_scroll + r < n; r++) {
        int i = ui.scan_scroll + r;
        int y = 76 + r * 22;
        Station *s = &ui.scan_results.items[i];
        if (i == ui.scan_sel) fill(28, y - 2, 584, 22, C_SEL);
        disc(40, y + 8, 5, cat_col(ui.scan_cat != CAT_ANY ? ui.scan_cat : s->cat));
        if (stations_find_url(&stations, s->url) >= 0) circle(40, y + 8, 8, C_TEXT);
        text(54, y, s->name, i == ui.scan_sel ? C_LEAF : C_TEXT, 33);
        text_right(604, y, s->info, C_DIM, 21);
    }
    char c[32];
    snprintf(c, sizeof c, "%d/%d", ui.scan_sel + 1, n);
    text_right(604, 50, c, C_DIM, 0);
}

/* ================================================================== shared widgets */

static Uint32 osc_col(int o) { return o ? C_AMBER : C_LEAF; }

static void osc_status_line(int o, char *buf, int n)
{
    char status[160], title[256];
    stream_status(st[o], status, sizeof status);
    stream_title(st[o], title, sizeof title);
    if (stream_state(st[o]) == ST_PLAYING && title[0]) snprintf(buf, n, "%s", title);
    else snprintf(buf, n, "%s", status);
}

static const char *osc_station_name(int o)
{
    int idx = station_param(o);
    if (ui.tuned[o] == -1 && stream_state(st[o]) != ST_IDLE) return "(trying a scan result)";
    if (idx >= 0 && idx < stations.count) return stations.items[idx].name;
    return "no station";
}

static Uint32 state_col(int o)
{
    switch (stream_state(st[o])) {
    case ST_PLAYING: return C_LEAF;
    case ST_ERROR: return C_RUST;
    case ST_IDLE: return C_PANEL2;
    default: return C_AMBER;
    }
}

static void draw_toast(void)
{
    if (SDL_GetTicks() > ui.toast_until) return;
    int w = (int)strlen(ui.toast) * FONT_W + 32;
    if (w > 620) w = 620;
    int x = 320 - w / 2;
    disc(x + 15, 420, 15, C_TEXT);
    disc(x + w - 15, 420, 15, C_TEXT);
    fill(x + 15, 405, w - 30, 31, C_TEXT);
    text(x + 16, 411, ui.toast, C_BG, 58);
}

static void draw_beat(int x, int y)
{
    float ph = synth_beat_phase(syn);
    char b[24];
    if (synth_running(syn)) {
        disc(x, y + 8, 6, ph < 0.15f ? C_TEXT : C_PANEL2);
        snprintf(b, sizeof b, "%.0f bpm", synth_get_param(syn, P_BPM));
    } else { /* stopped: a square, as on a transport */
        fill(x - 5, y + 3, 11, 11, C_RUST);
        snprintf(b, sizeof b, "stopped");
    }
    text(x + 14, y, b, synth_running(syn) ? C_DIM : C_RUST, 0);
    if (sc_mode() != SYNC_OFF) { /* Wi-Fi sync: ring = searching, filled = linked */
        const SyncStatus *ss = sync_status();
        const char *t = ss->leading ? "lead" : ss->following ? "follow" : "sync";
        int linked = ss->leading || ss->following;
        int w = (int)strlen(t) * FONT_W;
        int bx = x - w - 26;
        if (linked) disc(bx, y + 8, 5, C_WATER);
        else circle(bx, y + 8, 5, C_PANEL2);
        text(bx + 10, y, t, linked ? C_WATER : C_PANEL2, 0);
    }
}

/* voice sense as five dots: river-blue for speech, leaf for music */
static void draw_voice_dots(int cx, int y, int o)
{
    float sp = synth_speechness(syn, o);
    int vs = synth_voice_state(syn, o);
    int lit = (int)(sp * 5 + 0.5f);
    for (int i = 0; i < 5; i++) {
        int x = cx - 32 + i * 16;
        if (vs == VS_SILENT) circle(x, y, 4, C_PANEL2);
        else if (i < lit) disc(x, y, 4, C_WATER);
        else disc(x, y, 4, C_PANEL2);
    }
}

/* ================================================================== PLAY screen */

/* Abstract sound forms. Each tap of the signal chain drives a "bloom": its core swells with loudness,
 * brightness sets how many petals it has (3, 5, 8, 13, 21) and how fast it turns, a spiky signal gives
 * sharp petals, and sudden attacks send out ripples. No waveform is drawn literally. */
typedef struct { float r, a; } Ripple;
typedef struct {
    float level, peak_env, slow;
    float bright, rough, spin;
    Ripple rip[5];
} Bloom;
static Bloom blooms[TAP_COUNT];

static void bloom_update(int tap, float dir)
{
    float rms, br, cr;
    synth_tap_features(syn, tap, &rms, &br, &cr);
    Bloom *b = &blooms[tap];
    b->peak_env = rms > b->peak_env ? rms : b->peak_env * 0.985f;
    float lv = rms / (b->peak_env > 0.003f ? b->peak_env : 0.003f);
    if (rms < 0.0006f) lv *= rms / 0.0006f; /* true silence stays still */
    b->level += (lv - b->level) * 0.35f;
    b->bright += (br - b->bright) * 0.08f;
    float rough = (cr - 1.3f) / 2.5f;
    rough = rough < 0 ? 0 : rough > 1 ? 1 : rough;
    b->rough += (rough - b->rough) * 0.15f;
    b->spin += (0.004f + b->bright * 0.025f) * dir;
    if (rms > b->slow * 1.6f && rms > 0.002f) /* an attack: release a ripple */
        for (int i = 0; i < 5; i++)
            if (b->rip[i].a < 0.05f) { b->rip[i].r = 0; b->rip[i].a = 1; break; }
    b->slow += (rms - b->slow) * 0.05f;
    for (int i = 0; i < 5; i++) {
        b->rip[i].r += 0.05f;
        b->rip[i].a *= 0.88f;
    }
}

static void update_blooms(void)
{
    for (int t = 0; t < TAP_COUNT; t++) bloom_update(t, (t & 1) ? -1.0f : 1.0f);
}

/* a bloom in the band rin..rout; ripples travel out to rmax. fold > 0 shapes the petals through the
 * folder, so folding is seen as petals bending and splitting. */
static void draw_bloom(int cx, int cy, float rin, float rout, float rmax, int tap, Uint32 col, float fold)
{
    static const int fib[5] = {3, 5, 8, 13, 21};
    const Bloom *b = &blooms[tap];
    for (int i = 0; i < 5; i++)
        if (b->rip[i].a > 0.05f) circle(cx, cy, (int)(rin + (rmax - rin) * b->rip[i].r), mix_col(C_BG, col, b->rip[i].a * 0.6f));
    float bn = b->bright / 1.1f;
    bn = bn < 0 ? 0 : bn > 1 ? 1 : bn;
    float fi = bn * (rout - rin > 30 ? 4.0f : 3.0f);
    int k0i = (int)fi, k1i = k0i + 1 > 4 ? 4 : k0i + 1;
    float w = fi - k0i;
    float lv = b->level > 1 ? 1 : b->level;
    float rc = rin + (rout - rin) * (0.12f + 0.38f * lv);
    float amp = (rout - rin) * (0.22f + 0.45f * b->rough) * (0.25f + 0.75f * lv);
    float sharp = 1.0f + b->rough * 3.0f;
    int x0 = 0, y0 = 0;
    const int N = 120;
    for (int i = 0; i <= N; i++) {
        float th = (float)(i % N) / N * 2.0f * (float)M_PI, g = 0;
        for (int pass = 0; pass < 2; pass++) {
            float k = (float)fib[pass ? k1i : k0i];
            float u = sinf(k * th + b->spin), gv;
            if (fold > 0.01f) gv = 0.5f + 0.5f * synth_fold_preview(u * 0.8f, fold);
            else gv = powf(0.5f + 0.5f * u, sharp);
            g += pass ? w * gv : (1 - w) * gv;
        }
        int x, y;
        polar(cx, cy, rc + amp * g, (float)(i % N) / N, &x, &y);
        if (i) line(x0, y0, x, y, col);
        x0 = x;
        y0 = y;
    }
}

/* ------------------------------------------------------------------ spectrum backdrop */

#define FFT_N 1024
#define SPEC_BANDS 64

static void fft(float *re, float *im, int n) /* in-place radix-2 */
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / len, wr = cosf(ang), wi = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                float t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

/* band powers (log-spaced 50 Hz .. 16 kHz) of a tap */
static void tap_bands(int tap, float *bands)
{
    static float re[FFT_N], im[FFT_N], win[FFT_N];
    if (win[1] == 0)
        for (int i = 0; i < FFT_N; i++) win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (FFT_N - 1));
    synth_tap_read(syn, tap, re, FFT_N);
    for (int i = 0; i < FFT_N; i++) { re[i] *= win[i]; im[i] = 0; }
    fft(re, im, FFT_N);
    static int edge[SPEC_BANDS + 1];
    if (!edge[SPEC_BANDS]) { /* FFT bins at the band edges, computed once */
        float hz_per_bin = (float)audio_rate / FFT_N;
        for (int b = 0; b <= SPEC_BANDS; b++) {
            int k = (int)(50.0f * powf(320.0f, (float)b / SPEC_BANDS) / hz_per_bin);
            if (b && k <= edge[b - 1]) k = edge[b - 1] + 1;
            edge[b] = k > FFT_N / 2 ? FFT_N / 2 : k;
        }
    }
    for (int b = 0; b < SPEC_BANDS; b++) {
        int k0 = edge[b], k1 = edge[b + 1] > k0 ? edge[b + 1] : k0 + 1;
        float pw = 0;
        for (int k = k0; k < k1; k++) pw += re[k] * re[k] + im[k] * im[k];
        bands[b] = pw / (k1 - k0);
    }
}

/* Soft spectrum behind the circles, mirrored around the middle of the screen. Height follows the output;
 * colour shows which voice is strongest in each band: leaf for A, amber for B. */
static void draw_spectrum(void)
{
    static float height[SPEC_BANDS], mixab[SPEC_BANDS];
    float out[SPEC_BANDS], a[SPEC_BANDS], b[SPEC_BANDS];
    tap_bands(TAP_OUT, out);
    tap_bands(TAP_A, a);
    tap_bands(TAP_B, b);
    float w = (float)SCREEN_W / SPEC_BANDS;
    for (int i = 0; i < SPEC_BANDS; i++) {
        float db = 10.0f * log10f(out[i] + 1e-12f);       /* about -110 .. +30 */
        float h = (db + 60.0f) / 80.0f;
        h = h < 0 ? 0 : h > 1 ? 1 : h;
        height[i] = h > height[i] ? h : height[i] * 0.9f; /* rises fast, falls gently */
        float t = b[i] / (a[i] + b[i] + 1e-12f);
        mixab[i] += (t - mixab[i]) * 0.3f;
        int hp = (int)(height[i] * 130);
        if (hp < 1) continue;
        Uint32 col = mix_col(C_BG, mix_col(C_LEAF, C_AMBER, mixab[i]), 0.08f + 0.12f * height[i]); /* a quiet backdrop */
        int x = (int)(i * w);
        fill(x + 1, 240 - hp, (int)w - 2, hp * 2, col);
    }
}

/* PLAY layout: two voices with the cross stage between them, each wearing its rhythm as a ring of steps */
#define VOICE_Y 132
#define VOICE_R 88
#define VOICE_AX 108
#define VOICE_BX 532
#define CENTRE_R 76

static const char *seq_name(int q) { return q == SEQ_X ? "cross" : q == SEQ_B ? "B" : "A"; }
static Uint32 seq_col(int q) { return q == SEQ_X ? C_TEXT : osc_col(q); }

/* a rhythm, hi5 style: its steps around a circle, pulses lit, a white ring on the current step */
static void draw_seq_ring(int q, int cx, int cy, float r)
{
    const float *p = synth_params(syn);
    Uint32 col = seq_col(q);
    int on = (int)p[SEQ_PARAM(q, 0)], steps = (int)p[SEQ_PARAM(q, 1)];
    int pulses = (int)p[SEQ_PARAM(q, 2)], rot = (int)p[SEQ_PARAM(q, 3)];
    Uint32 cc = on ? col : mix_col(C_PANEL2, col, 0.3f);
    if (ui.cell == q) { circle(cx, cy, (int)r + 9, col); circle(cx, cy, (int)r + 10, col); }
    int cur = synth_seq_step(syn, q);
    float fl = synth_seq_flash(syn, q);
    for (int i = 0; i < steps; i++) {
        int dx, dy;
        polar(cx, cy, r, (float)i / steps, &dx, &dy);
        int hit = euclid_hit(steps, pulses, rot, i), now = i == cur && on;
        if (hit) disc(dx, dy, now ? 4 + (int)(fl * 2) : 4, now ? mix_col(cc, C_TEXT, fl) : cc);
        else disc(dx, dy, 1, C_PANEL2);
        if (now) circle(dx, dy, 6, C_TEXT);
    }
}

static void seq_label(int q, char *t, int n)
{
    const float *p = synth_params(syn);
    if ((int)p[SEQ_PARAM(q, 0)]) snprintf(t, n, "%d/%d", (int)p[SEQ_PARAM(q, 2)], (int)p[SEQ_PARAM(q, 1)]);
    else snprintf(t, n, "open");
}

/* a voice: faint bloom of the raw broadcast inside, bright bloom of the voice as it sounds outside,
 * and its rhythm around the rim */
static void draw_voice(int o, int cx, int cy, int R)
{
    Uint32 col = osc_col(o);
    const float *p = synth_params(syn);
    float gate = synth_gate_value(syn, o);
    disc(cx, cy, R, C_PANEL);
    circle(cx, cy, R, mix_col(C_PANEL2, col, gate * synth_osc_level(syn, o) * 2.0f));
    if (synth_floor(syn) == o) { circle(cx, cy, R + 1, col); circle(cx, cy, R + 2, col); } /* converse: has the floor */
    draw_bloom(cx, cy, R * 0.26f, R * 0.48f, R * 0.5f, o ? TAP_RAW_B : TAP_RAW_A, mix_col(C_PANEL, col, 0.45f), 0);
    draw_bloom(cx, cy, R * 0.52f, R * 0.92f, R + 6, o ? TAP_B : TAP_A, col, 0);
    draw_seq_ring(o, cx, cy, R - 7);
    static const char *const mn[] = {"live", "loop", "wave", "grain"};
    text_center(cx, cy - 20, mn[(int)p[P_A_MODE + o * OSC_STRIDE]], C_TEXT, 0);
    disc(cx, cy + 4, 4, state_col(o));
    char t[16];
    seq_label(o, t, sizeof t);
    text_center(cx, cy + 12, t, (int)p[SEQ_PARAM(o, 0)] ? C_DIM : C_PANEL2, 0);
}

/* five circles along the chain after the voices; each grows the more it changes the sound */
static const char *const stage_names[5] = {"cross", "filter", "fold", "echo", "space"};
static const int stage_taps[5] = {TAP_CROSS, TAP_FILTER, TAP_FOLD, TAP_ECHO, TAP_OUT};

static float stage_amount(int i)
{
    const float *p = synth_params(syn);
    switch (i) {
    case 0: return synth_cross_amount(syn);
    case 1: { /* filter and bass / treble */
        float f = fabsf(p[P_TONE]) > 0.02f ? fabsf(p[P_TONE]) + p[P_RESO] * 0.2f : 0;
        float eq = (fabsf(p[P_LOW]) + fabsf(p[P_HIGH])) / 24.0f;
        return fminf(1.0f, f > eq ? f : eq);
    }
    case 2: return fmaxf(p[P_FOLD], fmaxf(p[P_DRIVE], p[P_CRUSH])); /* drive, fold, crush */
    case 3: return p[P_ECHO];
    default: return fminf(1.0f, fmaxf(p[P_SPACE], fabsf(p[P_WIDTH] - 1.0f)));
    }
}

static void draw_stages(int x0, int y, int spacing, float rmin, float rmax, int highlight, int labels)
{
    for (int i = 0; i < 5; i++) {
        int cx = x0 + i * spacing;
        float amt = stage_amount(i);
        float R = rmin + (rmax - rmin) * amt;
        if (i < 4) line(cx + (int)R + 3, y, cx + spacing - (int)(rmin + (rmax - rmin) * stage_amount(i + 1)) - 3, y, C_PANEL2);
        disc(cx, y, (int)R, C_PANEL);
        Uint32 col = amt > 0.01f ? mix_col(C_DIM, C_TEXT, amt) : C_PANEL2;
        circle(cx, y, (int)R, i == highlight ? C_TEXT : mix_col(C_PANEL2, C_LEAF, amt));
        if (i == highlight) circle(cx, y, (int)R + 4, C_TEXT);
        draw_bloom(cx, y, R * 0.2f, R * 0.92f, R, stage_taps[i], col, i == 2 ? synth_get_param(syn, P_FOLD) : 0);
        if (labels) text_center(cx, y + (int)rmax + 6, stage_names[i], amt > 0.01f ? C_DIM : C_PANEL2, 0);
    }
}

/* centre: the final sound with the cross rhythm around it, the morph position travelling over the top from
 * A (left) to B (right), and underneath, in words, what the cross stage is doing to the sound right now */
static void draw_centre(int cx, int cy)
{
    const float *p = synth_params(syn);
    int on = (int)p[P_X_GATE_ON];
    float open = synth_cross_gate(syn) * fminf(p[P_CROSS_LEVEL], 1.0f); /* the cross rhythm and the MIX return */
    float amount = synth_cross_amount(syn);
    disc(cx, cy, CENTRE_R, C_PANEL);
    circle(cx, cy, CENTRE_R, mix_col(C_PANEL2, C_TEXT, amount * open));
    draw_bloom(cx, cy, 12, CENTRE_R - 16, CENTRE_R + 20, TAP_OUT, C_TEXT, 0);
    draw_seq_ring(SEQ_X, cx, cy, CENTRE_R - 7);
    text_center(cx, cy - 20, "cross", amount > 0.01f ? C_TEXT : C_DIM, 0);
    char t[16];
    seq_label(SEQ_X, t, sizeof t);
    text_center(cx, cy + 12, t, on ? C_DIM : C_PANEL2, 0);

    float m = p[P_MORPH];
    int ar = CENTRE_R + 20;
    arc(cx, cy, ar, 2, 0.75f, 1.25f, C_PANEL2);
    int mx, my;
    polar(cx, cy, ar, 0.75f + 0.5f * m, &mx, &my);
    disc(mx, my, m > 0.01f ? 6 : 4, m > 0.01f ? mix_col(C_LEAF, C_AMBER, m) : C_PANEL2);

    /* one line per active effect: a bar for how much it is applied now, and what it does */
    struct { float amt, now; Uint32 col; char what[24]; } fx[5];
    int n = 0;
    int voc = (int)p[P_VOCODE];
    if (voc && p[P_VOCODE_AMT] > 0.01f) {
        fx[n].amt = p[P_VOCODE_AMT]; fx[n].now = fx[n].amt * open; fx[n].col = voc == VOC_A_TO_B ? C_LEAF : C_AMBER;
        snprintf(fx[n++].what, sizeof fx[0].what, voc == VOC_A_TO_B ? "A talks thru B" : "B talks thru A");
    }
    if (p[P_MORPH] > 0.01f) {
        fx[n].amt = p[P_MORPH]; fx[n].now = fx[n].amt * open; fx[n].col = mix_col(C_LEAF, C_AMBER, m);
        snprintf(fx[n++].what, sizeof fx[0].what, "morph %.0f%% to B", m * 100);
    }
    if (p[P_FM] > 0.01f) {
        int ab = (int)p[P_FM_DIR] == FM_A_TO_B;
        fx[n].amt = p[P_FM]; fx[n].now = fx[n].amt * synth_cross_gate(syn); fx[n].col = ab ? C_LEAF : C_AMBER;
        snprintf(fx[n++].what, sizeof fx[0].what, ab ? "A bends B pitch" : "B bends A pitch");
    }
    if (p[P_RING] > 0.01f) {
        fx[n].amt = p[P_RING]; fx[n].now = fx[n].amt * open; fx[n].col = C_TEXT;
        snprintf(fx[n++].what, sizeof fx[0].what, "ring A x B");
    }
    if (p[P_CONVERSE] > 0.01f) {
        int f = synth_floor(syn);
        fx[n].amt = p[P_CONVERSE]; fx[n].now = fx[n].amt; fx[n].col = f == 1 ? C_AMBER : C_LEAF;
        snprintf(fx[n++].what, sizeof fx[0].what, "%c talks, %c waits", f == 1 ? 'B' : 'A', f == 1 ? 'A' : 'B');
    }
    int y = cy + CENTRE_R + 12, bx = cx - 92;
    if (n == 0) { text_center(cx, y, "no cross-mod", C_PANEL2, 0); return; }
    for (int i = 0; i < n && i < 4; i++, y += 20) {
        fill(bx, y + 6, 36, 6, C_PANEL);
        fill(bx, y + 6, (int)(36 * fminf(fx[i].amt, 1.0f)), 6, mix_col(C_PANEL, fx[i].col, 0.35f)); /* set */
        fill(bx, y + 6, (int)(36 * fminf(fx[i].now, 1.0f)), 6, fx[i].col);                          /* now */
        text(bx + 44, y, fx[i].what, fx[i].now > 0.02f ? C_TEXT : C_DIM, 15);
    }
}

static void draw_play(void)
{
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG);
    draw_spectrum();
    text(16, 8, "radioscan", C_DIM, 0);
    draw_beat(520, 8);
    char info[48];
    snprintf(info, sizeof info, "pitch %+d", ui.octave);
    text(126, 8, info, C_DIM, 0);

    draw_voice(0, VOICE_AX, VOICE_Y, VOICE_R);
    draw_voice(1, VOICE_BX, VOICE_Y, VOICE_R);
    draw_centre(320, VOICE_Y);
    for (int o = 0; o < 2; o++) {
        int cx = o ? VOICE_BX : VOICE_AX;
        draw_voice_dots(cx, VOICE_Y + VOICE_R + 16, o);
        text_center(cx, VOICE_Y + VOICE_R + 30, osc_station_name(o), C_TEXT, 19);
        char line_[256];
        osc_status_line(o, line_, sizeof line_);
        text_center(cx, VOICE_Y + VOICE_R + 48, line_, C_DIM, 19);
    }
    if (SDL_GetTicks() < ui.grab_flash)
        for (int o = 0; o < 2; o++)
            if (ui.grab_osc < 0 || ui.grab_osc == o) text_center(o ? VOICE_BX : VOICE_AX, VOICE_Y + 34, "grabbed", C_TEXT, 0);

    int hl = -1;
    if (ui.held[B_R2]) {
        if (ui.held[B_UP] || ui.held[B_DOWN]) hl = 1;
        if (ui.held[B_X] || ui.held[B_B]) hl = 2;
        if (ui.held[B_Y] || ui.held[B_A]) hl = 4;
        if (ui.held[B_LEFT] || ui.held[B_RIGHT]) hl = 0;
    }
    draw_stages(72, 386, 124, 16, 34, hl, 1);

    const char *hint = ui.held[B_R2] ? "dpad: filter / morph   X B: fold   Y A: space" :
                       ui.held[B_L2] ? "^v steps  <> rate  Y X length  A start/stop" :
                                       "<> rhythm  ^v pulses  A on  B fewer  X rotate  Y new";
    text_center(320, 322, hint, C_DIM, 0);
    text(16, 455, "L1/R1 pitch  L2 grab  SELECT tap  START edit  MENU x2 quit", C_PANEL2, 0);
}

/* ================================================================== EDIT screen */

static int tab_page(int tab) { return tab - 1; } /* VOICES, RHYTHM, ENGINE, MIX -> PAGE_* */

/* rows for the current tab; ids >= 0 are params, ROW_HEADER rows carry a section name */
static int build_rows(int tab, int *rows, const char **hdrs)
{
    int n = 0;
    const char *last = NULL;
    int page = tab_page(tab);
    for (int i = 0; i < P_COUNT; i++) {
        if (param_info[i].page != page) continue;
        const char *sec = param_section(i);
        if (!last || strcmp(sec, last)) { rows[n] = ROW_HEADER; hdrs[n++] = sec; last = sec; }
        rows[n] = i;
        hdrs[n++] = NULL;
    }
    return n;
}

static void draw_tabs(void)
{
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = 30 + i * 124;
        int sel = ui.tab == i;
        if (sel) disc(x, 16, 7, C_LEAF);
        else circle(x, 16, 7, C_DIM);
        text(x + 14, 7, tab_names[i], sel ? C_TEXT : C_DIM, 0);
    }
}

static void draw_radio_page(void)
{
    int rows = 13;
    char b[160];
    snprintf(b, sizeof b, "%d stations", stations.count);
    text(24, 38, b, C_DIM, 0);
    text_right(624, 38, "A/B to voice  Y add  X del  L2 kind  R2 scan", C_PANEL2, 0);
    if (stations.count == 0) { text_center(320, 180, "no stations yet - Y adds a URL, R2 scans the directory", C_DIM, 0); return; }
    if (ui.radio_sel >= stations.count) ui.radio_sel = stations.count - 1;
    if (ui.radio_sel < ui.radio_scroll) ui.radio_scroll = ui.radio_sel;
    if (ui.radio_sel >= ui.radio_scroll + rows) ui.radio_scroll = ui.radio_sel - rows + 1;
    for (int r = 0; r < rows && ui.radio_scroll + r < stations.count; r++) {
        int i = ui.radio_scroll + r;
        int y = 62 + r * 22;
        Station *s = &stations.items[i];
        if (i == ui.radio_sel) fill(8, y - 2, 624, 22, ui.confirm_delete ? RGB(80, 34, 24) : C_SEL);
        disc(24, y + 8, 5, cat_col(s->cat));
        text(40, y, s->name, i == ui.radio_sel ? C_TEXT : C_DIM, 48);
        for (int o = 0; o < 2; o++)
            if (station_param(o) == i) {
                disc(584 + o * 26, y + 8, 9, osc_col(o));
                text(580 + o * 26, y, o ? "B" : "A", C_BG, 0);
            }
    }
    Station *s = &stations.items[ui.radio_sel];
    if (ui.confirm_delete) text(24, 352, "press X again to let this station go", C_RUST, 0);
    else {
        snprintf(b, sizeof b, "%s  %s", category_names[s->cat], s->url);
        text(24, 352, b, C_PANEL2, 60);
    }
}

static void draw_param_row(int id, int x, int y, int knob_x, int val_x, int val_chars, int selected)
{
    const ParamInfo *pi = &param_info[id];
    const float *p = synth_params(syn);
    text(x, y, param_label(id, p), selected ? C_TEXT : C_DIM, (knob_x - x - 16) / FONT_W);
    int ky = y + 8;
    char val[128];
    if (!strcmp(pi->unit, "station")) {
        int idx = (int)p[id], o = id == P_A_STATION ? 0 : 1;
        snprintf(val, sizeof val, "%s", idx >= 0 && idx < stations.count ? stations.items[idx].name : "no station");
        if (ui.pending_station[o] >= -1) strcat(val, " ...");
        disc(knob_x, ky, 6, state_col(o));
        text(val_x, y, val, selected ? C_TEXT : C_DIM, val_chars);
        return;
    }
    param_format_ctx(id, p, val, sizeof val);
    Uint32 kc = selected ? C_LEAF : C_DIM;
    if (id >= P_A_STATION && id <= P_B_CHANCE) kc = selected ? osc_col(id >= P_B_STATION) : C_DIM;
    if (pi->choices) {
        int n = (int)pi->max + 1, v = (int)p[id];
        circle(knob_x, ky, 8, C_PANEL2); /* choices: a dot orbiting to its position */
        int dx, dy;
        polar(knob_x, ky, 8, (float)v / n, &dx, &dy);
        disc(dx, dy, 3, kc);
    } else {
        float f;
        if (pi->log) f = logf(p[id] / pi->min) / logf(pi->max / pi->min);
        else f = (p[id] - pi->min) / (pi->max - pi->min);
        knob(knob_x, ky, 9, f, pi->min < 0 && !pi->log, kc);
    }
    text(val_x, y, val, selected ? C_TEXT : C_DIM, val_chars);
}

static void draw_param_page(void)
{
    int rows[256];
    const char *hdrs[256];
    int n = build_rows(ui.tab, rows, hdrs);
    int *sel = &ui.sel[ui.tab], *scroll = &ui.scroll[ui.tab];
    if (*sel >= n) *sel = n - 1;
    while (*sel > 0 && rows[*sel] == ROW_HEADER) (*sel)--;
    if (rows[*sel] == ROW_HEADER) (*sel)++;
    int x = 24, knob_x = 300, val_x = 320;
    int val_chars = (636 - val_x) / FONT_W;
    int visible = 14;
    if (*sel < *scroll + 1) *scroll = *sel - 1;
    if (*sel >= *scroll + visible - 1) *scroll = *sel - visible + 2;
    if (*scroll > n - visible) *scroll = n - visible;
    if (*scroll < 0) *scroll = 0;
    for (int r = 0; r < visible && *scroll + r < n; r++) {
        int i = *scroll + r;
        int y = 40 + r * 22;
        if (rows[i] == ROW_HEADER) {
            text(x, y + 2, hdrs[i], C_LEAF, 0);
            fill(x + (int)strlen(hdrs[i]) * FONT_W + 10, y + 11, 636 - x - (int)strlen(hdrs[i]) * FONT_W - 20, 1, C_PANEL2);
            continue;
        }
        if (i == *sel) fill(x - 10, y - 2, 640 - x, 22, C_SEL);
        draw_param_row(rows[i], x, y, knob_x, val_x, val_chars, i == *sel);
    }
    if (n > visible) { /* scroll position as an arc */
        float a = (float)*scroll / (n - visible);
        arc(632, 200, 6, 2, 0.0f, 1.0f, C_PANEL2);
        int dx, dy;
        polar(632, 200, 6, a, &dx, &dy);
        disc(dx, dy, 2, C_DIM);
    }
}

/* ------------------------------------------------------------------ MIX page: a fader for every level */

/* one control on the MIX page: a fader, or a knob under its strip's fader (the voices' drive) */
typedef struct { int id, meter, group, strip, knob; const char *name; } MixStrip;
enum { MG_A, MG_B, MG_FX, MG_MASTER, MG_COUNT };
static const char *const mix_group_names[MG_COUNT] = {"VOICE A", "VOICE B", "EFFECTS", "MASTER"};
static const MixStrip mix_strips[] = { /* in Left / Right order */
    {P_A_LEVEL, MIX_A, MG_A, 0, 0, "level"},           {P_A_DRIVE, -1, MG_A, 0, 1, "drive"},
    {P_B_LEVEL, MIX_B, MG_B, 1, 0, "level"},           {P_B_DRIVE, -1, MG_B, 1, 1, "drive"},
    {P_CROSS_LEVEL, MIX_CROSS, MG_FX, 2, 0, "cross"},  {P_SHAPE_LEVEL, MIX_SHAPE, MG_FX, 3, 0, "shape"},
    {P_ECHO_LEVEL, MIX_ECHO, MG_FX, 4, 0, "echo"},     {P_SPACE_LEVEL, MIX_SPACE, MG_FX, 5, 0, "space"},
    {P_LIMIT, MIX_LIMIT, MG_MASTER, 6, 0, "limit"},    {P_VOLUME, MIX_OUT, MG_MASTER, 7, 0, "volume"},
};
#define MIX_N ((int)(sizeof mix_strips / sizeof mix_strips[0]))
#define MIX_STRIPS 8
#define MIX_W 70      /* strip width; the two voice strips are wider */
#define MIX_W_VOICE 94
#define MIX_TOP 84
#define MIX_BOT 236
#define MIX_KNOB_Y 288

static int mix_strip_x(int strip) { return 22 + (strip < 2 ? strip * MIX_W_VOICE : 2 * MIX_W_VOICE + (strip - 2) * MIX_W); }
static int mix_strip_w(int strip) { return strip < 2 ? MIX_W_VOICE : MIX_W; }

static Uint32 mix_group_col(int g) { return g == MG_A ? C_LEAF : g == MG_B ? C_AMBER : g == MG_FX ? C_WATER : C_TEXT; }

static float mix_frac(int id, float v)
{
    const ParamInfo *pi = &param_info[id];
    float f = (v - pi->min) / (pi->max - pi->min);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

/* short value for under a control (the full one is shown for the selected control) */
static void mix_short_value(int id, float v, char *b, int n)
{
    const char *unit = param_info[id].unit;
    if (!strcmp(unit, "lvl")) { if (v < 0.001f) snprintf(b, n, "off"); else snprintf(b, n, "%+.1f", 20.0f * log10f(v)); }
    else if (!strcmp(unit, "limit")) { if (v < 0.01f) snprintf(b, n, "off"); else snprintf(b, n, "%.1f", -18.0f * v); }
    else if (!strcmp(unit, "dB")) snprintf(b, n, "%+.0f", v);
    else snprintf(b, n, "%.0f%%", v * 100);
}

static void draw_mix_fader(const MixStrip *m, int cx, int sel, const float *p)
{
    Uint32 col = mix_group_col(m->group);
    int h = MIX_BOT - MIX_TOP;
    int tx = cx - 6; /* the track, with the meter to its right */
    fill(tx - 1, MIX_TOP, 3, h, C_PANEL2);
    float f = mix_frac(m->id, p[m->id]);
    int ty = MIX_BOT - (int)(f * h);
    fill(tx - 1, ty, 3, MIX_BOT - ty, sel ? col : mix_col(C_PANEL2, col, 0.6f));
    float unity = !strcmp(param_info[m->id].unit, "lvl") ? mix_frac(m->id, 1.0f) :
                  m->id == P_VOLUME ? mix_frac(m->id, 0.0f) : -1;
    if (unity >= 0) { int uy = MIX_BOT - (int)(unity * h); fill(tx - 9, uy, 4, 1, C_DIM); }
    fill(tx - 11, ty - 4, 23, 9, sel ? C_TEXT : C_DIM); /* thumb */
    fill(tx - 9, ty, 19, 1, C_BG);
    if (m->meter >= 0) {
        float mv = synth_mix_meter(syn, m->meter);
        int mx = cx + 10;
        fill(mx, MIX_TOP, 5, h, C_PANEL);
        if (m->meter == MIX_LIMIT) { /* gain reduction grows down from the top */
            int gh = (int)(fminf(mv * 2.0f, 1.0f) * h);
            fill(mx, MIX_TOP, 5, gh, C_RUST);
        } else {
            float db = 20.0f * log10f(mv + 1e-6f), mf = (db + 48.0f) / 48.0f; /* -48 .. 0 dB */
            mf = mf < 0 ? 0 : mf > 1 ? 1 : mf;
            int mh = (int)(mf * h);
            fill(mx, MIX_BOT - mh, 5, mh, db > -1.0f ? C_RUST : db > -9.0f ? C_AMBER : col);
        }
    }
    char v[24];
    mix_short_value(m->id, p[m->id], v, sizeof v);
    text_center(cx, MIX_BOT + 8, v, sel ? C_TEXT : C_DIM, 7);
}

static void draw_mix_knob(const MixStrip *m, int cx, int sel, const float *p)
{
    Uint32 col = mix_group_col(m->group);
    int w = mix_strip_w(m->strip);
    if (sel) fill(cx - w / 2 + 2, MIX_KNOB_Y - 26, w - 4, 66, C_SEL);
    text_center(cx, MIX_KNOB_Y - 24, m->name, sel ? C_TEXT : C_DIM, 7);
    knob(cx, MIX_KNOB_Y + 4, 13, mix_frac(m->id, p[m->id]), 0, sel ? col : C_DIM);
    char v[24];
    mix_short_value(m->id, p[m->id], v, sizeof v);
    text_center(cx, MIX_KNOB_Y + 20, v, sel ? C_TEXT : C_DIM, 7);
}

static void draw_mix_page(void)
{
    const float *p = synth_params(syn);
    if (ui.mix_sel < 0 || ui.mix_sel >= MIX_N) ui.mix_sel = 0;
    /* group headings over their strips */
    for (int g = 0; g < MG_COUNT; g++) {
        int first = -1, last = -1;
        for (int i = 0; i < MIX_N; i++)
            if (mix_strips[i].group == g) { if (first < 0) first = mix_strips[i].strip; last = mix_strips[i].strip; }
        int x0 = mix_strip_x(first) + 4, x1 = mix_strip_x(last) + mix_strip_w(last) - 4;
        Uint32 col = mix_group_col(g);
        text_center((x0 + x1) / 2, 36, mix_group_names[g], mix_col(C_DIM, col, 0.6f), 0);
        fill(x0, 58, x1 - x0, 1, mix_col(C_PANEL2, col, 0.4f));
    }
    const MixStrip *cur = &mix_strips[ui.mix_sel];
    for (int i = 0; i < MIX_N; i++) { /* the selected fader's strip is lit */
        const MixStrip *m = &mix_strips[i];
        if (m->knob) continue;
        int x = mix_strip_x(m->strip), w = mix_strip_w(m->strip), cx = x + w / 2, sel = m == cur;
        if (sel) fill(x + 2, 62, w - 4, MIX_BOT - 62 + 30, C_SEL);
        text_center(cx, 62, m->name, sel ? C_TEXT : C_DIM, 7);
        draw_mix_fader(m, cx, sel, p);
    }
    for (int i = 0; i < MIX_N; i++)
        if (mix_strips[i].knob)
            draw_mix_knob(&mix_strips[i], mix_strip_x(mix_strips[i].strip) + mix_strip_w(mix_strips[i].strip) / 2,
                          &mix_strips[i] == cur, p);
    char full[64], line_[160];
    param_format_ctx(cur->id, p, full, sizeof full);
    snprintf(line_, sizeof line_, "%s %s  %s", mix_group_names[cur->group], cur->name, full);
    for (char *c = line_; *c && c < line_ + 8; c++) *c = (char)tolower((unsigned char)*c);
    text(24, 340, line_, C_TEXT, 30);
    text_right(624, 340, "<> choose  ^v set  hold B fine", C_PANEL2, 0);
}

/* which of the five stages the selected setting acts on (-1: a voice or nothing) */
static int selected_stage(void)
{
    if (ui.tab == TAB_MIX) {
        static const int st[] = {[P_CROSS_LEVEL - P_CROSS_LEVEL] = 0, [P_SHAPE_LEVEL - P_CROSS_LEVEL] = 2,
                                 [P_ECHO_LEVEL - P_CROSS_LEVEL] = 3, [P_SPACE_LEVEL - P_CROSS_LEVEL] = 4};
        int id = mix_strips[ui.mix_sel].id;
        return id >= P_CROSS_LEVEL && id <= P_SPACE_LEVEL ? st[id - P_CROSS_LEVEL] : -1;
    }
    if (ui.tab != TAB_ENGINE) return -1;
    int rows[256];
    const char *hdrs[256];
    int n = build_rows(ui.tab, rows, hdrs);
    int sel = ui.sel[ui.tab];
    if (sel < 0 || sel >= n || rows[sel] < 0) return -1;
    int id = rows[sel];
    if (id >= P_VOCODE && id <= P_CONVERSE) return 0;
    if (id == P_TONE || id == P_RESO || id == P_LOW || id == P_HIGH) return 1;
    if (id == P_DRIVE || id == P_FOLD || id == P_CRUSH) return 2;
    if (id == P_ECHO) return 3;
    if (id == P_SPACE || id == P_WIDTH || id == P_VOLUME) return 4;
    return -1;
}

static void draw_edit(void)
{
    fill(0, 0, SCREEN_W, SCREEN_H, C_BG);
    draw_tabs();
    if (ui.tab == TAB_RADIO) draw_radio_page();
    else if (ui.tab == TAB_MIX) draw_mix_page();
    else draw_param_page();

    /* the two voices, then the five stages, with the stage being edited ringed */
    for (int o = 0; o < 2; o++) {
        int cx = 30 + o * 64;
        disc(cx, 402, 26, C_PANEL);
        circle(cx, 402, 26, osc_col(o));
        draw_bloom(cx, 402, 6, 24, 30, o ? TAP_B : TAP_A, osc_col(o), 0);
    }
    draw_stages(168, 402, 64, 12, 26, selected_stage(), 0);
    draw_beat(440, 456);
    text(16, 456, ui.tab == TAB_MIX ? "L1/R1 page  Y reset  START play" : "L1/R1 page  Y dice  START play", C_PANEL2, 0);
}

/* ================================================================== input */

static void set_pitch(void)
{
    int note = 60 + 12 * ui.octave + (int)synth_get_param(syn, P_ROOT);
    synth_note_on(syn, note);
    synth_note_off(syn, note); /* the pitch stays on the last note */
}

/* a fresh slice from the stream for one voice (0, 1) or both (-1); LIVE voices have nothing to grab */
static void do_grab(int osc)
{
    synth_grab(syn, osc);
    ui.grab_osc = osc;
    ui.grab_flash = SDL_GetTicks() + 600;
    for (int o = 0; o < 2; o++)
        if ((osc < 0 || osc == o) && (int)synth_get_param(syn, P_A_MODE + o * OSC_STRIDE) == OSC_LIVE)
            toast(osc < 0 ? "grabbed (voice %c is LIVE: nothing to grab)" : "voice %c is LIVE: nothing to grab", 'A' + o);
}

/* PLAY: the selected rhythm's voice, or both when the cross rhythm is selected */
static int play_voice(void) { return ui.cell == SEQ_X ? -1 : ui.cell; }

static void hop(int o)
{
    int next = pool_next(o, 1);
    if (next >= 0) tune_now(o, next);
}

static void adjust_param(int id, int dir)
{
    int fine = ui.held[B_B];
    const ParamInfo *pi = &param_info[id];
    if (!strcmp(pi->unit, "station")) {
        int o = id == P_A_STATION ? 0 : 1;
        int idx = (int)synth_get_param(syn, id) + dir;
        if (idx < -1) idx = stations.count - 1;
        if (idx >= stations.count) idx = -1;
        tune_later(o, idx);
        return;
    }
    float before = synth_get_param(syn, id);
    float v = param_adjust(id, before, dir, fine);
    synth_set_param(syn, id, v);
    if ((id == P_A_MODE || id == P_B_MODE) && v != before) { /* pick a size that suits the new engine */
        static const float size_for[] = {250, 300, 5, 120};
        synth_set_param(syn, id + (P_A_SIZE - P_A_MODE), size_for[(int)v]);
    }
    if (id == P_ROOT) set_pitch();
    ui.dirty_settings = 1;
}

/* ---- rhythm cells on the PLAY page, played like hi5's pattern grid ---- */

/* start / stop the rhythms; starting begins again from the downbeat */
static void transport_toggle(void)
{
    if (sync_status()->following) { toast("the lead unit starts and stops the rhythms"); return; }
    SDL_LockAudio();
    int run = !synth_running(syn);
    if (run) synth_tap_reset(syn);
    synth_set_running(syn, run);
    SDL_UnlockAudio();
    toast(run ? "rhythms playing" : "rhythms stopped (voices with a rhythm are quiet)");
}

static void cell_say(int q)
{
    const float *p = synth_params(syn);
    char rate[24];
    param_format_ctx(SEQ_SHAPE(q, RATE), p, rate, sizeof rate);
    if (!(int)p[SEQ_PARAM(q, 0)]) toast(q == SEQ_X ? "cross rhythm off (cross always on)" : "%s rhythm off (always open)", seq_name(q));
    else toast("%s  E(%d,%d) >%d  %s", seq_name(q), (int)p[SEQ_PARAM(q, 2)], (int)p[SEQ_PARAM(q, 1)],
               (int)p[SEQ_PARAM(q, 3)], rate);
}

/* keep pulses and rotation inside the pattern */
static void cell_fix(int q)
{
    int st = (int)synth_get_param(syn, SEQ_PARAM(q, 1));
    if (synth_get_param(syn, SEQ_PARAM(q, 2)) > st) synth_set_param(syn, SEQ_PARAM(q, 2), (float)st);
    if (synth_get_param(syn, SEQ_PARAM(q, 3)) >= st) synth_set_param(syn, SEQ_PARAM(q, 3), (float)(st - 1));
}

static void cell_button(int b)
{
    static const int order[SEQ_COUNT] = {SEQ_A, SEQ_X, SEQ_B}; /* left to right on screen */
    int q = ui.cell;
    int on = SEQ_PARAM(q, 0), steps = SEQ_PARAM(q, 1), pulses = SEQ_PARAM(q, 2), rot = SEQ_PARAM(q, 3);
    int st = (int)synth_get_param(syn, steps);
    if (ui.held[B_L2]) { /* steps, rate and gate length */
        ui.l2_used = 1;
        if (b == B_UP || b == B_DOWN) { synth_set_param(syn, steps, st + (b == B_UP ? 1 : -1)); cell_fix(q); }
        if (b == B_LEFT || b == B_RIGHT) {
            int r = SEQ_SHAPE(q, RATE); /* right = faster = a shorter note value */
            synth_set_param(syn, r, synth_get_param(syn, r) + (b == B_RIGHT ? -1 : 1));
        }
        if (b == B_A) { transport_toggle(); return; }
        if (b == B_X || b == B_Y) {
            int l = SEQ_SHAPE(q, LENGTH);
            synth_set_param(syn, l, synth_get_param(syn, l) + (b == B_X ? 0.05f : -0.05f));
            char v[16];
            param_format_ctx(l, synth_params(syn), v, sizeof v);
            toast("%s gate length %s", seq_name(q), v);
            ui.dirty_settings = 1;
            return;
        }
    } else {
        int pos = 0;
        for (int i = 0; i < SEQ_COUNT; i++) if (order[i] == q) pos = i;
        switch (b) {
        case B_LEFT: ui.cell = order[(pos + SEQ_COUNT - 1) % SEQ_COUNT]; break;
        case B_RIGHT: ui.cell = order[(pos + 1) % SEQ_COUNT]; break;
        case B_UP: synth_set_param(syn, pulses, synth_get_param(syn, pulses) + 1); synth_set_param(syn, on, 1); break;
        case B_DOWN: synth_set_param(syn, pulses, synth_get_param(syn, pulses) - 1); break;
        case B_A: synth_set_param(syn, on, !(int)synth_get_param(syn, on)); break;
        case B_B: { /* fewer pulses, wrapping back to all of them (as on hi5) */
            int k = (int)synth_get_param(syn, pulses);
            synth_set_param(syn, pulses, (float)(k > 1 ? k - 1 : st));
            synth_set_param(syn, on, 1);
            break;
        }
        case B_X: synth_set_param(syn, rot, (float)(((int)synth_get_param(syn, rot) + 1) % st)); break;
        case B_Y: { /* a new random Euclidean pattern */
            static const int lens[] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 16, 16, 16};
            int n = lens[rand() % 12], kmax = n * 2 / 3 > 1 ? n * 2 / 3 : 1;
            synth_set_param(syn, steps, (float)n);
            synth_set_param(syn, pulses, (float)(1 + rand() % kmax));
            synth_set_param(syn, rot, (float)(rand() % n));
            synth_set_param(syn, on, 1);
            break;
        }
        default: return;
        }
    }
    cell_say(ui.cell);
    ui.dirty_settings = 1;
}

static void dice_section(void)
{
    int rows[256];
    const char *hdrs[256];
    int n = build_rows(ui.tab, rows, hdrs);
    int sel = ui.sel[ui.tab];
    if (sel >= n) return;
    int start = sel, end = sel;
    while (start > 0 && rows[start - 1] != ROW_HEADER) start--;
    while (end + 1 < n && rows[end + 1] != ROW_HEADER) end++;
    int ids[64], k = 0;
    for (int i = start; i <= end && k < 64; i++)
        if (rows[i] >= 0) ids[k++] = rows[i];
    synth_randomize(syn, ids, k, 1.0f);
    const char *name = start > 0 && rows[start - 1] == ROW_HEADER ? hdrs[start - 1] : "section";
    toast("rolled the dice on %s", name);
    ui.dirty_settings = 1;
}

/* EDIT: the voice the selected setting belongs to, or -1 (both) */
static int edit_voice(void)
{
    if (ui.tab == TAB_MIX) { int g = mix_strips[ui.mix_sel].group; return g == MG_A ? 0 : g == MG_B ? 1 : -1; }
    if (ui.tab == TAB_RADIO) return -1;
    int rows[256];
    const char *hdrs[256];
    int n = build_rows(ui.tab, rows, hdrs), sel = ui.sel[ui.tab];
    if (sel < 0 || sel >= n || rows[sel] < P_A_STATION || rows[sel] > P_B_CHANCE) return -1;
    return rows[sel] >= P_B_STATION;
}

static void edit_nav(int b)
{
    if (ui.tab == TAB_MIX) {
        if (b == B_LEFT) ui.mix_sel = (ui.mix_sel + MIX_N - 1) % MIX_N;
        if (b == B_RIGHT) ui.mix_sel = (ui.mix_sel + 1) % MIX_N;
        if (b == B_UP || b == B_DOWN) adjust_param(mix_strips[ui.mix_sel].id, b == B_UP ? 1 : -1);
        return;
    }
    if (ui.tab == TAB_RADIO) {
        int n = stations.count;
        if (b == B_UP && n) ui.radio_sel = (ui.radio_sel + n - 1) % n;
        if (b == B_DOWN && n) ui.radio_sel = (ui.radio_sel + 1) % n;
        if (b == B_LEFT) ui.radio_sel = ui.radio_sel >= 13 ? ui.radio_sel - 13 : 0;
        if (b == B_RIGHT && n) ui.radio_sel = ui.radio_sel + 13 < n ? ui.radio_sel + 13 : n - 1;
        ui.confirm_delete = 0;
        return;
    }
    int rows[256];
    const char *hdrs[256];
    int n = build_rows(ui.tab, rows, hdrs);
    int *sel = &ui.sel[ui.tab];
    if (b == B_UP || b == B_DOWN) {
        int d = b == B_UP ? -1 : 1;
        do { *sel = (*sel + d + n) % n; } while (rows[*sel] == ROW_HEADER);
        return;
    }
    if (rows[*sel] >= 0) adjust_param(rows[*sel], b == B_LEFT ? -1 : 1);
}

static void on_press(int b)
{
    Uint32 now = SDL_GetTicks();
    if (ui.overlay == OV_KEYBOARD) { kb_input(b); return; }
    if (ui.overlay == OV_SCANMENU) { scan_menu_input(b); return; }
    if (ui.overlay == OV_SCANRESULTS) { scan_results_input(b); return; }

    if (b == B_MENU) {
        if (ui.menu_armed && now - ui.menu_armed < 1500) { ui.running = 0; return; }
        ui.menu_armed = now;
        toast("press MENU again to leave");
        return;
    }
    if (ui.screen == SCR_PLAY && ui.held[B_R2] && b != B_R2) { /* sound gestures and dice */
        if (b == B_L1) { synth_dice(syn, DICE_RHYTHM); toast("new rhythms"); ui.dirty_settings = 1; }
        if (b == B_R1) { synth_dice(syn, DICE_SOUND); toast("new sound"); ui.dirty_settings = 1; }
        if (b == B_SELECT) { hop(0); hop(1); toast("hopping stations"); }
        return; /* d-pad and face buttons act continuously while held */
    }
    if (b == B_SELECT) { tap_tempo(); return; }
    if (b == B_START) {
        ui.screen = ui.screen == SCR_PLAY ? SCR_EDIT : SCR_PLAY;
        return;
    }

    if (ui.screen == SCR_PLAY) {
        if (b == B_L2) { ui.l2_used = 0; return; }
        if (b == B_R2) return;
        if (b == B_L1 && ui.octave > -3) { ui.octave--; set_pitch(); ui.dirty_settings = 1; return; }
        if (b == B_R1 && ui.octave < 3) { ui.octave++; set_pitch(); ui.dirty_settings = 1; return; }
        cell_button(b);
        return;
    }

    /* EDIT */
    if (b == B_L1) { ui.tab = (ui.tab + TAB_COUNT - 1) % TAB_COUNT; ui.confirm_delete = 0; return; }
    if (b == B_R1) { ui.tab = (ui.tab + 1) % TAB_COUNT; ui.confirm_delete = 0; return; }
    if (b == B_UP || b == B_DOWN || b == B_LEFT || b == B_RIGHT) { edit_nav(b); return; }

    if (ui.tab == TAB_RADIO) {
        int n = stations.count;
        Station *s = n ? &stations.items[ui.radio_sel] : NULL;
        if (b == B_A && s) { tune_now(0, ui.radio_sel); toast("voice A hears %s", s->name); }
        if (b == B_B && s) { tune_now(1, ui.radio_sel); toast("voice B hears %s", s->name); }
        if (b == B_Y) open_keyboard(KB_ADD_URL, "http://");
        if (b == B_R2) ui.overlay = OV_SCANMENU;
        if (b == B_L2 && s) {
            s->cat = (s->cat + 1) % 4;
            save_stations();
            toast("%s is %s", s->name, s->cat ? category_names[s->cat] : "uncategorised");
        }
        if (b == B_X && s) {
            if (ui.confirm_delete) {
                char name[96];
                snprintf(name, sizeof name, "%s", s->name);
                delete_station(ui.radio_sel);
                toast("let go of %s", name);
                ui.confirm_delete = 0;
            } else {
                ui.confirm_delete = 1;
            }
        }
        return;
    }
    if (b == B_L2 || b == B_X) do_grab(edit_voice());
    if (b == B_Y && ui.tab == TAB_MIX) { /* back to unity / the default */
        const MixStrip *m = &mix_strips[ui.mix_sel];
        synth_set_param(syn, m->id, param_info[m->id].def);
        toast("%s %s reset", mix_group_names[m->group], m->name);
        ui.dirty_settings = 1;
    } else if (b == B_Y) dice_section();
}

static void on_release(int b)
{
    if (ui.screen == SCR_PLAY && ui.overlay == OV_NONE && b == B_L2 && !ui.l2_used) do_grab(play_voice()); /* a tap of L2 */
}

static void held_keys_update(Uint32 now)
{
    static const int keys[8] = {B_UP, B_DOWN, B_LEFT, B_RIGHT, B_X, B_B, B_Y, B_A};
    for (int i = 0; i < 8; i++) {
        int b = keys[i];
        if (!ui.held[b]) continue;
        if (ui.screen == SCR_PLAY && ui.overlay == OV_NONE && ui.held[B_R2]) {
            /* real-time sound: d-pad = filter (up brighter) / morph (right toward B), X/B = fold, Y/A = space */
            float step = ui.held[B_L2] ? 0.004f : 0.015f;
            int id = -1, dir = 0;
            switch (b) {
            case B_UP: id = P_TONE; dir = 1; break;
            case B_DOWN: id = P_TONE; dir = -1; break;
            case B_LEFT: id = P_MORPH; dir = -1; break;
            case B_RIGHT: id = P_MORPH; dir = 1; break;
            case B_X: id = P_FOLD; dir = 1; break;
            case B_B: id = P_FOLD; dir = -1; break;
            case B_Y: id = P_SPACE; dir = -1; break;
            case B_A: id = P_SPACE; dir = 1; break;
            }
            synth_set_param(syn, id, synth_get_param(syn, id) + dir * step);
            ui.dirty_settings = 1;
            continue;
        }
        if (ui.screen == SCR_PLAY && i >= 4 && ui.overlay == OV_NONE) { /* face buttons repeat on PLAY only */
        } else if (i >= 4) {
            continue;
        }
        Uint32 held_for = now - ui.held_since[b];
        Uint32 interval = ui.screen == SCR_PLAY ? 140 : held_for > 1500 ? 30 : 70;
        if (held_for > 320 && now - ui.last_repeat[b] >= interval) {
            ui.last_repeat[b] = now;
            on_press(b);
        }
    }
}

/* ================================================================== periodic logic */

static void logic_update(Uint32 now)
{
    for (int o = 0; o < 2; o++)
        if (ui.pending_station[o] >= -1 && now >= ui.pending_at[o]) tune_now(o, ui.pending_station[o]);
    if (ui.scan_busy && scan_done) {
        ui.scan_busy = 0;
        ui.scan_results = scan_tmp;
        ui.scan_sel = ui.scan_scroll = 0;
    }
    static Uint32 last_save;
    if (ui.dirty_settings && now - last_save > 5000) { save_settings(); last_save = now; }
}

/* ================================================================== presentation */

static int rotate180;

static void present(void)
{
    if (rotate180) {
        SDL_LockSurface(video);
        Uint32 *dst = video->pixels;
        int dp = video->pitch / 4;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                dst[(SCREEN_H - 1 - y) * dp + (SCREEN_W - 1 - x)] = px[y * pitch_px + x];
        SDL_UnlockSurface(video);
    } else {
        SDL_BlitSurface(screen, NULL, video, NULL);
    }
    SDL_Flip(video);
}

static void render(void)
{
    update_blooms();
    if (ui.screen == SCR_PLAY) draw_play();
    else draw_edit();
    if (ui.overlay == OV_KEYBOARD) draw_keyboard();
    else if (ui.overlay == OV_SCANMENU) draw_scan_menu();
    else if (ui.overlay == OV_SCANRESULTS) draw_scan_results();
    draw_toast();
}

/* ================================================================== setup */

static void resolve_paths(void)
{
    const char *env = getenv("RADIOSCAN_DIR");
    if (env) snprintf(app_dir, sizeof app_dir, "%s", env);
    else if (!getcwd(app_dir, sizeof app_dir)) snprintf(app_dir, sizeof app_dir, ".");
    snprintf(stations_path, sizeof stations_path, "%s/stations.txt", app_dir);
    snprintf(settings_path, sizeof settings_path, "%s/settings.cfg", app_dir);
}

static void init_common(void)
{
    char ca[PATH_MAX + 16];
    snprintf(ca, sizeof ca, "%s/cacert.pem", app_dir);
    net_init(ca);
    if (stations_load(&stations, stations_path) < 0) {
        stations_add(&stations, "SomaFM Drone Zone", "http://ice1.somafm.com/dronezone-128-mp3", CAT_SOUND);
        stations_add(&stations, "SomaFM Mission Control", "http://ice1.somafm.com/missioncontrol-128-mp3", CAT_VOICE);
        stations_add(&stations, "SomaFM Groove Salad", "http://ice1.somafm.com/groovesalad-128-mp3", CAT_MUSIC);
        save_stations();
    }
    ui.pending_station[0] = ui.pending_station[1] = -2;
    ui.tuned[0] = ui.tuned[1] = -2;
    ui.tab = TAB_VOICES;
    if (load_settings() < 0) { /* first run: music on A, a talk station sliced into rhythm on B */
        synth_set_param(syn, P_A_STATION, (float)first_of_kind(CAT_MUSIC, 0));
        synth_set_param(syn, P_B_STATION, (float)first_of_kind(CAT_VOICE, 1));
    }
    if (settings_were_reset) toast("new version: sound settings reset, stations kept");
    set_pitch();
    for (int o = 0; o < 2; o++) {
        int idx = station_param(o);
        if (idx >= stations.count) idx = -1;
        tune_now(o, idx);
    }
}

/* ================================================================== offline render (testing) */

static void write_wav_header(FILE *f, long data_bytes)
{
    unsigned char h[44];
    unsigned v32;
    unsigned short v16;
    memcpy(h, "RIFF", 4);
    v32 = (unsigned)(data_bytes + 36); memcpy(h + 4, &v32, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v32 = 16; memcpy(h + 16, &v32, 4);
    v16 = 1; memcpy(h + 20, &v16, 2);
    v16 = 2; memcpy(h + 22, &v16, 2);
    v32 = (unsigned)audio_rate; memcpy(h + 24, &v32, 4);
    v32 = (unsigned)audio_rate * 4; memcpy(h + 28, &v32, 4);
    v16 = 4; memcpy(h + 32, &v16, 2);
    v16 = 16; memcpy(h + 34, &v16, 2);
    memcpy(h + 36, "data", 4);
    v32 = (unsigned)data_bytes; memcpy(h + 40, &v32, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 44, 1, f);
}

/* radioscan --render <seconds> <out.wav> [key=value ...] [note=N] [grab=1] [dice=0|1|2] */
static int offline_render(int argc, char **argv)
{
    double secs = atof(argv[2]);
    audio_rate = getenv("RS_RATE") ? atoi(getenv("RS_RATE")) : 48000;
    syn = synth_create(audio_rate);
    st[0] = stream_create(audio_rate);
    st[1] = stream_create(audio_rate);
    init_common();
    for (int i = 4; i < argc; i++) {
        char k[64];
        float v;
        if (sscanf(argv[i], "%63[^=]=%f", k, &v) != 2) continue;
        if (!strcmp(k, "note")) { synth_note_on(syn, (int)v); continue; }
        if (!strcmp(k, "grab")) { synth_grab(syn, -1); continue; }
        if (!strcmp(k, "dice")) { synth_dice(syn, (int)v); continue; }
        for (int p = 0; p < P_COUNT; p++)
            if (!strcmp(param_info[p].key, k)) synth_set_param(syn, p, v);
    }
    for (int i = 0; i < 200; i++) {
        int ready = 1;
        for (int o = 0; o < 2; o++) {
            StreamState s = stream_state(st[o]);
            if (s == ST_CONNECTING || s == ST_BUFFERING) ready = 0;
        }
        if (ready) break;
        SDL_Delay(100);
    }
    for (int o = 0; o < 2; o++) {
        char m[160];
        stream_status(st[o], m, sizeof m);
        fprintf(stderr, "OSC %c: %s (%d Hz, %d kbps)\n", 'A' + o, m, stream_src_rate(st[o]), stream_bitrate(st[o]));
    }
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 1;
    write_wav_header(f, 0);
    long frames = (long)(secs * audio_rate);
    int16_t pcm[1024 * 2];
    double peak = 0, sum = 0;
    Uint32 dsp_ms = 0;
    for (long done = 0; done < frames; done += 1024) {
        Uint32 t0 = SDL_GetTicks();
        audio_cb(NULL, (Uint8 *)pcm, sizeof pcm);
        dsp_ms += SDL_GetTicks() - t0;
        for (int i = 0; i < 2048; i++) { double v = pcm[i] / 32768.0; sum += v * v; if (fabs(v) > peak) peak = fabs(v); }
        fwrite(pcm, sizeof pcm, 1, f);
        if (!getenv("RS_FAST")) SDL_Delay(1024 * 1000 / audio_rate); /* real time so the network keeps up */
    }
    write_wav_header(f, frames / 1024 * 1024 * 4);
    fclose(f);
    fprintf(stderr, "rendered %.1fs: peak %.3f rms %.4f, dsp %.1f%% of real time\n", secs, peak,
            sqrt(sum / (frames * 2.0)), 100.0 * dsp_ms / (secs * 1000.0));
    static const char *const vs[] = {"silent", "music", "voice"};
    for (int o = 0; o < 2; o++) {
        char t[256];
        stream_title(st[o], t, sizeof t);
        fprintf(stderr, "OSC %c: voice sense %.2f (%s), title \"%s\"\n", 'A' + o, synth_speechness(syn, o),
                vs[synth_voice_state(syn, o)], t);
    }
    stream_destroy(st[0]);
    stream_destroy(st[1]);
    synth_destroy(syn);
    return 0;
}

/* ================================================================== main */

static volatile sig_atomic_t got_signal;
static void on_signal(int sig) { got_signal = sig; }

int main(int argc, char **argv)
{
    resolve_paths();
    if (argc >= 4 && !strcmp(argv[1], "--render")) return offline_render(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--synctest")) { /* --synctest LEAD|FOLLOW|AUTO [bpm] [seconds] */
        int mode = argc > 2 && !strcmp(argv[2], "LEAD") ? SYNC_LEAD : argc > 2 && !strcmp(argv[2], "FOLLOW") ? SYNC_FOLLOW : SYNC_AUTO;
        float bpm = argc > 3 ? (float)atof(argv[3]) : 120.0f;
        int secs = argc > 4 ? atoi(argv[4]) : 5;
        syn = synth_create(48000);
        synth_set_param(syn, P_SYNC, (float)mode);
        synth_set_param(syn, P_BPM, bpm);
        synth_set_running(syn, mode == SYNC_LEAD);
        sync_init(&sync_clock);
        static float zin[2 * 480], zout[2 * 480];
        uint64_t t0 = now_us();
        while (now_us() - t0 < (uint64_t)secs * 1000000ULL) {
            synth_process(syn, zin, zin, zout, 480);
            sync_poll(now_us());
            usleep(10000);
        }
        const SyncStatus *ss = sync_status();
        printf("RADIOSCAN %s: bpm %.1f playing %d peers %d leading %d following %d offset %+.1f ms pos %.2f\n",
               argc > 2 ? argv[2] : "AUTO", synth_get_param(syn, P_BPM), synth_running(syn), ss->peers, ss->leading,
               ss->following, ss->offset_ms, synth_position(syn));
        sync_close();
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--scan")) { /* --scan <0 tag|1 name|2 random> [query]: test the directory */
        char ca[PATH_MAX + 16], err[200];
        snprintf(ca, sizeof ca, "%s/cacert.pem", app_dir);
        net_init(ca);
        int rc = directory_search(atoi(argv[2]), argc > 3 ? argv[3] : "", &scan_tmp, err, sizeof err);
        if (rc < 0) { fprintf(stderr, "%s\n", err); return 1; }
        for (int i = 0; i < scan_tmp.count; i++)
            printf("%s | %s | %s | %s\n", scan_tmp.items[i].name, scan_tmp.items[i].url, scan_tmp.items[i].info,
                   category_names[scan_tmp.items[i].cat]);
        return 0;
    }

    /* --shot file.bmp <play|edit|radio|motion|kb|scan> [tab] : render one frame (for docs/tests) */
    const char *shot = NULL, *shot_view = "play";
    int shot_tab = TAB_VOICES;
    if (argc >= 3 && !strcmp(argv[1], "--shot")) {
        shot = argv[2];
        if (argc >= 4) shot_view = argv[3];
        if (argc >= 5) shot_tab = atoi(argv[4]);
    }

#ifdef __arm__
    rotate180 = 1; /* the Mini Plus panel is mounted upside down for this SDL */
#endif
    if (getenv("RS_ROTATE180")) rotate180 = atoi(getenv("RS_ROTATE180"));
    if (SDL_Init(SDL_INIT_VIDEO | (shot ? 0 : SDL_INIT_AUDIO)) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_SetCaption("RadioScan", NULL);
    video = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 32, SDL_HWSURFACE);
    if (!video) { fprintf(stderr, "SDL_SetVideoMode: %s\n", SDL_GetError()); return 1; }
    screen = SDL_CreateRGBSurface(SDL_SWSURFACE, SCREEN_W, SCREEN_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    px = screen->pixels;
    pitch_px = screen->pitch / 4;

    audio_rate = getenv("RS_RATE") ? atoi(getenv("RS_RATE")) : 48000;
    int buffer = getenv("RS_BUFFER") ? atoi(getenv("RS_BUFFER")) : 1024;
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)buffer;
    want.callback = audio_cb;
    int audio_ok = 0;
    if (!shot) {
        static const int rates[] = {0, 48000, 44100, 32000};
        for (int i = 0; i < 4 && !audio_ok; i++) {
            want.freq = i == 0 ? audio_rate : rates[i];
            if (SDL_OpenAudio(&want, &have) == 0) { audio_ok = 1; audio_rate = have.freq; }
        }
        if (!audio_ok) fprintf(stderr, "SDL_OpenAudio failed: %s\n", SDL_GetError());
    }

    srand((unsigned)SDL_GetTicks() ^ (unsigned)getpid());
    syn = synth_create(audio_rate);
    st[0] = stream_create(audio_rate);
    st[1] = stream_create(audio_rate);
    if (!syn || !st[0] || !st[1]) { fprintf(stderr, "out of memory\n"); return 1; }
    init_common();
    if (audio_ok) SDL_PauseAudio(0);
    else if (!shot) toast("audio device unavailable");

    if (shot) {
        ui.screen = SCR_EDIT;
        if (!strcmp(shot_view, "play")) { ui.screen = SCR_PLAY; ui.held[B_UP] = 1; }
        if (!strcmp(shot_view, "playmod")) { ui.screen = SCR_PLAY; ui.held[B_R2] = 1; ui.held[B_UP] = 0; }
        if (!strcmp(shot_view, "edit")) ui.tab = shot_tab;
        if (!strcmp(shot_view, "radio")) ui.tab = TAB_RADIO;
        if (!strcmp(shot_view, "rhythm")) ui.tab = TAB_RHYTHM;
        if (!strcmp(shot_view, "rhythmx")) { ui.tab = TAB_RHYTHM; ui.sel[TAB_RHYTHM] = 99; }
        if (!strcmp(shot_view, "engine")) ui.tab = TAB_ENGINE;
        if (!strcmp(shot_view, "mix")) ui.tab = TAB_MIX;
        if (!strcmp(shot_view, "kb")) open_keyboard(KB_ADD_URL, "http://ice1.somafm.com/");
        if (!strcmp(shot_view, "scan")) ui.overlay = OV_SCANMENU;
        /* let the engine run a moment so the rings and waves have something to show */
        static float fa[2048], fb[2048], fo[2048];
        for (int i = 0; i < 2048; i++) { /* A: noisy, bursty; B: a plain tone */
            fa[i] = 0.25f * sinf(i * 0.05f) * sinf(i * 0.002f) + 0.15f * ((float)rand() / RAND_MAX - 0.5f) * (i % 700 < 200);
            fb[i] = 0.2f * sinf(i * 0.011f);
        }
        for (int k = 0; k < 40; k++) synth_process(syn, fa, fb, fo, 1024);
        synth_grab(syn, -1);
        for (int k = 0; k < 30; k++) {
            synth_process(syn, fa, fb, fo, 1024);
            update_blooms();
        }
        render();
        SDL_SaveBMP(screen, shot);
        stream_destroy(st[0]);
        stream_destroy(st[1]);
        SDL_Quit();
        return 0;
    }

    sync_init(&sync_clock);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    fprintf(stderr, "radioscan: audio %s at %d Hz, %d frames/buffer\n", audio_ok ? "open" : "FAILED", audio_rate, buffer);
    ui.running = 1;
    while (ui.running && !got_signal) {
        SDL_Event e;
        Uint32 now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) ui.running = 0;
            if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int b = map_key(e.key.keysym.sym);
                if (b == B_NONE) continue;
                if (e.type == SDL_KEYDOWN) {
                    if (ui.held[b]) continue;
                    ui.held[b] = 1;
                    ui.held_since[b] = ui.last_repeat[b] = now;
                    on_press(b);
                } else {
                    ui.held[b] = 0;
                    on_release(b);
                }
            }
        }
        held_keys_update(now);
        logic_update(now);
        sync_poll(now_us());
        render();
        present();
        Uint32 spent = SDL_GetTicks() - now; /* ~30 frames per second, whatever the drawing cost */
        if (spent < 33) SDL_Delay(33 - spent);
    }

    sync_close();
    save_settings();
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    stream_destroy(st[0]);
    stream_destroy(st[1]);
    synth_destroy(syn);
    SDL_Quit();
    return 0;
}
