/****************************************************************************\
 *  AWETEST - calibration recording for the Sound Blaster AWE32
 *
 *  Plays a series of precisely defined sounds from which the behaviour of the
 *  EMU8000 chip can be measured afterwards: attenuation to dB mapping, pan
 *  law, filter cutoff and slope, resonance, envelope rates, modulation
 *  depths, effects.
 *
 *  This is not music. It is a calibration signal - every tone changes exactly
 *  ONE thing and leaves everything else alone. That is the only way anything
 *  can be read back out of the recording.
 *
 *  Two paths are used at once:
 *    1) Direct writes to the EMU8000 registers through the I/O ports. Going
 *       through MIDI would not allow setting one value and leaving the rest
 *       alone - the driver computes the registers itself from the bank,
 *       velocity and controllers - so the calibration blocks have to be done
 *       this way.
 *    2) The Creative AWE32 DOS SDK (awe32NoteOn, awe32ProgramChange, ...),
 *       i.e. the same path a game uses. Blocks 26 and 27 verify the whole
 *       chain including the bank-to-register conversion.
 *
 *  Run it from the directory that holds BULLFROG.SBK (the Magic Carpet 2
 *  directory). Without that file only the last block is skipped.
 *
 *  Build: see BUILD.CMD (Open Watcom) or MAKEFILE.BC (Borland).
\****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>

#include "ctaweapi.h"

#if defined(__WATCOMC__)
  #include <i86.h>
  #include <dos.h>
#endif

/* The program is built two ways. 16-bit real mode is the one that gets
   shipped: it needs no DOS extender, so there is a single EXE to copy and
   nothing else. The 32-bit build exists because the SDK documents it, and it
   is handy while developing. */
#if defined(__386__)
  #define REGCALL(n, i, o)   int386((n), (i), (o))
#else
  #define REGCALL(n, i, o)   int86((n), (i), (o))
#endif

/* Sound Blaster base port; the EMU8000 sits 0x400 above it. */
static unsigned g_sb_base = 0x220;

/* ------------------------------------------------------------------------ *
 *  Portable port I/O
 * ------------------------------------------------------------------------ */
#if defined(__WATCOMC__)
  #include <conio.h>
  #define OUTB(p,v)   outp((p),(v))
  #define OUTW(p,v)   outpw((p),(v))
  #define INB(p)      inp((p))
#else
  #include <dos.h>
  #define OUTB(p,v)   outportb((p),(v))
  #define OUTW(p,v)   outport((p),(v))
  #define INB(p)      inportb((p))
#endif

/* ------------------------------------------------------------------------ *
 *  Timing
 *
 *  Delays are busy-waits on timer channel 2 - the one that otherwise drives
 *  the PC speaker. That leaves the system clock and the interrupts alone, so
 *  nothing else breaks. Channel 2 ticks 1,193,182 times per second.
 * ------------------------------------------------------------------------ */
#define PIT_HZ      1193182L

static void RecPoll(void);          /* defined below, called from the wait */

/* Tik BIOSu: 18,2 Hz, tj. jedno zvyseni na kazde pretoceni kanalu 2.
   Prerusení ho zvysuje i kdyz zrovna cekame na disk, takze jako jedine
   nemuze o cas prijit. */
static unsigned long BiosTicks(void)
{
    unsigned long a, b;

    /* Dve cteni po sobe misto zakazu preruseni - kdyz se shoduji, hodnota se
       pod rukama nezmenila. Zakaz preruseni je tady prilis draha operace na
       to, jak casto se sem chodi. */
    do {
#if defined(__386__)
        a = *((volatile unsigned long *) 0x0000046CUL);
        b = *((volatile unsigned long *) 0x0000046CUL);
#else
        a = *((unsigned long __far *) MK_FP(0x0040, 0x006C));
        b = *((unsigned long __far *) MK_FP(0x0040, 0x006C));
#endif
    } while (a != b);
    return a;
}

static void DelayMs(unsigned ms)
{
    unsigned long ticks;
    unsigned      last, cur;
    unsigned long done = 0;
    unsigned long bios0, bdelta, floor_ticks;
    unsigned      guard = 0;

    if (!ms) return;
    ticks = (PIT_HZ / 1000L) * (unsigned long) ms;

    /* channel 2, mode 2, two-byte access; gate on, speaker disconnected */
    OUTB(0x61, (INB(0x61) & 0xFC) | 0x01);
    OUTB(0x43, 0xB4);
    OUTB(0x42, 0xFF);
    OUTB(0x42, 0xFF);

    OUTB(0x43, 0x80);                 /* latch channel 2 */
    last = (unsigned) INB(0x42);
    last |= ((unsigned) INB(0x42)) << 8;
    bios0 = BiosTicks();

    while (done < ticks) {
        OUTB(0x43, 0x80);
        cur = (unsigned) INB(0x42);
        cur |= ((unsigned) INB(0x42)) << 8;
        /* the counter runs down and wraps */
        done += (unsigned long) ((last - cur) & 0xFFFF);
        last = cur;

        /* Kdyz nas RecPoll() drzel dele nez jedno pretoceni, radek vyse o ne
           prisel a cekani by bezelo dlouho. Tiky BIOSu davaji spodni mez,
           ktera se ztratit nemuze: n tiku znamena aspon (n-1) pretoceni, tedy
           (n-1)*65536 tiku PITu. Je to mez, ne presny cas, takze cekani nikdy
           nezkrati pod spravnou hodnotu - jen dozene, co se ztratilo. */
        if (++guard >= 64) {
            guard  = 0;
            bdelta = BiosTicks() - bios0;
            if (bdelta > 1UL) {
                floor_ticks = (bdelta - 1UL) * 65536UL;
                if (floor_ticks > done) done = floor_ticks;
            }
        }

        RecPoll();          /* the capture is drained from right here */
    }
    OUTB(0x61, INB(0x61) & 0xFC);
}

/* ------------------------------------------------------------------------ *
 *  Optional WAV capture through the Sound Blaster 16 ADC
 *
 *  Switched on with /REC:<file>. The card records its own output, which gives
 *  a capture that is already lined up with the log and needs no external
 *  recorder. It is a bonus, not the main path: whether the wavetable output
 *  can be selected as a record source differs between cards, so if the file
 *  comes out silent, the external recording is the one that counts.
 *
 *  44.1 kHz, 16 bit, stereo - the same format the analysis expects. That is
 *  about 240 MB for the full 22.6 minutes, so make sure there is room.
 *
 *  No interrupt is hooked. The DMA runs auto-init into a ring buffer and the
 *  buffer is drained by polling the DMA counter from inside the delay loop,
 *  which the program is in nearly all the time anyway.
 * ------------------------------------------------------------------------ */
#define REC_RATE     44100
/* Zmereno na skutecnem stroji: bloky 1-22 trvaly 999 s, cely beh vyjde
   pres pul tretiho tisice sekund i s MIDI bloky na konci. */
#define AWETEST_SECONDS 1330L
/* 64 kB = 372 ms zvuku, tedy dvojnasobna rezerva proti tomu, kdyz disk na
   chvili nestiha. Kdyz se 128 kB v DOSu nesezene, spadne se na 32 kB. */
static long rec_ring = 65536L;         /* ring buffer, ~372 ms of audio     */
#define REC_BYTES    rec_ring
/* IRQ and the 16-bit DMA channel come from BLASTER ("I5 H5"); they are not
   always 5 and they are two different things - mixing them up would mask the
   wrong interrupt. */
static int rec_dma = 5;
static int rec_irq = 5;
#define REC_DMA      rec_dma

static FILE          *rec_file;
static unsigned char *rec_buf;         /* linear pointer to the ring        */
static unsigned long  rec_phys;        /* its physical address              */
static unsigned       rec_sel;         /* selector or segment, for freeing  */
static long           rec_read;        /* how far we have drained           */
static unsigned long  rec_total;       /* data bytes written to the file    */
static unsigned long  rec_hdr_at;      /* rec_total pri poslednim zapisu hlavicky */
static unsigned long  rec_tick;        /* tik BIOSu pri poslednim vyprazdneni */
static unsigned long  rec_drops;       /* kolikrat prstenec prejel            */
static int            rec_on;
static int            rec_any;         /* did anything but silence arrive?  */
static int            rec_ok;          /* a usable capture path was found   */
static int            rec_peak;        /* loudest sample seen, 0..32767     */

static unsigned SbBase(void) { return g_sb_base; }

/* Startup trace. Opened and closed around every single line, so whatever the
   machine does next, what got this far is already on disk. Only used while
   working out the recording path - the run itself does not need it. */
static int trace_on = 1;

static void Trace(const char *msg, long a)
{
    FILE *f;
    if (!trace_on) return;
    f = fopen("AWETRACE.LOG", "a");
    if (!f) return;
    fprintf(f, msg, a);
    fputc('\n', f);
    fclose(f);
}

/* --- DSP ---------------------------------------------------------------- */
static int DspWrite(unsigned char v)
{
    int i;
    for (i = 0; i < 20000; i++)
        if (!(INB(SbBase() + 0x0C) & 0x80)) { OUTB(SbBase() + 0x0C, v); return 0; }
    return 1;
}

static int DspReset(void)
{
    int i;
    OUTB(SbBase() + 0x06, 1);
    for (i = 0; i < 100; i++) (void) INB(SbBase() + 0x06);   /* ~3 us */
    OUTB(SbBase() + 0x06, 0);
    for (i = 0; i < 20000; i++)
        if ((INB(SbBase() + 0x0E) & 0x80) && INB(SbBase() + 0x0A) == 0xAA)
            return 0;
    return 1;
}

static void MixerW(unsigned char reg, unsigned char val)
{
    OUTB(SbBase() + 0x04, reg);
    OUTB(SbBase() + 0x05, val);
}

/* --- DOS memory for the DMA buffer -------------------------------------- *
 *  16-bit DMA needs a physically contiguous block that does not cross a
 *  128 KB boundary. We ask DPMI for 64 KB of conventional memory and use the
 *  32 KB inside it that starts on a 32 KB boundary - such a block can never
 *  straddle one.
 * ----------------------------------------------------------------------- */
