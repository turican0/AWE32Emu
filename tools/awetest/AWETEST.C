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
  #define INW(p)      inpw((p))
#else
  #include <dos.h>
  #define OUTB(p,v)   outportb((p),(v))
  #define OUTW(p,v)   outport((p),(v))
  #define INB(p)      inportb((p))
  #define INW(p)      inport((p))
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

/* BIOS tick: 18.2 Hz, i.e. one increment per wrap of channel 0.
   The interrupt increments it even while we wait for the disk, so it is
   the only thing that cannot lose time. */
static unsigned long BiosTicks(void)
{
    unsigned long a, b;

    /* Two reads in a row instead of disabling interrupts - when they match,
       the value did not change under our hands. Disabling interrupts is too
       expensive here for how often this is called. */
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

/* PIT cycles really spent waiting. The channel 2 counter runs on its own
   and does not depend on interrupts, so it is a clock source independent
   of the BIOS ticks. Used to find out where time gets lost. */
static unsigned long g_pit_lo;         /* PIT cycles, low part      */
static unsigned long g_pit_sec;        /* and whole seconds of them */

static void PitAdd(unsigned long cycles)
{
    g_pit_lo += cycles;
    while (g_pit_lo >= PIT_HZ) {
        g_pit_lo -= PIT_HZ;
        g_pit_sec++;
    }
}

/* Real time for the log timestamps.
 *
 * g_ms is the PLANNED time and PIT channel 2 counts only the waiting, so
 * neither includes the overhead (log writes, registers, disk). In run5 the
 * log grid therefore drifted and in block 4 every note was assigned one
 * off. Here the BIOS tick plus the phase of channel 0 is used: a time that
 * loses nothing.
 *
 * Channel 0 is switched to mode 2 (the divisor 65536 stays, the interrupt
 * still fires 18.2x per second). In mode 3, as the BIOS leaves it, the
 * counter runs twice per period in steps of two and one read cannot tell
 * which half it is in.
 * The timestamp is "tick:phase", phase 0..65535 in PIT cycles (1193182 per
 * second).
 */
static int g_rt_mode2;

static void RtInit(void)
{
    OUTB(0x43, 0x34);                 /* channel 0, LSB+MSB, mode 2 */
    OUTB(0x40, 0x00);
    OUTB(0x40, 0x00);
    g_rt_mode2 = 1;
}

static void RtDone(void)
{
    if (!g_rt_mode2) return;
    OUTB(0x43, 0x36);                 /* back to mode 3, as the BIOS sets it */
    OUTB(0x40, 0x00);
    OUTB(0x40, 0x00);
    g_rt_mode2 = 0;
}

static void RtNow(unsigned long *ticks, unsigned *phase)
{
    unsigned long t0, t1;
    unsigned      c, ph;

    for (;;) {
        t0 = BiosTicks();
        OUTB(0x43, 0x00);             /* latch channel 0 */
        c  = (unsigned) INB(0x40);
        c |= ((unsigned) INB(0x40)) << 8;
        t1 = BiosTicks();
        ph = (unsigned) ((0x10000UL - (unsigned long) c) & 0xFFFFUL);
        /* Right after the counter reload the phase has started again, but the
           interrupt that increments the tick may not have run yet - the time
           would come out 55 ms early. So we rather wait out the first
           millisecond of the period. */
        if (t0 == t1 && ph >= 0x0500u) break;
    }
    *ticks = t0;
    *phase = ph;
}

static long RtCycles(unsigned long tk0, unsigned ph0,
                     unsigned long tk1, unsigned ph1)
{
    return (long) ((tk1 - tk0) * 65536UL) + (long) ph1 - (long) ph0;
}

static void DelayMs(unsigned ms)
{
    unsigned long ticks;
    unsigned      last, cur;
    unsigned long done = 0;
    unsigned long bios0, bdelta, floor_ticks;

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

        /* When RecPoll() held us longer than one wrap, the line above lost it
           and the wait would run long. BIOS ticks give a lower bound that
           cannot get lost: n ticks mean at least (n-1) wraps, i.e.
           (n-1)*65536 PIT ticks. It is a bound, not an exact time, so it never
           shortens the wait below the right value - it only catches up what
           was lost.

           Formerly this was tried only every 64th iteration. But when RecPoll
           takes tens of ms, a short wait runs only a few iterations and the
           safeguard never applied - the run then fell behind by 12 %
           (measured at the tester 2026-09-08). Two memory reads cost nothing
           next to RecPoll. */
        bdelta = BiosTicks() - bios0;
        if (bdelta > 1UL) {
            floor_ticks = (bdelta - 1UL) * 65536UL;
            if (floor_ticks > done) done = floor_ticks;
        }

        RecPoll();          /* the capture is drained from right here */
    }
    OUTB(0x61, INB(0x61) & 0xFC);
    PitAdd(done);
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
/* Measured on a real machine: blocks 1-22 took 999 s, the whole run comes
   to over two and a half thousand seconds with the MIDI blocks at the end. */
#define AWETEST_SECONDS 3530L     /* v28: blocks 43-46 (+300 s); v27: block 42 (+130 s); v26: blocks 40-41 (+100 s); v25 longer gaps and blocks 35-38 */
/* 64 kB = 372 ms of sound, i.e. twice the reserve for when the disk cannot
   keep up for a moment. If 128 kB cannot be had in DOS, it falls back to
   32 kB. */
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
static unsigned long  rec_hdr_at;      /* rec_total at the last header write */
static unsigned long  rec_tick;        /* BIOS tick at the last drain         */
static unsigned long  rec_drops;       /* how many times the ring overran     */
static int            rec_on;
static int            rec_any;         /* did anything but silence arrive?  */
static int            rec_ok;          /* a usable capture path was found   */
static int            rec_peak;        /* loudest sample seen, 0..32767     */
static int            rec_peak_l;      /* left channel peak (since QUALITY) */
static int            rec_peak_r;      /* right channel peak               */
static unsigned long  rec_scan_bytes;  /* bytes RecScan has gone through    */
static unsigned long  rec_rt0_tk;      /* real DMA start time (tick)        */
static unsigned       rec_rt0_ph;      /* and PIT phase                     */
static unsigned long  rec_rt1_tk;      /* the same at stop                  */
static unsigned       rec_rt1_ph;
static unsigned long  rec_rereads;     /* DMA counter read again (v25)      */
static long           rec_zc;          /* zero crossings in the left channel */
static int            rec_zc_on;       /* counted only during the pitch check */
static int            rec_zc_sign;     /* sign of the last sample           */
static long           rec_zc_pos;      /* frame index since counting started */
static long           rec_zc_first;    /* frame of the first crossing, -1 = none */
static long           rec_zc_last;     /* frame of the last crossing        */

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

static int DspRead(void)
{
    long i;
    for (i = 0; i < 100000L; i++)
        if (INB(SbBase() + 0x0E) & 0x80) return (int) INB(SbBase() + 0x0A);
    return -1;
}

static void MixerW(unsigned char reg, unsigned char val)
{
    OUTB(SbBase() + 0x04, reg);
    OUTB(SbBase() + 0x05, val);
}

static unsigned MixerR(unsigned char reg)
{
    OUTB(SbBase() + 0x04, reg);
    return (unsigned) INB(SbBase() + 0x05);
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

    /* 128 kB aligned to 64 kB - then the 64 kB ring fits entirely within
       one page of 16-bit DMA. When that much memory is not available, 64 kB
       for a 32 kB ring is enough, as before. */
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
/* Manual input routing (0x3D/0x3E), -1 = per rec_src. RecHwStart applies
   it only after RecSource, so it survives a capture restart between files
   too. */
static int g_mix3d = -1, g_mix3e = -1;

/* Wavetable level in the mixer. The maximum (0xF8) overdrives the card's
   output stage: measured on a recording from real hardware, the first 28
   IFATN steps then made 0.144 dB per step instead of 0.375 dB, and the
   distortion of the loudest note was 58 % against 2.6 % in a quieter run.
   The mixer has 2 dB per step, so 0xC8 is 12 dB below maximum - enough for
   the loudest note to fit without clipping, while keeping enough distance
   from the noise. */
/* Default wavetable level in the mixer: 12 dB below full. More overdrives
   the card's output stage (measured on test4.wav: 58 % distortion against
   2.6 %). The same value also sets the level into the recording
   multiplexer, though, so the internal capture loses 12 dB of headroom -
   that is what /WT is for. */
#define WT_LEVEL 0xC8
static unsigned wt_level = WT_LEVEL;

/* Output paths to a known value. Without it the volume depends on what
   the previous program left in the mixer, and two measurements on the same
   card differ. 0x30/0x31 is the master volume, 0x34/0x35 wavetable (MIDI),
   0x36..0x39 are CD and line - we turn those down so they do not get into
   the mix and no loop forms. */
static void MixerInit(void)
{
    MixerW(0x30, 0xF8);               /* master  L */
    MixerW(0x31, 0xF8);               /* master  R */
    MixerW(0x34, (unsigned char) wt_level);   /* MIDI / wavetable L */
    MixerW(0x35, (unsigned char) wt_level);   /* MIDI / wavetable R */
    MixerW(0x36, 0x00);               /* CD      L */
    MixerW(0x37, 0x00);               /* CD      R */
    MixerW(0x38, 0x00);               /* line    L - to the output only */
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
           and 64 kB does not fit into an unsigned */
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
    if (g_mix3d >= 0) {
        MixerW(0x3D, (unsigned char) g_mix3d);
        MixerW(0x3E, (unsigned char) g_mix3e);
    }
    rec_scan_bytes = 0UL;   /* a new ring starts at a frame boundary (v25) */
    rec_rereads    = 0UL;

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
    RtNow(&rec_rt0_tk, &rec_rt0_ph);
    Trace("RecHwStart: running, DMA %ld", (long) rec_dma);
    return 0;
}

/* Free space on the disk that will be recorded to. */
static FILE          *g_log;           /* log of what was played when        */
static unsigned long  g_ms;            /* planned milliseconds elapsed       */
static int            g_block;

/* ------------------------------------------------------------------------ *
 *  Capture to memory
 *
 *  Originally the recording was written to disk continuously. A period disk
 *  cannot keep up, though: when a write takes longer than the ring can hold,
 *  part of the sound is lost for good. So the whole cycle is collected in
 *  memory and saved to disk only afterwards, when timing no longer matters.
 *  Each cycle gets its own file with a sequence number before the extension
 *  and a sound mark at both ends, so the files can be put together in order
 *  during analysis.
 * ------------------------------------------------------------------------ */
static void CommitFile(FILE *f);        /* make DOS write the file length   */
static void LogFinish(void);            /* finish the stamp of the open line */
static void CycleMark(int start);       /* mark at the end and start of a cycle */
static void RefTone(void);              /* level at both ends of the file   */
static void ChannelMark(void);          /* which channel is which           */

static unsigned char *cap_buf;         /* buffer for the whole cycle         */
static unsigned long  cap_size;        /* how big it is                      */
static unsigned long  cap_used;        /* how much is in it                  */
static unsigned long  cap_limit;       /* at how much to end the cycle       */
static unsigned long  cap_cap;         /* cap from /MB: in bytes, 0 = none   */
static int            cap_index;       /* sequence number of the file        */
static const char    *cap_pattern;     /* name from /REC:                    */
static unsigned long  cap_t0;          /* g_ms at the start of the cycle     */
static unsigned long  cap_lost;        /* what no longer fitted              */

/* How much memory is available. In protected mode it asks DPMI (function
   0500h returns, among other things, the largest contiguous free block). */
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

/* Buffer for one cycle. We take half of the free memory - the rest
   belongs to the system and the extender, and if we took everything, it
   would start paging to disk, exactly what we are avoiding. */
/* Default chunk size. Smaller than before: when a write stalls, less work
   is repeated and the write itself is shorter. Whoever wants more has /MB. */
#define CAP_DEFAULT (4UL * 1024UL * 1024UL)

/* Really tests the allocated memory.
 *
 * `malloc` only says the allocation succeeded. Under a DOS extender that
 * does not yet mean the memory really is in RAM - it may be paged out to
 * disk, which is exactly what the buffer is meant to avoid. So the whole of
 * it is written with a pattern, read back and timed. Honest RAM does a
 * megabyte far below one BIOS tick (54.9 ms); when it takes over two ticks
 * per megabyte, it is paging.
 *
 * Returns 0 when the contents differ or the memory is suspiciously slow. */
static int MemUsable(unsigned char *p, unsigned long n)
{
    const unsigned long STEP = 61UL;      /* a prime - touches every page */
    unsigned long i, t0, ticks, limit;
    unsigned char v;

    t0 = BiosTicks();

    for (i = 0; i < n; i += STEP)
        p[i] = (unsigned char) (i & 0xFF);
    for (i = 0; i < n; i += STEP) {
        v = (unsigned char) (i & 0xFF);
        if (p[i] != v) {
            printf("  memory check FAILED at offset %lu\n", i);
            Trace("MemUsable: mismatch at %ld", (long) i);
            return 0;
        }
    }

    ticks = BiosTicks() - t0;
    limit = (n / (1024UL * 1024UL)) * 2UL + 2UL;    /* 2 ticks per MB plus a reserve */
    if (ticks > limit) {
        printf("  %lu MB is too slow (%lu ticks, limit %lu) - probably swapped\n",
               n / (1024UL * 1024UL), ticks, limit);
        Trace("MemUsable: too slow, %ld ticks", (long) ticks);
        return 0;
    }
    Trace("MemUsable: %ld ticks", (long) ticks);
    return 1;
}

static int CapAlloc(void)
{
    unsigned long freeb = FreeMemBytes();
    unsigned long want;

    if (freeb == 0UL) freeb = 8UL * 1024UL * 1024UL;   /* unknown: be careful */

    /* /MB takes precedence over everything else. Formerly an 8 MB cap came
       before this line, so the switch could only lower the size and
       `/MB:20` was silently ignored - reported by the tester 2026-09-08. */
    if (cap_cap) {
        want = cap_cap;
        if (want > freeb) want = freeb;         /* more than there is will not work anyway */
    } else {
        want = freeb / 2UL;
        if (want > CAP_DEFAULT) want = CAP_DEFAULT;
    }
    want -= want % 4UL;                                /* whole frames */

    while (want >= 512UL * 1024UL) {
        cap_buf = (unsigned char *) malloc((size_t) want);
        if (cap_buf && MemUsable(cap_buf, want)) break;
        if (cap_buf) { free(cap_buf); cap_buf = 0; }
        want /= 2UL;
        want -= want % 4UL;
    }
    if (!cap_buf) return 1;

    cap_size = want;
    /* Reserve for the longest block step (release 7.1 s) and for RefTone with
       the marks that CapCheck still plays into the old file (1.7 s).
       Up to v25 it was 3 s: in the VM (/MB:8) every file overflowed, because
       a step of block 35 is 3.5 s - the end of the note ended up in
       cap_lost. */
    cap_limit = cap_size - (unsigned long) REC_RATE * 4UL * 10UL;
    if (cap_limit > cap_size || cap_limit < cap_size / 4UL)
        cap_limit = cap_size / 4UL;                         /* maly /MB */
    return 0;
}

/* Cycle file name: the sequence number goes before the extension. DOS has
   only 8 characters per name and the number takes three of them, so the
   base is shortened to five. */
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

/* Saving one cycle. Nothing plays any more here, so a slow disk does not
   matter. */
/* Convinces DOS to really write the file into the directory. fflush
   alone hands the data to DOS, but the file length shows in the directory
   only on close - after the program is killed an empty file remains. This
   is function 68h ("commit file"), which does it at once. */
static void CommitFile(FILE *f)
{
    union REGS r;

    if (!f) return;
    fflush(f);
    memset(&r, 0, sizeof(r));
    r.h.ah = 0x68;
    r.w.bx = (unsigned short) fileno(f);
#if defined(__386__)
    int386(0x21, &r, &r);
#else
    int86(0x21, &r, &r);
#endif
}

/* Write in chunks, so it is visible that something is happening. A single
   big fwrite of 14 MB takes tens of seconds and looks like a hang from the
   outside. */
static int CapWriteAll(FILE *f, const unsigned char *buf, unsigned long n,
                       const char *name)
{
    unsigned long chunk  = 32768UL;
    unsigned long done   = 0UL;
    unsigned long marked = 0UL;
    int           fails  = 0;

    while (done < n) {
        unsigned long k = n - done;

        if (k > chunk) k = chunk;

        if (fwrite(buf + done, 1, (size_t) k, f) != (size_t) k) {
            /* The chunk failed. Go back EXACTLY to its start and try again
               with half the size - the resulting file is therefore byte for
               byte the same as if written at once. It halves down to 2 kB,
               below a tenth of the shortest note (0.35 s = 62 kB). */
            clearerr(f);
            if (fseek(f, (long) (44UL + done), SEEK_SET) != 0) return 1;
            if (chunk > 2048UL) {
                chunk /= 2UL;
                fails  = 0;
                Trace("CapWriteAll: chunk down to %ld", (long) chunk);
            } else if (++fails > 8) {
                Trace("CapWriteAll: stuck at %ld", (long) done);
                return 1;
            }
            continue;
        }
        done += k;
        fails = 0;

        /* Header and commit every 256 kB: if the program is killed, a valid
           WAV remains, accurate to 1.5 s of recording. */
        if (done - marked >= 262144UL || done == n) {
            marked = done;
            printf("\r  saving %s ... %lu of %lu kB ",
                   name, done / 1024UL, (n + 1023UL) / 1024UL);
            fflush(stdout);
            Trace("CapFlush: %ld B written", (long) done);
            if (fseek(f, 0L, SEEK_SET) == 0) {
                WavHeader(f, done);
                if (fseek(f, (long) (44UL + done), SEEK_SET) != 0) return 1;
            }
            CommitFile(f);
        }
    }
    return 0;
}

static int CapFlush(void)
{
    char  name[96];
    FILE *f;

    if (!cap_buf || cap_used == 0UL) return 0;

    LogFinish();
    cap_index++;
    CapName(name, cap_pattern, cap_index);
    Trace("CapFlush: writing %ld B", (long) cap_used);

    /* Three attempts, each into the next sequence number. Whatever got
       written stays a valid WAV - the header is rewritten after every
       megabyte - so not even a failed attempt is to be thrown away. */
    {
        int try_i;

        for (try_i = 0; ; try_i++) {
            f = fopen(name, "wb");
            if (f) {
                WavHeader(f, cap_used);
                if (!CapWriteAll(f, cap_buf, cap_used, name)) {
                    fclose(f);
                    break;                       /* success */
                }
                fclose(f);
                printf("\n*** Could not write all of %s.\n", name);
                Trace("CapFlush: write failed", (long) try_i);
            } else {
                printf("\n*** Cannot create %s - out of disk space?\n", name);
                Trace("CapFlush: cannot create file", (long) try_i);
            }

            if (try_i >= 2) {
                printf("    giving up on this cycle; the run continues.\n");
                Trace("CapFlush: giving up", 0);
                cap_used = 0UL;
                return 1;
            }
            printf("    retrying as the next file...\n");
            cap_index++;
            CapName(name, cap_pattern, cap_index);
        }
    }
    Trace("CapFlush: done", 0);
    /* Losses used to go only to the screen, so the handed-in recording did
       not show whether it was complete. */
    /* v25: frame count and the real start and end time of the DMA - they
       give the real sample rate of the capture and how much of it is
       missing. */
    if (g_log)
        fprintf(g_log, "%8lu -- QUALITY ring overruns %lu, dropped so far %lu B,"
                " peak L %d R %d, counter rereads %lu, frames %lu,"
                " dma rt %lu:%u..%lu:%u\n",
                g_ms, rec_drops, cap_lost, rec_peak_l, rec_peak_r,
                rec_rereads, cap_used / 4UL,
                rec_rt0_tk, rec_rt0_ph, rec_rt1_tk, rec_rt1_ph);
    /* Per-channel peaks are counted again for every file - in run5 the
       right channel was dead for the whole run and it showed nowhere. */
    rec_peak_l = 0;
    rec_peak_r = 0;

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
    unsigned          drive = 0;            /* 0 = current drive */

    if (path && path[0] && path[1] == ':')
        drive = (unsigned) ((path[0] & 0xDF) - 'A' + 1);
    if (_dos_getdiskfree(drive, &df) != 0) return 0UL;
    return (unsigned long) df.avail_clusters *
           (unsigned long) df.sectors_per_cluster *
           (unsigned long) df.bytes_per_sector;
}

static int RecStart(const char *file)
{
    unsigned long rate = (unsigned long) REC_RATE * 4UL;   /* B per second */
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

    /* The disk no longer has to keep up continuously, but the files must
       fit on it. */
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
        /* counts down, so b <= a is consistent - and both reads must be
           close to each other. b <= a alone let through even a read torn
           across a byte boundary (an error of 256 words, 3 ms). */
        if (b <= a && a - b < 0x40u) break;
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
    /* A chunk from the ring may start in the middle of a frame. The
       alignment follows from how many bytes have passed - only whole frames
       are dropped (v22), so modulo 4 holds. Formerly byte 0 of the chunk was
       read as the left sample, which was sometimes the right channel or
       half of two samples. */
    long i0 = (long) ((4UL - (rec_scan_bytes % 4UL)) % 4UL);
    for (i = i0; i + 3 < n; i += 128) {
        int l = (int) ((short) (p[i]     | (p[i + 1] << 8)));
        int r = (int) ((short) (p[i + 2] | (p[i + 3] << 8)));
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > rec_peak_l) rec_peak_l = l;
        if (r > rec_peak_r) rec_peak_r = r;
        if (l > rec_peak) { rec_peak = l; if (l > 64) rec_any = 1; }
        if (r > rec_peak) { rec_peak = r; if (r > 64) rec_any = 1; }
    }

    /* During the pitch check every frame is scanned - every 32nd sample
       would no longer be enough at 1357 Hz. The threshold 256 keeps noise
       out of the count. */
    if (rec_zc_on) {
        for (i = i0; i + 3 < n; i += 4) {
            /* Sum of both channels and from a frame boundary: in run5 the right
               channel was dead, and had the left one been dead, the pitch
               check would have seen nothing. */
            int v = (int) ((short) (p[i] | (p[i + 1] << 8)))
                  + (int) ((short) (p[i + 2] | (p[i + 3] << 8)));
            int sg = (v > 256) ? 1 : ((v < -256) ? -1 : 0);

            rec_zc_pos++;
            if (sg) {
                if (rec_zc_sign && sg != rec_zc_sign) {
                    /* The frequency is computed from the span between the first
                       and the last crossing, so a fragment of the capture
                       is enough - a crossing count over a fixed time would
                       lie. */
                    if (rec_zc_first < 0) rec_zc_first = rec_zc_pos;
                    rec_zc_last = rec_zc_pos;
                    rec_zc++;
                }
                rec_zc_sign = sg;
            }
        }
    }
    rec_scan_bytes += (unsigned long) n;
}

/* Write to the capture. Returns 1 when not everything could be written -
   typically a full disk. Without this check the program went on and
   pretended to record even when nothing reached the disk any more. */
static int RecWrite(const void *buf, long n)
{
    if (!cap_buf || n <= 0) return 0;
    if (cap_used + (unsigned long) n > cap_size) {
        cap_lost += (unsigned long) n;      /* should not happen, the cycle ends earlier */
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
    if (n < 0) n += REC_BYTES;             /* the ring wrapped */

    if (n > (REC_BYTES / 4) * 3) {
        /* v25: first read the counter again. When the previous read was
           torn and jumped ahead, this looks like almost the whole ring and
           370 ms of capture would be thrown away. */
        long pos2 = RecWritePos();
        long n2   = pos2 - rec_read;
        if (n2 < 0) n2 += REC_BYTES;
        rec_rereads++;
        if (n2 <= (REC_BYTES / 4) * 3) {
            pos = pos2;
            n   = n2;
        }
    }
    if (n > (REC_BYTES / 4) * 3) {
        /* Half the ring cannot fill up within one drain unless something
           held us up. The data cannot be caught up, we only resynchronise.

           Only WHOLE frames are dropped (4 bytes = L16 + R16). Formerly this
           did `rec_read = pos`, and when the dropped count was not a multiple
           of 4 the alignment shifted: by 2 bytes the channels swapped (run5 -
           notes alternate L/R regardless of the pan), by 1 or 3 bytes noise. */
        n -= n % 4L;
        rec_drops++;
        rec_read += n;
        if (rec_read >= REC_BYTES) rec_read -= REC_BYTES;
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
        long head = REC_BYTES - rec_read;   /* to the end of the ring */

        RecWrite(rec_buf + (unsigned) rec_read, head);
        RecScan(rec_buf + (unsigned) rec_read, head);
        RecWrite(rec_buf, pos);
        RecScan(rec_buf, pos);
    }
    rec_total += (unsigned long) n;
    rec_read = pos;
}

/* Writing the correct length into the header. Called between blocks, not
   from RecPoll - inside the ring drain the delay would make the ring
   overrun. Thanks to it the file is usable even when the run ends other than
   cleanly. */
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
    RtNow(&rec_rt1_tk, &rec_rt1_ph);
    DspWrite(0xD9);                        /* stop 16-bit auto-init */
    DspWrite(0xD5);                        /* pause 16-bit          */
    OUTB(0xD4, 0x04 | (REC_DMA - 4));      /* mask the channel      */
    (void) INB(SbBase() + 0x0F);           /* clear a pending ack   */
    IrqHook(0);
    Trace("RecHwStop: done", 0);

}

static void RecStop(void)
{
    if (rec_on && cap_buf) {
        RefTone();                          /* level at the end of the last file */
        CycleMark(0);                       /* closing mark of the last cycle */
    }
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
/* The same noise, but with a loop of 1000 samples (22.7 ms). On a 3.5 s
   note that is 154 repetitions, so the period must be visible in the
   recording at first glance - it tells "the loop does not work" from "the
   capture has dropouts". */
static SAMPLE SMP_NOISE_SHORT = { 458995L, 459001L, 460001L };
static SAMPLE SMP_TICK  = { 491098L, 491104L, 491164L };  /* sinetick       */
/* Only for the silent chip clock probe: a loop over 900 000 ROM samples
   (20 s), so the address does not wrap during the measurement. Plays with
   attenuation 255. */
static SAMPLE SMP_LONG  = { 100000L, 100000L, 1000000L };

#define IP_UNITY    0xE000u           /* IP for 1:1 playback                */
#define IP_OCT      4096              /* IP units per octave                */

/* Voices taken for direct writes. The MIDI engine allocates from the bottom,
   so we work from the top down. */
/* The driver takes voices 30 and 31 for the DRAM refresh (see the
   initialisation in Emu8000.cpp, step 8: PSST/CSL/PTRX/CPF/CCCA are set to
   fixed values for them). On a real card an IP write does not show on
   them - the tone then plays at the pitch of the last mark instead of its
   own. That is why the test voices lie lower. */
/* Version number. Raise it on EVERY change that alters the contents of
   the recording, and at the same time rename the output in BUILD32.CMD to
   AWETESTnn.EXE. The number is in the header and on the first log line, so
   every recording shows what produced it. */
#define AWETEST_VER "28"

#define V_TEST      29
#define V_MARK      28
#define V_EXTRA     22                /* 22 DOWN for the voice summing test. */
                                      /* Not up - 16 voices would go over    */
                                      /* V_MARK and V_TEST and overwrite the marks. */

/* Which blocks to play. Useful for a quick check, and so that a single block
   can be repeated if it went wrong in the recording. */
static int          g_from = 1, g_to = 99;
/* /ONLY:22,23,42 - just these blocks, in one run and one recording. */
static char         g_only[100];
static int          g_only_set = 0;

/* ------------------------------------------------------------------------ *
 *  Record of what was played and when   (declared above the capture code,
 *  which stamps each saved file with the test time)
 * ------------------------------------------------------------------------ */

static void RecPoll(void);

/* Timestamps (v25). A log line is not terminated at once: the nearest
 * note (ToneStart / MidiNote) appends the stamp, right before the write
 * that starts it. The line then carries the exact onset moment:
 *
 *     <ms> <block> <description>\t@rt <tick>:<phase> cap <file from ms>:<frame>
 *
 *   rt   real time (RtNow), phase in PIT cycles
 *   cap  the capture file by "from ms" of its FILE line and the frame index
 *        in it, including what the card has recorded and still sits in the
 *        ring
 *
 * When no note comes (another LogLine, a block mark, a file save), the line
 * is terminated with the stamp of that moment. */
static int g_line_open;

static void LogStamp(void)
{
    unsigned long tk, frames = 0UL;
    unsigned      ph;
    int           have_cap = 0;

    if (!g_log) return;
    if (rec_on && cap_buf) {
        long pend = RecWritePos() - rec_read;
        if (pend < 0) pend += REC_BYTES;
        frames   = (cap_used + (unsigned long) pend) / 4UL;
        have_cap = 1;
    }
    RtNow(&tk, &ph);
    fprintf(g_log, "\t@rt %lu:%u", tk, ph);
    if (have_cap) fprintf(g_log, " cap %lu:%lu", cap_t0, frames);
}

void LogFinish(void)
{
    if (!g_log || !g_line_open) return;
    LogStamp();
    fputc('\n', g_log);
    g_line_open = 0;
}

static void LogLine(const char *fmt, long a, long b, long c)
{
    if (!g_log) return;
    LogFinish();
    fprintf(g_log, "%8lu %2d ", g_ms, g_block);
    fprintf(g_log, fmt, a, b, c);
    g_line_open = 1;
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
    /* Modulation envelope - it has its own registers, other than the volume
       ones. The defaults are exactly what ToneStart wrote here as constants,
       so no older block changes by this. */
    unsigned mod_atk;     /* ATKHLD  bits 6..0,  0x7F = instant            */
    unsigned mod_hold;    /* ATKHLD  bits 14..8, 0x7F = no delay           */
    unsigned mod_decay;   /* DCYSUS  bits 6..0                             */
    unsigned mod_sustain; /* DCYSUS  bits 14..8, 0x7F = no drop            */
    unsigned mod_delay;   /* ENVVAL, 0x8000 = no delay                     */
    unsigned ptrx_target; /* PTRX upper half - pitch target               */
    unsigned raw;         /* 1 = without g_att_offset (absolute attenuation) */
} TONE;

/* Attenuation offset of all direct tones (v25). LevelLadder sets it when
   the capture compresses and lowering the mixer did not help. In run5 the
   capture had a ceiling of ~1495 and notes louder than atten 32 all came out
   the same. The offset is in the log (LEVEL). */
static unsigned g_att_offset;

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
    t->mod_atk     = 0x7F;
    t->mod_hold    = 0x7F;
    t->mod_decay   = 0x00;
    t->mod_sustain = 0x7F;
    t->mod_delay   = 0x8000;
    t->ptrx_target = 0x4000;
}

static void VoiceOff(unsigned v)
{
    RegW(P_DATA1, R_DCYSUSV, v, 0x0080);      /* silence                   */
    RegW(P_DATA0, R_VTFT,    v, 0xFFFF);      /* low  half = filter, open  */
    RegW(P_DATA0HI, R_VTFT,  v, 0x0000);      /* high half = volume, mute  */
}

/* Everything but the write that starts the note (DCYSUSV). ToneStart does
   setup, stamp, trigger - the same writes in the same order as before v28;
   several voices can be set up first and triggered back to back, so that
   they start within microseconds of each other (block 43). */
static void ToneSetup(unsigned v, TONE *t)
{
    unsigned long ccca, psst, csl;
    unsigned      att;

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
    att = t->atten + (t->raw ? 0u : g_att_offset);
    if (att > 255u) att = 255u;
    RegW(P_DATA3,   R_IFATN,   v, (unsigned) ((t->cutoff << 8) | (att & 0xFF)));
    RegW(P_DATA3,   R_PEFE,    v, t->pefe);
    RegW(P_DATA3,   R_FMMOD,   v, t->fmmod);
    RegW(P_DATA3,   R_TREMFRQ, v, t->tremfrq);
    RegW(P_DATA3,   R_FM2FRQ2, v, t->fm2frq2);

    RegW(P_DATA1,   R_ENVVAL,  v, t->mod_delay);
    RegW(P_DATA1HI, R_ATKHLD,  v,
         (unsigned) (((t->mod_hold & 0x7F) << 8) | (t->mod_atk & 0x7F)));
    RegW(P_DATA1,   R_DCYSUS,  v,
         (unsigned) (((t->mod_sustain & 0x7F) << 8) | (t->mod_decay & 0x7F)));

    RegW(P_DATA1,   R_ENVVOL,  v, t->envvol);
    RegW(P_DATA1HI, R_ATKHLDV, v, (unsigned) ((t->hold << 8) | (t->atk & 0x7F)));
    RegW(P_DATA1HI, R_LFO1VAL, v, t->lfo1val);
    RegW(P_DATA1HI, R_LFO2VAL, v, t->lfo2val);

    /* PTRX: bits 31..16 pitch target, 15..8 REVERB SEND, 7..0 auxiliary
       pan [PG]. Until 2026-09-10 the send was written into the low byte, so
       the card got reverb 0 and block 22 never measured the reverb (full
       send +2 dB, no reverberation - run5 and ver3). */
    RegDW(P_DATA0, R_PTRX, v,
          ((unsigned long) (t->ptrx_target & 0xFFFF) << 16)
          | ((unsigned long) (t->reverb & 0xFF) << 8));

}

static void ToneTrigger(unsigned v, TONE *t)
{
    /* this one starts the note */
    RegW(P_DATA1, R_DCYSUSV, v,
         (unsigned) (((t->sustain & 0x7F) << 8) | (t->decay & 0x7F)));
}

static void ToneStart(unsigned v, TONE *t)
{
    ToneSetup(v, t);
    /* stamp of the open log line = onset moment (marks excluded) */
    if (v != V_MARK) LogFinish();
    ToneTrigger(v, t);
}

static void ToneRelease(unsigned v, TONE *t)
{
    /* The sustain field stays ZERO - exactly as the driver does it
       (Synth::ReleaseVoice: kDcysusvRelease | releaseRate). Formerly
       `t->sustain` was written here, i.e. 0x7F, and the chip then had
       nowhere to fall: block 12 was recorded on the card as a flat note
       without decay. */
    RegW(P_DATA1, R_DCYSUSV, v, (unsigned) (0x8000 | (t->release & 0x7F)));
}

/* Nothing should be left sounding when the program stops to ask a question -
   the EMU8000 keeps playing quite happily on its own. */
static void SilenceAll(void)
{
    unsigned v;
    /* Leave 30 and 31 alone - they are the driver's refresh channels. */
    for (v = 0; v < 30; v++) VoiceOff(v);
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

static int AutoLevel(int src, int *gain_out, int *too_hot);

/* Wavetable level depending on where the recording goes.
 *
 * With EXTERNAL recording 12 dB of headroom are left, because at full level
 * the card's output stage is overdriven (test4.wav: 58 % distortion against
 * 2.6 %). With the INTERNAL capture, though, the same value is also the
 * level into the recording multiplexer, so that headroom costs 12 dB of
 * signal-to-noise - and quiet blocks then get lost in the noise. Measured
 * 2026-09-08: a peak of 1544 out of 32767.
 *
 * It is raised in 6 dB steps until the signal is comfortable or full. */
static void WavetableForInternal(int src)
{
    int gain = 0, hot = 0, peak, prev_peak = 0;
    unsigned prev_level = wt_level;

    for (;;) {
        peak = AutoLevel(src, &gain, &hot);
        if (peak < 0) return;

        /* Overdrive shows as the peak not growing after a raise as much as
           it should. The mixer step is 4 dB, i.e. 1.585x; when the growth is
           below 1.4x, the output stage is already compressing. The digital
           PEAK_CLIP does not catch this - at 58 % distortion on test4.wav
           the tone had a peak around 9500 of 32767, far from full. */
        if (prev_peak > 0 && peak < prev_peak * 14 / 10) {
            wt_level = prev_level;
            MixerW(0x34, (unsigned char) wt_level);
            MixerW(0x35, (unsigned char) wt_level);
            printf("  output stage starts compressing - back to 0x%02X\n",
                   wt_level);
            Trace("wavetable compressing at 0x%02lX, back", (long) prev_level);
            peak = AutoLevel(src, &gain, &hot);
            break;
        }

        if (hot || peak >= PEAK_TARGET || wt_level >= 0xF8) break;

        prev_peak  = peak;
        prev_level = wt_level;
        wt_level  += 0x10;
        if (wt_level > 0xF8) wt_level = 0xF8;
        MixerW(0x34, (unsigned char) wt_level);
        MixerW(0x35, (unsigned char) wt_level);
        printf("  capture quiet (peak %d) - wavetable up to 0x%02X\n",
               peak, wt_level);
        Trace("wavetable up to 0x%02lX", (long) wt_level);
    }

    if (peak >= 0 && !hot) RecGain(gain);
    printf("  internal capture: wavetable 0x%02X, gain %d dB, peak %d of 32767\n",
           wt_level, gain * 6, peak);
    Trace("internal capture peak %ld", (long) peak);
}

/* A wait that keeps draining the ring.
 *
 * The plain Wait() leaves it alone. The ring is 64 kB, i.e. 371 ms of
 * capture, so a longer wait overflows it - and RecPoll then throws away the
 * whole contents with its overrun safeguard without measuring it. All the
 * probes thus measured blind. */
static void WaitPolling(unsigned ms)
{
    unsigned done = 0;

    while (done < ms) {
        unsigned k = ms - done;

        if (k > 20) k = 20;
        Wait(k);
        RecPoll();
        done += k;
    }
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
    WaitPolling(PROBE_MS);
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
        /* Keep the BEST result, not the last one: when some higher gain
           fails or measures zero, it must not lose a usable level measured
           earlier. */
        if (pk > best_pk) {
            best_pk = pk;
            best_g  = g;
        }
        if (pk >= PEAK_TARGET) break;   /* loud enough, stop here */
    }
    *gain_out = best_g;
    return best_pk;
}

/* One pitch measurement: plays a tone with the given IP and returns the
   number of zero crossings that came back from the ADC. The frequency is
   about half of them per second. */
/* Returns the measured frequency in hundredths of Hz, or -1 when nothing
   came. It is computed from the span between the first and the last zero
   crossing, so it is enough when only a piece of the tone arrives. */
static long ProbeZc(unsigned ip)
{
    TONE t;
    int  try_i;

    for (try_i = 0; try_i < 3; try_i++) {
        if (RecHwStart()) return -1L;
        rec_zc       = 0;
        rec_zc_sign  = 0;
        rec_zc_pos   = 0;
        rec_zc_first = -1L;
        rec_zc_last  = 0;
        rec_zc_on    = 1;

        ToneDefaults(&t);
        t.atten = 0;
        t.ip    = ip;
        ToneStart(V_TEST, &t);
        WaitPolling(500);
        VoiceOff(V_TEST);
        RecHwStop();

        rec_zc_on = 0;
        SilenceAll();

        /* One of the probes sometimes returns nothing - try again before
           declaring it unmeasurable. */
        if (rec_zc >= 8L && rec_zc_first >= 0L
            && rec_zc_last > rec_zc_first) {
            long span = rec_zc_last - rec_zc_first;
            /* half period per crossing: f = (crossings-1) * rate / (2 * span) */
            return (rec_zc - 1L) * (long) REC_RATE * 50L / span;
        }
        Trace("ProbeZc: nic, zkousim znovu", (long) try_i);
    }
    return -1L;
}

/* Verifies that the test voice really obeys an IP write. The original
   AWETEST played on voice 31, which the driver keeps for the DRAM refresh,
   and an IP write did not show on it - blocks 4, 5 and 17 were recorded at
   a single pitch and it was found only by analysing the twenty-minute
   recording. Here the answer comes within a second. */
static void PitchCheck(void)
{
    long lo, hi, ratio;

    printf("\nChecking that the test voice follows the pitch register...\n");
    Trace("PitchCheck: start", 0);

    lo = ProbeZc((unsigned) IP_UNITY);
    hi = ProbeZc((unsigned) (IP_UNITY + IP_OCT));
    Trace("PitchCheck: unity %ld", lo);
    Trace("PitchCheck: octave %ld", hi);

    if (lo <= 0L || hi <= 0L) {
        printf("  cannot tell - too little signal came back.\n");
        Trace("PitchCheck: inconclusive", 0);
        return;
    }

    ratio = hi * 100L / lo;
    printf("  unity %ld.%02ld Hz, one octave up %ld.%02ld Hz (ratio %ld.%02ld)\n",
           lo / 100L, lo % 100L, hi / 100L, hi % 100L,
           ratio / 100L, ratio % 100L);

    if (ratio < 170L) {
        printf("\n  *** The pitch register is NOT taking effect on voice %d.\n",
               V_TEST);
        printf("  *** Blocks 4, 5 and 17 would all be recorded at one pitch.\n");
        printf("  *** Please report this instead of leaving the run going.\n\n");
        Trace("PitchCheck: FAILED", (long) V_TEST);
        WaitKey(30, "  any key to carry on anyway (%2d s) ");
    } else {
        printf("  pitch register works.\n");
        Trace("PitchCheck: ok", ratio);
    }
}

/* Channel marking at the start of the run: three tones fully left, then
   two fully right. Without it the recording does not show which channel of
   the file belongs to which side of the card, and the pan cannot be
   evaluated. */
static void ChannelMark(void)
{
    TONE t;
    int  i;

    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        t.pan = 255;                  /* PSST 0xFF = fully left [PG] */
        PlayTone(&t, 250, 150);
    }
    Wait(400);
    for (i = 0; i < 2; i++) {
        ToneDefaults(&t);
        t.pan = 0;                    /* PSST 0x00 = fully right [PG] */
        PlayTone(&t, 250, 150);
    }
    Wait(600);
}

