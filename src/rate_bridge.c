/* mpc-addin-rate-bridge: lets MPC use a USB audio device that cannot run at 44.1 kHz (a 48 kHz-only Neural DSP Quad Cortex
 * mini, for one) as its audio device.
 *
 * MPC runs at 44100 Hz and asks for it exactly (snd_pcm_hw_params_set_rate(..., 44100, 0)); on a 48 kHz-only card that is
 * the one call that fails (-EINVAL), so MPC refuses the device. This addin hooks snd_pcm_open only: when MPC opens "hw:<card>"
 * (or hw:<card>,<dev>, hw:CARD=<id>,DEV=<n>, hw:<id>) and that card is a USB audio card whose /proc/asound/card<N>/stream0
 * lists no 44100 for the direction being opened, it opens alsa-lib's plain "type rate" PCM over that hw device at the card's
 * own rate instead (snd_pcm_open_lconf with an inline config). The rate plugin converts the rate only: channels, format and
 * access stay the device's, so MPC's own negotiation (set_channels_near 32, period 192, explicit start) sees the card's real
 * 8 channels. Every other open, and everything after the open, goes to alsa-lib unchanged.
 *
 * Settings, rate_bridge.conf next to the .so (key=value, # comments):
 *   enabled=1        0 = pass every open through untouched
 *   cards=           empty = any USB card without 44.1 kHz; else a space/comma list of card ids or indexes to bridge
 *                    (only those, and only when they lack 44.1 kHz)
 *   log=1            one line per bridged or examined hw open, in rate_bridge.log next to the .so (capped at 256 KB)
 * Test hooks (not used on a device): RATE_BRIDGE_ASOUND = a stand-in for /proc/asound; RATE_BRIDGE_SLAVE = an alsa-lib PCM
 * type to use instead of "hw" for the slave (e.g. "null"); RATE_BRIDGE_DIR = the folder for the conf and the log.
 *
 * Rules from docs/ADDINS.md (mpc-vst-plugins): forward with dlsym(RTLD_NEXT), export only the hook, no work on the audio
 * thread (opens happen on MPC's main thread; nothing here runs per period). MIT licence. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rate_bridge.h"

#define EXPORT __attribute__((visibility("default")))

typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_config snd_config_t;
typedef struct _snd_input snd_input_t;

/* ---- settings and log ---- */
static struct {
    int enabled, log;
    char cards[256];
    char dir[512];
    int loaded;
} g_set = { 1, 1, "", "", 0 };
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void addin_dir(char *out, size_t n) {
    const char *e = getenv("RATE_BRIDGE_DIR");
    if (e && *e) { snprintf(out, n, "%s", e); return; }
    Dl_info info;
    if (dladdr((void *)addin_dir, &info) && info.dli_fname) {
        snprintf(out, n, "%s", info.dli_fname);
        char *s = strrchr(out, '/');
        if (s) { *s = 0; return; }
    }
    snprintf(out, n, "/tmp");
}

static void load_settings(void) {   /* under g_lock */
    if (g_set.loaded) return;
    g_set.loaded = 1;
    addin_dir(g_set.dir, sizeof g_set.dir);
    char path[600];
    snprintf(path, sizeof path, "%s/rate_bridge.conf", g_set.dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        char *ke = k + strlen(k); while (ke > k && isspace((unsigned char)ke[-1])) *--ke = 0;
        while (isspace((unsigned char)*v)) v++;
        char *ve = v + strlen(v); while (ve > v && isspace((unsigned char)ve[-1])) *--ve = 0;
        if (!strcmp(k, "enabled")) g_set.enabled = atoi(v) != 0;
        else if (!strcmp(k, "log")) g_set.log = atoi(v) != 0;
        else if (!strcmp(k, "cards")) snprintf(g_set.cards, sizeof g_set.cards, "%s", v);
    }
    fclose(f);
}