static int RecAlloc(void)
{
    unsigned long base, aligned;

#if defined(__386__)
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x0100;                    /* DPMI: allocate DOS memory */
    rec_ring = 65536L;
    r.w.bx = (unsigned short) (131072L / 16L);
    int386(0x31, &r, &r);
    if (r.w.cflag) {
        rec_ring = 32768L;
        memset(&r, 0, sizeof(r));
        r.w.ax = 0x0100;
        r.w.bx = (unsigned short) (65536L / 16L);
        int386(0x31, &r, &r);
        if (r.w.cflag) return 1;
    }
    rec_sel = r.w.dx;
    base    = ((unsigned long) r.w.ax) << 4;
    aligned = (rec_ring == 65536L) ? ((base + 0xFFFFL) & ~0xFFFFL)
                                   : ((base + 0x7FFFL) & ~0x7FFFL);
    rec_phys = aligned;
    rec_buf  = (unsigned char *) aligned;   /* the first MB is mapped 1:1 */
#else
    unsigned seg;

    /* 128 kB a zarovnat na 64 kB - pak se 64 kB prstenec vejde cely pod
       jednu stranku 16bitoveho DMA. Kdyz tolik pameti neni, staci 64 kB
       na 32 kB prstenec jako driv. */
    rec_ring = 65536L;
    if (_dos_allocmem((unsigned) (131072L / 16L), &seg)) {
        rec_ring = 32768L;
        if (_dos_allocmem((unsigned) (65536L / 16L), &seg)) return 1;
    }
    rec_sel = seg;                      /* freed with _dos_freemem */
    base    = ((unsigned long) seg) << 4;
    if (rec_ring == 65536L)
        aligned = (base + 0xFFFFL) & ~0xFFFFL;
    else
        aligned = (base + 0x7FFFL) & ~0x7FFFL;
    rec_phys = aligned;
    rec_buf  = (unsigned char *) MK_FP((unsigned) (aligned >> 4), 0);
#endif
    return 0;
}

static void RecFree(void)
{
    if (!rec_sel) return;
#if defined(__386__)
    {
        union REGS r;
        memset(&r, 0, sizeof(r));
        r.w.ax = 0x0101;                /* DPMI: free DOS memory */
        r.w.dx = (unsigned short) rec_sel;
        int386(0x31, &r, &r);
    }
#else
    _dos_freemem((unsigned) rec_sel);
#endif
    rec_sel = 0;
}

/* --- picking the recording source and level ------------------------------ *
 *  Bit layout of the input mixer registers 0x3D (left ADC) and 0x3E (right),
 *  from the CT1745 documentation - the same in both registers:
 *
 *      D6 MIDI.L   D5 MIDI.R   D4 Line.L   D3 Line.R
 *      D2 CD.L     D1 CD.R     D0 Mic
 *
 *  On the AWE32 the wavetable is mixed into the MIDI path, so "MIDI" is what
 *  a direct capture of the EMU8000 needs. Line is for the case where the
 *  tester loops the card's output back into its own line input with a cable.
 * ------------------------------------------------------------------------- */
#define SRC_WAVETABLE   0
#define SRC_LINEIN      1

static int rec_src  = SRC_WAVETABLE;
static int rec_gain;                  /* 0..3 -> 0 / 6 / 12 / 18 dB */

/* Vystupni cesty na znamou hodnotu. Bez toho zavisi hlasitost na tom, co v
   mixeru nechal predchozi program, a dve mereni na stejne karte se lisi.
   0x30/0x31 je hlavni hlasitost, 0x34/0x35 wavetable (MIDI), 0x36..0x39 jsou
   CD a linka - ty stahujeme, aby se do smesi nepletly a nevznikala smycka. */
static void MixerInit(void)
{
    MixerW(0x30, 0xF8);               /* master  L */
    MixerW(0x31, 0xF8);               /* master  R */
    MixerW(0x34, 0xF8);               /* MIDI / wavetable L */
    MixerW(0x35, 0xF8);               /* MIDI / wavetable R */
    MixerW(0x36, 0x00);               /* CD      L */
    MixerW(0x37, 0x00);               /* CD      R */
    MixerW(0x38, 0x00);               /* line    L - jen do vystupu */
    MixerW(0x39, 0x00);               /* line    R */
}

static void RecSource(int src)
{
    if (src == SRC_LINEIN) {
        MixerW(0x3D, 0x10);           /* Line.L  */
        MixerW(0x3E, 0x08);           /* Line.R  */
    } else {
        MixerW(0x3D, 0x40);           /* MIDI.L  */
        MixerW(0x3E, 0x20);           /* MIDI.R  */
    }
    rec_src = src;
}

/* Input gain is the only level we touch. The output mixer is deliberately
   left alone: the external recording is the primary measurement and must not
   be disturbed by anything we do for the optional internal capture. */
static void RecGain(int step)
{
    unsigned char v;
    if (step < 0) step = 0;
    if (step > 3) step = 3;
    v = (unsigned char) (step << 6);
    MixerW(0x3F, v);                  /* input gain, left  */
    MixerW(0x40, v);                  /* input gain, right */
    rec_gain = step;
}

/* --- interrupt handling -------------------------------------------------- *
 *  The SB16 DSP halts an auto-init transfer if a block finishes while the
 *  previous interrupt has still not been acknowledged. That is documented
 *  behaviour of the real chip (older cards and clones just keep going), and
 *  it is exactly what happened here: with a block of half the ring, two
 *  blocks went through and the capture stopped dead at 32768 bytes.
 *
 *  The first attempt at fixing that just masked the interrupt and cleared the
 *  DSP flag from the polling loop. That is not enough: the card can assert
 *  the line at any moment, and the instant the mask is lifted the CPU jumps
 *  to whatever the vector happens to hold. It crashed the machine mid-probe
 *  with the test tone still sounding.
 *
 *  So we install a real handler. It does the two things that must happen -
 *  tell the DSP the block was seen, tell the interrupt controller we are
 *  done - and nothing else. The vector is put back on the way out.
 * ------------------------------------------------------------------------- */
static unsigned char pic_mask_saved;
static int           irq_hooked;

#if !defined(__386__)

static void (__interrupt __far *irq_old)(void);

static int IrqVector(void)
{
    return (rec_irq < 8) ? (0x08 + rec_irq) : (0x70 + rec_irq - 8);
}

static void __interrupt __far IrqHandler(void)
{
    (void) inp(g_sb_base + 0x0F);      /* acknowledge 16-bit DMA at the DSP */
    (void) inp(g_sb_base + 0x0E);      /* and 8-bit, harmless if not pending */
    if (rec_irq >= 8)
        outp(0xA0, 0x20);              /* EOI to the slave  */
    outp(0x20, 0x20);                  /* EOI to the master */
}

static void IrqHook(int on)
{
    unsigned port = (rec_irq < 8) ? 0x21 : 0xA1;
    int      bit  = (rec_irq < 8) ? rec_irq : (rec_irq - 8);

    if (on && !irq_hooked) {
        irq_old = _dos_getvect((unsigned) IrqVector());
        _dos_setvect((unsigned) IrqVector(), IrqHandler);
        pic_mask_saved = (unsigned char) INB(port);
        OUTB(port, (unsigned char) (pic_mask_saved & ~(1 << bit)));
        irq_hooked = 1;
    } else if (!on && irq_hooked) {
        OUTB(port, (unsigned char) (pic_mask_saved | (1 << bit)));
        _dos_setvect((unsigned) IrqVector(), irq_old);
        irq_hooked = 0;
    }
}

#else   /* 32-bit build: no handler, just keep the line masked throughout */

static void IrqHook(int on)
{
    unsigned port = (rec_irq < 8) ? 0x21 : 0xA1;
    int      bit  = (rec_irq < 8) ? rec_irq : (rec_irq - 8);

    if (on && !irq_hooked) {
        pic_mask_saved = (unsigned char) INB(port);
        OUTB(port, (unsigned char) (pic_mask_saved | (1 << bit)));
        irq_hooked = 1;
    } else if (!on && irq_hooked) {
        OUTB(port, pic_mask_saved);
        irq_hooked = 0;
    }
}

#endif

/* --- WAV header --------------------------------------------------------- */
static void WavHeader(FILE *f, unsigned long data_bytes)
{
    unsigned char h[44];
    unsigned long rate = REC_RATE, byte_rate = (unsigned long) REC_RATE * 4L;

    memcpy(h, "RIFF", 4);
    h[4] = (unsigned char) ((data_bytes + 36) & 0xFF);
    h[5] = (unsigned char) (((data_bytes + 36) >> 8) & 0xFF);
    h[6] = (unsigned char) (((data_bytes + 36) >> 16) & 0xFF);
    h[7] = (unsigned char) (((data_bytes + 36) >> 24) & 0xFF);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;      /* fmt chunk size */
    h[20] = 1;  h[21] = 0;                            /* PCM            */
    h[22] = 2;  h[23] = 0;                            /* stereo         */
    h[24] = (unsigned char) (rate & 0xFF);
    h[25] = (unsigned char) ((rate >> 8) & 0xFF);
    h[26] = (unsigned char) ((rate >> 16) & 0xFF);
    h[27] = (unsigned char) ((rate >> 24) & 0xFF);
    h[28] = (unsigned char) (byte_rate & 0xFF);
    h[29] = (unsigned char) ((byte_rate >> 8) & 0xFF);
    h[30] = (unsigned char) ((byte_rate >> 16) & 0xFF);
    h[31] = (unsigned char) ((byte_rate >> 24) & 0xFF);
    h[32] = 4;  h[33] = 0;                            /* block align    */
    h[34] = 16; h[35] = 0;                            /* bits           */
    memcpy(h + 36, "data", 4);
    h[40] = (unsigned char) (data_bytes & 0xFF);
    h[41] = (unsigned char) ((data_bytes >> 8) & 0xFF);
    h[42] = (unsigned char) ((data_bytes >> 16) & 0xFF);
    h[43] = (unsigned char) ((data_bytes >> 24) & 0xFF);
    fwrite(h, 1, 44, f);
}

/* --- start / poll / stop ------------------------------------------------- */
/* Starts the DMA capture only - no file. Used both by the real recording and
   by the short probes that decide which input to use. */
