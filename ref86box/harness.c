/*
 * Standalone harness around 86Box's unmodified src/sound/snd_emu8k.c.
 *
 * Purpose: render a trace of EMU8000 port writes through the real 86Box
 * implementation, so our own Emu8000.cpp can be diffed against it sample
 * by sample.
 *
 * upstream/snd_emu8k.c and upstream/snd_emu8k.h are byte-identical copies of
 * 86Box master; nothing in them is edited. Everything 86Box-specific that they
 * reference is provided here or in include/86box/*.h.
 *
 * Trace format (text, one event per line, '#' starts a comment):
 *     <sample_time> <port_hex> <value_hex>
 * sample_time is an absolute frame index at 44100 Hz, monotonically
 * non-decreasing. Ports are the real ISA addresses (0x620/0x622/0xA20/...).
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/sound.h>
#include <86box/snd_emu8k.h>

/* ------------------------------------------------------------------ */
/* 86Box globals and services that snd_emu8k.c expects                  */
/* ------------------------------------------------------------------ */

int music_pos_global     = 0;
int wavetable_pos_global = 0;

static const char *g_rom_path = NULL;
static int         g_verbose  = 0;

void
pclog_ex(const char *fmt, va_list ap)
{
    if (g_verbose)
        vfprintf(stderr, fmt, ap);
}

void
pclog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

/* snd_emu8k.c asks for EMU8K_ROM_PATH; we redirect to whatever --rom gave us. */
FILE *
rom_fopen(const char *fn, char *mode)
{
    (void) fn;
    return fopen(g_rom_path, mode);
}