static void logf_(const char *fmt, ...) {   /* under g_lock */
    if (!g_set.log) return;
    char path[600];
    snprintf(path, sizeof path, "%s/rate_bridge.log", g_set.dir);
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 256 * 1024) return;
    FILE *f = fopen(path, "a");
    if (!f) return;
    time_t t = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime(&t));
    fprintf(f, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- /proc/asound ---- */
static const char *asound_root(void) {
    const char *e = getenv("RATE_BRIDGE_ASOUND");
    return e && *e ? e : "/proc/asound";
}

static int read_small(const char *path, char *buf, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t got = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[got] = 0;
    return (int)got;
}

static void trim(char *s) {
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
}

/* card index for an id ("mini"), or -1 */
static int card_by_id(const char *id) {
    for (int c = 0; c < 32; c++) {
        char p[300], b[64];
        snprintf(p, sizeof p, "%s/card%d/id", asound_root(), c);
        if (read_small(p, b, sizeof b) < 0) continue;
        trim(b);
        if (!strcmp(b, id)) return c;
    }
    return -1;
}

int rb_parse_hw_name(const char *name, int *card, int *dev, char *id, size_t idn) {
    /* "hw:2" "hw:2,0" "hw:CARD=mini,DEV=0" "hw:mini" "hw:mini,1"; anything else (plughw, default, ...) -> 0 */
    if (!name || strncmp(name, "hw:", 3)) return 0;
    const char *s = name + 3;
    char cardpart[64] = "", devpart[16] = "0";
    if (!strncmp(s, "CARD=", 5)) {
        s += 5;
        size_t i = 0;
        while (*s && *s != ',' && i < sizeof cardpart - 1) cardpart[i++] = *s++;
        cardpart[i] = 0;
        if (*s == ',') {
            s++;
            if (!strncmp(s, "DEV=", 4)) s += 4;
            else return 0;
            snprintf(devpart, sizeof devpart, "%s", s);
        }
    } else {
        size_t i = 0;
        while (*s && *s != ',' && i < sizeof cardpart - 1) cardpart[i++] = *s++;
        cardpart[i] = 0;
        if (*s == ',') snprintf(devpart, sizeof devpart, "%s", s + 1);
    }
    if (!*cardpart) return 0;
    for (const char *d = devpart; *d; d++) if (!isdigit((unsigned char)*d)) return 0;   /* a subdevice or odd suffix: leave it */
    *dev = atoi(devpart);
    int all_digits = 1;
    for (const char *d = cardpart; *d; d++) if (!isdigit((unsigned char)*d)) all_digits = 0;
    if (all_digits) { *card = atoi(cardpart); if (id && idn) id[0] = 0; }
    else { *card = card_by_id(cardpart); if (id && idn) snprintf(id, idn, "%s", cardpart); }
    return *card >= 0;
}

int rb_stream_rates(const char *stream0, int capture, int *has44, unsigned *best) {
    /* Parses /proc/asound/cardN/stream0 (snd-usb-audio). Looks only at the "Playback:" or "Capture:" section asked for.
     * has44: some altset lists 44100 (or a continuous range covering it). best: the rate to run the device at
     * (48000 if listed, else the first discrete rate listed, else 0). Returns the number of Rates lines seen in the section. */
    const char *want = capture ? "Capture:" : "Playback:", *other = capture ? "Playback:" : "Capture:";
    *has44 = 0; *best = 0;
    const char *sec = strstr(stream0, want);
    if (!sec) return 0;
    const char *end = strstr(sec + strlen(want), other);
    if (end && end < sec) end = NULL;
    int lines = 0;
    unsigned first = 0;
    int has48 = 0;
    for (const char *p = sec; (p = strstr(p, "Rates:")) && (!end || p < end); p += 6) {
        lines++;
        const char *v = p + 6, *e = strchr(v, '\n');
        char buf[256];
        size_t n = e ? (size_t)(e - v) : strlen(v);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, v, n);
        buf[n] = 0;
        unsigned lo = 0, hi = 0;
        if (strstr(buf, "continuous") && sscanf(buf, " %u - %u", &lo, &hi) == 2) {
            if (lo <= 44100 && hi >= 44100) *has44 = 1;
            if (lo <= 48000 && hi >= 48000) has48 = 1;
            if (!first) first = lo;
            continue;
        }
        char *tok = strtok(buf, " ,");
        while (tok) {
            unsigned r = (unsigned)strtoul(tok, NULL, 10);
            if (r == 44100) *has44 = 1;
            if (r == 48000) has48 = 1;
            if (r && !first) first = r;
            tok = strtok(NULL, " ,");
        }
    }
    *best = has48 ? 48000 : first;
    return lines;
}

static int card_selected(int card, const char *id) {   /* cards= list, under g_lock */
    if (!*g_set.cards) return 1;
    char list[256];
    snprintf(list, sizeof list, "%s", g_set.cards);
    char cid[64] = "";
    if (id && *id) snprintf(cid, sizeof cid, "%s", id);
    else {
        char p[300];
        snprintf(p, sizeof p, "%s/card%d/id", asound_root(), card);
        if (read_small(p, cid, sizeof cid) >= 0) trim(cid);
    }
    for (char *tok = strtok(list, " ,"); tok; tok = strtok(NULL, " ,")) {
        if (!strcmp(tok, cid)) return 1;
        char *endp;
        long n = strtol(tok, &endp, 10);
        if (!*endp && n == card) return 1;
    }
    return 0;
}

