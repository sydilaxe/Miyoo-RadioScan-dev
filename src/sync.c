#include "sync.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_PEERS 8
#define PEER_TIMEOUT_US 3000000ULL   /* forget a unit after 3 s of silence */
#define SEND_INTERVAL_US 100000ULL   /* 10 packets per second */
#define IFACE_REFRESH_US 5000000ULL  /* re-read broadcast addresses (Wi-Fi may reconnect) */
#define ERR_WINDOW 8                 /* phase errors kept for the median */
#define BAR_STEPS 16.0
#define JUMP_STEPS 0.5               /* farther off than this: jump instead of trimming */
#define TRIM_GAIN 0.15
#define TRIM_MAX 0.03

typedef struct {
    uint32_t id;
    uint64_t seen_us;
    int playing, mode;
    float bpm;
    double pos;
} Peer;

static const SyncClock *clk;
static SyncStatus status;
static Peer peers[MAX_PEERS];
static double errs[ERR_WINDOW];
static int n_errs, err_head;

static int sock = -1;
static struct sockaddr_in targets[8];
static int n_targets;
static uint64_t last_send_us, last_iface_us;

/* ------------------------------------------------------------ protocol core */

static void wr32(uint8_t *b, uint32_t v) { memcpy(b, &v, 4); }
static uint32_t rd32(const uint8_t *b) { uint32_t v; memcpy(&v, b, 4); return v; }

static int mode_rank(int mode) { return mode == SYNC_LEAD ? 0 : mode == SYNC_AUTO ? 1 : 3; }

static void reset_errors(void) { n_errs = err_head = 0; }

void sync_core_init(const SyncClock *clock, uint32_t id)
{
    clk = clock;
    memset(&status, 0, sizeof status);
    memset(peers, 0, sizeof peers);
    status.id = id;
    reset_errors();
}

int sync_make_packet(uint8_t *b, uint64_t now_us)
{
    memset(b, 0, SYNC_PKT_LEN);
    memcpy(b, "HI5S", 4);
    b[4] = 1;
    b[5] = (uint8_t)(clk->playing() ? 1 : 0);
    b[6] = (uint8_t)clk->mode();
    wr32(b + 8, status.id);
    float bpm = clk->bpm();
    double pos = clk->position();
    memcpy(b + 12, &bpm, 4);
    memcpy(b + 16, &pos, 8);
    memcpy(b + 24, &now_us, 8);
    return SYNC_PKT_LEN;
}

static Peer *find_peer(uint32_t id, uint64_t now_us)
{
    Peer *spare = NULL;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].seen_us && peers[i].id == id) return &peers[i];
        if (!spare && (!peers[i].seen_us || now_us - peers[i].seen_us > PEER_TIMEOUT_US)) spare = &peers[i];
    }
    if (spare) memset(spare, 0, sizeof *spare);
    return spare;
}

/* signed distance between two bar positions, in (-8, 8] steps */
static double bar_delta(double d)
{
    d = fmod(d, BAR_STEPS);
    if (d > BAR_STEPS / 2) d -= BAR_STEPS;
    if (d <= -BAR_STEPS / 2) d += BAR_STEPS;
    return d;
}

static double median_error(void)
{
    double a[ERR_WINDOW];
    int n = n_errs < ERR_WINDOW ? n_errs : ERR_WINDOW;
    memcpy(a, errs, sizeof(double) * n);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && a[j - 1] > a[j]; j--) { double t = a[j]; a[j] = a[j - 1]; a[j - 1] = t; }
    return n ? a[n / 2] : 0.0;
}

/* lock to the leader: tempo and transport first, then bar phase */
static void follow(const Peer *p)
{
    if (fabsf(p->bpm - clk->bpm()) > 0.01f && p->bpm >= 40.0f && p->bpm <= 250.0f) clk->set_bpm(p->bpm);
    if (p->playing != clk->playing()) {
        clk->set_playing(p->playing);
        if (p->playing) clk->set_position(p->pos);
        reset_errors();
    }
    double rate = 1.0;
    if (clk->playing()) {
        errs[err_head] = bar_delta(p->pos - clk->position());
        err_head = (err_head + 1) % ERR_WINDOW;
        if (n_errs < ERR_WINDOW) n_errs++;
        double med = median_error();
        if (n_errs >= 3 && fabs(med) > JUMP_STEPS) {
            clk->set_position(clk->position() + med);
            reset_errors();
            med = 0;
        } else if (n_errs >= 3) {
            double k = med * TRIM_GAIN;
            rate = 1.0 + (k > TRIM_MAX ? TRIM_MAX : k < -TRIM_MAX ? -TRIM_MAX : k);
        }
        status.offset_ms = (float)(med * 15000.0 / clk->bpm()); /* one 16th = 15000 / bpm ms */
    }
    clk->set_rate(rate);
}

