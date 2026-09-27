/* Prazdna varianta stopovacich hacku z 86Boxu.
 *
 * `snd_emu8k.c` v nasem stromu 86Boxu ma dopsane volani `emu8k_trace_*`,
 * kterymi se z bezici VM dostavaji stopy ven. Nas render je nepotrebuje -
 * stopu si delá sam o patro vys, ve `Synth` - ale prekladame **tentyz
 * soubor**, aby byl cip v obou projektech doslova stejny kod. Proto tady
 * jsou ta volani jako prazdne funkce, ktere prekladac zahodi.
 *
 * Kdyby se sem nekdy dopsalo skutecne telo, plati jedno: nesmi sahat na
 * stav cipu. Jakmile by stopovani zvuk ovlivnilo, prestane platit, ze
 * nas render a 86Box pocitaji totez.
 */
#ifndef SND_EMU8K_TRACE_H
#define SND_EMU8K_TRACE_H

#include <stdint.h>

static inline void
emu8k_trace_read(uint16_t addr, uint16_t val)
{
    (void) addr;
    (void) val;
}

static inline void
emu8k_trace_write(uint16_t addr, uint16_t val)
{
    (void) addr;
    (void) val;
}

static inline void
emu8k_trace_advance(int num_samples)
{
    (void) num_samples;
}

static inline void
emu8k_trace_wav(const void *buf, int num_samples)
{
    (void) buf;
    (void) num_samples;
}

#endif /* SND_EMU8K_TRACE_H */