/* ------------------------------------------------------------------------ *
 *  Start-up checks (v25)
 * ------------------------------------------------------------------------ */

/* Playback address of a voice (low 24 bits of CCCA). The high word is read
   twice so as not to take a pair across a carry. */
static unsigned long ReadCcca(unsigned v)
{
    unsigned hi1 = 0, lo = 0, hi2 = 0;
    int      k;

    OUTW(P_PTR, (unsigned) ((R_CCCA << 5) | (v & 31)));
    for (k = 0; k < 4; k++) {
        hi1 = (unsigned) INW(P_DATA1HI);
        lo  = (unsigned) INW(P_DATA1);
        hi2 = (unsigned) INW(P_DATA1HI);
        if (hi1 == hi2) break;
    }
    return ((((unsigned long) hi2) << 16) | (unsigned long) lo) & 0xFFFFFFUL;
}

/* Chip clock and loops - a SILENT measurement, log only.
 *
 * 1) How many ROM samples a voice passes in 4 s at IP 1:1, against the PIT.
 *    That is the chip clock, independent of the capture and of the sound
 *    (nominal 44100/s).
 * 2) Whether the address really returns for the loops of the sine, the
 *    noise and the short noise. Block 24 in run5 did not find the noise
 *    repetition in the recording - here the answer comes straight from the
 *    chip, without the capture path.
 */