void sync_handle_packet(const uint8_t *b, int n, uint64_t now_us)
{
    if (n < SYNC_PKT_LEN || memcmp(b, "HI5S", 4) || b[4] != 1) return;
    uint32_t id = rd32(b + 8);
    if (id == status.id || clk->mode() == SYNC_OFF) return;
    Peer *p = find_peer(id, now_us);
    if (!p) return;
    p->id = id;
    p->seen_us = now_us;
    p->playing = b[5];
    p->mode = b[6];
    memcpy(&p->bpm, b + 12, 4);
    memcpy(&p->pos, b + 16, 8);
    if (p->bpm != p->bpm || p->pos != p->pos) { p->seen_us = 0; return; } /* NaN: ignore */
    sync_elect(now_us);
    if (status.following && status.leader_id == id) follow(p);
}

void sync_elect(uint64_t now_us)
{
    int mode = clk->mode();
    int best_rank = mode_rank(mode), alive = 0;
    uint32_t best_id = best_rank < 3 ? status.id : 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        const Peer *p = &peers[i];
        if (!p->seen_us || now_us - p->seen_us > PEER_TIMEOUT_US) continue;
        alive++;
        int r = mode_rank(p->mode);
        if (r < best_rank || (r == best_rank && r < 3 && p->id < best_id)) { best_rank = r; best_id = p->id; }
    }
    int on = mode != SYNC_OFF && best_rank < 3;
    int leading = on && best_id == status.id;
    int following = on && best_id != status.id;
    if (!following && status.following) { /* lost the leader: run free again */
        clk->set_rate(1.0);
        if (!clk->playing()) clk->set_playing(1);
        reset_errors();
        status.offset_ms = 0;
    }
    if (following && (!status.following || status.leader_id != best_id)) reset_errors();
    status.peers = alive;
    status.leading = leading;
    status.following = following;
    status.leader_id = following ? best_id : 0;
}

const SyncStatus *sync_status(void) { return &status; }

/* ------------------------------------------------------------ sockets */

static void refresh_targets(void)
{
    struct ifaddrs *list, *it;
    n_targets = 0;
    if (getifaddrs(&list) != 0) return;
    for (it = list; it && n_targets < 8; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (!(it->ifa_flags & IFF_BROADCAST) || (it->ifa_flags & IFF_LOOPBACK) || !it->ifa_broadaddr) continue;
        targets[n_targets] = *(struct sockaddr_in *)it->ifa_broadaddr;
        targets[n_targets].sin_port = htons(SYNC_PORT);
        n_targets++;
    }
    freeifaddrs(list);
}

static uint32_t random_id(void)
{
    uint32_t id = (uint32_t)time(NULL) ^ ((uint32_t)getpid() * 2654435761u);
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        uint32_t r;
        if (fread(&r, 1, 4, f) == 4) id ^= r;
        fclose(f);
    }
    return id ? id : 1;
}

void sync_init(const SyncClock *clock)
{
    sync_core_init(clock, random_id());
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(SYNC_PORT);
    if (bind(sock, (struct sockaddr *)&a, sizeof a) != 0) {
        fprintf(stderr, "sync: cannot bind UDP %d: %s\n", SYNC_PORT, strerror(errno));
        close(sock);
        sock = -1;
        return;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    refresh_targets();
}

void sync_poll(uint64_t now_us)
{
    if (sock < 0 || !clk) return;
    uint8_t b[64];
    ssize_t n;
    while ((n = recv(sock, b, sizeof b, 0)) > 0) sync_handle_packet(b, (int)n, now_us);
    sync_elect(now_us);
    if (clk->mode() == SYNC_OFF) return;
    if (now_us - last_iface_us > IFACE_REFRESH_US) { last_iface_us = now_us; refresh_targets(); }
    if (now_us - last_send_us < SEND_INTERVAL_US) return;
    last_send_us = now_us;
    int len = sync_make_packet(b, now_us);
    for (int i = 0; i < n_targets; i++) sendto(sock, b, len, 0, (struct sockaddr *)&targets[i], sizeof targets[i]);
    if (!n_targets) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        a.sin_port = htons(SYNC_PORT);
        sendto(sock, b, len, 0, (struct sockaddr *)&a, sizeof a);
    }
}

void sync_close(void)
{
    if (sock >= 0) close(sock);
    sock = -1;
}