/* Decide whether to bridge this open. Returns the rate to run the device at, or 0 to pass through. Under g_lock. */
static unsigned decide(const char *name, int stream, int *card, int *dev) {
    char id[64] = "";
    if (!rb_parse_hw_name(name, card, dev, id, sizeof id)) return 0;
    char p[300], usbid[32], s0[8192];
    snprintf(p, sizeof p, "%s/card%d/usbid", asound_root(), *card);
    if (read_small(p, usbid, sizeof usbid) < 0) return 0;                 /* not a USB audio card: leave it */
    snprintf(p, sizeof p, "%s/card%d/stream%d", asound_root(), *card, *dev);
    if (read_small(p, s0, sizeof s0) < 0) return 0;
    int has44;
    unsigned best;
    int lines = rb_stream_rates(s0, stream == 1, &has44, &best);
    trim(usbid);
    if (!lines) return 0;
    if (has44) { logf_("pass %s %s: card %d (%s) has 44100", name, stream ? "capture" : "playback", *card, usbid); return 0; }
    if (!best) return 0;
    if (!card_selected(*card, id)) { logf_("pass %s: card %d not in cards=%s", name, *card, g_set.cards); return 0; }
    return best;
}

/* ---- the hook ---- */
EXPORT int snd_pcm_open(snd_pcm_t **pcm, const char *name, int stream, int mode) {
    static int (*real_open)(snd_pcm_t **, const char *, int, int);
    static int (*open_lconf)(snd_pcm_t **, const char *, int, int, snd_config_t *);
    static int (*cfg_top)(snd_config_t **);
    static int (*cfg_load)(snd_config_t *, snd_input_t *);
    static int (*cfg_delete)(snd_config_t *);
    static int (*in_buffer)(snd_input_t **, const char *, ssize_t);
    static int (*in_close)(snd_input_t *);
    static const char *(*strerr)(int);
    if (!real_open) {
        *(void **)&real_open = dlsym(RTLD_NEXT, "snd_pcm_open");
        *(void **)&open_lconf = dlsym(RTLD_NEXT, "snd_pcm_open_lconf");
        *(void **)&cfg_top = dlsym(RTLD_NEXT, "snd_config_top");
        *(void **)&cfg_load = dlsym(RTLD_NEXT, "snd_config_load");
        *(void **)&cfg_delete = dlsym(RTLD_NEXT, "snd_config_delete");
        *(void **)&in_buffer = dlsym(RTLD_NEXT, "snd_input_buffer_open");
        *(void **)&in_close = dlsym(RTLD_NEXT, "snd_input_close");
        *(void **)&strerr = dlsym(RTLD_NEXT, "snd_strerror");
        if (!real_open) return -ENOSYS;
    }
    if (!open_lconf || !cfg_top || !cfg_load || !cfg_delete || !in_buffer || !in_close)
        return real_open(pcm, name, stream, mode);

    int card = -1, dev = 0;
    unsigned rate;
    pthread_mutex_lock(&g_lock);
    load_settings();
    rate = g_set.enabled ? decide(name, stream, &card, &dev) : 0;
    pthread_mutex_unlock(&g_lock);
    if (!rate) return real_open(pcm, name, stream, mode);

    const char *slave = getenv("RATE_BRIDGE_SLAVE");
    char cfgtext[512];
    if (slave && *slave)
        snprintf(cfgtext, sizeof cfgtext,
                 "pcm.rate_bridge { type rate converter \"linear\" slave { pcm { type %s } rate %u } }", slave, rate);
    else
        snprintf(cfgtext, sizeof cfgtext,
                 "pcm.rate_bridge { type rate converter \"linear\" slave { pcm { type hw card %d device %d } rate %u } }",
                 card, dev, rate);
    snd_config_t *top = NULL;
    snd_input_t *in = NULL;
    int r = cfg_top(&top);
    if (r >= 0) r = in_buffer(&in, cfgtext, -1);
    if (r >= 0) { r = cfg_load(top, in); in_close(in); }
    if (r >= 0) r = open_lconf(pcm, "rate_bridge", stream, mode, top);
    if (top) cfg_delete(top);   /* alsa-lib copies what it needs while opening */

    pthread_mutex_lock(&g_lock);
    if (r >= 0)
        logf_("bridge %s %s: card %d dev %d runs at %u Hz, MPC sees 44100 (alsa-lib rate plugin, linear)", name,
              stream ? "capture" : "playback", card, dev, rate);
    else
        logf_("bridge %s %s: card %d at %u Hz failed: %s; falling back to the plain open", name, stream ? "capture" : "playback",
              card, rate, strerr ? strerr(r) : "?");
    pthread_mutex_unlock(&g_lock);
    if (r < 0) return real_open(pcm, name, stream, mode);   /* MPC then gets the device's own answer, as without the addin */
    return r;
}