static void ChipProbe(void)
{
    static SAMPLE *loops[3];
    TONE          t;
    unsigned long tk0, tk1, a0, a1, a, prev, amin, amax;
    unsigned      ph0, ph1;
    long          cyc;
    int           i, k, wraps;

    loops[0] = &SMP_SINE;
    loops[1] = &SMP_NOISE;
    loops[2] = &SMP_NOISE_SHORT;

    printf("\nMeasuring the chip's own sample clock (silent, 6 s)...\n");
    ToneDefaults(&t);
    t.smp   = &SMP_LONG;
    t.atten = 255;
    t.raw   = 1;
    ToneStart(V_TEST, &t);
    DelayMs(200);
    RtNow(&tk0, &ph0);
    a0 = ReadCcca(V_TEST);
    DelayMs(4000);
    RtNow(&tk1, &ph1);
    a1 = ReadCcca(V_TEST);
    VoiceOff(V_TEST);
    cyc = RtCycles(tk0, ph0, tk1, ph1);
    if (g_log)
        fprintf(g_log, "# CHIP clock: CCCA %lu -> %lu, %ld samples in %ld PIT"
                " cycles (1193182/s) at IP 0x%04X\n",
                a0, a1, (long) (a1 - a0), cyc, (unsigned) IP_UNITY);
    if (cyc > 1000L && a1 > a0)
        printf("  about %lu samples per second (nominal 44100)\n",
               (a1 - a0) * 1193UL / (unsigned long) (cyc / 1000L));
    else
        printf("  the play position could not be read back\n");

    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        t.smp   = loops[i];
        t.atten = 255;
        t.raw   = 1;
        ToneStart(V_TEST, &t);
        DelayMs(50);
        prev = amin = amax = ReadCcca(V_TEST);
        wraps = 0;
        for (k = 0; k < 150; k++) {
            DelayMs(4);
            a = ReadCcca(V_TEST);
            if (a < prev) wraps++;
            if (a < amin) amin = a;
            if (a > amax) amax = a;
            prev = a;
        }
        VoiceOff(V_TEST);
        if (g_log)
            fprintf(g_log, "# CHIP loop %lu..%lu (start %lu): %d wraps in 150"
                    " reads 4 ms apart, address seen %lu..%lu\n",
                    loops[i]->loop_start, loops[i]->loop_end, loops[i]->start,
                    wraps, amin, amax);
    }
    if (g_log) CommitFile(g_log);
}

/* One tone with the given pan, peaks of both capture channels. m3d/m3e
   >= 0 override the input routing for this measurement only. */
static void ProbeLR(int pan, int m3d, int m3e, int *pl, int *pr)
{
    TONE t;

    *pl = *pr = -1;
    g_mix3d = m3d;
    g_mix3e = m3e;
    if (RecHwStart()) { g_mix3d = g_mix3e = -1; return; }
    rec_peak_l = 0;
    rec_peak_r = 0;
    ToneDefaults(&t);
    t.pan = (unsigned) pan;
    ToneStart(V_TEST, &t);
    WaitPolling(PROBE_MS);
    VoiceOff(V_TEST);
    RecHwStop();
    SilenceAll();
    g_mix3d = g_mix3e = -1;
    *pl = rec_peak_l;
    *pr = rec_peak_r;
}

static void StereoLog(const char *what, int m3d, int m3e,
                      int ll, int lr, int rl, int rr)
{
    if (g_log)
        fprintf(g_log, "# STEREO %s: 3D=%02X 3E=%02X, left tone L %d R %d,"
                " right tone L %d R %d\n",
                what, m3d & 0xFF, m3e & 0xFF, ll, lr, rl, rr);
    printf("  %-24s left tone L %5d R %5d, right tone L %5d R %5d\n",
           what, ll, lr, rl, rr);
}

static void StereoVerdict(const char *v)
{
    if (g_log) fprintf(g_log, "# STEREO verdict: %s\n", v);
    printf("  => %s\n", v);
}

/* 0 = both capture channels are alive, 'L' / 'R' = only that one is. */
static int g_mono_adc;

/* Both capture channels (v25).
 *
 * run5: the right channel did not carry even the ADC noise and the program
 * did not notice, because it measured only the peak of both together. Here a
 * tone is played fully left and fully right and each channel is measured
 * separately. When one is silent, it tries swapping what flows into which
 * ADC - that tells whether an ADC channel is missing or the signal from the
 * chip. With a one-channel capture block 3 is then played a second time with
 * the other chip channel in the live ADC, so the pan can be measured anyway.
 */
static void StereoCheck(void)
{
    int srcL = (rec_src == SRC_LINEIN) ? 0x10 : 0x40;
    int srcR = (rec_src == SRC_LINEIN) ? 0x08 : 0x20;
    int lim  = PEAK_USABLE / 2;
    int ll, lr, rl, rr, a, b, c, d, alive_l, alive_r;

    printf("\nChecking both capture channels...\n");
    ProbeLR(255, -1, -1, &ll, &lr);
    ProbeLR(0,   -1, -1, &rl, &rr);
    StereoLog("normal routing", srcL, srcR, ll, lr, rl, rr);
    if (g_log)
        fprintf(g_log, "# STEREO mixer readback 3D=%02X 3E=%02X 3F=%02X 40=%02X"
                " 41=%02X 42=%02X 34=%02X 35=%02X\n",
                MixerR(0x3D), MixerR(0x3E), MixerR(0x3F), MixerR(0x40),
                MixerR(0x41), MixerR(0x42), MixerR(0x34), MixerR(0x35));
    if (ll < 0 || rr < 0) return;

    alive_l = (ll > lim || rl > lim);
    alive_r = (lr > lim || rr > lim);

    if (alive_l && alive_r) {
        if (ll > 4 * lr && rr > 4 * rl)
            StereoVerdict("stereo OK");
        else if (lr > 4 * ll && rl > 4 * rr)
            StereoVerdict("channels SWAPPED - left tone lands in the right channel");
        else
            StereoVerdict("both channels live but NOT separated (mono mix?)");
    } else if (!alive_l && !alive_r) {
        StereoVerdict("no usable signal in either channel");
    } else {
        g_mono_adc = alive_l ? 'L' : 'R';
        printf("  *** Only the %s capture channel carries signal.\n",
               alive_l ? "LEFT" : "RIGHT");
        /* The live ADC gets the other chip channel: does it arrive at all? */
        if (alive_l) {
            ProbeLR(255, srcR, srcR, &a, &b);
            ProbeLR(0,   srcR, srcR, &c, &d);
            StereoLog("both ADC <- chip RIGHT", srcR, srcR, a, b, c, d);
            ProbeLR(255, srcL, srcL, &a, &b);
            ProbeLR(0,   srcL, srcL, &c, &d);
            StereoLog("both ADC <- chip LEFT", srcL, srcL, a, b, c, d);
        } else {
            ProbeLR(255, srcL, srcL, &a, &b);
            ProbeLR(0,   srcL, srcL, &c, &d);
            StereoLog("both ADC <- chip LEFT", srcL, srcL, a, b, c, d);
            ProbeLR(255, srcR, srcR, &a, &b);
            ProbeLR(0,   srcR, srcR, &c, &d);
            StereoLog("both ADC <- chip RIGHT", srcR, srcR, a, b, c, d);
        }
        StereoVerdict(alive_l
            ? "MONO capture, left ADC only - block 3 repeats with chip right in it"
            : "MONO capture, right ADC only - block 3 repeats with chip left in it");
        printf("      The run continues; the log says which input was used when.\n");
    }
    RecSource(rec_src);
    rec_peak_l = 0;
    rec_peak_r = 0;
    if (g_log) CommitFile(g_log);
}

/* Peak of one tone with the given absolute attenuation (no offset). */
static int ProbeAttOnce(unsigned att)
{
    TONE t;

    if (RecHwStart()) return -1;
    rec_peak_l = 0;
    rec_peak_r = 0;
    ToneDefaults(&t);
    t.atten = att > 255u ? 255u : att;
    t.raw   = 1;
    ToneStart(V_TEST, &t);
    WaitPolling(700);
    VoiceOff(V_TEST);
    RecHwStop();
    SilenceAll();
    return rec_peak_l > rec_peak_r ? rec_peak_l : rec_peak_r;
}

/* The probe sometimes returns nothing or only a part (3 of 16 in the VM,
   formerly ProbeZc too), so up to three attempts and the largest peak is
   taken. */