static int RecHwStart(void)
{
    unsigned long words, addr;
    unsigned      page;

    Trace("RecHwStart: enter", 0);
    if (!rec_buf && RecAlloc()) {
        printf("ERROR: no DOS memory for the DMA buffer.\n");
        Trace("RecHwStart: RecAlloc failed", 0);
        return 1;
    }
    Trace("RecHwStart: buffer at %08lX", (long) rec_phys);
    Trace("RecHwStart: ring %ld B", REC_BYTES);
    {   /* clear the ring; a plain memset would be near in some models,
           a nez 64 kB do unsigned nevejdeme */
        long i;
        for (i = 0; i < REC_BYTES; i++) rec_buf[(unsigned) i] = 0;
    }

    rec_total = 0;
    rec_hdr_at = 0;
    rec_tick   = 0;
    rec_drops  = 0;
    rec_read  = 0;
    rec_peak  = 0;
    rec_any   = 0;

    if (DspReset()) {
        printf("ERROR: Sound Blaster DSP does not respond.\n");
        Trace("RecHwStart: DSP reset failed", 0);
        return 1;
    }
    Trace("RecHwStart: DSP reset ok", 0);
    IrqHook(1);
    Trace("RecHwStart: IRQ %ld hooked", (long) rec_irq);

    RecSource(rec_src);
    RecGain(rec_gain);

    /* DMA channel 5: auto-init, device writes into memory */
    words = REC_BYTES / 2L;
    addr  = rec_phys >> 1;                 /* 16-bit channels count words */
    page  = (unsigned) (rec_phys >> 16);

    OUTB(0xD4, 0x04 | (REC_DMA - 4));      /* mask   */
    OUTB(0xD8, 0x00);                      /* clear flip-flop */
    OUTB(0xD6, 0x54 | (REC_DMA - 4));      /* single, auto-init, write */
    OUTB(0xC4, (unsigned char) (addr & 0xFF));
    OUTB(0xC4, (unsigned char) ((addr >> 8) & 0xFF));
    OUTB(0x8B, (unsigned char) page);
    OUTB(0xC6, (unsigned char) ((words - 1) & 0xFF));
    OUTB(0xC6, (unsigned char) (((words - 1) >> 8) & 0xFF));
    OUTB(0xD4, (REC_DMA - 4));             /* unmask */

    /* sample rate, then 16-bit signed stereo A/D with auto-init */
    DspWrite(0x42);
    DspWrite((unsigned char) ((REC_RATE >> 8) & 0xFF));
    DspWrite((unsigned char) (REC_RATE & 0xFF));
    DspWrite(0xBE);
    DspWrite(0x30);                        /* stereo + signed */
    DspWrite((unsigned char) (((words / 2L) - 1) & 0xFF));
    DspWrite((unsigned char) ((((words / 2L) - 1) >> 8) & 0xFF));

    rec_on = 1;
    Trace("RecHwStart: running, DMA %ld", (long) rec_dma);
    return 0;
}

/* Volne misto na disku, kam se bude nahravat. */
static FILE          *g_log;           /* zaznam co se kdy hralo             */
static unsigned long  g_ms;            /* planned milliseconds elapsed       */
static int            g_block;

/* ------------------------------------------------------------------------ *
 *  Zaznam do pameti
 *
 *  Puvodne se nahravka zapisovala na disk prubezne. Dobovy disk to ale
 *  neutahne: kdyz se zapis zdrzi dele, nez se vejde do prstence, cast zvuku
 *  je nenavratne pryc. Proto se cely cyklus posbira do pameti a na disk se
 *  ulozi az potom, kdy uz na case nezalezi. Kazdy cyklus dostane vlastni
 *  soubor s poradovym cislem pred priponou a na obou koncich zvukovou
 *  znacku, aby se daly soubory pri rozboru poskladat za sebe.
 * ------------------------------------------------------------------------ */
static void CycleMark(int start);       /* znacka na konci a zacatku cyklu    */

static unsigned char *cap_buf;         /* vyrovnavaci pamet celeho cyklu     */
static unsigned long  cap_size;        /* jak je velka                       */
static unsigned long  cap_used;        /* kolik je v ni                      */
static unsigned long  cap_limit;       /* pri kolika ukoncit cyklus          */
static unsigned long  cap_cap;         /* strop z /MB: v bajtech, 0 = bez nej */
static int            cap_index;       /* poradove cislo souboru             */
static const char    *cap_pattern;     /* jmeno z /REC:                      */
static unsigned long  cap_t0;          /* g_ms na zacatku cyklu              */
static unsigned long  cap_lost;        /* co se uz neveslo                   */

/* Kolik pameti je k dispozici. V chranenem rezimu se zepta DPMI (funkce
   0500h vraci mimo jine nejvetsi souvisly volny blok). */
static unsigned long FreeMemBytes(void)
{
#if defined(__386__)
    union  REGS          r;
    struct SREGS         s;
    static unsigned long info[12];

    memset(&r, 0, sizeof(r));
    segread(&s);
    r.w.ax  = 0x0500;
    r.x.edi = (unsigned long) info;
    s.es    = s.ds;
    int386x(0x31, &r, &r, &s);
    if (r.w.cflag) return 0UL;
    return info[0];
#else
    return 0UL;
#endif
}

/* Vyrovnavaci pamet na jeden cyklus. Bereme pulku volne pameti - zbytek
   patri systemu a extenderu, a kdybychom si vzali vsechno, zacne to
   strankovat na disk, tedy presne to, cemu se vyhybame. */
static int CapAlloc(void)
{
    unsigned long freeb = FreeMemBytes();
    unsigned long want;

    if (freeb == 0UL) freeb = 8UL * 1024UL * 1024UL;   /* neznamo: opatrne */

    want = freeb / 2UL;
    if (want > 48UL * 1024UL * 1024UL) want = 48UL * 1024UL * 1024UL;
    if (cap_cap && want > cap_cap)     want = cap_cap;
    want -= want % 4UL;                                /* cele ramce */

    while (want >= 512UL * 1024UL) {
        cap_buf = (unsigned char *) malloc((size_t) want);
        if (cap_buf) break;
        want /= 2UL;
        want -= want % 4UL;
    }
    if (!cap_buf) return 1;

    cap_size = want;
    /* rezerva, aby se doslo dohrat rozdelanou notu a doteklo z prstence */
    cap_limit = cap_size - (unsigned long) REC_RATE * 4UL * 3UL;
    if (cap_limit > cap_size) cap_limit = cap_size / 2UL;   /* podteceni */
    return 0;
}

/* Jmeno souboru cyklu: poradove cislo jde pred priponu. DOS ma jen 8 znaku
   na jmeno a tri z nich zabere cislo, takze zaklad zkracujeme na pet. */
static void CapName(char *dst, const char *pat, int idx)
{
    const char *dot = strrchr(pat, '.');
    const char *sl  = strrchr(pat, '\\');
    int         n   = dot ? (int) (dot - pat) : (int) strlen(pat);
    int         base;

    if (!sl) sl = strrchr(pat, '/');
    base = sl ? (int) (sl - pat) + 1 : 0;
    if (n - base > 5) n = base + 5;

    memcpy(dst, pat, (size_t) n);
    sprintf(dst + n, "%03d%s", idx, dot ? dot : ".WAV");
}

/* Ulozeni jednoho cyklu. Tady uz se nehraje, takze pomaly disk nevadi. */
static int CapFlush(void)
{
    char  name[96];
    FILE *f;

    if (!cap_buf || cap_used == 0UL) return 0;

    cap_index++;
    CapName(name, cap_pattern, cap_index);

    f = fopen(name, "wb");
    if (!f) {
        printf("\n*** Cannot create %s - out of disk space?\n", name);
        cap_used = 0UL;
        return 1;
    }
    WavHeader(f, cap_used);
    if (fwrite(cap_buf, 1, (size_t) cap_used, f) != (size_t) cap_used) {
        printf("\n*** Could not write all of %s - the disk is full.\n", name);
        fclose(f);
        cap_used = 0UL;
        return 1;
    }
    fclose(f);

    printf("\n  saved %s - %lu s of audio, test time %lu..%lu s\n",
           name, cap_used / ((unsigned long) REC_RATE * 4UL),
           cap_t0 / 1000UL, g_ms / 1000UL);
    if (g_log)
        fprintf(g_log, "%8lu -- FILE %s, %lu B, from %lu ms\n",
                g_ms, name, cap_used, cap_t0);
    cap_used = 0UL;
    return 0;
}

static unsigned long FreeBytes(const char *path)
{
    struct diskfree_t df;
    unsigned          drive = 0;            /* 0 = soucasny disk */

    if (path && path[0] && path[1] == ':')
        drive = (unsigned) ((path[0] & 0xDF) - 'A' + 1);
    if (_dos_getdiskfree(drive, &df) != 0) return 0UL;
    return (unsigned long) df.avail_clusters *
           (unsigned long) df.sectors_per_cluster *
           (unsigned long) df.bytes_per_sector;
}

static int RecStart(const char *file)
{
    unsigned long rate = (unsigned long) REC_RATE * 4UL;   /* B za sekundu */
    unsigned long secs, freeb, diskb;

    cap_pattern = file;
    cap_index   = 0;
    cap_used    = 0UL;
    cap_lost    = 0UL;

    freeb = FreeMemBytes();
    if (CapAlloc()) {
        printf("ERROR: not enough memory for the capture buffer.\n");
        return 1;
    }
    secs = cap_size / rate;

    printf("  Memory: %lu MB free, %lu MB taken for the capture.\n",
           freeb / 1048576UL, cap_size / 1048576UL);
    printf("  That is %lu s per file - about %lu notes.\n",
           secs, secs * 10UL / 6UL);
    printf("  The whole run is about %d minutes, so expect roughly\n",
           (int) (AWETEST_SECONDS / 60L));
    printf("  %lu files named like %s with a number before the dot.\n",
           (unsigned long) ((AWETEST_SECONDS + (long) secs - 1L)
                            / (long) (secs ? secs : 1UL)),
           file);

    /* Disk uz nemusi stihat prubezne, ale soubory se na nej vejit musi. */
    diskb = FreeBytes(file);
    printf("  Free disk space: %lu MB", diskb / 1048576UL);
    if (diskb < cap_size + cap_size / 4UL)
        printf(" - that is less than one file needs!\n");
    else
        printf(" - room for %lu of them.\n", diskb / (cap_size ? cap_size : 1UL));

    if (RecHwStart()) { free(cap_buf); cap_buf = 0; return 1; }
    cap_t0 = 0UL;
    return 0;
}

/* How many bytes the card has already put into the ring.
   The counter is read twice: it keeps counting down while we read its two
   halves, so a single read can be torn across the byte boundary. */
static long RecWritePos(void)
{
    unsigned a, b;
    long     left;
    int      tries;

    for (tries = 0; tries < 4; tries++) {
        OUTB(0xD8, 0x00);
        a = (unsigned) INB(0xC6);
        a |= ((unsigned) INB(0xC6)) << 8;
        OUTB(0xD8, 0x00);
        b = (unsigned) INB(0xC6);
        b |= ((unsigned) INB(0xC6)) << 8;
        if (b <= a) break;            /* counts down, so b <= a is consistent */
    }
    left = (long) ((unsigned long) b + 1UL) * 2L;   /* words -> bytes */
    if (left > REC_BYTES) left = REC_BYTES;
    return REC_BYTES - left;
}

/* Watch the level while draining, so the tester is told at the end whether
   the card actually routed anything into the ADC. Every 64th sample is
   enough to spot silence and costs nothing. */
static void RecScan(const unsigned char *p, long n)
{
    long i;
    for (i = 0; i + 1 < n; i += 128) {
        int v = (int) ((short) (p[i] | (p[i + 1] << 8)));
        if (v < 0) v = -v;
        if (v > rec_peak) { rec_peak = v; if (v > 64) rec_any = 1; }
    }
}

/* Zapis do nahravky. Vraci 1, kdyz se nepodarilo zapsat vsechno - typicky
   plny disk. Bez teto kontroly program pokracoval dal a tvaril se, ze
   nahrava, i kdyz uz nic nedoteklo na disk. */
