/* Prazdna varianta stopy instrukci z 86Boxu - viz snd_emu8k_trace.h.
 *
 * Ve stromu 86Boxu je `awe32_trace.c` sonda, ktera k zapisum do registru
 * pripisuje kontext z CPU (odkud ovladac zapisuje). Bez bezici VM nema co
 * hlasit, takze v nasem renderu nedela nic.
 */
#ifndef AWE32_TRACE_H
#define AWE32_TRACE_H

#include <stdint.h>

static inline void
awe32_trace_ioctx(const char *dir, uint16_t addr, uint16_t val)
{
    (void) dir;
    (void) addr;
    (void) val;
}

#endif /* AWE32_TRACE_H */