static int ProbeAtt(unsigned att)
{
    int k, pk, best = -1;

    for (k = 0; k < 3; k++) {
        pk = ProbeAttOnce(att);
        if (pk < 0) return best;
        if (pk > best) best = pk;
        if (k >= 1 && best >= 64) break;     /* two measurements when something came */
    }
    return best;
}

/* Ratio of the peaks at offset +0 and +32 (12.0 dB, 0.375 dB/step verified
   on run5). Linearly 3.98; returns a hundredfold, -1 when nothing came. */
static long LadderRatio(void)
{
    int  p0  = ProbeAtt(g_att_offset);
    int  p32 = ProbeAtt(g_att_offset + 32u);
    long r;

    /* zero on either side is no measurement - formerly the ratio came out
       0 and the program lowered the gain because of a probe that returned
       nothing */
    if (p0 < 64 || p32 <= 0) return -1L;
    r = (long) p0 * 100L / (long) p32;
    if (g_log)
        fprintf(g_log, "# LEVEL wavetable 0x%02X, input gain %d dB, atten offset"
                " %u: peak %d at +0, %d at +32 (12 dB), ratio %ld.%02ld"
                " (linear 3.98)\n", wt_level, rec_gain * 6, g_att_offset,
                p0, p32, r / 100L, r % 100L);
    printf("  wavetable 0x%02X, gain %2d dB, offset %3u: ratio %ld.%02ld (3.98 = linear)\n",
           wt_level, rec_gain * 6, g_att_offset, r / 100L, r % 100L);
    return r;
}

/* One step of lowering the level: 0 = input gain, 1 = wavetable in the
   mixer, 2 = chip attenuation offset. undo takes the step back. Returns 0
   when it cannot go further. */
static int LadderKnob(int knob, int undo)
{
    switch (knob) {
    case 0:
        if (undo) { RecGain(rec_gain + 1); return 1; }
        if (rec_gain <= 0) return 0;
        RecGain(rec_gain - 1);
        return 1;
    case 1:
        if (undo) wt_level += 0x18;
        else if (wt_level < 0x98) return 0;
        else wt_level -= 0x18;
        MixerW(0x34, (unsigned char) wt_level);
        MixerW(0x35, (unsigned char) wt_level);
        return 1;
    default:
        if (undo) { g_att_offset -= 16u; return 1; }
        if (g_att_offset >= 64u) return 0;
        g_att_offset += 16u;
        return 1;
    }
}

/* Capture linearity at full level (v25).
 *
 * run5 had a hard ceiling of ~1495 (-27 dBFS): atten 0 to 32 came out the
 * same and all loud notes were cut off. WavetableForInternal did not notice,
 * because it looked for the ceiling by the growth of the peak per mixer step.
 * Here it is measured directly: a tone at +0 and +32 must have a ratio of
 * 3.98. Step by step the input gain is lowered, then the mixer, then the chip
 * attenuation - and a step that does not improve the ratio is taken back, so
 * signal-to-noise is not lost needlessly. Finally a ladder 0..96 goes to the
 * log; the transfer curve of the capture can be computed from it.
 */
static void LevelLadder(void)
{
    static const unsigned steps[7] = { 0, 16, 32, 48, 64, 80, 96 };
    long r, r2;
    int  knob, i, pk;

    printf("\nChecking that the capture stays linear at full level...\n");
    r = LadderRatio();
    for (knob = 0; knob < 3 && r >= 0L && r < 350L; knob++) {
        while (r < 350L && LadderKnob(knob, 0)) {
            r2 = LadderRatio();
            if (r2 < 0L) {                      /* unmeasurable: take the step back */
                LadderKnob(knob, 1);
                break;
            }
            if (r2 < r + 30L) {
                LadderKnob(knob, 1);
                if (g_log)
                    fprintf(g_log, "# LEVEL step on knob %d did not help - undone\n",
                            knob);
                break;
            }
            r = r2;
        }
    }
    for (i = 0; i < 7; i++) {
        pk = ProbeAtt(g_att_offset + steps[i]);
        if (g_log)
            fprintf(g_log, "# LADDER atten %u peak %d\n",
                    g_att_offset + steps[i], pk);
    }
    if (g_log)
        fprintf(g_log, "# LEVEL final: wavetable 0x%02X, input gain %d dB,"
                " atten offset %u, ratio x100 %ld%s\n",
                wt_level, rec_gain * 6, g_att_offset, r,
                (r < 0L) ? " - NOT MEASURABLE" : ((r >= 350L) ? "" : " - STILL COMPRESSED"));
    if (r >= 0L && r < 350L)
        printf("  *** capture still compresses at full level - loud notes\n"
               "      will be flattened; the log records how much.\n");
    rec_peak_l = 0;
    rec_peak_r = 0;
    if (g_log) CommitFile(g_log);
}

/* Repeating the quiet parts at a higher level (block 38). The capture input
   and the mixer are raised in 6 dB steps; the real gain is measured with an
   anchor in the recording. */
static unsigned g_wt_saved;
static int      g_gain_saved;

static int LevelBoost(int want_db)
{
    int db = 0;

    g_wt_saved   = wt_level;
    g_gain_saved = rec_gain;
    while (db + 6 <= want_db && rec_on && rec_gain < 3) {
        RecGain(rec_gain + 1);
        db += 6;
    }
    while (db + 6 <= want_db && wt_level + 0x18 <= 0xF8) {
        wt_level += 0x18;
        db += 6;
    }
    MixerW(0x34, (unsigned char) wt_level);
    MixerW(0x35, (unsigned char) wt_level);
    LogLine("LEVEL boost %ld dB (wanted %ld), wavetable 0x%02lX",
            (long) db, (long) want_db, (long) wt_level);
    LogLine("LEVEL input gain %ld dB, atten offset %ld",
            (long) (rec_gain * 6), (long) g_att_offset, 0);
    LogFinish();
    return db;
}

static void LevelRestore(void)
{
    RecGain(g_gain_saved);
    wt_level = g_wt_saved;
    MixerW(0x34, (unsigned char) wt_level);
    MixerW(0x35, (unsigned char) wt_level);
    LogLine("LEVEL restored: wavetable 0x%02lX, input gain %ld dB",
            (long) wt_level, (long) (rec_gain * 6), 0);
    LogFinish();
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

static unsigned long g_bios0;         /* BIOS tick at the start of the run */

static int  g_steps;                  /* steps in the current block       */
static int  g_step;                   /* of which done                    */
static char g_bname[48];              /* block name for redrawing         */

static void SetSteps(int steps)
{
    g_steps = steps;
    g_step  = 0;
}

/* Redraws the line of the current block. It stays in place, so the output
   does not grow - only the percentage and time change. */
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

/* End of one block step: advance the indicator and the minute mark. */
static void CapCheck(void);

static void StepDone(void)
{
    if (g_step < g_steps) g_step++;
    ShowProgress();
    RecPoll();              /* screen output takes time; do not let the ring stall */
    CapCheck();             /* memory full? save the cycle and start the next */
    /* The minute mark is NO LONGER placed here. It broke the regular
       spacing of notes in the middle of a block (1.2 s instead of 0.6) and
       every analysis then had to watch for the exception. The time anchor is
       given by the block marks, which lie on the boundary. */
}

static void MinuteMarkIfDue(void)
{
    if (g_ms < g_next_minute) return;
    LogLine("MINUTE %ld", (long) (g_next_minute / 1000L), 0, 0);
    LogFinish();
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

/* Reference tone at the start and end of every file. The same settings as
   block 1, only shorter - it is about the level, not the length. Without it
   the level cannot be compared between the individual files. */
void RefTone(void)
{
    TONE t;

    ToneDefaults(&t);
    LogLine("reference tone", 0, 0, 0);
    PlayTone(&t, 1000, 300);
}

/* End of a cycle: play the mark, stop the capture, save the file and start
   again. Called at a note boundary, so nothing gets split in the middle. */
void CapCheck(void)
{
    if (!cap_buf || !rec_on) return;
    if (cap_used < cap_limit) return;

    RefTone();                     /* level at the end of the file */
    CycleMark(0);                  /* still recording, the mark will be in the file */
    RecHwStop();
    CapFlush();
    if (RecHwStart()) {            /* could not restart - go on without capture */
        printf("\n*** Could not restart the capture; carrying on without it.\n");
        return;
    }
    cap_t0 = g_ms;
    CycleMark(1);
    RefTone();                     /* and at the start of the next one */
}

static int BlockMark(int n, const char *name)
{
    int i;
    if (n < g_from || n > g_to) return 0;
    if (g_only_set && (n < 0 || n > 99 || !g_only[n])) return 0;
    g_block = n;
    LogFinish();
    if (g_log) fprintf(g_log, "%8lu %2d ---- %s\n", g_ms, n, name);
    CommitFile(g_log);
    Trace("block %ld", (long) n);
    {   /* planned vs real time - 1 BIOS tick = 54.925 ms */
        unsigned long real_ms = (BiosTicks() - g_bios0) * 10985UL / 200UL;
        unsigned long pit_ms  = g_pit_sec * 1000UL + g_pit_lo / (PIT_HZ / 1000UL);
        if (g_log) {
            fprintf(g_log,
                    "%8lu %2d ---- planned %lu ms, PIT %lu ms, BIOS %lu ms",
                    g_ms, n, g_ms, pit_ms, real_ms);
            LogStamp();
            fputc('\n', g_log);
        }
        CommitFile(g_log);
    }
    if (g_bname[0]) printf("\n");     /* finish the line of the previous block */
    strncpy(g_bname, name, sizeof(g_bname) - 1);
    g_bname[sizeof(g_bname) - 1] = '\0';
    g_steps = 0;
    g_step  = 0;
    ShowProgress();
    MinuteMarkIfDue();               /* bothers nobody at a block boundary */
    for (i = 0; i < n; i++) Tick(1);
    Wait(500);
    RefTone();                       /* a known level INSIDE the block */
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
    SetSteps(5);
    ToneDefaults(&t);
    LogLine("reference sine, IP %ld, atten %ld", (long) t.ip, 0, 0);
    PlayTone(&t, 3000, 1000);
    StepDone();

    /* Control clock probe. In run5 the LFO and envelopes ran ~9 % slower
       than [PG] (LFO1 2.448 Hz instead of 2.698) while the pitch was right;
       the old recording ver3 had the clock per [PG]. The time blocks must
       therefore refer to the clock of THIS run - and that is measured here,
       at the start and at the end (block 1 is played again as the last). */
    ToneDefaults(&t);
    t.tremfrq = 0x7F40;              /* hloubka 0x7F, LFO1 0x40 = 2,698 Hz [PG] */
    LogLine("clock probe: tremolo, LFO1 0x%02lX, expect %ld mHz", 0x40L, 2698L, 0);
    PlayTone(&t, 3000, 500);
    StepDone();

    ToneDefaults(&t);
    t.hold    = 0x6F;                /* 16 steps = 1472 ms [PG] */
    t.decay   = 0x50;
    t.sustain = 0x20;
    LogLine("clock probe: hold 0x%02lX, expect %ld ms", 0x6FL, 1472L, 0);
    PlayTone(&t, 2600, 400);
    StepDone();

    /* v25: LFO2 has its own counter. When only LFO1 comes out slow, it is
       not the clock. */
    ToneDefaults(&t);
    t.fm2frq2 = 0x7F40;
    LogLine("clock probe: vibrato, LFO2 0x%02lX, expect %ld mHz", 0x40L, 2698L, 0);
    PlayTone(&t, 3000, 500);
    StepDone();

    /* v25: envelope delay, 1200 units x 725 us = 870 ms [PG]. A mark right
       before the note - the gap mark -> onset is measured. */
    ToneDefaults(&t);
    t.envvol = (unsigned) (0x8000 - 1200);
    LogLine("clock probe: envelope delay %ld units, expect %ld ms", 1200L, 870L, 0);
    Tick(0);
    PlayTone(&t, 2000, 400);
    StepDone();
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
        t.raw   = 1;              /* absolute attenuation, without the LevelLadder offset */
        LogLine("atten %ld", (long) a, 0, 0);
        /* v25: 600 ms of silence - with 250 ms the noise floor merged with the
           note's reverb */
        PlayTone(&t, 400, 600);
        StepDone();
    }
}

static void BlockPan(int n)
{
    TONE t;
    int p;
    if (!BlockMark(n, "pan PSST 0..255 step 8")) return;
    SetSteps(g_mono_adc ? 64 : 32);
    for (p = 0; p <= 255; p += 8) {
        ToneDefaults(&t);
        t.pan = (unsigned) p;
        LogLine("pan %ld", (long) p, 0, 0);
        PlayTone(&t, 400, 600);
        StepDone();
    }

    /* v25: one-channel capture (StereoCheck) - the same again with the
       other chip channel in the live ADC. Both halves of the pan can then be
       measured, just not at once. */
    if (g_mono_adc) {
        int srcL = (rec_src == SRC_LINEIN) ? 0x10 : 0x40;
        int srcR = (rec_src == SRC_LINEIN) ? 0x08 : 0x20;
        g_mix3d = srcR;           /* both ADCs from the chip's right channel */
        g_mix3e = srcR;
        if (g_mono_adc == 'R') { g_mix3d = srcL; g_mix3e = srcL; }
        MixerW(0x3D, (unsigned char) g_mix3d);
        MixerW(0x3E, (unsigned char) g_mix3e);
        LogLine("MIXER both ADC inputs from the other chip channel: 3D=%02lX 3E=%02lX",
                (long) g_mix3d, (long) g_mix3e, 0);
        for (p = 0; p <= 255; p += 8) {
            ToneDefaults(&t);
            t.pan = (unsigned) p;
            LogLine("pan other channel %ld", (long) p, 0, 0);
            PlayTone(&t, 400, 600);
            StepDone();
        }
        g_mix3d = g_mix3e = -1;
        RecSource(rec_src);
        LogLine("MIXER routing restored", 0, 0, 0);
        LogFinish();
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
        PlayTone(&t, 400, 500);
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
        PlayTone(&t, 500, 600);
        StepDone();
    }
}

/* Filter cutoff measured with a sine. Four frequencies: IP is 16-bit and
   IP_UNITY is 0xE000, so above +2 octaves it would overflow - hence -1, 0,
   +1 and +2 octaves, i.e. about 339, 678, 1357 and 2714 Hz. Q stays 0. */
static void BlockFilterSine(int n)
{
    static const int probe[4] = { -4096, 0, 4096, 8191 };
    TONE t;
    int i, c;

    if (!BlockMark(n, "filter by sine: cutoff sweep, then PEFE and FMMOD depth"))
        return;
    SetSteps(4 * 32 + 2 * 16);

    for (i = 0; i < 4; i++) {
        for (c = 0; c <= 248; c += 8) {
            ToneDefaults(&t);
            t.ip     = (unsigned) (IP_UNITY + probe[i]);
            t.cutoff = (unsigned) c;
            LogLine("filter sine IP %ld, cutoff %ld", (long) t.ip, (long) c, 0);
            /* v25: the floor window in the analysis reached into the next note */
            PlayTone(&t, 400, 600);
            StepDone();
        }
    }

    /* ENVELOPE depth to the filter (PEFE low byte). Block 15 measures it
       with noise, and in the recording of 2026-09-07 three usable points of
       16 were left of it - negative depths close the filter and the noise
       below it disappears. A sine at a fixed frequency always has full
       level. The cutoff is in the middle, so the envelope has room both up
       and down. */
    for (i = 0; i < 16; i++) {
        int depth = -128 + i * 16;
        ToneDefaults(&t);
        t.ip     = (unsigned) (IP_UNITY + 4096);   /* 1357 Hz */
        t.cutoff = 128;
        t.pefe   = (unsigned) (depth & 0xFF);
        LogLine("env->filter depth %ld, PEFE 0x%04lX",
                (long) depth, (long) t.pefe, 0);
        PlayTone(&t, 400, 600);
        StepDone();
    }

    /* And the same for the LFO1 depth to the filter (FMMOD low byte).
       Block 19 did work, but only for small depths - for large ones the
       measurement saturated, because the cutoff left the observed band. */
    for (i = 0; i < 16; i++) {
        int depth = -128 + i * 16;
        ToneDefaults(&t);
        t.ip      = (unsigned) (IP_UNITY + 4096);
        t.cutoff  = 128;
        t.fmmod   = (unsigned) (depth & 0xFF);
        t.tremfrq = 0x0040;
        LogLine("LFO1->filter depth %ld, FMMOD 0x%04lX",
                (long) depth, (long) t.fmmod, 0);
        PlayTone(&t, 1200, 500);
        StepDone();
    }
}

static void BlockResonance(int n)
{
    /* 32 and 64 were below the recording noise with the filter closed
       (run5). The top end instead: 255 came out with a cutoff of ~11 kHz
       instead of 8 kHz, but the drivers write 254 for "wide open" - we need
       to see where that change starts. */
    static int cuts[6] = { 96, 144, 192, 248, 254, 255 };
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
            /* v25: 700 ms - the resonance at Q15 rings out */
            PlayTone(&t, 500, 700);
            StepDone();
        }
    }
}

/* Envelope rate divisor - the same table as RateDivisor in Emu8000.cpp;
   the envelopes are driven by index rate - 1. */
static long RateDiv(int index)
{
    int group = (index >> 4) & 7;
    int m     = index & 15;
    return (group == 0) ? (long) (m + 1) : ((long) (m + 17) << (group - 1));
}