static int RecWrite(const void *buf, long n)
{
    if (!cap_buf || n <= 0) return 0;
    if (cap_used + (unsigned long) n > cap_size) {
        cap_lost += (unsigned long) n;      /* nemelo by nastat, cyklus konci driv */
        return 1;
    }
    memcpy(cap_buf + cap_used, buf, (size_t) n);
    cap_used += (unsigned long) n;
    return 0;
}

static void RecPoll(void)
{
    long pos, n;

    if (!rec_on) return;

    /* Acknowledge the 16-bit DMA interrupt. Without this the DSP stops the
       auto-init transfer at the second block boundary - see the note above
       IrqHook(). Reading the port when nothing is pending does no harm. */
    (void) INB(SbBase() + 0x0F);

    pos = RecWritePos();
    if (pos == rec_read) return;

    n = pos - rec_read;
    if (n < 0) n += REC_BYTES;             /* prstenec se pretocil */

    if (n > (REC_BYTES / 4) * 3) {
        /* Za jedno vyprazdneni se pul prstence nasbirat nemuze, pokud nas
           neco nezdrzelo. Data uz nedohonime, jen se srovnáme. */
        rec_drops++;
        rec_read = pos;
        return;
    }

    /* The ring is 32 KB, so the offset always fits in 16 bits - important for
       the real-mode build, where pointer arithmetic stays inside the segment. */
    /* rec_file is NULL during the probes at startup - then we only measure
       the level and throw the samples away. */
    if (pos > rec_read) {
        RecWrite(rec_buf + (unsigned) rec_read, n);
        RecScan(rec_buf + (unsigned) rec_read, n);
    } else {
        long head = REC_BYTES - rec_read;   /* do konce prstence */

        RecWrite(rec_buf + (unsigned) rec_read, head);
        RecScan(rec_buf + (unsigned) rec_read, head);
        RecWrite(rec_buf, pos);
        RecScan(rec_buf, pos);
    }
    rec_total += (unsigned long) n;
    rec_read = pos;
}

/* Dopsani spravne delky do hlavicky. Vola se mezi bloky, ne z RecPoll -
   uvnitr vyprazdnovani prstence by to zdrzeni zpusobilo jeho prejeti.
   Diky tomu je soubor pouzitelny i kdyz beh skonci jinak nez slusne. */
static void RecFlushHeader(void)
{
    if (!rec_file || rec_total == rec_hdr_at) return;
    rec_hdr_at = rec_total;
    fseek(rec_file, 0L, SEEK_SET);
    WavHeader(rec_file, rec_total);
    fseek(rec_file, 0L, SEEK_END);
    fflush(rec_file);
}

static void RecHwStop(void)
{
    if (!rec_on) return;
    Trace("RecHwStop: enter, peak %ld", (long) rec_peak);
    rec_on = 0;
    DspWrite(0xD9);                        /* stop 16-bit auto-init */
    DspWrite(0xD5);                        /* pause 16-bit          */
    OUTB(0xD4, 0x04 | (REC_DMA - 4));      /* mask the channel      */
    (void) INB(SbBase() + 0x0F);           /* clear a pending ack   */
    IrqHook(0);
    Trace("RecHwStop: done", 0);

}

static void RecStop(void)
{
    if (rec_on && cap_buf) CycleMark(0);    /* zaverecna znacka posledniho cyklu */
    RecHwStop();
    CapFlush();
    if (cap_buf) { free(cap_buf); cap_buf = 0; }
    RecFree();
    printf("Recording written: %d file(s), %lu bytes (%lu s), peak %d of 32767.\n",
           cap_index, rec_total,
           rec_total / ((unsigned long) REC_RATE * 4L), rec_peak);
    if (cap_lost)
        printf("  %lu bytes did not fit in the buffer - report this, it is a bug.\n",
               cap_lost);
    if (rec_drops)
        printf("  %lu gap(s) - the disk could not keep up there. Those spots\n"
               "  are missing from the file; the tones themselves played.\n",
               rec_drops);
    if (!rec_any) {
        printf("\n*** The capture came out SILENT even though the check at\n");
        printf("*** startup found a usable input. Something changed the\n");
        printf("*** mixer underneath us. The sounds themselves played\n");
        printf("*** correctly - use the external recording.\n\n");
    }
}

/* ------------------------------------------------------------------------ *
 *  Direct EMU8000 register access
 *
 *  The pointer register lives at base+0x802 and takes (index << 5) | voice.
 *  The data ports are base+0x000/0x002/0x400/0x402/0x800, where "base" is the
 *  Sound Blaster port + 0x400 (so 0x620 for a card at 0x220).
 *
 *  Verified against an 86Box register trace: Data3 used to be computed as
 *  +0xC00, which turned port 0xE20 into 0x1220 - off the card entirely. The
 *  IP, IFATN and all modulation writes went nowhere and every tone played
 *  with whatever pitch and attenuation happened to be left over.
 * ------------------------------------------------------------------------ */
static unsigned g_base;               /* 0x620 for a Sound Blaster at 0x220 */

#define P_DATA0     (g_base + 0x000)      /* 0x620 */
#define P_DATA0HI   (g_base + 0x002)      /* 0x622 */
#define P_DATA1     (g_base + 0x400)      /* 0xA20 */
#define P_DATA1HI   (g_base + 0x402)      /* 0xA22 */
#define P_DATA3     (g_base + 0x800)      /* 0xE20 */
#define P_PTR       (g_base + 0x802)      /* 0xE22 */

static void RegW(unsigned port, unsigned idx, unsigned voice, unsigned val)
{
    OUTW(P_PTR, (unsigned) ((idx << 5) | (voice & 31)));
    OUTW(port, val);
}

static void RegDW(unsigned port, unsigned idx, unsigned voice, unsigned long val)
{
    OUTW(P_PTR, (unsigned) ((idx << 5) | (voice & 31)));
    OUTW(port,      (unsigned) (val & 0xFFFFu));
    OUTW(port + 2,  (unsigned) (val >> 16));
}

/* Registers we use. The numbers are indices within one data port. */
#define R_CPF       0   /* Data0   */
#define R_PTRX      1   /* Data0   */
#define R_CVCF      2   /* Data0   */
#define R_VTFT      3   /* Data0   */
#define R_PSST      6   /* Data0   */
#define R_CSL       7   /* Data0   */
#define R_CCCA      0   /* Data1   */
#define R_ENVVOL    4   /* Data1   */
#define R_DCYSUSV   5   /* Data1   */
#define R_ENVVAL    6   /* Data1   */
#define R_DCYSUS    7   /* Data1   */
#define R_ATKHLDV   4   /* Data1Hi */
#define R_LFO1VAL   5   /* Data1Hi */
#define R_ATKHLD    6   /* Data1Hi */
#define R_LFO2VAL   7   /* Data1Hi */
#define R_IP        0   /* Data3   */
#define R_IFATN     1   /* Data3   */
#define R_PEFE      2   /* Data3   */
#define R_FMMOD     3   /* Data3   */
#define R_TREMFRQ   4   /* Data3   */
#define R_FM2FRQ2   5   /* Data3   */

/* ------------------------------------------------------------------------ *
 *  Wave ROM samples
 *
 *  The addresses come from SYNTHGM.SBK and for ROM samples they already are
 *  chip addresses. Every AWE32 carries the same ROM, so the signal going into
 *  the chip is known exactly.
 * ------------------------------------------------------------------------ */
typedef struct { unsigned long start, loop_start, loop_end; } SAMPLE;

static SAMPLE SMP_SINE  = { 430271L, 430339L, 430404L };  /* sinewave       */
static SAMPLE SMP_NOISE = { 458995L, 459001L, 467282L };  /* whitenoisewave */
static SAMPLE SMP_TICK  = { 491098L, 491104L, 491164L };  /* sinetick       */

#define IP_UNITY    0xE000u           /* IP for 1:1 playback                */
#define IP_OCT      4096              /* IP units per octave                */

/* Voices taken for direct writes. The MIDI engine allocates from the bottom,
   so we work from the top down. */
#define V_TEST      31
#define V_MARK      30
#define V_EXTRA     22                /* 22..29 for the voice-summing test  */

/* Which blocks to play. Useful for a quick check, and so that a single block
   can be repeated if it went wrong in the recording. */
static int          g_from = 1, g_to = 99;

/* ------------------------------------------------------------------------ *
 *  Record of what was played and when   (declared above the capture code,
 *  which stamps each saved file with the test time)
 * ------------------------------------------------------------------------ */

static void RecPoll(void);

static void LogLine(const char *fmt, long a, long b, long c)
{
    if (!g_log) return;
    fprintf(g_log, "%8lu %2d ", g_ms, g_block);
    fprintf(g_log, fmt, a, b, c);
    fputc('\n', g_log);
}

static void Wait(unsigned ms)
{
    DelayMs(ms);
    g_ms += ms;
}

/* ------------------------------------------------------------------------ *
 *  One tone, written straight into the registers
 * ------------------------------------------------------------------------ */
typedef struct {
    SAMPLE  *smp;
    unsigned ip;          /* pitch                                         */
    unsigned atten;       /* 0..255, 0 = full level                        */
    unsigned cutoff;      /* 0..255, 255 = filter wide open                */
    unsigned q;           /* 0..15                                         */
    unsigned pan;         /* 0..255; in PSST 0 = hard right                */
    unsigned atk;         /* 0..127                                        */
    unsigned hold;        /* 0..127                                        */
    unsigned decay;       /* 0..127                                        */
    unsigned sustain;     /* 0..127, 127 = no drop                         */
    unsigned release;     /* 0..127                                        */
    unsigned envvol;      /* envelope delay, 0x8000 = none                 */
    unsigned pefe;        /* hi = env to pitch, lo = env to filter         */
    unsigned fmmod;       /* hi = LFO1 to pitch, lo = LFO1 to filter       */
    unsigned tremfrq;     /* hi = LFO1 to volume, lo = LFO1 rate           */
    unsigned fm2frq2;     /* hi = LFO2 to pitch, lo = LFO2 rate            */
    unsigned lfo1val;     /* LFO1 delay                                    */
    unsigned lfo2val;     /* LFO2 delay                                    */
    unsigned reverb;      /* 0..255                                        */
    unsigned chorus;      /* 0..255                                        */
} TONE;

static void ToneDefaults(TONE *t)
{
    memset(t, 0, sizeof(*t));
    t->smp     = &SMP_SINE;
    t->ip      = IP_UNITY;
    t->atten   = 0;
    t->cutoff  = 255;
    t->q       = 0;
    t->pan     = 128;
    t->atk     = 0x7F;      /* instant attack   */
    t->hold    = 0x7F;      /* no hold          */
    t->decay   = 0;         /* no decay         */
    t->sustain = 0x7F;      /* holds at full    */
    t->release = 0x7F;
    t->envvol  = 0x8000;
    t->pefe    = 0;
    t->fmmod   = 0;
    t->tremfrq = 0;
    t->fm2frq2 = 0;
    t->lfo1val = 0x8000;
    t->lfo2val = 0x8000;
    t->reverb  = 0;
    t->chorus  = 0;
}

