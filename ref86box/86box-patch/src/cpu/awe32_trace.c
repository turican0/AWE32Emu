/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          CPU and memory state tracer for reverse engineering AWE32 drivers.
 *          NOT PART OF upstream 86Box. See 86box/awe32_trace.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/awe32_trace.h>

#include "cpu.h"
#include "x86.h"

static FILE    *tf          = NULL;
static int      checked     = 0;
static int      trace_insn  = 0;
static uint32_t range_lo    = 0;
static uint32_t range_hi    = 0;
static uint32_t auto_window = 0;
static int      mem_bytes   = 128;
/* AWE32Emu: a window on the driver's channel structures. They lie at
   `edi + ch*0x24`; of interest are `+0x44f` (pitch bend range), `+0x450`
   (tuning) and `+0x456` (computed offset). Dumped **only on IP writes**,
   otherwise the trace would grow by two orders of magnitude. */
static uint32_t ch_off     = 0;
static int      ch_len     = 0;
static uint16_t cur_ptr    = 0;
/* AWE32Emu: a one-off dump of the driver code from the guest's memory. A
   static disassembler does not apply the LE file fixups, so call targets come
   out shifted. In memory the code is already **loaded and linked**, so it is
   enough to take it from there as it is. The base is derived from the EIP of
   the first port access - that is always in the driver's I/O routine. */
static uint32_t code_back  = 0;
static int      code_len   = 0;
static uint32_t code_min   = 0;
static int      code_done  = 0;
/* AWE32Emu: the instruction trace can be armed only after the first note.
   The generator conversion runs **before** the note's port writes, so it
   cannot be caught by an address range - the VxD loads elsewhere on every
   boot. So it is armed at the first note-on and catches the processing of
   the next note. */
static int      insn_after = 0;
static int      insn_armed = 1;
static uint64_t max_lines   = 20000000ULL;
static uint64_t lines       = 0;

static uint32_t
env_u32(const char *name, uint32_t def)
{
    const char *s = getenv(name);
    return (s && *s) ? (uint32_t) strtoul(s, NULL, 0) : def;
}

static void
trace_open(void)
{
    const char *path;

    checked = 1;

    path = getenv("AWE32_TRACE_FILE");
    if ((path == NULL) || (path[0] == '\0'))
        return;

    tf = fopen(path, "wb");
    if (tf == NULL)
        return;

    trace_insn  = (int) env_u32("AWE32_TRACE_INSN", 0);
    range_lo    = env_u32("AWE32_TRACE_LO", 0);
    range_hi    = env_u32("AWE32_TRACE_HI", 0);
    auto_window = env_u32("AWE32_TRACE_AUTO", 0);
    mem_bytes   = (int) env_u32("AWE32_TRACE_MEM", 128);
    ch_off      = env_u32("AWE32_TRACE_CH_OFF", 0);
    ch_len      = (int) env_u32("AWE32_TRACE_CH_LEN", 0);
    code_back   = env_u32("AWE32_TRACE_CODE_BACK", 0x1000);
    code_len    = (int) env_u32("AWE32_TRACE_CODE_LEN", 0);
    /* The first port access is still made by 16-bit code during boot; the
       Windows driver lies high, so it waits for an EIP above this limit. */
    code_min    = env_u32("AWE32_TRACE_CODE_MIN", 0xC0000000);
    insn_after  = (int) env_u32("AWE32_TRACE_INSN_AFTER_NOTE", 0);
    if (insn_after)
        insn_armed = 0;
    max_lines   = env_u32("AWE32_TRACE_MAX", 20000000);

    fprintf(tf, "# AWE32 CPU trace\n");
    fprintf(tf, "# I <linearni EIP> <opcode> <EAX> <EBX> <ECX> <EDX> <ESI> <EDI> <EBP> <ESP>\n");
    fprintf(tf, "# P <what> <port> <value> <EIP> <EAX> <EBX> <ECX> <EDX> <ESI> <EDI> <EBP> <ESP>\n");
    fprintf(tf, "# M <adresa> <bajty hex>\n");
    fprintf(tf, "# insn=%d lo=%08X hi=%08X auto=%X mem=%d ch_off=%X ch_len=%d\n",
            trace_insn, range_lo, range_hi, auto_window, mem_bytes, ch_off, ch_len);
    fflush(tf);
}