/* Note length by the expected duration of the event (v25). EnvHold had
   four steps and attack 0x04 (2.97 s) did not fit into 2.8 s. A 40 %
   reserve covers a slower clock (run5 +9 %) and 800 ms of extra plateau
   give an anchor for the end of the event. */
static unsigned EnvNoteMs(long expect_ms, unsigned lo, unsigned hi)
{
    long ms = expect_ms * 14L / 10L + 800L;
    if (ms < (long) lo) ms = (long) lo;
    if (ms > (long) hi) ms = (long) hi;
    return (unsigned) ms;
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
        /* the rise takes 11.878 s / divisor [PG] */
        PlayTone(&t, EnvNoteMs(11878L / RateDiv(r - 1), 600, 6000), 500);
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
        t.hold    = (unsigned) h;
        t.decay   = 0x50;
        /* Without this line the block measures nothing: ToneDefaults leaves
           the sustain at 0x7F (no attenuation), so the decay after the hold
           has nowhere to fall and the end of the hold is not visible in the
           recording. Measured on ver3.wav - all 16 notes were exactly the
           same. */
        t.sustain = 0x20;
        LogLine("hold 0x%02lX, sustain 0x20", (long) h, 0, 0);
        /* 3400 ms instead of 1600: the chip holds 102.74 ms per step (run5,
           block 9), so the longest hold 0x61 is 3.08 s. With 1600 ms VoiceOff
           cut the plateau of all steps from 0x6D on and only 8 steps of 16
           went into the fit. */
        PlayTone(&t, 3400, 400);
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
        /* Sustain 0x20 is 71 dB below full level. At the slowest step
           (8.4 dB/s) such a drop takes 8.4 s, but the note is 3.2 s - the
           drop did not finish and steps 0x04..0x20 could not be measured.
           The lower part also vanished in the recording noise (floor
           ~ -37 dB). Sustain 0x60 is a drop of 23.25 dB: it fits above the
           noise and finishes even at the slowest step. Measured on the
           tester's recording 2026-09-08. */
        t.sustain = 0x60;
        LogLine("decay 0x%02lX, sustain 0x60", (long) r, 0, 0);
        /* 23.25 dB at 100 dB per 47.513 s / divisor = 11 047 ms / divisor */
        PlayTone(&t, EnvNoteMs(11047L / RateDiv(r - 1), 900, 6000), 500);
        StepDone();
    }
}

static void BlockSustain(int n)
{
    TONE t;
    int s;
    if (!BlockMark(n, "envelope: sustain 0x51..0x7F")) return;
    /* The step is 0.75 dB, so sustain 64 is already 47 dB of attenuation
       and gets lost in the noise of the recording. The original range
       0..127 in steps of 8 had 7 usable steps of 16. This one covers
       0..35 dB and ends at 0x7F, i.e. no attenuation - an absolute anchor
       right in the block. */
    SetSteps(24);
    for (s = 0x51; s <= 0x7F; s += 2) {
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
        /* v25: down to -60 dB (28 508 ms / divisor), at most 6 s */
        Wait(EnvNoteMs(28508L / RateDiv(r - 1), 800, 6000));
        VoiceOff(V_TEST);
        Wait(600);
        StepDone();
    }
}

static void BlockEnvDelay(int n)
{
    TONE t;
    int d;
    if (!BlockMark(n, "envelope: ENVVOL delay")) return;
    SetSteps(16);
    /* A step of 400 units is 0.29 s, so already the sixth delay was longer
       than the 1600 ms note and the rest of the block was silence. A step of
       200 and a longer note fit entirely. */
    for (d = 0; d < 16; d++) {
        ToneDefaults(&t);
        t.envvol = (unsigned) (0x8000 - d * 200);
        LogLine("envvol 0x%04lX", (long) t.envvol, 0, 0);
        /* A mark right before the note. The delay is then measured as the
           gap MARK -> ONSET, i.e. within one pair - insensitive to g_ms and
           to the conversion of the log time. Measuring it from the shortening
           of the note (as until 2026-09-09) does not work: with large delays
           only a few hundred ms of the note remain and the two halves of the
           block then come out 45 % different. */
        Tick(0);
        /* 4000 ms instead of 2400: even at the largest delay a detectable
           note remains. */
        PlayTone(&t, 4000, 400);
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
    SetSteps(is_reverb ? 48 : 56);
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
            /* v25: the reverb lasts seconds, with 1100 ms the next note cut it */
            PlayTone(&t, 700, is_reverb ? 2500 : 1300);
            StepDone();
        }
        if (is_reverb) {
            /* impulse: a tick with full send - the reverb response without the
               shape of a note */
            ToneDefaults(&t);
            t.smp    = &SMP_TICK;
            t.reverb = 255;
            LogLine("preset %ld, tick impulse, send %ld", (long) p, 255L, 0);
            PlayTone(&t, 30, 3000);
            StepDone();
        } else {
            /* a steady tone without envelope: the chorus return level directly
               as the ratio send 255 / send 0 (an open question from run5) */
            for (s = 0; s < 2; s++) {
                ToneDefaults(&t);
                t.chorus = s ? 255u : 0u;
                LogLine("preset %ld, sustained tone, send %ld",
                        (long) p, s ? 255L : 0L, 0);
                PlayTone(&t, 2500, 1200);
                StepDone();
            }
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
    SetSteps(9);
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
        LogLine("noise, semitone %ld, loop %ld samples",
                (long) semis[i],
                (long) (SMP_NOISE.loop_end - SMP_NOISE.loop_start), 0);
        PlayTone(&t, 3500, 500);
        StepDone();
    }
    /* Short loop: 154 repetitions per note instead of 19. When the period
       shows here and not with the long loop, the loop length is to blame;
       when it does not show even here, the capture loses samples (see the
       QUALITY line in the log). */
    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        t.smp = &SMP_NOISE_SHORT;
        t.ip  = (unsigned) (IP_UNITY + (long) semis[i] * IP_OCT / 12);
        LogLine("noise, semitone %ld, loop %ld samples", (long) semis[i],
                (long) (SMP_NOISE_SHORT.loop_end - SMP_NOISE_SHORT.loop_start), 0);
        PlayTone(&t, 3500, 500);
        StepDone();
    }
}

/* Voice summing. Two passes, because each measures something else:
 *
 *   same   - all voices at the same IP. The in-phase sum is the only correct
 *            result, so any deviation is limiting or division by the voice
 *            count. This measures the CHIP.
 *   detune - detuned voices. Different frequencies add in power by
 *            arithmetic alone (sqrt N), so this pass mainly measures the
 *            detuning; it is here because that is how real music sounds.
 *
 * Until 2026-09-09 there was only the second pass, passed off as a summing
 * measurement. The detune step was also 1 IP unit (~0.3 cent), a beat with a
 * period around 9 s - longer than the note, so the momentary phase of the
 * beat was measured. DETUNE_STEP 8 gives a period around 1 s, which fits
 * into the note. */
#define DETUNE_STEP 8

static void BlockVoiceSum(int n)
{
    static int counts[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    /* The IFATN that brings the sum back to the level of one voice, in
       0.375 dB steps: 20 log10 N for in-phase voices, 10 log10 N for detuned
       ones (power). */
    static int comp20[8] = { 0, 16, 25, 32, 42, 48, 58, 64 };
    static int comp10[8] = { 0,  8, 13, 16, 21, 24, 29, 32 };
    TONE t;
    int i, k, pass, att;

    if (!BlockMark(n, "voice summing 1..16")) return;
    /* pass 0  same IP, no compensation (as in v24; 16 voices = +24 dB,
               which on the card in run5 hit the capture ceiling)
       pass 1  same IP, compensated - the level must stay; a deviation is
               the chip
       pass 2  same IP, base attenuation 64 (-24 dB): 16 voices reach just
               full level, measures the linearity of the sum without a
               ceiling
       pass 3  detuned, compensated in power */
    SetSteps(32);
    for (pass = 0; pass < 4; pass++) {
        for (i = 0; i < 8; i++) {
            switch (pass) {
            case 1:  att = comp20[i]; break;
            case 2:  att = 64;        break;
            case 3:  att = comp10[i]; break;
            default: att = 0;         break;
            }
            /* LogLine takes three longs, so no %s - the string is chosen in
               the format already, otherwise the pointer would be passed as
               a long. */
            LogLine(pass == 3 ? "voices %ld, detuned, atten %ld"
                              : "voices %ld, same pitch, atten %ld",
                    (long) counts[i], (long) att, 0);
            for (k = 0; k < counts[i]; k++) {
                ToneDefaults(&t);
                t.atten = (unsigned) att;
                t.ip = (unsigned) (IP_UNITY + (pass == 3 ? k * DETUNE_STEP : 0));
                ToneStart((unsigned) (V_EXTRA - k), &t);
            }
            Wait(1200);
            for (k = 0; k < counts[i]; k++) VoiceOff((unsigned) (V_EXTRA - k));
            Wait(800);
            StepDone();
        }
    }
}

/* ------------------------------------------------------------------------ *
 *  Blocks through the MIDI API - exactly the way a game plays
 * ------------------------------------------------------------------------ */
static void MidiNote(WORD ch, WORD note, WORD vel, unsigned on, unsigned off)
{
    LogFinish();
    awe32NoteOn(ch, note, vel);
    Wait(on);
    awe32NoteOff(ch, note, 64);
    Wait(off);
}

/* Timing of the MODULATION envelope. Blocks 8..13 measure only the volume
   one; the modulation envelope has its own registers and was not varied so
   far. It is measured through the filter: cutoff low, PEFE full, so the
   envelope opens the filter and its course is heard as a change of colour.
   Noise, so that the change shows in the whole spectrum. */
static void BlockModEnvTime(int n)
{
    /* Divisors 16..112, i.e. a rise of 742 to 106 ms. The original range
       (0x7F..0x44) gave 6 to 74 ms and could not be measured from the
       recording - changed per the measurement of 2026-09-09. */
    static int rate[8] = { 0x10, 0x14, 0x18, 0x20, 0x24, 0x2C, 0x34, 0x3C };
    TONE t;
    int  i;

    if (!BlockMark(n, "modulation envelope: attack and decay")) return;
    SetSteps(16);
    for (i = 0; i < 8; i++) {
        ToneDefaults(&t);
        t.smp     = &SMP_NOISE;
        t.cutoff  = 64;
        t.pefe    = 0x7F;              /* envelope -> filter, full up */
        t.mod_atk = (unsigned) rate[i];
        LogLine("mod attack 0x%02lX", (long) rate[i], 0, 0);
        PlayTone(&t, 2200, 400);
        StepDone();
    }
    for (i = 0; i < 8; i++) {
        ToneDefaults(&t);
        t.smp         = &SMP_NOISE;
        t.cutoff      = 64;
        t.pefe        = 0x7F;
        t.mod_decay   = (unsigned) rate[i];
        t.mod_sustain = 0x20;
        LogLine("mod decay 0x%02lX, mod sustain 0x20", (long) rate[i], 0, 0);
        PlayTone(&t, 2200, 400);
        StepDone();
    }
}

/* Note-off in different envelope phases. Everywhere else in this program
   it comes only in sustain, but in music notes are released during the
   attack and decay too - a different state of the automaton that nothing
   verified. */
static void BlockReleaseStage(int n)
{
    static int when[8] = { 40, 80, 160, 320, 640, 1000, 1500, 2200 };
    TONE t;
    int  i;

    if (!BlockMark(n, "note off during attack, decay and sustain")) return;
    SetSteps(8);
    for (i = 0; i < 8; i++) {
        ToneDefaults(&t);
        t.atk     = 0x58;        /* slow rise, so it can be hit */
        t.decay   = 0x58;
        t.sustain = 0x50;
        t.release = 0x60;
        LogLine("note off after %ld ms", (long) when[i], 0, 0);
        ToneStart(V_TEST, &t);
        Wait((unsigned) when[i]);
        ToneRelease(V_TEST, &t);
        Wait(1200);
        VoiceOff(V_TEST);
        Wait(400);
        StepDone();
    }
}

/* Pitch change WHILE a note plays - pitch bend and portamento. Block 4
   changes IP only before the start. Four step sizes show whether the chip
   switches the pitch abruptly or glides to it, and how the quantisation of
   a coarse step sounds. */
static void BlockPitchGlide(int n)
{
    static int gran[4] = { 16, 64, 256, 1024 };
    TONE t;
    int  i;
    long k;

    if (!BlockMark(n, "pitch changed while the note sounds")) return;
    SetSteps(4);
    for (i = 0; i < 4; i++) {
        ToneDefaults(&t);
        LogLine("glide +1 octave, %ld IP units per write",
                (long) gran[i], 0, 0);
        ToneStart(V_TEST, &t);
        for (k = 0; k <= (long) IP_OCT; k += gran[i]) {
            RegW(P_DATA3, R_IP, V_TEST, (unsigned) (IP_UNITY + k));
            Wait((unsigned) (1500L * gran[i] / (long) IP_OCT));
        }
        VoiceOff(V_TEST);
        Wait(500);
        StepDone();
    }
}

/* Cutoff change WHILE a note plays - CC74, modulation wheel, wah. The
   attenuation in the low half of IFATN stays zero, only the cutoff
   changes. */
static void BlockFilterGlide(int n)
{
    static int gran[4] = { 1, 4, 16, 64 };
    TONE t;
    int  i;
    long c;

    if (!BlockMark(n, "filter cutoff changed while the note sounds")) return;
    SetSteps(4);
    for (i = 0; i < 4; i++) {
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.cutoff = 0;
        LogLine("cutoff 0..255, %ld units per write", (long) gran[i], 0, 0);
        ToneStart(V_TEST, &t);
        for (c = 0; c <= 255; c += gran[i]) {
            RegW(P_DATA3, R_IFATN, V_TEST,
                 (unsigned) (((c << 8) & 0xFF00) | (g_att_offset & 0xFF)));
            Wait((unsigned) (1500L * gran[i] / 256L));
        }
        VoiceOff(V_TEST);
        Wait(500);
        StepDone();
    }
}

/* PTRX upper half (pitch target). The whole program wrote 0x4000 there so
   far and never verified what it does. In the Magic Carpet 2 intro 28 notes
   remain that differ exactly in it (difference -9216), so without this
   block we do not know whether the difference is audible at all. */
static void BlockPitchTarget(int n)
{
    static unsigned tgt[8] = { 0x0000, 0x1000, 0x2000, 0x3000,
                               0x4000, 0x5000, 0x6000, 0x7000 };
    TONE t;
    int  i;

    if (!BlockMark(n, "PTRX pitch target, without and with a pitch envelope"))
        return;
    SetSteps(16);

    /* Without the pitch envelope. In the first run all eight notes came
       out exactly the same - so PTRX on its own does nothing. */
    for (i = 0; i < 8; i++) {
        ToneDefaults(&t);
        t.ptrx_target = tgt[i];
        LogLine("PTRX target 0x%04lX, no pitch envelope",
                (long) tgt[i], 0, 0);
        PlayTone(&t, 1200, 400);
        StepDone();
    }

    /* With the pitch envelope on - "pitch target" makes sense only here.
       PEFE high byte = envelope depth to the pitch, the modulation envelope
       starts after a delay and with a slow rise, so the course is audible. */
    for (i = 0; i < 8; i++) {
        ToneDefaults(&t);
        t.ptrx_target = tgt[i];
        t.pefe        = 0x4000;      /* envelope -> pitch, to half the range */
        t.mod_atk     = 0x50;        /* slow rise of the mod envelope    */
        t.mod_decay   = 0x50;
        t.mod_sustain = 0x40;
        LogLine("PTRX target 0x%04lX, pitch envelope on",
                (long) tgt[i], 0, 0);
        PlayTone(&t, 1800, 400);
        StepDone();
    }
}

/* Drums from the wave ROM - the only reference for what sounds wrong in
   the Magic Carpet 2 intro. BULLFROG.SBK has no drums, the game takes them
   from the GM bank in the wave ROM, and nothing else in this program plays
   them.

   The block starts with checks, because in the first run (2026-09-08) all
   94 notes were written to the log but nothing sounded - and every further
   attempt costs one run of the VM. The order is therefore:
     1) a note on a melodic channel  - does the MIDI path play at all?
     2) drums without bank select    - as awe32InitMIDI left it
     3) drums with explicit bank 0   - the original (broken) setting
   Only then the whole set follows, in the setting of point 2. The log shows
   for every note which variant it is. */
static void BlockDrums(int n)
{
    static WORD chk[3] = { 36, 38, 42 };     /* kick, snare, hi-hat */
    static WORD vels[2] = { 40, 120 };
    WORD note;
    int  v, i;

    if (!BlockMark(n, "GM drum kit from wave ROM (MIDI ch 10)")) return;
    SetSteps(2 + 3 + 3 + 47 * 3);

    /* 1) melodic path check */
    awe32Controller(0, 0, 0);
    awe32ProgramChange(0, 0);
    for (i = 0; i < 2; i++) {
        LogLine("control: melodic ch 1, preset 0, note %ld",
                (long) (60 + i * 7), 0, 0);
        MidiNote(0, (WORD) (60 + i * 7), 120, 600, 900);
        StepDone();
    }

    /* 2) drums as awe32InitMIDI left them - no bank select */
    awe32ProgramChange(9, 0);
    for (i = 0; i < 3; i++) {
        LogLine("drum probe A (no bank select), note %ld",
                (long) chk[i], 0, 0);
        MidiNote(9, chk[i], 120, 600, 900);
        StepDone();
    }

    /* 3) drums with explicit bank 0 - the original setting that did not sound */
    awe32Controller(9, 0, 0);
    awe32ProgramChange(9, 0);
    for (i = 0; i < 3; i++) {
        LogLine("drum probe B (bank select 0), note %ld",
                (long) chk[i], 0, 0);
        MidiNote(9, chk[i], 120, 600, 900);
        StepDone();
    }

    /* The whole set back in the setting of point 2. */
    awe32InitMIDI();
    awe32ProgramChange(9, 0);
    for (note = 35; note <= 81; note++) {
        for (v = 0; v < 2; v++) {
            LogLine("drum note %ld, velocity %ld",
                    (long) note, (long) vels[v], 0);
            MidiNote(9, note, vels[v], 600, 900);
            StepDone();
        }
    }

    /* v25: the same set without effects (CC91 reverb, CC93 chorus = 0). In
       run5 the drum envelope could not be separated from the reverb. A
       different start of the description, so these notes do not get mixed up
       with "drum note". */
    awe32Controller(9, 91, 0);
    awe32Controller(9, 93, 0);
    for (note = 35; note <= 81; note++) {
        LogLine("dry drum note %ld, velocity %ld, CC91=0 CC93=0",
                (long) note, 120L, 0);
        MidiNote(9, note, 120, 600, 900);
        StepDone();
    }
    awe32InitMIDI();
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
                MidiNote(0, notes[i], vels[v], 600, 900);
                StepDone();
            }
        }
    }
}