static void VoiceOff(unsigned v)
{
    RegW(P_DATA1, R_DCYSUSV, v, 0x0080);      /* silence                   */
    RegW(P_DATA0, R_VTFT,    v, 0xFFFF);      /* low  half = filter, open  */
    RegW(P_DATA0HI, R_VTFT,  v, 0x0000);      /* high half = volume, mute  */
}

static void ToneStart(unsigned v, TONE *t)
{
    unsigned long ccca, psst, csl;

    /* Same write order the driver uses: silence first, set everything up, and
       write DCYSUSV last - that is what starts the envelope. */
    RegW(P_DATA1,   R_DCYSUSV, v, 0x0080);
    RegW(P_DATA0,   R_VTFT,    v, 0xFFFF);
    RegW(P_DATA0HI, R_VTFT,    v, 0x0000);
    RegW(P_DATA0,   R_CVCF,    v, 0xFFFF);
    RegW(P_DATA0HI, R_CVCF,    v, 0x0000);

    psst = ((unsigned long) (t->pan & 0xFF) << 24) | (t->smp->loop_start & 0xFFFFFFL);
    csl  = ((unsigned long) (t->chorus & 0xFF) << 24) | (t->smp->loop_end & 0xFFFFFFL);
    ccca = ((unsigned long) (t->q & 0x0F) << 28) | (t->smp->start & 0xFFFFFFL);

    RegDW(P_DATA0, R_PSST, v, psst);
    RegDW(P_DATA0, R_CSL,  v, csl);
    RegDW(P_DATA1, R_CCCA, v, ccca);

    RegW(P_DATA3,   R_IP,      v, t->ip);
    RegW(P_DATA3,   R_IFATN,   v, (unsigned) ((t->cutoff << 8) | (t->atten & 0xFF)));
    RegW(P_DATA3,   R_PEFE,    v, t->pefe);
    RegW(P_DATA3,   R_FMMOD,   v, t->fmmod);
    RegW(P_DATA3,   R_TREMFRQ, v, t->tremfrq);
    RegW(P_DATA3,   R_FM2FRQ2, v, t->fm2frq2);

    RegW(P_DATA1,   R_ENVVAL,  v, 0x8000);
    RegW(P_DATA1HI, R_ATKHLD,  v, 0x7F7F);
    RegW(P_DATA1,   R_DCYSUS,  v, 0x7F00);

    RegW(P_DATA1,   R_ENVVOL,  v, t->envvol);
    RegW(P_DATA1HI, R_ATKHLDV, v, (unsigned) ((t->hold << 8) | (t->atk & 0x7F)));
    RegW(P_DATA1HI, R_LFO1VAL, v, t->lfo1val);
    RegW(P_DATA1HI, R_LFO2VAL, v, t->lfo2val);

    /* The reverb send is the low byte of PTRX; the high half is pitch target */
    RegDW(P_DATA0, R_PTRX, v, ((unsigned long) 0x4000L << 16) | (t->reverb & 0xFF));

    /* this one starts the note */
    RegW(P_DATA1, R_DCYSUSV, v,
         (unsigned) (((t->sustain & 0x7F) << 8) | (t->decay & 0x7F)));
}

static void ToneRelease(unsigned v, TONE *t)
{
    RegW(P_DATA1, R_DCYSUSV, v,
         (unsigned) (0x8000 | ((t->sustain & 0x7F) << 8) | (t->release & 0x7F)));
}

/* Nothing should be left sounding when the program stops to ask a question -
   the EMU8000 keeps playing quite happily on its own. */
static void SilenceAll(void)
{
    unsigned v;
    for (v = 0; v < 32; v++) VoiceOff(v);
}

/* Play a tone directly: sounding for `on` ms, then `off` ms of silence. */
static void PlayTone(TONE *t, unsigned on, unsigned off)
{
    ToneStart(V_TEST, t);
    Wait(on);
    VoiceOff(V_TEST);
    Wait(off);
}

/* ------------------------------------------------------------------------ *
 *  Working out how - or whether - the card can record itself
 *
 *  Whether the EMU8000 output reaches the recording multiplexer differs
 *  between board revisions: the SB16 mixer does offer MIDI as a record
 *  source, and on the AWE32 the wavetable is mixed into that path, but on
 *  some boards it is summed after the multiplexer and cannot be captured.
 *  Rather than guess, the card is asked: a loud tone is played and the level
 *  that comes back is measured. Two seconds, and the answer is definitive
 *  for that particular card.
 *
 *  Only the input gain is adjusted. The output mixer is left exactly as the
 *  driver set it, because the external recording is the primary measurement
 *  and must not be disturbed by anything done here.
 * ------------------------------------------------------------------------ */
#define PROBE_MS     700
#define PEAK_USABLE  600           /* above the noise floor by a good margin */
#define PEAK_TARGET  8000          /* comfortable, nowhere near clipping     */
#define PEAK_CLIP    30000

static void SilenceAll(void);

/* Waits for a key, but not forever: if nobody is sitting at the machine the
   run has to carry on by itself. Returns the key, or -1 when the time is up.
   The remaining seconds are shown so it is obvious what is happening. */
static int WaitKey(int seconds, const char *prompt)
{
    int i, k;

    for (i = seconds; i > 0; i--) {
        printf("\r");
        printf(prompt, i);
        fflush(stdout);
        for (k = 0; k < 20; k++) {
            if (kbhit()) {
                int c = getch();
                printf("\n\n");
                return c;
            }
            DelayMs(50);
        }
    }
    printf("\n\n");
    return -1;
}

/* One measurement: play a full-level sine and report the loudest sample that
   came back through the ADC. */
static int Probe(int src, int gain)
{
    TONE t;

    Trace("Probe: src %ld", (long) src);
    if (RecHwStart()) return -1;
    RecSource(src);
    RecGain(gain);

    ToneDefaults(&t);
    t.atten = 0;
    ToneStart(V_TEST, &t);
    Wait(PROBE_MS);
    VoiceOff(V_TEST);
    RecHwStop();
    SilenceAll();
    Trace("Probe: peak %ld", (long) rec_peak);
    return rec_peak;
}

/* Finds the highest input gain that still leaves headroom. Returns the peak
   reached, and writes the chosen gain step through `gain_out`. */
static int AutoLevel(int src, int *gain_out, int *too_hot)
{
    int g, pk, best_pk = 0, best_g = 0;

    *too_hot = 0;
    for (g = 0; g <= 3; g++) {
        pk = Probe(src, g);
        if (pk < 0) return -1;
        if (pk > PEAK_CLIP) {
            /* Already clipping. At the lowest gain there is nothing left to
               turn down, so this is a signal that is present but unusable -
               which must not be reported as silence. */
            if (g == 0) {
                *too_hot  = 1;
                *gain_out = 0;
                return pk;
            }
            break;                      /* one step too far already */
        }
        best_pk = pk;
        best_g  = g;
        if (pk >= PEAK_TARGET) break;   /* loud enough, stop here */
    }
    *gain_out = best_g;
    return best_pk;
}

/* ------------------------------------------------------------------------ *
 *  Markers
 *
 *  Start of every block: the block number in ticks, at a high pitch.
 *  Every minute: two ticks at a low pitch - told apart by pitch.
 * ------------------------------------------------------------------------ */
static unsigned long g_next_minute = 60000L;

static void Tick(int high)
{
    TONE t;
    ToneDefaults(&t);
    t.smp = &SMP_TICK;
    /* IP is 16 bit; IP_UNITY + 2 octaves would be 0x10000 and wrap to zero,
       so the high tick only goes up by one octave. */
    t.ip  = high ? (unsigned) (IP_UNITY + IP_OCT)
                 : (unsigned) (IP_UNITY - 2 * IP_OCT);
    ToneStart(V_MARK, &t);
    Wait(30);
    VoiceOff(V_MARK);
    Wait(70);
}

static unsigned long g_bios0;         /* tik BIOSu na zacatku behu        */

static int  g_steps;                  /* kroku v soucasnem bloku          */
static int  g_step;                   /* z toho hotovych                  */
static char g_bname[48];              /* jmeno bloku pro prekresleni      */

static void SetSteps(int steps)
{
    g_steps = steps;
    g_step  = 0;
}

/* Prekresli radek soucasneho bloku. Zustava na miste, takze vypis
   nenaroste - meni se jen procenta a cas. */
static void ShowProgress(void)
{
    long pct = (g_steps > 0) ? ((long) g_step * 100L / (long) g_steps) : 0L;

    if (pct > 100L) pct = 100L;
    printf("\r%2d. %-44s %3ld%% %3lu:%02lu",
           g_block, g_bname, pct,
           (unsigned long) (g_ms / 60000L),
           (unsigned long) ((g_ms / 1000L) % 60L));
    fflush(stdout);
}

static void MinuteMarkIfDue(void);

/* Konec jednoho kroku bloku: posun ukazatele a minutova znacka. */
static void CapCheck(void);

static void StepDone(void)
{
    if (g_step < g_steps) g_step++;
    ShowProgress();
    RecPoll();              /* vypis na obrazovku trva; nenechat prstenec stat */
    CapCheck();             /* plna pamet? ulozit cyklus a zacit dalsi */
    MinuteMarkIfDue();
}

static void MinuteMarkIfDue(void)
{
    if (g_ms < g_next_minute) return;
    LogLine("MINUTE %ld", (long) (g_next_minute / 1000L), 0, 0);
    Tick(0);
    Tick(0);
    Wait(400);
    g_next_minute += 60000L;
}

void CycleMark(int start)
{
    int i;
    for (i = 0; i < 4; i++) Tick(start ? 1 : 0);
}

/* Konec cyklu: dohrat znacku, zastavit zaznam, ulozit soubor a zase se
   rozjet. Vola se na hranici noty, takze se nic nerozdeli uprostred. */
void CapCheck(void)
{
    if (!cap_buf || !rec_on) return;
    if (cap_used < cap_limit) return;

    CycleMark(0);                  /* jeste se nahrava, znacka bude v souboru */
    RecHwStop();
    CapFlush();
    if (RecHwStart()) {            /* nepovedlo se znovu rozjet - dal bez zaznamu */
        printf("\n*** Could not restart the capture; carrying on without it.\n");
        return;
    }
    cap_t0 = g_ms;
    CycleMark(1);
}