/* A note into the trace. Used by the MPU-401 hook - the trace is the only
   channel through which we learn anything from the running VM. */
void
awe32_trace_note(const char *fmt, ...)
{
    va_list ap;
    if (!checked)
        trace_open();
    if (tf == NULL)
        return;
    fputs("# ", tf);
    va_start(ap, fmt);
    vfprintf(tf, fmt, ap);
    va_end(ap);
    fputc('\n', tf);
    fflush(tf);
}

int
awe32_trace_active(void)
{
    if (!checked)
        trace_open();
    return (tf != NULL) && trace_insn && insn_armed && (lines < max_lines);
}

static void
dump_regs(void)
{
    fprintf(tf, " %08X %08X %08X %08X %08X %08X %08X %08X",
            EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP);
}

/* Dumps a memory window; the addresses are linear, reads go through the
   host MMU. */
static void
dump_mem(const char *label, uint32_t addr, int len)
{
    if ((tf == NULL) || (len <= 0) || (addr == 0))
        return;

    fprintf(tf, "M %s %08X", label, addr);
    for (int i = 0; i < len; i += 4) {
        const uint32_t v = readmemll(addr + i);
        if (cpu_state.abrt) {
            cpu_state.abrt = 0;
            fprintf(tf, " ????????");
        } else
            fprintf(tf, " %08X", v);
    }
    fputc('\n', tf);
}

void
awe32_trace_insn(uint32_t linear_pc, uint8_t opcode)
{
    if (!checked)
        trace_open();
    if ((tf == NULL) || !trace_insn || (lines >= max_lines))
        return;
    if ((range_hi != 0) && ((linear_pc < range_lo) || (linear_pc > range_hi)))
        return;

    fprintf(tf, "I %08X %02X", linear_pc, opcode);
    dump_regs();
    fputc('\n', tf);
    lines++;
}

void
awe32_trace_ioctx(const char *what, uint16_t port, uint32_t val)
{
    if (!checked)
        trace_open();
    if (tf == NULL)
        return;

    const uint32_t pc = cs + cpu_state.pc;

    /* The first port access tells where the driver lies in memory; from it
       the range for the instruction trace can be derived, which is otherwise
       unknown. */
    if (auto_window && (range_hi == 0)) {
        range_lo = (pc > auto_window) ? (pc - auto_window) : 0;
        range_hi = pc + auto_window;
        fprintf(tf, "# auto range from the first access: %08X..%08X\n",
                range_lo, range_hi);
    }

    if ((port & 0xF02) == 0xE02)
        cur_ptr = (uint16_t) val;

    /* The code is dumped only at **the start of a note** (a DCYSUSV write
       without the release and off bits). The first port access is made by
       another module and the VxD loads elsewhere on every boot, so it cannot
       be used. */
    if (code_len && !code_done && (pc >= code_min)
        && ((port & 0xF02) == 0xA00) && (((cur_ptr >> 5) & 7) == 5)
        && !(val & 0x8000) && !(val & 0x0080)) {
        const uint32_t base = (pc > code_back) ? (pc - code_back) : 0;
        code_done = 1;
        fprintf(tf, "# driver code from memory: %08X..%08X (EIP %08X)\n",
                base, base + code_len, pc);
        dump_mem("code", base, code_len);
    }

    if (insn_after && !insn_armed && ((port & 0xF02) == 0xA00)
        && (((cur_ptr >> 5) & 7) == 5)
        && !(val & 0x8000) && !(val & 0x0080)) {
        insn_armed = 1;
        fprintf(tf, "# instrukcni stopa odjistena u note-onu, EIP %08X\n", pc);
    }

    fprintf(tf, "P %s %03X %08X %08X", what, port, val, pc);
    dump_regs();
    fputc('\n', tf);

    dump_mem("ebx", EBX, mem_bytes);
    dump_mem("ebp", (EBP > 0x80) ? (EBP - 0x80) : EBP, mem_bytes);

    /* AWE32Emu: channel structures only on IP writes (Data3, register 0), so
       the trace stays usable. The register pointer is tracked by us - the
       tracer sees the writes to 0xE22 too. */

    if (ch_len && ((port & 0xF02) == 0xE00) && (((cur_ptr >> 5) & 7) == 0))
        dump_mem("chan", EDI + ch_off, ch_len);
    fflush(tf);
    lines++;
}