/* ------------------------------------------------------------------------ *
 *  Bloky v25
 * ------------------------------------------------------------------------ */

/* EMU8000 equalizer (bass / treble) - registers INIT3 (Data1, index 3) and
 * INIT4 (Data2, index 3), values from alsa_emu8000_init.c. At initialisation
 * the SDK writes bass 0 dB and treble "+8 dB (HW default)" - verified in a
 * VM trace (b29gm_w.trace). Neither 86Box nor our core had the equalizer, and
 * the tilt of the recordings (+2.6 dB/oct) may be exactly it. The game driver
 * (trace dos97) writes other bytes to the same slots (C280 instead of C208) -
 * that variant is played too.
 */
static const unsigned eq_bass[12][3] = {
    { 0xD26A, 0xD36A, 0x0000 }, { 0xD25B, 0xD35B, 0x0000 },   /* -12, -8 */
    { 0xD24C, 0xD34C, 0x0000 }, { 0xD23D, 0xD33D, 0x0000 },   /*  -6, -4 */
    { 0xD21F, 0xD31F, 0x0000 }, { 0xC208, 0xC308, 0x0001 },   /*  -2,  0 */
    { 0xC219, 0xC319, 0x0001 }, { 0xC22A, 0xC32A, 0x0001 },   /*  +2, +4 */
    { 0xC24C, 0xC34C, 0x0001 }, { 0xC26E, 0xC36E, 0x0001 },   /*  +6, +8 */
    { 0xC248, 0xC384, 0x0002 }, { 0xC26A, 0xC36A, 0x0002 }    /* +10, +12 */
};
static const unsigned eq_treble[12][9] = {
    { 0x821E, 0xC26A, 0x031E, 0xC36A, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001 },
    { 0x821E, 0xC25B, 0x031E, 0xC35B, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001 },
    { 0x821E, 0xC24C, 0x031E, 0xC34C, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001 },
    { 0x821E, 0xC23D, 0x031E, 0xC33D, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001 },
    { 0x821E, 0xC21F, 0x031E, 0xC31F, 0x021E, 0xD208, 0x831E, 0xD308, 0x0001 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021E, 0xD208, 0x831E, 0xD308, 0x0002 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021D, 0xD219, 0x831D, 0xD319, 0x0002 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021C, 0xD22A, 0x831C, 0xD32A, 0x0002 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x021A, 0xD24C, 0x831A, 0xD34C, 0x0002 },
    { 0x821E, 0xD208, 0x031E, 0xD308, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002 },
    { 0x821D, 0xD219, 0x031D, 0xD319, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002 },
    { 0x821C, 0xD22A, 0x031C, 0xD32A, 0x0219, 0xD26E, 0x8319, 0xD36E, 0x0002 }
};
/* slot order as in EqWrite; from the dos97 trace (game driver) */
static const unsigned eq_driver[12] = {
    0xC280, 0xC380, 0x821E, 0xD280, 0x031E, 0xD380,
    0x0219, 0xD2E6, 0x8319, 0xD3E6, 0x0265, 0x8365
};

/* Write order as in snd_emu8000_update_equalizer. */
static void EqWrite(const unsigned *v)
{
    RegW(P_DATA1HI, 3, 0x01, v[0]);
    RegW(P_DATA1HI, 3, 0x11, v[1]);
    RegW(P_DATA1,   3, 0x11, v[2]);
    RegW(P_DATA1,   3, 0x13, v[3]);
    RegW(P_DATA1,   3, 0x1B, v[4]);
    RegW(P_DATA1HI, 3, 0x07, v[5]);
    RegW(P_DATA1HI, 3, 0x0B, v[6]);
    RegW(P_DATA1HI, 3, 0x0D, v[7]);
    RegW(P_DATA1HI, 3, 0x17, v[8]);
    RegW(P_DATA1HI, 3, 0x19, v[9]);
    RegW(P_DATA1HI, 3, 0x15, v[10]);
    RegW(P_DATA1HI, 3, 0x1D, v[11]);
}

static void EqSet(int bass, int treble)
{
    unsigned v[12], w;
    int      i;

    v[0] = eq_bass[bass][0];
    v[1] = eq_bass[bass][1];
    for (i = 0; i < 8; i++) v[2 + i] = eq_treble[treble][i];
    w = eq_bass[bass][2] + eq_treble[treble][8];
    v[10] = (w + 0x0262u) & 0xFFFFu;
    v[11] = (w + 0x8362u) & 0xFFFFu;
    EqWrite(v);
}

/* 35: equalizer. Noise at 1:1 with the filter wide open; the spectral
   difference between settings is the pure EQ curve (the filter and the
   capture tilt cancel). The shape of the "flat" setting is also a
   calibration of the whole path. */
static void BlockEq(int n)
{
    TONE t;
    int  i;

    if (!BlockMark(n, "EMU8000 equalizer: treble and bass, noise")) return;
    SetSteps(3 + 12 + 12 + 1);

    ToneDefaults(&t);
    t.smp = &SMP_NOISE;
    LogLine("EQ as the SDK left it (bass index 5, treble index 9 expected), noise",
            0, 0, 0);
    PlayTone(&t, 3000, 500);
    StepDone();

    EqSet(5, 5);
    LogLine("EQ flat: bass index %ld, treble index %ld, noise", 5L, 5L, 0);
    PlayTone(&t, 3000, 500);
    StepDone();

    EqWrite(eq_driver);
    LogLine("EQ bytes as the game driver writes them (C280 ...), noise", 0, 0, 0);
    PlayTone(&t, 3000, 500);
    StepDone();

    for (i = 0; i < 12; i++) {
        EqSet(5, i);
        LogLine("EQ treble index %ld, bass index %ld, noise", (long) i, 5L, 0);
        PlayTone(&t, 3000, 500);
        StepDone();
    }
    for (i = 0; i < 12; i++) {
        EqSet(i, 5);
        LogLine("EQ bass index %ld, treble index %ld, noise", (long) i, 5L, 0);
        PlayTone(&t, 3000, 500);
        StepDone();
    }

    EqSet(5, 9);                   /* rest of the run as before */
    LogLine("EQ back to the SDK setting: bass index %ld, treble index %ld, noise",
            5L, 9L, 0);
    PlayTone(&t, 3000, 500);
    StepDone();
}

/* 36: filter map. Where I am not sure:
 *  - cutoff shift with Q: a joint fit of blocks 7 and 28 wants -0.16 oct at
 *    Q15, but both blocks show it only indirectly. Here a 1357 Hz sine (the
 *    cutoff there lies around register 148) and the cutoff swept around it
 *    at Q 0, 5, 10, 15. The attenuation grows with Q, so the resonance peak
 *    does not hit the capture ceiling.
 *  - the top of the map: 255 came out ~11.2 kHz instead of 8 kHz; noise at
 *    240..255 in steps of 1.
 *  - clamping of the modulated cutoff from below (cutoff 0 and PEFE) and
 *    from above (255 and PEFE).
 */
static void BlockFilterMap(int n)
{
    static const int qs[4]   = { 0, 5, 10, 15 };
    static const int qatt[4] = { 0, 16, 32, 48 };
    static const int depth[5] = { -128, -64, 0, 64, 127 };
    TONE t;
    int  k, c;

    if (!BlockMark(n, "filter: cutoff map vs Q (sine), top of the map, clamps"))
        return;
    SetSteps(4 * 25 + 2 * 16 + 10);

    for (k = 0; k < 4; k++) {
        for (c = 100; c <= 196; c += 4) {
            ToneDefaults(&t);
            t.ip     = (unsigned) (IP_UNITY + 4096);
            t.cutoff = (unsigned) c;
            t.q      = (unsigned) qs[k];
            t.atten  = (unsigned) qatt[k];
            LogLine("map sine IP %ld, cutoff %ld, Q %ld",
                    (long) t.ip, (long) c, (long) qs[k]);
            PlayTone(&t, 400, 600);
            StepDone();
        }
    }

    for (k = 0; k < 2; k++) {
        for (c = 240; c <= 255; c++) {
            ToneDefaults(&t);
            t.smp    = &SMP_NOISE;
            t.cutoff = (unsigned) c;
            t.q      = k ? 15u : 0u;
            t.atten  = k ? 32u : 0u;
            LogLine("top noise cutoff %ld, Q %ld, atten %ld",
                    (long) c, k ? 15L : 0L, k ? 32L : 0L);
            PlayTone(&t, 500, 600);
            StepDone();
        }
    }

    for (k = 0; k < 5; k++) {
        ToneDefaults(&t);
        t.ip     = (unsigned) (IP_UNITY - 4096);     /* 339 Hz */
        t.cutoff = 0;
        t.pefe   = (unsigned) (depth[k] & 0xFF);
        LogLine("clamp low: sine IP %ld, cutoff 0, env->filter %ld",
                (long) t.ip, (long) depth[k], 0);
        PlayTone(&t, 1000, 600);
        StepDone();
    }
    for (k = 0; k < 5; k++) {
        ToneDefaults(&t);
        t.ip     = (unsigned) (IP_UNITY + 8191);     /* 2714 Hz */
        t.cutoff = 255;
        t.pefe   = (unsigned) (depth[k] & 0xFF);
        LogLine("clamp high: sine IP %ld, cutoff 255, env->filter %ld",
                (long) t.ip, (long) depth[k], 0);
        PlayTone(&t, 1000, 600);
        StepDone();
    }
}

/* 37: capture diagnostics. In run5 there were often ~150 ms of sound
 * ~8 dB quieter before notes, and we do not know whether it belongs to the
 * previous note, the next note or the capture. Here: silence, isolated notes
 * with long silence around them and two ways of ending a note (VoiceOff as
 * everywhere, and release 0x7F as the driver does).
 */
static void BlockDiag(int n)
{
    TONE t;
    int  i;

    if (!BlockMark(n, "capture diagnostics: silence, isolated notes, note endings"))
        return;
    SetSteps(10);

    SilenceAll();
    LogLine("silence %ld ms, all voices off", 5000L, 0, 0);
    LogFinish();
    Wait(5000);
    StepDone();

    for (i = 0; i < 3; i++) {
        ToneDefaults(&t);
        LogLine("isolated sine 400 ms, %ld ms silence around, VoiceOff", 2000L, 0, 0);
        LogFinish();
        Wait(2000);
        LogLine("isolated sine note on", 0, 0, 0);
        PlayTone(&t, 400, 2000);
        StepDone();
    }
    for (i = 0; i < 2; i++) {
        ToneDefaults(&t);
        t.release = 0x7F;
        LogLine("isolated sine 400 ms, ended by release 0x7F, VoiceOff after %ld ms",
                300L, 0, 0);
        LogFinish();
        Wait(2000);
        LogLine("isolated sine note on", 0, 0, 0);
        ToneStart(V_TEST, &t);
        Wait(400);
        ToneRelease(V_TEST, &t);
        Wait(300);
        VoiceOff(V_TEST);
        Wait(2000);
        StepDone();
    }
    for (i = 0; i < 2; i++) {
        ToneDefaults(&t);
        t.smp = &SMP_NOISE;
        LogLine("isolated noise 400 ms, %ld ms silence around", 2000L, 0, 0);
        LogFinish();
        Wait(2000);
        LogLine("isolated noise note on", 0, 0, 0);
        PlayTone(&t, 400, 2000);
        StepDone();
    }
    for (i = 0; i < 2; i++) {
        ToneDefaults(&t);
        t.atten = 48;
        LogLine("isolated sine atten %ld, %ld ms silence around", 48L, 2000L, 0);
        LogFinish();
        Wait(2000);
        LogLine("isolated sine note on", 0, 0, 0);
        PlayTone(&t, 400, 2000);
        StepDone();
    }
}

/* One pass of block 38 at the given gain. */
static void QuietPass(int want_db)
{
    static const int rel[4] = { 0x30, 0x40, 0x50, 0x60 };
    TONE t;
    WORD note;
    int  db, a, c, i, p;

    db = LevelBoost(want_db);

    /* anchor: the real gain is measured from the recording, not from the
       mixer step */
    ToneDefaults(&t);
    t.atten = 64;
    t.raw   = 1;
    LogLine("anchor sine atten %ld, boost %ld dB", 64L, (long) db, 0);
    PlayTone(&t, 1000, 600);
    StepDone();

    for (a = 64; a <= 126; a += 4) {               /* block 2, quiet end */
        ToneDefaults(&t);
        t.atten = (unsigned) a;
        t.raw   = 1;
        LogLine("quiet atten %ld, boost %ld dB", (long) a, (long) db, 0);
        PlayTone(&t, 400, 600);
        StepDone();
    }
    for (c = 0; c <= 96; c += 8) {                 /* block 6, closed filter */
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.cutoff = (unsigned) c;
        LogLine("quiet cutoff %ld, boost %ld dB", (long) c, (long) db, 0);
        PlayTone(&t, 500, 600);
        StepDone();
    }
    for (c = 0; c <= 64; c += 8) {                 /* block 28, 339 Hz */
        ToneDefaults(&t);
        t.ip     = (unsigned) (IP_UNITY - 4096);
        t.cutoff = (unsigned) c;
        LogLine("quiet filter sine IP %ld, cutoff %ld, boost %ld dB",
                (long) t.ip, (long) c, (long) db);
        PlayTone(&t, 400, 600);
        StepDone();
    }
    for (i = 0x31; i <= 0x51; i += 4) {            /* block 11 further down */
        ToneDefaults(&t);
        t.decay   = 0x60;
        t.sustain = (unsigned) i;
        LogLine("quiet sustain 0x%02lX, boost %ld dB", (long) i, (long) db, 0);
        PlayTone(&t, 1600, 600);
        StepDone();
    }
    for (i = 0x30; i <= 0x78; i += 8) {            /* block 10 to sustain 0x20 */
        ToneDefaults(&t);
        t.decay   = (unsigned) i;
        t.sustain = 0x20;
        LogLine("quiet decay 0x%02lX, sustain 0x20, boost %ld dB",
                (long) i, (long) db, 0);
        /* 71 dB: 33 734 ms / divisor */
        PlayTone(&t, EnvNoteMs(33734L / RateDiv(i - 1), 900, 6000), 500);
        StepDone();
    }
    for (i = 0; i < 4; i++) {                      /* block 12, tails */
        ToneDefaults(&t);
        t.release = (unsigned) rel[i];
        LogLine("quiet release 0x%02lX, boost %ld dB", (long) rel[i], (long) db, 0);
        ToneStart(V_TEST, &t);
        Wait(500);
        ToneRelease(V_TEST, &t);
        Wait(EnvNoteMs(28508L / RateDiv(rel[i] - 1), 800, 6000));
        VoiceOff(V_TEST);
        Wait(600);
        StepDone();
    }

    awe32InitMIDI();                               /* block 29, velocity 40 */
    awe32ProgramChange(9, 0);
    for (note = 35; note <= 81; note += 2) {
        LogLine("quiet drum note %ld, velocity %ld, boost %ld dB",
                (long) note, 40L, (long) db);
        MidiNote(9, note, 40, 600, 900);
        StepDone();
    }
    awe32Controller(0, 0, 0);                      /* block 26, velocity 40 */
    for (p = 0; p < 16; p++) {
        awe32ProgramChange(0, (WORD) p);
        LogLine("quiet GM preset %ld, note 60, velocity 40, boost %ld dB",
                (long) p, (long) db, 0);
        MidiNote(0, 60, 40, 600, 900);
        StepDone();
    }
    awe32InitMIDI();

    ToneDefaults(&t);
    t.atten = 64;
    t.raw   = 1;
    LogLine("anchor sine atten %ld, boost %ld dB", 64L, (long) db, 0);
    PlayTone(&t, 1000, 600);
    StepDone();

    LevelRestore();
}

