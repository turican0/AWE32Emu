/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          CPU and memory state tracer for reverse engineering AWE32 drivers.
 *
 *          NOT PART OF upstream 86Box. Added for the AWE32Emu project.
 *
 *          It can do two things:
 *
 *          1) On every write to the EMU8000 ports, dump the complete CPU
 *             state and memory windows around EBX and EBP. So for every
 *             register write it is visible which values the driver worked
 *             with.
 *
 *          2) Dump every instruction in a given EIP range, including the
 *             registers. The range can be set by hand, or it is **learned**
 *             from the first port write (see AWE32_TRACE_AUTO).
 *
 *          Everything is switched on by environment variables; without them
 *          it costs nothing:
 *
 *            AWE32_TRACE_FILE   output path (without it the tracer is off)
 *            AWE32_TRACE_MEM    how many bytes around EBX/EBP to dump (default 128)
 *            AWE32_TRACE_INSN   1 = dump single instructions too
 *            AWE32_TRACE_LO/HI  linear address range for instructions
 *            AWE32_TRACE_AUTO   size of the window around the EIP of the first
 *                               port write, from which the range is derived
 *                               (e.g. 0x8000); overrides LO/HI
 *            AWE32_TRACE_MAX    line cap, so the trace does not run away
 *
 *          The instruction mode needs a build with -DDYNAREC=OFF, otherwise
 *          most code runs in translated blocks and the hook is missed.
 */
#ifndef EMU_AWE32_TRACE_H
#define EMU_AWE32_TRACE_H

#include <stdint.h>

/* Called from exec386_dynarec_int() before an instruction is executed. */
extern void awe32_trace_insn(uint32_t linear_pc, uint8_t opcode);

/* Called from emu8k_outw()/emu8k_inw() after a register write or read. */
extern void awe32_trace_ioctx(const char *what, uint16_t port, uint32_t val);

/* Non-zero when the tracer is active - so nothing is computed on the hot path. */
extern int awe32_trace_active(void);

/* A note into the trace - used by the MPU-401 hook to show whether the
   MFBEN loopback applies at all. */
extern void awe32_trace_note(const char *fmt, ...);

#endif /*EMU_AWE32_TRACE_H*/