void
io_sethandler(uint16_t base, uint16_t size,
              uint8_t (*inb)(uint16_t, void *), uint16_t (*inw)(uint16_t, void *),
              uint32_t (*inl)(uint16_t, void *), void (*outb)(uint16_t, uint8_t, void *),
              void (*outw)(uint16_t, uint16_t, void *), void (*outl)(uint16_t, uint32_t, void *),
              void *priv)
{
    (void) base; (void) size; (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
}

void
io_removehandler(uint16_t base, uint16_t size,
                 uint8_t (*inb)(uint16_t, void *), uint16_t (*inw)(uint16_t, void *),
                 uint32_t (*inl)(uint16_t, void *), void (*outb)(uint16_t, uint8_t, void *),
                 void (*outw)(uint16_t, uint16_t, void *), void (*outl)(uint16_t, uint32_t, void *),
                 void *priv)
{
    (void) base; (void) size; (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
}

/* Declared in snd_emu8k.c, not in the header. */
extern void     emu8k_outw(uint16_t addr, uint16_t val, void *priv);
extern void     emu8k_outb(uint16_t addr, uint8_t val, void *priv);
extern uint16_t emu8k_inw(uint16_t addr, void *priv);

/* ------------------------------------------------------------------ */
/* Trace                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t time;
    uint16_t port;
    uint16_t val;
    uint8_t  is_byte;
} event_t;

static event_t *g_events = NULL;
static size_t   g_nev    = 0;

static int
load_trace(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "harness: cannot open trace %s\n", path);
        return 0;
    }

    size_t cap = 4096;
    g_events = malloc(cap * sizeof(event_t));

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        unsigned long long t;
        unsigned           port;
        unsigned           val;
        char               width = 'w';
        int n = sscanf(p, "%llu %x %x %c", &t, &port, &val, &width);
        if (n < 3)
            continue;

        if (g_nev == cap) {
            cap *= 2;
            g_events = realloc(g_events, cap * sizeof(event_t));
        }
        g_events[g_nev].time    = (uint64_t) t;
        g_events[g_nev].port    = (uint16_t) port;
        g_events[g_nev].val     = (uint16_t) val;
        g_events[g_nev].is_byte = (width == 'b' || width == 'B');
        g_nev++;
    }
    fclose(f);
    return 1;
}

/* ------------------------------------------------------------------ */
/* WAV out                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE    *f;
    uint32_t frames;
} wav_t;

static void
put32(FILE *f, uint32_t v)
{
    fputc(v & 0xff, f);
    fputc((v >> 8) & 0xff, f);
    fputc((v >> 16) & 0xff, f);
    fputc((v >> 24) & 0xff, f);
}

static void
put16(FILE *f, uint16_t v)
{
    fputc(v & 0xff, f);
    fputc((v >> 8) & 0xff, f);
}

static int
wav_open(wav_t *w, const char *path)
{
    w->f = fopen(path, "wb");
    if (!w->f)
        return 0;
    w->frames = 0;
    fwrite("RIFF", 1, 4, w->f);
    put32(w->f, 0);
    fwrite("WAVEfmt ", 1, 8, w->f);
    put32(w->f, 16);
    put16(w->f, 1);
    put16(w->f, 2);
    put32(w->f, WT_FREQ);
    put32(w->f, WT_FREQ * 4);
    put16(w->f, 4);
    put16(w->f, 16);
    fwrite("data", 1, 4, w->f);
    put32(w->f, 0);
    return 1;
}

static void
wav_close(wav_t *w)
{
    uint32_t data = w->frames * 4;
    fseek(w->f, 4, SEEK_SET);
    put32(w->f, 36 + data);
    fseek(w->f, 40, SEEK_SET);
    put32(w->f, data);
    fclose(w->f);
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* AWE32Emu: per-sample tap into the chip voice state.                  */
/* Upstream snd_emu8k.c is never touched -- everything below only reads */
/* emu8k_t after driving emu8k_update() one sample at a time.           */
/* ------------------------------------------------------------------ */
static const char *g_dump_path  = NULL;
static int         g_dump_voice = -1;
static uint64_t    g_dump_from  = 0;
static uint64_t    g_dump_to    = 0;
static int         g_force_step = 0;
static FILE       *g_dump_f     = NULL;

static void
dump_header(void)
{
    fprintf(g_dump_f,
            "frame,voice,int_addr,fract_addr,cpf_pitch,ip,vol,ctoff,"
            "vol_target,filt_target,venv_state,venv_amp,venv_db,"
            "menv_state,menv_amp,menv_db,filtq,filt_att,filt_b0,filt_b4,"
            "vol_l,vol_r,revb,chor,buf_l,buf_r\n");
}

static void
dump_frame(const emu8k_t *emu8k, uint64_t frame, int pos)
{
    for (int c = 0; c < 32; c++) {
        const emu8k_voice_t *v = &emu8k->voice[c];
        if (g_dump_voice >= 0) {
            if (c != g_dump_voice)
                continue;
        } else if (!v->env_engine_on && !v->cvcf_curr_volume)
            continue;

        fprintf(g_dump_f,
                "%llu,%d,%u,%u,%u,%u,%u,%u,%u,%u,"
                "%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%d,%d,%u,%u,%d,%d\n",
                (unsigned long long) frame, c,
                (unsigned) v->addr.int_address, (unsigned) v->addr.fract_address,
                (unsigned) v->cpf_curr_pitch, (unsigned) v->ip,
                (unsigned) v->cvcf_curr_volume, (unsigned) v->cvcf_curr_filt_ctoff,
                (unsigned) v->vtft_vol_target, (unsigned) v->vtft_filter_target,
                v->vol_envelope.state, v->vol_envelope.value_amp_hz, v->vol_envelope.value_db_oct,
                v->mod_envelope.state, v->mod_envelope.value_amp_hz, v->mod_envelope.value_db_oct,
                v->filterq_idx, v->filt_att,
                (long long) v->filt_buffer[0], (long long) v->filt_buffer[4],
                v->vol_l, v->vol_r,
                (unsigned) v->ptrx_revb_send, (unsigned) v->csl_chor_send,
                emu8k->buffer[pos * 2], emu8k->buffer[pos * 2 + 1]);
    }
}

static void
usage(void)
{
    fprintf(stderr,
            "usage: emu8k_ref --rom <awe32.raw> --trace <trace.txt> --wav <out.wav>\n"
            "                 [--frames N] [--ram KB] [--addr 0x620] [--gain F] [-v]\n"
            "                 [--dump <csv>] [--dump-voice N] [--dump-from F] [--dump-to F]\n"
            "                 [--step]\n");
}

int
main(int argc, char **argv)
{
    const char *trace_path = NULL;
    const char *wav_path   = NULL;
    uint64_t    frames     = 0;
    int         ram_kb     = 512;
    uint16_t    addr       = 0x620;
    double      gain       = 1.0;
    const char *dram_path  = NULL;
    uint32_t    dram_off   = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc)
            g_rom_path = argv[++i];
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc)
            trace_path = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--ram") && i + 1 < argc)
            ram_kb = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--addr") && i + 1 < argc)
            addr = (uint16_t) strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
            gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dram") && i + 1 < argc)
            dram_path = argv[++i];
        else if (!strcmp(argv[i], "--dram-offset") && i + 1 < argc)
            dram_off = (uint32_t) strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            g_dump_path = argv[++i];
        else if (!strcmp(argv[i], "--dump-voice") && i + 1 < argc)
            g_dump_voice = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-from") && i + 1 < argc)
            g_dump_from = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--dump-to") && i + 1 < argc)
            g_dump_to = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--step"))
            g_force_step = 1;
        else if (!strcmp(argv[i], "-v"))
            g_verbose = 1;
        else {
            usage();
            return 1;
        }
    }

    if (!g_rom_path || !trace_path || !wav_path) {
        usage();
        return 1;
    }

    if (!load_trace(trace_path))
        return 1;

    if (frames == 0) {
        /* Default: run to the last event plus four seconds of tail. */
        uint64_t last = g_nev ? g_events[g_nev - 1].time : 0;
        frames        = last + 4 * WT_FREQ;
    }

    static emu8k_t emu8k;
    memset(&emu8k, 0, sizeof(emu8k));
    emu8k_init(&emu8k, addr, ram_kb);

    /* Our player uploads samples into DRAM with a memcpy rather than through
       SMLD port writes, so the trace does not carry them. Load the dump
       straight into the chip's RAM instead. */
    if (dram_path) {
        FILE *df = fopen(dram_path, "rb");
        if (!df) {
            fprintf(stderr, "harness: cannot open dram %s\n", dram_path);
            return 1;
        }
        fseek(df, 0, SEEK_END);
        long bytes = ftell(df);
        fseek(df, 0, SEEK_SET);
        if (!emu8k.ram) {
            fprintf(stderr, "harness: --dram given but chip has no onboard RAM (--ram 0)\n");
            return 1;
        }
        long max_words = (long) ram_kb * 512 - (long) dram_off;
        long words     = bytes / 2;
        if (words > max_words)
            words = max_words;
        if (words > 0 && fread(emu8k.ram + dram_off, 2, (size_t) words, df) != (size_t) words)
            fprintf(stderr, "harness: short read on %s\n", dram_path);
        fclose(df);
        fprintf(stderr, "harness: loaded %ld DRAM words at word offset %u\n", words, dram_off);
    }

    wav_t wav;
    if (!wav_open(&wav, wav_path)) {
        fprintf(stderr, "harness: cannot write %s\n", wav_path);
        return 1;
    }

    if (g_dump_path) {
        if (g_dump_to == 0)
            g_dump_to = frames;
        g_dump_f = fopen(g_dump_path, "wb");
        if (!g_dump_f) {
            fprintf(stderr, "harness: cannot write %s\n", g_dump_path);
            return 1;
        }
        dump_header();
        fprintf(stderr, "harness: dumping frames %llu..%llu, voice %d, to %s\n",
                (unsigned long long) g_dump_from, (unsigned long long) g_dump_to,
                g_dump_voice, g_dump_path);
    }

    fprintf(stderr, "harness: %zu events, %llu frames, ram=%dKB addr=0x%03X\n",
            g_nev, (unsigned long long) frames, ram_kb, addr);

    size_t   ev    = 0;
    uint64_t block = 0;

    while (block < frames) {
        uint64_t block_end = block + WTBUFLEN;

        /* Apply every write that lands inside this block, at its exact
           sample offset -- emu8k_outw() calls emu8k_update() itself, so the
           chip catches up to wavetable_pos_global before the write lands. */
        /* AWE32Emu: when the dump window overlaps this block, drive the chip
           one sample at a time so the voice state can be read after every
           sample. Writes still land at the very same offsets. */
        const int stepping = g_force_step
            || (g_dump_f && block < g_dump_to && block_end > g_dump_from);

        if (stepping) {
            for (int step = 0; step < WTBUFLEN; step++) {
                const uint64_t now = block + (uint64_t) step;
                while (ev < g_nev && g_events[ev].time <= now) {
                    uint64_t t = g_events[ev].time;
                    if (t < block)
                        t = block;
                    wavetable_pos_global = (int) (t - block);
                    if (g_events[ev].is_byte)
                        emu8k_outb(g_events[ev].port, (uint8_t) g_events[ev].val, &emu8k);
                    else
                        emu8k_outw(g_events[ev].port, g_events[ev].val, &emu8k);
                    ev++;
                }
                wavetable_pos_global = step + 1;
                emu8k_update(&emu8k);
                if (g_dump_f && now >= g_dump_from && now < g_dump_to)
                    dump_frame(&emu8k, now, step);
            }
        } else {
            while (ev < g_nev && g_events[ev].time < block_end) {
                uint64_t t = g_events[ev].time;
                if (t < block)
                    t = block;
                wavetable_pos_global = (int) (t - block);
                if (g_events[ev].is_byte)
                    emu8k_outb(g_events[ev].port, (uint8_t) g_events[ev].val, &emu8k);
                else
                    emu8k_outw(g_events[ev].port, g_events[ev].val, &emu8k);
                ev++;
            }
        }

        wavetable_pos_global = WTBUFLEN;
        emu8k_update(&emu8k);

        for (int i = 0; i < WTBUFLEN; i++) {
            if (block + (uint64_t) i >= frames)
                break;
            for (int ch = 0; ch < 2; ch++) {
                double   v  = (double) emu8k.buffer[i * 2 + ch] * gain;
                int32_t  iv = (int32_t) v;
                if (iv > 32767)
                    iv = 32767;
                if (iv < -32768)
                    iv = -32768;
                put16(wav.f, (uint16_t) (int16_t) iv);
            }
            wav.frames++;
        }

        emu8k_reset_buffer(&emu8k);
        block = block_end;
    }

    wav_close(&wav);
    if (g_dump_f)
        fclose(g_dump_f);
    fprintf(stderr, "harness: wrote %u frames to %s\n", wav.frames, wav_path);

    emu8k_close(&emu8k);
    free(g_events);
    return 0;
}