/* ------------------------------------------------------------------------ *
 *  40 (v26): chorus, what block 23 does not answer
 *
 *  Block 23 plays short tones and two sustained ones per preset. What is
 *  still open is the shape of the wet path itself:
 *
 *    - the delay taps and the flanger sweep. A tick with full send gives
 *      them directly, the way the reverb tick does in block 23; eight ticks
 *      spaced over two seconds catch the LFO at different points of its
 *      cycle, so the sweep shows up as the echo moving.
 *    - the slow amplitude modulation of the wet signal reported for presets
 *      1 to 3. Two and a half seconds is not enough to see a cycle, so the
 *      sustained tone here runs for seven.
 *    - the comb notches, which noise shows far better than a sine.
 *
 *  Presets 1..4 only (0 is the shortest delay, 5..7 were measured in 23).
 * ------------------------------------------------------------------------ */
static void BlockChorusDetail(int n)
{
    TONE t;
    int  p, i;

    if (!BlockMark(n, "chorus detail: ticks, long sustained tone, noise"))
        return;
    SetSteps(4 * (8 + 1 + 1));

    for (p = 1; p <= 4; p++) {
        Trace("chorus detail preset %ld", (long) p);
        awe32Chorus((WORD) p);

        /* Eight ticks over two seconds - the LFO is somewhere else each
           time, so the delay of the echo moves between them. */
        for (i = 0; i < 8; i++) {
            ToneDefaults(&t);
            t.smp    = &SMP_TICK;
            t.chorus = 255u;
            LogLine("preset %ld, tick %ld, send 255", (long) p, (long) i, 0);
            PlayTone(&t, 30, 250);
            StepDone();
        }

        /* Long tone: one full cycle of the modulation has to fit in. */
        ToneDefaults(&t);
        t.chorus = 255u;
        LogLine("preset %ld, sustained tone 7 s, send 255", (long) p, 0, 0);
        PlayTone(&t, 7000, 1200);
        StepDone();

        /* Noise: the comb notches of the delay are visible in one shot. */
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.chorus = 255u;
        LogLine("preset %ld, noise 3 s, send 255", (long) p, 0, 0);
        PlayTone(&t, 3000, 1000);
        StepDone();
    }

    Trace("chorus detail: resetting", 0);
    awe32Chorus(0);
}

/* ------------------------------------------------------------------------ *
 *  41 (v26): the filter far below cutoff (stopband)
 *
 *  Blocks 6, 28 and 36 sweep the cutoff under a tone of a fixed pitch, so
 *  the deep part of the slope ends up under the noise floor of the capture
 *  and cannot be read. Here it is the other way round: the cutoff stays at
 *  the bottom of the range and the TONE climbs away from it, with the
 *  capture level raised by 24 dB (the same way block 38 does it) and the
 *  tone at full level, so even 60 dB down is still above the floor.
 *
 *  IP is 16 bit and IP_UNITY is 0xE000, so +2 octaves (2714 Hz) is as high
 *  as the sine goes; noise covers the rest of the band.
 *
 *  Q is measured too: it lifts the peak, and whether it changes anything
 *  three octaves below is exactly what our filter cannot answer.
 * ------------------------------------------------------------------------ */
static void BlockStopband(int n)
{
    static const int cut[3] = { 0, 16, 32 };
    static const int ip[4]  = { -4096, 0, 4096, 8191 };  /* 339..2714 Hz */
    TONE t;
    int  db, c, i, k;

    if (!BlockMark(n, "filter stopband: tone far above cutoff, +24 dB"))
        return;
    SetSteps(3 * 4 + 2 * 4 + 3 + 1);

    db = LevelBoost(24);

    /* Anchor: the same tone with the filter wide open. Everything else in
       the block is read against it, so the boost cancels out. */
    ToneDefaults(&t);
    t.cutoff = 255u;
    LogLine("anchor sine, cutoff 255, boost %ld dB", (long) db, 0, 0);
    PlayTone(&t, 1000, 600);
    StepDone();

    for (c = 0; c < 3; c++) {
        for (i = 0; i < 4; i++) {
            ToneDefaults(&t);
            t.ip     = (unsigned) (IP_UNITY + ip[i]);
            t.cutoff = (unsigned) cut[c];
            LogLine("stopband sine IP %ld, cutoff %ld, boost %ld dB",
                    (long) t.ip, (long) cut[c], (long) db);
            PlayTone(&t, 600, 700);
            StepDone();
        }
    }

    /* Same thing with resonance: Q 15 at the bottom of the range. The peak
       is loud, hence the extra attenuation, as in block 36. */
    for (k = 0; k < 2; k++) {
        for (i = 0; i < 4; i++) {
            ToneDefaults(&t);
            t.ip     = (unsigned) (IP_UNITY + ip[i]);
            t.cutoff = (unsigned) (k ? 32u : 0u);
            t.q      = 15u;
            t.atten  = 48u;
            LogLine("stopband Q15 sine IP %ld, cutoff %ld, boost %ld dB",
                    (long) t.ip, (long) (k ? 32 : 0), (long) db);
            PlayTone(&t, 600, 700);
            StepDone();
        }
    }

    /* Noise at the very bottom: the whole band at once, so the shape of the
       slope can be read from one spectrum. */
    for (c = 0; c < 3; c++) {
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.cutoff = (unsigned) cut[c];
        LogLine("stopband noise cutoff %ld, boost %ld dB",
                (long) cut[c], (long) db, 0);
        PlayTone(&t, 1500, 800);
        StepDone();
    }

    LevelRestore();
}

/* 38: what drowned in noise in run5, again at +12 and +24 dB. */
/* ------------------------------------------------------------------------ *
 *  42 (v27): the chorus feedback loop, without the LFO
 *
 *  Block 40 left one thing open: after a tick the card's chorus return dies
 *  away smoothly (about 0.27 dB/ms for presets 2 and 3), while a delay line
 *  with the feedback from the register (1/16, 1/8) falls by 18-24 dB per
 *  pass. The first repeat has the register's level, the later ones decay
 *  far slower. With an 18 ms loop, a 30 ms tick and the delay swinging it
 *  cannot be told what the loop really is.
 *
 *  Here preset 6 (short delay: 64 ms, LFO depth 0, the right tap 40 ms
 *  earlier) is set and only its feedback register (INIT3 slot 9, low byte)
 *  is overwritten, so the repeats come 64 ms apart and stand still. Ticks at
 *  three pitches show whether the loop gain depends on frequency, feedback
 *  0x00 shows whether there is a tail with no feedback at all, and a lower
 *  send at 0x80 whether it depends on the level. The right output of the
 *  card carries the return alone - record the line out as well.
 * ------------------------------------------------------------------------ */
static void BlockChorusLoop(int n)
{
    static const unsigned fb[5] = { 0x00, 0x10, 0x40, 0x80, 0xC0 };
    static const int      ip[3] = { -8192, 0, 8191 };   /* 184, 735, 2940 Hz; IP is 16 bit */
    TONE t;
    int  f, i, k;

    if (!BlockMark(n, "chorus loop: preset 6 without LFO, feedback 00..C0, ticks"))
        return;
    SetSteps(5 * 3 * 2 + 2);

    for (f = 0; f < 5; f++) {
        Trace("chorus loop feedback 0x%02lX", (long) fb[f]);
        awe32Chorus(6);
        RegW(P_DATA1, 3, 9, 0xE600u | fb[f]);
        for (i = 0; i < 3; i++) {
            for (k = 0; k < 2; k++) {
                ToneDefaults(&t);
                t.smp    = &SMP_TICK;
                t.ip     = (unsigned) (IP_UNITY + ip[i]);
                t.chorus = 255u;
                LogLine("feedback 0x%02lX, tick IP %ld, send 255", (long) fb[f], (long) t.ip, 0);
                PlayTone(&t, 30, fb[f] >= 0xC0 ? 5000 : 2500);
                StepDone();
            }
        }
        if (fb[f] == 0x80) {
            for (k = 0; k < 2; k++) {
                ToneDefaults(&t);
                t.smp    = &SMP_TICK;
                t.chorus = 64u;
                LogLine("feedback 0x%02lX, tick IP %ld, send 64", (long) fb[f], (long) t.ip, 0);
                PlayTone(&t, 30, 2500);
                StepDone();
            }
        }
    }

    Trace("chorus loop: resetting", 0);
    awe32Chorus(0);
}

/* ------------------------------------------------------------------------ *
 *  43-46 (v28): the last line-out measurements
 *
 *  Everything here is meant for the LINE OUT recording (the card's right
 *  output carries the effect returns alone). Earlier line recordings were
 *  clipped by the recorder, so all of this is played quieter (atten 12 and
 *  more), and block 43 opens with the loudest signal of blocks 43-46 as a
 *  calibration: the recording level is set so that it stays clear of full
 *  scale.
 * ------------------------------------------------------------------------ */

/* N voices of the same low sine (170 Hz), set up first and triggered back to
   back so they start in phase; absolute level (no g_att_offset). */
static void PlayVoices(int nv, unsigned att, unsigned on, unsigned off)
{
    TONE t;
    int  k;

    ToneDefaults(&t);
    t.ip    = (unsigned) (IP_UNITY - 2 * IP_OCT);
    t.atten = att;
    t.raw   = 1;
    for (k = 0; k < nv; k++) ToneSetup((unsigned) (V_EXTRA - k), &t);
    LogFinish();
    for (k = 0; k < nv; k++) ToneTrigger((unsigned) (V_EXTRA - k), &t);
    Wait(on);
    for (k = 0; k < nv; k++) VoiceOff((unsigned) (V_EXTRA - k));
    Wait(off);
}

/* 43: headroom of the output, the volume slide with the envelope running,
   chorus saturation. */
static void BlockHeadroom(int n)
{
    static const int      cnt[6]  = { 1, 2, 3, 4, 6, 8 };
    static const unsigned csend[4] = { 32u, 64u, 128u, 255u };
    unsigned saved = wt_level;
    TONE     t;
    int      pass, i, k, p;

    if (!BlockMark(n, "line out: headroom, volume slide, chorus saturation"))
        return;
    SetSteps(2 + 3 * 6 + 2 + 4 + 8 + 4 + 8);

    /* Calibration: the loudest signal of blocks 43-46 (8 voices in phase,
       +18 dB over one voice). The recording level must keep it clear of
       full scale. */
    for (k = 0; k < 2; k++) {
        LogLine("calibration: 8 voices in phase, loudest signal of blocks 43-46", 0, 0, 0);
        PlayVoices(8, 0u, 2000, 800);
        StepDone();
    }

    /* Headroom: 1..8 voices in phase = 0 .. +18 dB. Pass 1 with the
       wavetable mixer 12 dB lower - if the knee moves with it, it is the
       analog stage, if not, the chip. Pass 2 at -18 dB: linear sum. */
    for (pass = 0; pass < 3; pass++) {
        if (pass == 1) {
            wt_level = (saved >= 0x30u) ? saved - 0x30u : 0u;
            MixerW(0x34, (unsigned char) wt_level);
            MixerW(0x35, (unsigned char) wt_level);
        }
        if (pass == 2) {
            wt_level = saved;
            MixerW(0x34, (unsigned char) wt_level);
            MixerW(0x35, (unsigned char) wt_level);
        }
        for (i = 0; i < 6; i++) {
            LogLine(pass == 1 ? "headroom: %ld voices in phase, atten %ld, mixer -12 dB"
                              : "headroom: %ld voices in phase, atten %ld",
                    (long) cnt[i], pass == 2 ? 48L : 0L, 0);
            PlayVoices(cnt[i], pass == 2 ? 48u : 0u, 1500, 700);
            StepDone();
        }
    }
    wt_level = saved;
    MixerW(0x34, (unsigned char) wt_level);
    MixerW(0x35, (unsigned char) wt_level);

    /* Volume slide with the envelope on: IFATN steps during a held note
       (12 -> 72 -> 12 -> 48, i.e. -22.5 / +22.5 / -13.5 dB - the low level
       has to stay well above the line noise). */
    for (k = 0; k < 2; k++) {
        ToneDefaults(&t);
        t.atten = 12;
        LogLine("slide: held sine, IFATN 12 -> 72 -> 12 -> 48 every 700 ms", 0, 0, 0);
        ToneStart(V_TEST, &t);
        Wait(700);
        RegW(P_DATA3, R_IFATN, V_TEST, (255u << 8) | ((72u + g_att_offset) & 0xFF));
        Wait(700);
        RegW(P_DATA3, R_IFATN, V_TEST, (255u << 8) | ((12u + g_att_offset) & 0xFF));
        Wait(700);
        RegW(P_DATA3, R_IFATN, V_TEST, (255u << 8) | ((48u + g_att_offset) & 0xFF));
        Wait(700);
        VoiceOff(V_TEST);
        Wait(800);
        StepDone();
    }
    /* Instant attack, high sine: the rise of the note shows the upward
       volume slide. */
    for (k = 0; k < 4; k++) {
        ToneDefaults(&t);
        t.ip    = (unsigned) (IP_UNITY + 2 * IP_OCT - 1);
        t.atten = 12;
        LogLine("slide: note-on, sine +2 oct, instant attack", 0, 0, 0);
        PlayTone(&t, 300, 500);
        StepDone();
    }

    /* Chorus saturation: preset 6 (64 ms, no LFO) with feedback 0 and 0xC0,
       ticks with send 32..255; then held tones of presets 1 and 4 at three
       levels. */
    for (k = 0; k < 2; k++) {
        unsigned fb = k ? 0xC0u : 0x00u;
        awe32Chorus(6);
        RegW(P_DATA1, 3, 9, 0xE600u | fb);
        for (i = 0; i < 4; i++) {
            if (k && (i == 0 || i == 2)) continue;
            for (p = 0; p < 2; p++) {
                ToneDefaults(&t);
                t.smp    = &SMP_TICK;
                t.chorus = csend[i];
                t.atten  = 12;
                LogLine("chorus ladder: feedback 0x%02lX, tick send %ld", (long) fb, (long) csend[i], 0);
                PlayTone(&t, 30, k ? 4000 : 1000);
                StepDone();
            }
        }
    }
    for (p = 1; p <= 4; p += 3) {
        awe32Chorus((WORD) p);
        for (i = 0; i < 4; i++) {
            ToneDefaults(&t);
            t.atten  = (unsigned) (i == 0 ? 12 : 12 * i);
            t.chorus = i == 0 ? 0u : 255u;
            LogLine("chorus ladder: preset %ld, held sine atten %ld, send %ld",
                    (long) p, (long) t.atten, (long) t.chorus);
            PlayTone(&t, 3000, 1200);
            StepDone();
        }
    }
    awe32Chorus(0);
}

/* 44: interpolation and filter on the line out. */
static void BlockInterpFilter(int n)
{
    static const long nip[9] = { -12288L, -8192L, -4096L, -2048L, -683L, 0L, 2048L, 4096L, 8191L };
    static const long sip[4] = { 1365L, 4096L, 6827L, 8191L };
    static const int  cut[7] = { 32, 64, 96, 128, 160, 192, 224 };
    static const int  qs[3]  = { 0, 8, 15 };
    TONE t;
    int  i, k;

    if (!BlockMark(n, "line out: interpolation, filter on noise"))
        return;
    SetSteps(9 + 4 + 3 + 21);

    for (i = 0; i < 9; i++) {
        ToneDefaults(&t);
        t.smp   = &SMP_NOISE;
        t.ip    = (unsigned) ((long) IP_UNITY + nip[i]);
        t.atten = 12;
        LogLine("interpolation: noise, IP offset %ld", nip[i], 0, 0);
        PlayTone(&t, 1500, 600);
        StepDone();
    }
    for (i = 0; i < 4; i++) {
        ToneDefaults(&t);
        t.ip    = (unsigned) ((long) IP_UNITY + sip[i]);
        t.atten = 12;
        LogLine("interpolation: sine, IP offset %ld", sip[i], 0, 0);
        PlayTone(&t, 1200, 600);
        StepDone();
    }

    /* Q 0 / 8 / 15 at atten 12 / 24 / 36: the resonance peak stays under
       the calibration level, the stopband as far above the noise as it
       goes. Each level has its own anchor (filter open). */
    for (k = 0; k < 3; k++) {
        ToneDefaults(&t);
        t.smp   = &SMP_NOISE;
        t.atten = (unsigned) (12 + 12 * k);
        LogLine("filter: noise anchor, cutoff 255, Q 0, atten %ld", (long) t.atten, 0, 0);
        PlayTone(&t, 1500, 600);
        StepDone();
    }
    for (k = 0; k < 3; k++) {
        for (i = 0; i < 7; i++) {
            ToneDefaults(&t);
            t.smp    = &SMP_NOISE;
            t.atten  = (unsigned) (12 + 12 * k);
            t.cutoff = (unsigned) cut[i];
            t.q      = (unsigned) qs[k];
            LogLine("filter: noise, cutoff %ld, Q %ld, atten %ld", (long) cut[i], (long) qs[k], (long) t.atten);
            PlayTone(&t, 1500, 600);
            StepDone();
        }
    }
}