static int BlockMark(int n, const char *name)
{
    int i;
    if (n < g_from || n > g_to) return 0;
    g_block = n;
    if (g_log) fprintf(g_log, "%8lu %2d ---- %s\n", g_ms, n, name);
    if (g_log) fflush(g_log);
    Trace("block %ld", (long) n);
    {   /* planovany vs skutecny cas - 1 tik BIOSu = 54,925 ms */
        unsigned long real_ms = (BiosTicks() - g_bios0) * 10985UL / 200UL;
        if (g_log)
            fprintf(g_log, "%8lu %2d ---- planned %lu ms, real %lu ms\n",
                    g_ms, n, g_ms, real_ms);
    }
    if (g_bname[0]) printf("\n");     /* dokonci radek predchoziho bloku */
    strncpy(g_bname, name, sizeof(g_bname) - 1);
    g_bname[sizeof(g_bname) - 1] = '\0';
    g_steps = 0;
    g_step  = 0;
    ShowProgress();
    for (i = 0; i < n; i++) Tick(1);
    Wait(500);
    MinuteMarkIfDue();
    return 1;
}

/* ------------------------------------------------------------------------ *
 *  Direct-write blocks
 * ------------------------------------------------------------------------ */
static void BlockReference(int n)
{
    TONE t;
    if (!BlockMark(n, "reference tone")) return;
    SetSteps(1);
    ToneDefaults(&t);
    LogLine("reference sine, IP %ld, atten %ld", (long) t.ip, 0, 0);
    PlayTone(&t, 3000, 1000);
}

static void BlockAtten(int n)
{
    TONE t;
    int a;
    if (!BlockMark(n, "attenuation IFATN 0..126 step 2")) return;
    SetSteps(64);
    for (a = 0; a <= 126; a += 2) {
        ToneDefaults(&t);
        t.atten = (unsigned) a;
        LogLine("atten %ld", (long) a, 0, 0);
        PlayTone(&t, 350, 250);
        StepDone();
    }
}

static void BlockPan(int n)
{
    TONE t;
    int p;
    if (!BlockMark(n, "pan PSST 0..255 step 8")) return;
    SetSteps(32);
    for (p = 0; p <= 255; p += 8) {
        ToneDefaults(&t);
        t.pan = (unsigned) p;
        LogLine("pan %ld", (long) p, 0, 0);
        PlayTone(&t, 350, 250);
        StepDone();
    }
}

static void BlockPitch(int n, SAMPLE *smp, const char *name)
{
    TONE t;
    int s;
    if (!BlockMark(n, name)) return;
    SetSteps(60);
    /* +24 semitones would be 0xE000 + 8192 = 0x10000, i.e. a wrap. Stop at +23. */
    for (s = -36; s <= 23; s++) {
        ToneDefaults(&t);
        t.smp = smp;
        t.ip  = (unsigned) (IP_UNITY + (long) s * IP_OCT / 12);
        LogLine("semitone %ld, IP %ld", (long) s, (long) t.ip, 0);
        PlayTone(&t, 350, 250);
        StepDone();
    }
}

static void BlockCutoff(int n)
{
    TONE t;
    int c;
    if (!BlockMark(n, "filter cutoff, noise, Q=0")) return;
    SetSteps(64);
    for (c = 0; c <= 252; c += 4) {
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.cutoff = (unsigned) c;
        LogLine("cutoff %ld", (long) c, 0, 0);
        PlayTone(&t, 450, 250);
        StepDone();
    }
}

static void BlockResonance(int n)
{
    static int cuts[6] = { 32, 64, 96, 144, 192, 255 };
    TONE t;
    int i, q;
    if (!BlockMark(n, "resonance, noise, 6 cutoffs x Q 0..15")) return;
    SetSteps(96);
    for (i = 0; i < 6; i++) {
        for (q = 0; q <= 15; q++) {
            ToneDefaults(&t);
            t.smp    = &SMP_NOISE;
            t.cutoff = (unsigned) cuts[i];
            t.q      = (unsigned) q;
            LogLine("cutoff %ld, Q %ld", (long) cuts[i], (long) q, 0);
            PlayTone(&t, 450, 250);
            StepDone();
        }
    }
}

/* Fast envelopes are over in a few milliseconds, slow ones take seconds. The
   tone length therefore follows what is being measured - otherwise the fast
   rates would waste time and the slow ones would not show at all. */
static unsigned EnvHold(int rate)
{
    if (rate >= 0x70) return 600;
    if (rate >= 0x50) return 1000;
    if (rate >= 0x30) return 1800;
    return 2800;
}

static void BlockAttack(int n)
{
    TONE t;
    int r;
    if (!BlockMark(n, "envelope: attack 0x04..0x7F")) return;
    SetSteps(32);
    for (r = 4; r <= 0x7F; r += 4) {
        ToneDefaults(&t);
        t.atk = (unsigned) r;
        LogLine("attack 0x%02lX", (long) r, 0, 0);
        PlayTone(&t, EnvHold(r), 400);
        StepDone();
    }
}

static void BlockHold(int n)
{
    TONE t;
    int h;
    if (!BlockMark(n, "envelope: hold")) return;
    SetSteps(16);
    for (h = 0x7F; h >= 0x60; h -= 2) {
        ToneDefaults(&t);
        t.hold  = (unsigned) h;
        t.decay = 0x50;              /* so the end of hold is audible */
        LogLine("hold 0x%02lX", (long) h, 0, 0);
        PlayTone(&t, 1600, 400);
        StepDone();
    }
}

static void BlockDecay(int n)
{
    TONE t;
    int r;
    if (!BlockMark(n, "envelope: decay 0x04..0x7F")) return;
    SetSteps(32);
    for (r = 4; r <= 0x7F; r += 4) {
        ToneDefaults(&t);
        t.decay   = (unsigned) r;
        t.sustain = 0x20;            /* low enough for the drop to show */
        LogLine("decay 0x%02lX, sustain 0x20", (long) r, 0, 0);
        PlayTone(&t, EnvHold(r) + 400, 400);
        StepDone();
    }
}

static void BlockSustain(int n)
{
    TONE t;
    int s;
    if (!BlockMark(n, "envelope: sustain")) return;
    SetSteps(16);
    for (s = 0; s <= 0x7F; s += 8) {
        ToneDefaults(&t);
        t.decay   = 0x60;
        t.sustain = (unsigned) s;
        LogLine("sustain 0x%02lX", (long) s, 0, 0);
        PlayTone(&t, 1600, 400);
        StepDone();
    }
}

static void BlockRelease(int n)
{
    TONE t;
    int r;
    if (!BlockMark(n, "envelope: release 0x04..0x7F")) return;
    SetSteps(32);
    for (r = 4; r <= 0x7F; r += 4) {
        ToneDefaults(&t);
        t.release = (unsigned) r;
        LogLine("release 0x%02lX", (long) r, 0, 0);
        ToneStart(V_TEST, &t);
        Wait(500);
        ToneRelease(V_TEST, &t);
        Wait(EnvHold(r));
        VoiceOff(V_TEST);
        Wait(400);
        StepDone();
    }
}

static void BlockEnvDelay(int n)
{
    TONE t;
    int d;
    if (!BlockMark(n, "envelope: ENVVOL delay")) return;
    SetSteps(16);
    for (d = 0; d < 16; d++) {
        ToneDefaults(&t);
        t.envvol = (unsigned) (0x8000 - d * 400);
        LogLine("envvol 0x%04lX", (long) t.envvol, 0, 0);
        PlayTone(&t, 1600, 400);
        StepDone();
    }
}