/* 45: reverb with noise - colour of the tail and the impulse response. */
static void BlockReverbNoise(int n)
{
    TONE t;
    int  p;

    if (!BlockMark(n, "line out: reverb with noise (colour, impulse, EQ)"))
        return;
    SetSteps(8 * 2 + 2);

    for (p = 0; p < 8; p++) {
        awe32Reverb((WORD) p);
        /* 500 ms at atten 6: the tail's upper bands (damping) must stay
           above the line noise up to 1.4 s */
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 6;
        t.reverb = 255u;
        LogLine("reverb preset %ld, noise burst 500 ms, send 255", (long) p, 0, 0);
        PlayTone(&t, 500, 3000);
        StepDone();
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 6;
        t.reverb = 255u;
        LogLine("reverb preset %ld, noise click 10 ms, send 255", (long) p, 0, 0);
        PlayTone(&t, 10, 2500);
        StepDone();
    }

    /* Does the EQ act on the reverb return? Treble flat, then +12 dB. */
    awe32Reverb(4);
    for (p = 0; p < 2; p++) {
        EqSet(5, p ? 11 : 5);
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 6;
        t.reverb = 255u;
        LogLine("reverb EQ: preset 4, treble %ld, noise burst 500 ms", p ? 11L : 5L, 0, 0);
        PlayTone(&t, 500, 3000);
        StepDone();
    }
    EqSet(5, 9);                    /* back to the SDK default */
    awe32Reverb(0);
}

/* 46: chorus with noise, long flanger tone, chorus -> reverb, EQ. */
static void BlockChorusNoise(int n)
{
    TONE t;
    int  p;

    if (!BlockMark(n, "line out: chorus with noise, flanger, chorus to reverb, EQ"))
        return;
    SetSteps(8 + 2 + 2 + 2);

    for (p = 0; p < 8; p++) {
        awe32Chorus((WORD) p);
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 12;
        t.chorus = 255u;
        LogLine("chorus preset %ld, noise 3 s, send 255", (long) p, 0, 0);
        PlayTone(&t, 3000, 1200);
        StepDone();
    }

    /* Long tones: the flanger LFO is 0.1 Hz, preset 0 about 0.3 Hz. */
    awe32Chorus(5);
    ToneDefaults(&t);
    t.atten  = 12;
    t.chorus = 255u;
    LogLine("chorus preset 5 (flanger), held sine 12 s, send 255", 0, 0, 0);
    PlayTone(&t, 12000, 1500);
    StepDone();
    awe32Chorus(0);
    ToneDefaults(&t);
    t.atten  = 12;
    t.chorus = 255u;
    LogLine("chorus preset 0, held sine 8 s, send 255", 0, 0, 0);
    PlayTone(&t, 8000, 1500);
    StepDone();

    /* Chorus -> reverb: 64 ms echo without feedback and hall 2. With chorus
       send only, any reverb tail in the return means the chorus feeds the
       reverb. */
    awe32Chorus(6);
    awe32Reverb(4);
    for (p = 0; p < 2; p++) {
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 6;
        t.chorus = p ? 0u : 255u;
        t.reverb = p ? 255u : 0u;
        LogLine(p ? "chorus to reverb: noise 300 ms, reverb send only"
                  : "chorus to reverb: noise 300 ms, chorus send only", 0, 0, 0);
        PlayTone(&t, 300, 3000);
        StepDone();
    }
    awe32Reverb(0);

    /* Does the EQ act on the chorus return? */
    awe32Chorus(2);
    for (p = 0; p < 2; p++) {
        EqSet(5, p ? 11 : 5);
        ToneDefaults(&t);
        t.smp    = &SMP_NOISE;
        t.atten  = 12;
        t.chorus = 255u;
        LogLine("chorus EQ: preset 2, treble %ld, noise 2 s", p ? 11L : 5L, 0, 0);
        PlayTone(&t, 2000, 1200);
        StepDone();
    }
    EqSet(5, 9);
    awe32Chorus(0);
}

static void BlockQuiet(int n)
{
    if (!BlockMark(n, "quiet material again at +12 and +24 dB capture level"))
        return;
    SetSteps(2 * 100);
    QuietPass(12);
    QuietPass(24);
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

/* Bank 0 = the GM set built into the library, bank 1 = the game bank.
 *
 * With SBKLIB the GM presets are directly in `PAWE32.LIB` (objects
 * awe32SPad1Obj to SPad7Obj) and their samples lie in the wave ROM on the
 * card - hence bank 0 needs zero bytes of memory. Exactly this is what the
 * SDK demo does at the comment "use embeded preset objects", and exactly
 * this AWETEST never did, so bank 0 stayed empty and blocks 26 and 29 were
 * silent.
 *
 * The sizes of both banks must be defined with one call, which is why it is
 * here and not in LoadBank. Always called, even when the game bank is
 * missing. */
static void SetupBanks(void)
{
    awe32TotalPatchRam(&spSound);
    Trace("SetupBanks: patch RAM %ld B", (long) spSound.total_patch_ram);

    spSound.bank_no     = 0;
    spSound.total_banks = 2;
    lBankSizes[0] = 0;                        /* GM is in ROM, no memory needed */
    lBankSizes[1] = (long) spSound.total_patch_ram;   /* game bank to DRAM */
    spSound.banksizes = lBankSizes;
    awe32DefineBankSizes(&spSound);

    awe32SoundPad.SPad1 = awe32SPad1Obj;
    awe32SoundPad.SPad2 = awe32SPad2Obj;
    awe32SoundPad.SPad3 = awe32SPad3Obj;
    awe32SoundPad.SPad4 = awe32SPad4Obj;
    awe32SoundPad.SPad5 = awe32SPad5Obj;
    awe32SoundPad.SPad6 = awe32SPad6Obj;
    awe32SoundPad.SPad7 = awe32SPad7Obj;
    Trace("SetupBanks: embedded GM presets in bank 0", 0);
}

static int LoadBank(const char *file)
{
    FILE *fp;
    WORD  i;

    fp = fopen(file, "rb");
    if (!fp) return 1;

    spSound.bank_no = 1;                      /* the game bank is 1 */
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
 *  What card is this (v25)
 *
 *  The card model of run5 is unknown, and that card had an LFO 9 % slower
 *  than ver3. Everything the card reveals about itself therefore goes into
 *  the log (lines "# CARD"). The model guess from the DSP version is only a
 *  hint - the raw numbers decide.
 * ------------------------------------------------------------------------ */
static unsigned EmuReadW(unsigned port, unsigned idx, unsigned voice)
{
    OUTW(P_PTR, (unsigned) ((idx << 5) | (voice & 31)));
    return (unsigned) INW(port);
}

static void CardLog(const char *line)
{
    if (g_log) fprintf(g_log, "# CARD %s\n", line);
    printf("  %s\n", line);
}

static void CardInfo(void)
{
    static const char *envs[4] = { "BLASTER", "SOUND", "CTSYN", "MIDI" };
    char        buf[200], copy[81];
    const char *env, *guess;
    int         maj = -1, mnr = -1, i, c, k;
    unsigned    reg;

    printf("\nCard identification:\n");
    for (i = 0; i < 4; i++) {
        env = getenv(envs[i]);
        if (!env && i > 0) continue;
        sprintf(buf, "%s=%.160s", envs[i], env ? env : "(not set)");
        CardLog(buf);
    }
    sprintf(buf, "Sound Blaster base 0x%03X, EMU8000 base 0x%03X, IRQ %d,"
            " 16-bit DMA %d", g_sb_base, g_base, rec_irq, rec_dma);
    CardLog(buf);

    copy[0] = 0;
    if (DspReset() == 0) {
        DspWrite(0xE1);                     /* DSP version */
        maj = DspRead();
        mnr = DspRead();
        DspWrite(0xE3);                     /* copyright */
        for (i = 0; i < 80; i++) {
            c = DspRead();
            if (c <= 0) break;
            copy[i] = (char) ((c >= 32 && c < 127) ? c : '?');
        }
        copy[i] = 0;
        DspReset();
    }
    sprintf(buf, "DSP version %d.%02d, copyright \"%s\"", maj, mnr, copy);
    CardLog(buf);

    /* awe32DramSize is in words (VM: 262144 at 512 kB) */
    sprintf(buf, "DRAM awe32DramSize %lu words = %lu B, free for samples %lu B",
            (unsigned long) awe32DramSize, (unsigned long) awe32DramSize * 2UL,
            (unsigned long) spSound.total_patch_ram);
    CardLog(buf);

    sprintf(buf, "EMU8000 HWCF1 0x%04X HWCF2 0x%04X HWCF3 0x%04X",
            EmuReadW(P_DATA1, 1, 29), EmuReadW(P_DATA1, 1, 30),
            EmuReadW(P_DATA1, 1, 31));
    CardLog(buf);

    /* The equalizer as the SDK left it - slot order as in EqWrite. Reading
       these registers is undocumented; if it returns nonsense, no harm. */
    sprintf(buf, "EQ readback %04X %04X %04X %04X %04X %04X %04X %04X %04X"
            " %04X %04X %04X",
            EmuReadW(P_DATA1HI, 3, 0x01), EmuReadW(P_DATA1HI, 3, 0x11),
            EmuReadW(P_DATA1, 3, 0x11),   EmuReadW(P_DATA1, 3, 0x13),
            EmuReadW(P_DATA1, 3, 0x1B),   EmuReadW(P_DATA1HI, 3, 0x07),
            EmuReadW(P_DATA1HI, 3, 0x0B), EmuReadW(P_DATA1HI, 3, 0x0D),
            EmuReadW(P_DATA1HI, 3, 0x17), EmuReadW(P_DATA1HI, 3, 0x19),
            EmuReadW(P_DATA1HI, 3, 0x15), EmuReadW(P_DATA1HI, 3, 0x1D));
    CardLog(buf);

    /* The mixer as the previous program left it (before MixerInit). */
    k = sprintf(buf, "mixer");
    for (reg = 0x30; reg <= 0x3F; reg++)
        k += sprintf(buf + k, " %02X:%02X", reg, MixerR((unsigned char) reg));
    CardLog(buf);
    k = sprintf(buf, "mixer");
    for (reg = 0x40; reg <= 0x47; reg++)
        k += sprintf(buf + k, " %02X:%02X", reg, MixerR((unsigned char) reg));
    for (reg = 0x80; reg <= 0x82; reg++)
        k += sprintf(buf + k, " %02X:%02X", reg, MixerR((unsigned char) reg));
    CardLog(buf);

    if (maj == 4 && mnr >= 16)      guess = "AWE64 family (DSP 4.16)";
    else if (maj == 4 && mnr == 13) guess = "AWE32 PnP or SB32 (DSP 4.13)";
    else if (maj == 4 && mnr == 12) guess = "AWE32 non-PnP (DSP 4.12)";
    else if (maj == 4 && mnr == 11) guess = "early AWE32 / SB16 (DSP 4.11)";
    else if (maj == 4)              guess = "Sound Blaster 16 family";
    else if (maj < 0)               guess = "DSP did not answer";
    else                            guess = "not an SB16-class DSP (clone or emulator)";
    sprintf(buf, "guess: %s, %s", guess,
            awe32DramSize == 0UL ? "no sample RAM (SB32?)"
            : (awe32DramSize <= 262144UL ? "512 kB sample RAM (stock)"
                                               : "expanded sample RAM"));
    CardLog(buf);
    printf("  Please also note the CT number printed on the card itself.\n");
    if (g_log) CommitFile(g_log);
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
        else if (!strncmp(argv[n], "/ONLY:", 6) || !strncmp(argv[n], "/only:", 6)) {
            const char *s = argv[n] + 6;
            while (*s) {
                int b = atoi(s);
                if (b >= 0 && b <= 99) g_only[b] = 1;
                g_only_set = 1;
                while (*s && *s != ',') s++;
                if (*s == ',') s++;
            }
        }
        else if (!strncmp(argv[n], "/MB:", 4) || !strncmp(argv[n], "/mb:", 4))
            cap_cap = (unsigned long) atol(argv[n] + 4) * 1024UL * 1024UL;
        else if (!strncmp(argv[n], "/REC:", 5) || !strncmp(argv[n], "/rec:", 5))
            rec_name = argv[n] + 5;
        else if (!strncmp(argv[n], "/SBK:", 5) || !strncmp(argv[n], "/sbk:", 5))
            sbk_arg = argv[n] + 5;
        else if (!strncmp(argv[n], "/WT:", 4) || !strncmp(argv[n], "/wt:", 4)) {
            /* Manual override of the wavetable level. Normally not needed -
               with the internal capture the program sets it itself. */
            wt_level = (unsigned) strtoul(argv[n] + 4, 0, 16);
            if (wt_level > 0xFF) wt_level = 0xFF;
        }
        else {
            printf("Usage: AWETEST [/FROM:n] [/TO:n] [/ONLY:n,n,..] [/MB:n] [/REC:file.wav]"
                   " [/SBK:path] [/WT:hex]\n");
            printf("  with no switches all 46 blocks play (about %ld minutes)\n",
                   AWETEST_SECONDS / 60L);
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
    printf("AWETEST v%s - AWE32 calibration recording\n", AWETEST_VER);

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
    RtInit();                       /* real time for the log stamps */
    Trace("hardware ok, base %03lX", (long) sb);
    Trace("  irq %ld", (long) rec_irq);
    Trace("  dma %ld", (long) rec_dma);

    awe32TotalPatchRam(&spSound);
    printf("Free sample RAM: %lu bytes\n", (unsigned long) spSound.total_patch_ram);

    if (sbk_arg) {
        strcpy(bank_path, sbk_arg);
        SetupBanks();
        rc = FileThere(bank_path) ? LoadBank(bank_path) : -1;
    } else {
        SetupBanks();
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
    /* A big buffer: the line gets its stamp right before the note, and a
       disk write in the middle of a block would delay the note. It goes to
       disk at the block mark. */
    if (g_log) setvbuf(g_log, 0, _IOFBF, 32768);
    if (g_log) fprintf(g_log, "# AWETEST v%s\n", AWETEST_VER);
    if (g_log) fprintf(g_log, "# ms block description\n");
    if (g_log) fprintf(g_log, "# stamps: \\t@rt <BIOS tick>:<PIT phase 0..65535,"
                       " 1193182/s> cap <file from ms>:<frame>, taken at note on\n");

    CardInfo();
    ChipProbe();

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
        printf("  Mixer set to a known state: master full, wavetable 0x%02X\n",
               wt_level);
        printf("  below full (headroom - at full the card's output stage\n");
        printf("  clips and the loudest notes come out distorted), CD and\n");
        printf("  line muted in the output mix.\n");

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

    /* The level is fine-tuned only when the output is the internal capture -
       with external recording it would overdrive the card's output. */
    if (rec_ok && rec_name) WavetableForInternal(rec_src);
    if (rec_ok && rec_name) LevelLadder();

    if (rec_ok) PitchCheck();

    /* Both capture channels must be alive (v25: each separately, with
       diagnostics). */
    if (rec_ok) StereoCheck();
    if (g_log)
        fprintf(g_log, "# CAPTURE source %s, wavetable 0x%02X, input gain %d dB,"
                " atten offset %u, mono %s\n",
                rec_ok ? (rec_src == SRC_WAVETABLE ? "MIDI/wavetable" : "line in")
                       : "none (external)",
                wt_level, rec_gain * 6, g_att_offset,
                g_mono_adc == 'L' ? "left only" : (g_mono_adc == 'R' ? "right only" : "no"));

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

    /* First say which channel is which - without it the pan cannot be
       evaluated */
    printf("Marking channels: 3 tones hard LEFT, then 2 hard RIGHT.\n");
    ChannelMark();
    Trace("channel marks done", 0);
    RefTone();                     /* level at the start of the first file */

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
    BlockFilterSine(n++);           /* at the end, so the block numbers 1..27 stay */
    BlockDrums(n++);                /* reference for the drums in the MC2 intro */
    BlockModEnvTime(n++);
    BlockReleaseStage(n++);
    BlockPitchGlide(n++);
    BlockFilterGlide(n++);
    BlockPitchTarget(n++);
    BlockEq(n++);                   /* 35 v25: chip equalizer            */
    BlockFilterMap(n++);            /* 36 v25: filter map, Q, clamps      */
    BlockDiag(n++);                 /* 37 v25: silence, isolated notes    */
    BlockQuiet(n++);                /* 38 v25: quiet things at +12/+24 dB */
    BlockReference(n++);            /* 39 (was 35 up to v24)             */
    BlockChorusDetail(n++);         /* 40 v26: chorus up close           */
    BlockStopband(n++);             /* 41 v26: filter far below cutoff   */
    BlockChorusLoop(n++);           /* 42 v27: chorus loop without LFO   */
    BlockHeadroom(n++);             /* 43 v28: headroom, slide, saturation */
    BlockInterpFilter(n++);         /* 44 v28: interpolation, filter on noise */
    BlockReverbNoise(n++);          /* 45 v28: reverb with noise         */
    BlockChorusNoise(n++);          /* 46 v28: chorus with noise, flanger */

    RecStop();
    if (g_bname[0]) printf("\n");
    printf("\nDone, %lu seconds total.\n", (unsigned long) (g_ms / 1000L));
    LogFinish();
    if (g_log) { fprintf(g_log, "%8lu -- END\n", g_ms); fclose(g_log); }
    RtDone();

    awe32ReleaseAllBanks(&spSound);
    if (pPresets) free(pPresets);
    awe32Terminate();
    return 0;
}