static void BlockModEnv(int n, int to_pitch)
{
    TONE t;
    int d;
    if (!BlockMark(n, to_pitch ? "modulation envelope -> pitch"
                               : "modulation envelope -> filter")) return;
    SetSteps(16);
    for (d = 0; d < 16; d++) {
        int depth = -128 + d * 16;               /* -128..112 */
        ToneDefaults(&t);
        if (to_pitch) {
            t.pefe = (unsigned) ((depth & 0xFF) << 8);
        } else {
            t.smp    = &SMP_NOISE;
            t.cutoff = 128;
            t.pefe   = (unsigned) (depth & 0xFF);
        }
        LogLine("depth %ld, PEFE 0x%04lX", (long) depth, (long) t.pefe, 0);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
}

static void BlockLfo1(int n, int what)
{
    /* what: 0 = rate, 1 = volume, 2 = pitch, 3 = filter */
    static const char *names[4] = {
        "LFO1: rate", "LFO1 -> volume (tremolo)",
        "LFO1 -> pitch (vibrato)", "LFO1 -> filter (wah)"
    };
    TONE t;
    int i;
    if (!BlockMark(n, names[what])) return;
    SetSteps(16);
    for (i = 0; i < 16; i++) {
        ToneDefaults(&t);
        switch (what) {
        case 0:
            t.tremfrq = (unsigned) ((0x40 << 8) | (i * 17));
            break;
        case 1:
            t.tremfrq = (unsigned) ((((-128 + i * 16) & 0xFF) << 8) | 0x40);
            break;
        case 2:
            t.fmmod   = (unsigned) ((((-128 + i * 16) & 0xFF) << 8));
            t.tremfrq = 0x0040;
            break;
        default:
            t.smp     = &SMP_NOISE;
            t.cutoff  = 128;
            t.fmmod   = (unsigned) ((-128 + i * 16) & 0xFF);
            t.tremfrq = 0x0040;
            break;
        }
        LogLine("step %ld, TREMFRQ 0x%04lX, FMMOD 0x%04lX",
                (long) i, (long) t.tremfrq, (long) t.fmmod);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
}

static void BlockLfo2(int n)
{
    TONE t;
    int i;
    if (!BlockMark(n, "LFO2: rate and depth")) return;
    SetSteps(32);
    for (i = 0; i < 16; i++) {           /* rate at a fixed depth */
        ToneDefaults(&t);
        t.fm2frq2 = (unsigned) ((0x40 << 8) | (i * 17));
        LogLine("rate, FM2FRQ2 0x%04lX", (long) t.fm2frq2, 0, 0);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
    for (i = 0; i < 16; i++) {           /* depth at a fixed rate */
        ToneDefaults(&t);
        t.fm2frq2 = (unsigned) ((((-128 + i * 16) & 0xFF) << 8) | 0x40);
        LogLine("depth, FM2FRQ2 0x%04lX", (long) t.fm2frq2, 0, 0);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
}

static void BlockLfoDelay(int n)
{
    TONE t;
    int i;
    if (!BlockMark(n, "LFO delay")) return;
    SetSteps(12);
    for (i = 0; i < 12; i++) {
        ToneDefaults(&t);
        t.tremfrq = (unsigned) ((0x60 << 8) | 0x40);
        t.lfo1val = (unsigned) (0x8000 - i * 500);
        LogLine("LFO1VAL 0x%04lX", (long) t.lfo1val, 0, 0);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
}

static void BlockEffect(int n, int is_reverb)
{
    static int sends[5] = { 0, 48, 96, 160, 255 };
    TONE t;
    int p, s;
    if (!BlockMark(n, is_reverb ? "reverb: 8 presets x 5 send levels"
                                : "chorus: 8 presets x 5 send levels")) return;
    SetSteps(40);
    for (p = 0; p < 8; p++) {
        Trace(is_reverb ? "reverb preset %ld: calling" : "chorus preset %ld: calling",
              (long) p);
        if (is_reverb) awe32Reverb((WORD) p); else awe32Chorus((WORD) p);
        Trace(is_reverb ? "reverb preset %ld: returned" : "chorus preset %ld: returned",
              (long) p);
        for (s = 0; s < 5; s++) {
            ToneDefaults(&t);
            t.decay   = 0x60;          /* short tone so the tail is audible */
            t.sustain = 0x00;
            if (is_reverb) t.reverb = (unsigned) sends[s];
            else           t.chorus = (unsigned) sends[s];
            LogLine("preset %ld, send %ld", (long) p, (long) sends[s], 0);
            PlayTone(&t, 700, 1100);
            StepDone();
        }
    }
    Trace("effect block: resetting", 0);
    if (is_reverb) awe32Reverb(0); else awe32Chorus(0);
    Trace("effect block: done", 0);
}

static void BlockLoop(int n)
{
    TONE t;
    int i;
    static int semis[3] = { -12, 0, 7 };
    if (!BlockMark(n, "loop: long sustained tones")) return;
    SetSteps(6);
    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        t.ip = (unsigned) (IP_UNITY + (long) semis[i] * IP_OCT / 12);
        LogLine("sine, semitone %ld", (long) semis[i], 0, 0);
        PlayTone(&t, 3500, 500);
        StepDone();
    }
    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        t.smp = &SMP_NOISE;
        t.ip  = (unsigned) (IP_UNITY + (long) semis[i] * IP_OCT / 12);
        LogLine("noise, semitone %ld", (long) semis[i], 0, 0);
        PlayTone(&t, 3500, 500);
        StepDone();
    }
}

static void BlockVoiceSum(int n)
{
    static int counts[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    TONE t;
    int i, k;
    if (!BlockMark(n, "voice summing 1..16")) return;
    SetSteps(8);
    for (i = 0; i < 8; i++) {
        LogLine("voices %ld", (long) counts[i], 0, 0);
        for (k = 0; k < counts[i]; k++) {
            ToneDefaults(&t);
            /* same tone, detuned by a few IP units so they do not add in phase */
            t.ip = (unsigned) (IP_UNITY + k);
            ToneStart((unsigned) (V_EXTRA - k), &t);
        }
        Wait(1200);
        for (k = 0; k < counts[i]; k++) VoiceOff((unsigned) (V_EXTRA - k));
        Wait(800);
        StepDone();
    }
}

/* ------------------------------------------------------------------------ *
 *  Blocks through the MIDI API - exactly the way a game plays
 * ------------------------------------------------------------------------ */
static void MidiNote(WORD ch, WORD note, WORD vel, unsigned on, unsigned off)
{
    awe32NoteOn(ch, note, vel);
    Wait(on);
    awe32NoteOff(ch, note, 64);
    Wait(off);
}

static void BlockMidiBank(int n, int bank, int nprog, const char *name)
{
    static WORD notes[5] = { 36, 48, 60, 72, 84 };
    static WORD vels[2]  = { 40, 120 };
    int p, i, v;

    if (!BlockMark(n, name)) return;
    SetSteps(nprog * 10);
    awe32Controller(0, 0, (WORD) bank);       /* CC0 = bank select */
    for (p = 0; p < nprog; p++) {
        awe32ProgramChange(0, (WORD) p);
        for (i = 0; i < 5; i++) {
            for (v = 0; v < 2; v++) {
                LogLine("preset %ld, note %ld, velocity %ld",
                        (long) p, (long) notes[i], (long) vels[v]);
                MidiNote(0, notes[i], vels[v], 450, 250);
                StepDone();
            }
        }
    }
}

/* ------------------------------------------------------------------------ *
 *  Load BULLFROG.SBK the same way the game loads it
 * ------------------------------------------------------------------------ */
static SOUND_PACKET spSound;
static long         lBankSizes[2];   /* type follows SOUND_PACKET.banksizes */
static char        *pPresets;
static char         Packet[PACKETSIZE];
static int          g_have_bank = 0;

/* ------------------------------------------------------------------------ *
 *  Finding things without asking the user
 *
 *  Magic Carpet 2 keeps BULLFROG.SBK in SOUND\ **on the CD** - the installer
 *  copies only the drivers to the hard disk - so looking in the current
 *  directory alone would nearly always miss it. We therefore try, in order:
 *
 *      the current directory and SOUND\ under it
 *      the directory this program was started from, and SOUND\ under that
 *      C:\NETHERW\SOUND\, where the game's own installer puts things
 *      SOUND\ on every CD-ROM drive MSCDEX reports
 *
 *  Only drives MSCDEX actually knows about are touched, so nothing pops up
 *  a "not ready" error on an empty floppy.
 *
 *  The game's own settings are read too: SOUND\MDI.INI holds the Miles
 *  driver configuration written by SETSOUND.EXE. Its IO_ADDR is normally -1
 *  ("detect"), in which case BLASTER decides, but if it names a port we take
 *  that - it is what the game itself would use.
 * ------------------------------------------------------------------------ */
static char  bank_path[128];
static char  game_dir[128];        /* where MDI.INI was found, with slash    */

static int FileThere(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int TryBank(const char *dir, const char *name)
{
    bank_path[0] = 0;
    if (dir && *dir) { strcpy(bank_path, dir); strcat(bank_path, name); }
    else             { strcpy(bank_path, name); }
    if (FileThere(bank_path)) return 1;
    bank_path[0] = 0;
    return 0;
}

/* Directory the program was started from, trailing separator included. */
static void ExeDir(const char *argv0, char *out)
{
    int i, cut = -1;
    for (i = 0; argv0[i]; i++)
        if (argv0[i] == '\\' || argv0[i] == '/' || argv0[i] == ':') cut = i;
    if (cut < 0) { out[0] = 0; return; }
    memcpy(out, argv0, (size_t) (cut + 1));
    out[cut + 1] = 0;
}

/* MSCDEX: how many CD-ROM drives there are and which letter is first. */
static int CdDrives(int *first)
{
    union REGS r;
    memset(&r, 0, sizeof(r));
    r.w.ax = 0x1500;
    r.w.bx = 0;
    REGCALL(0x2F, &r, &r);
    *first = (int) r.w.cx;
    return (int) r.w.bx;
}

static int FindBank(const char *argv0)
{
    char exed[96], tmp[128];
    int  firstcd, ncd, i;

    if (TryBank(0, "BULLFROG.SBK"))         return 1;
    if (TryBank("SOUND\\", "BULLFROG.SBK")) return 1;

    ExeDir(argv0, exed);
    if (exed[0]) {
        if (TryBank(exed, "BULLFROG.SBK")) return 1;
        strcpy(tmp, exed); strcat(tmp, "SOUND\\");
        if (TryBank(tmp, "BULLFROG.SBK")) return 1;
    }

    if (game_dir[0] && TryBank(game_dir, "BULLFROG.SBK")) return 1;
    if (TryBank("C:\\NETHERW\\SOUND\\", "BULLFROG.SBK"))  return 1;

    ncd = CdDrives(&firstcd);
    for (i = 0; i < ncd && i < 26; i++) {
        tmp[0] = (char) ('A' + firstcd + i);
        tmp[1] = ':'; tmp[2] = 0;
        strcat(tmp, "\\SOUND\\");
        if (TryBank(tmp, "BULLFROG.SBK")) return 1;
    }
    return 0;
}

/* Read IO_ADDR out of the game's MDI.INI. Returns 0 when there is nothing
   usable there (the usual "-1", meaning detect). */
static int GameIoAddr(const char *argv0)
{
    static const char *dirs[4] = { "SOUND\\", "C:\\NETHERW\\SOUND\\", "", 0 };
    char  exed[96], path[128], line[128];
    FILE *f;
    int   i, port = 0;

    ExeDir(argv0, exed);
    game_dir[0] = 0;

    for (i = 0; i < 4; i++) {
        if (i == 3) { strcpy(path, exed); strcat(path, "SOUND\\MDI.INI"); }
        else if (!dirs[i]) break;
        else { strcpy(path, dirs[i]); strcat(path, "MDI.INI"); }
        if (!FileThere(path)) continue;

        f = fopen(path, "r");
        if (!f) continue;
        /* remember the directory - the bank may be beside it */
        strcpy(game_dir, path);
        game_dir[strlen(game_dir) - 7] = 0;      /* strip "MDI.INI" */
        printf("Game sound config: %s\n", path);
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "IO_ADDR", 7)) {
                char *q = line + 7;
                while (*q == ' ' || *q == '\t') q++;
                if (*q != '-') port = (int) strtol(q, (char **) 0, 16);
            } else if (!strncmp(line, "DRIVER", 6)) {
                char *q = line + 6;
                while (*q == ' ' || *q == '\t') q++;
                printf("  driver: %s", q);
            }
        }
        fclose(f);
        break;
    }
    return port;
}

static int LoadBank(const char *file)
{
    FILE *fp;
    WORD  i;

    fp = fopen(file, "rb");
    if (!fp) return 1;

    spSound.bank_no     = 1;                  /* load as bank 1 */
    spSound.total_banks = 2;
    lBankSizes[0] = 0;
    lBankSizes[1] = (long) spSound.total_patch_ram;
    spSound.banksizes = lBankSizes;
    awe32DefineBankSizes(&spSound);

    spSound.data = Packet;
    fread(Packet, 1, PACKETSIZE, fp);
    if (awe32SFontLoadRequest(&spSound)) { fclose(fp); return 2; }

    fseek(fp, spSound.sample_seek, SEEK_SET);
    for (i = 0; i < spSound.no_sample_packets; i++) {
        fread(Packet, 1, PACKETSIZE, fp);
        awe32StreamSample(&spSound);
    }

    fseek(fp, spSound.preset_seek, SEEK_SET);
    pPresets = (char *) malloc((unsigned) spSound.preset_read_size);
    if (!pPresets) { fclose(fp); return 3; }
    fread(pPresets, 1, (unsigned) spSound.preset_read_size, fp);
    spSound.presets = pPresets;
    if (awe32SetPresets(&spSound)) { fclose(fp); return 4; }

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------------ *
 *  main
 * ------------------------------------------------------------------------ */
/* BLASTER looks like "A220 I5 D1 H5 P330 E620 T6". Each field is a letter
   followed by a number - hexadecimal for the port, decimal for the rest. */
static WORD GetBasePort(void)
{
    char *b = getenv("BLASTER");
    WORD  port = 0x220;
    int   prev = ' ';

    if (!b) return port;
    while (*b) {
        if (prev == ' ' || prev == '\t') {
            if (*b == 'A' || *b == 'a')
                port = (WORD) strtol(b + 1, (char **) 0, 16);
            else if (*b == 'I' || *b == 'i')
                rec_irq = (int) strtol(b + 1, (char **) 0, 10);
            else if (*b == 'H' || *b == 'h')
                rec_dma = (int) strtol(b + 1, (char **) 0, 10);
        }
        prev = *b;
        b++;
    }
    if (rec_dma < 5 || rec_dma > 7) rec_dma = 5;
    if (rec_irq < 2 || rec_irq > 15) rec_irq = 5;
    return port;
}

int main(int argc, char **argv)
{
    WORD sb = GetBasePort();
    const char *rec_name = 0;      /* cleared again if no capture path works */
    const char *sbk_arg  = 0;
    int         game_port;
    int  n;
    int  rc;

    for (n = 1; n < argc; n++) {
        if (!strncmp(argv[n], "/FROM:", 6) || !strncmp(argv[n], "/from:", 6))
            g_from = atoi(argv[n] + 6);
        else if (!strncmp(argv[n], "/TO:", 4) || !strncmp(argv[n], "/to:", 4))
            g_to = atoi(argv[n] + 4);
        else if (!strncmp(argv[n], "/MB:", 4) || !strncmp(argv[n], "/mb:", 4))
            cap_cap = (unsigned long) atol(argv[n] + 4) * 1024UL * 1024UL;
        else if (!strncmp(argv[n], "/REC:", 5) || !strncmp(argv[n], "/rec:", 5))
            rec_name = argv[n] + 5;
        else if (!strncmp(argv[n], "/SBK:", 5) || !strncmp(argv[n], "/sbk:", 5))
            sbk_arg = argv[n] + 5;
        else {
            printf("Usage: AWETEST [/FROM:n] [/TO:n] [/MB:n] [/REC:file.wav]"
                   " [/SBK:path]\n");
            printf("  with no switches all 28 blocks play (22.6 minutes)\n");
            printf("  /REC also captures the card's own output to a WAV file\n");
            printf("       (44.1 kHz stereo, about 240 MB for the full run)\n");
            printf("  /SBK points at BULLFROG.SBK. Normally not needed - it is\n");
            printf("       looked for on the CD and in the installed game.\n");
            return 0;
        }
    }
    n = 1;

    remove("AWETRACE.LOG");
    Trace("start", 0);
    printf("AWETEST - AWE32 calibration recording\n");

    /* The game's own configuration wins when it names a port; otherwise it
       says "-1" for detect and the BLASTER variable decides, exactly as the
       game would do it. */
    game_port = GameIoAddr(argv[0]);
    if (game_port) {
        printf("Sound Blaster at port 0x%03X (from the game config)\n", game_port);
        sb = (WORD) game_port;
    } else {
        printf("Sound Blaster at port 0x%03X (from BLASTER)\n", sb);
    }

    if (awe32Detect(sb)) {
        printf("ERROR: no AWE32 found.\n");
        return 1;
    }
    if (awe32InitHardware()) {
        printf("ERROR: AWE32 initialisation failed.\n");
        return 1;
    }
    g_sb_base = (unsigned) sb;
    g_base    = (unsigned) (sb + 0x400);
    Trace("hardware ok, base %03lX", (long) sb);
    Trace("  irq %ld", (long) rec_irq);
    Trace("  dma %ld", (long) rec_dma);

    awe32TotalPatchRam(&spSound);
    printf("Free sample RAM: %lu bytes\n", (unsigned long) spSound.total_patch_ram);

    if (sbk_arg) {
        strcpy(bank_path, sbk_arg);
        rc = FileThere(bank_path) ? LoadBank(bank_path) : -1;
    } else {
        rc = FindBank(argv[0]) ? LoadBank(bank_path) : -1;
    }

    if (rc == -1) {
        printf("BULLFROG.SBK not found - block 27 will be skipped.\n");
        printf("  It lives in SOUND\\ on the Magic Carpet 2 CD; the installer\n");
        printf("  does not copy it to the hard disk. Put the CD in, or use\n");
        printf("  /SBK:D:\\SOUND\\BULLFROG.SBK\n");
    } else if (rc) {
        printf("%s could not be loaded (%d) - block 27 skipped.\n", bank_path, rc);
    } else {
        g_have_bank = 1;
        printf("%s loaded as bank 1.\n", bank_path);
    }

    awe32InitMIDI();
    awe32InitNRPN();
    awe32Reverb(0);
    awe32Chorus(0);

    g_log = fopen("AWETEST.LOG", "w");
    if (g_log) fprintf(g_log, "# ms block description\n");

    /* ---- can this card record itself, and if so, how? ------------------ *
     *  Asked before anything else, because the answer decides whether the
     *  tester needs to bother with an external recorder at all.
     * -------------------------------------------------------------------- */
    {
        int wt_gain = 0, li_gain = 0;
        int wt_peak, li_peak;
        int wt_hot  = 0, li_hot  = 0;

        printf("\nChecking how this card can record itself...\n");
        MixerInit();
        printf("  Mixer set to a known state: master and wavetable at full,\n");
        printf("  CD and line muted in the output mix.\n");

        /* MIDI first: on the AWE32 the wavetable is mixed into that path, so
           if the board routes it to the recording multiplexer at all, this is
           where it shows up - and then no cable and no external recorder are
           needed. */
        Trace("probing wavetable", 0);
        wt_peak = AutoLevel(SRC_WAVETABLE, &wt_gain, &wt_hot);
        if (wt_peak < 0) {
            printf("  MIDI / wavetable : cannot test (no DMA buffer)\n");
            wt_peak = 0;
        } else {
            printf("  MIDI / wavetable : peak %d of 32767 at %d dB gain%s\n",
                   wt_peak, wt_gain * 6, wt_hot ? "  - CLIPPING" : "");
        }

        if (wt_hot) wt_peak = 0;        /* present, but unusable */
        if (wt_peak >= PEAK_USABLE) {
            rec_ok = 1;
            RecSource(SRC_WAVETABLE);
            RecGain(wt_gain);
        } else {
            /* Then line in, in case a loopback cable is already fitted. */
            Trace("probing line in", 0);
            li_peak = AutoLevel(SRC_LINEIN, &li_gain, &li_hot);
            if (li_peak < 0) li_peak = 0;
            printf("  line in          : peak %d of 32767 at %d dB gain%s\n",
                   li_peak, li_gain * 6, li_hot ? "  - CLIPPING" : "");

            if (li_hot) li_peak = 0;
            if (li_peak >= PEAK_USABLE) {
                rec_ok = 1;
                RecSource(SRC_LINEIN);
                RecGain(li_gain);
            }
        }

        /* Neither worked. Offer the cable, and keep offering: someone who
           goes looking for one should not have to restart the program, and
           a cable in the wrong socket should not end the matter either. */
        while (!rec_ok) {
            int key;

            if (li_hot) {
                printf("\n  The recording input is OVERLOADED - the signal is\n");
                printf("  there, but it clips even at the lowest input gain.\n");
                printf("  Turn the card's output volume down (or put an\n");
                printf("  attenuator in the loopback cable) and try again.\n\n");
            } else {
                printf("\n  Nothing reached the recording input. The card can\n");
                printf("  still record itself if you connect its LINE OUT to\n");
                printf("  its LINE IN with a short audio cable.\n\n");
            }
            printf("    [C] try again\n");
            printf("    [E] carry on without it - record externally\n\n");

            Trace("asking about the cable", 0);
            key = WaitKey(60, "  C or E (E in %2d s) ");

            if (key == 'c' || key == 'C') {
                Trace("retesting line in", 0);
                li_peak = AutoLevel(SRC_LINEIN, &li_gain, &li_hot);
                if (li_peak < 0) li_peak = 0;
                printf("  line in          : peak %d of 32767 at %d dB gain%s\n",
                       li_peak, li_gain * 6, li_hot ? "  - CLIPPING" : "");
                if (li_hot) li_peak = 0;
                if (li_peak >= PEAK_USABLE) {
                    rec_ok = 1;
                    RecSource(SRC_LINEIN);
                    RecGain(li_gain);
                }
                /* still nothing - round again, the loop asks once more */
            } else {
                break;                  /* E, ESC or the time ran out */
            }
        }

        printf("\n=> ");
        if (!rec_ok) {
            printf("no internal capture - use an EXTERNAL recorder on\n");
            printf("   the card's line output.\n");
            if (rec_name) {
                printf("   /REC would only write silence, so it is turned off.\n");
                rec_name = 0;
            }
        } else {
            printf("the card CAN record itself, through %s.\n",
                   rec_src == SRC_WAVETABLE ? "MIDI / wavetable" : "line in");
            if (!rec_name)
                printf("   Add /REC:OUT.WAV to use it; recording externally"
                       " otherwise.\n");
        }
    }

    if (rec_name) {
        if (RecStart(rec_name))
            printf("Capture disabled; the external recording is what counts.\n");
        else
            printf("Capturing to %s (44.1 kHz stereo).\n", rec_name);
    }

    Trace("path decided, starting the run", 0);
    printf("\nStarting. Record in STEREO, with no equalisation.\n");
    printf("First sound in 5 seconds.\n\n");
    Wait(5000);
    Trace("countdown done", 0);
    g_ms = 0;
    g_bios0 = BiosTicks();                 /* time is counted from the first sound */
    g_next_minute = 60000L;

    BlockReference(n++);
    BlockAtten(n++);
    BlockPan(n++);
    BlockPitch(n++, &SMP_SINE,  "pitch: sine -36..+23 semitones");
    BlockPitch(n++, &SMP_NOISE, "pitch: noise -36..+23 semitones (interpolation)");
    BlockCutoff(n++);
    BlockResonance(n++);
    BlockAttack(n++);
    BlockHold(n++);
    BlockDecay(n++);
    BlockSustain(n++);
    BlockRelease(n++);
    BlockEnvDelay(n++);
    BlockModEnv(n++, 1);
    BlockModEnv(n++, 0);
    BlockLfo1(n++, 0);
    BlockLfo1(n++, 1);
    BlockLfo1(n++, 2);
    BlockLfo1(n++, 3);
    BlockLfo2(n++);
    BlockLfoDelay(n++);
    BlockEffect(n++, 1);
    BlockEffect(n++, 0);
    BlockLoop(n++);
    BlockVoiceSum(n++);
    BlockMidiBank(n++, 0, 16, "as a game does: ROM GM presets 0..15");
    if (g_have_bank)
        BlockMidiBank(n++, 1, 15, "as a game does: BULLFROG.SBK");
    else
        n++;
    BlockReference(n++);

    RecStop();
    if (g_bname[0]) printf("\n");
    printf("\nDone, %lu seconds total.\n", (unsigned long) (g_ms / 1000L));
    if (g_log) { fprintf(g_log, "%8lu -- END\n", g_ms); fclose(g_log); }

    awe32ReleaseAllBanks(&spSound);
    if (pPresets) free(pPresets);
    awe32Terminate();
    return 0;
}
