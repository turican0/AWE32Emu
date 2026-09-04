#include "Emu8000Box.h"

#ifndef AWE32EMU_WITH_86BOX

// ---------------------------------------------------------------------------
// Zaslepka pro sestaveni bez zdrojaku 86Boxu.
//
// `snd_emu8k.c` lezi v datovem adresari (`AWE32EmuData`), ktery neni soucasti
// tohohle repozitare - je moc velky a je v nem i obsah, ktery se sirit nesmi.
// Bez nej se `--chip 86box` proste nenabizi; vsechno ostatni, vcetne naseho
// vlastniho jadra, funguje beze zmeny.
//
// Zapnout jde pres CMake: `-DAWE32EMU_WITH_86BOX=ON -DAWE32EMU_DATA=<cesta>`.
// ---------------------------------------------------------------------------

Emu8000Box::Emu8000Box() = default;
Emu8000Box::~Emu8000Box() = default;

bool Emu8000Box::Init(const std::string&, uint16_t, int, std::string& err)
{
    err = "sestaveno bez jadra z 86Boxu (AWE32EMU_WITH_86BOX=OFF)";
    return false;
}

int16_t* Emu8000Box::Ram()             { return nullptr; }
size_t   Emu8000Box::RamWords() const  { return 0; }
void     Emu8000Box::PortWrite(uint16_t, uint16_t, bool) {}
void     Emu8000Box::RenderFrame(int32_t& l, int32_t& r) { l = 0; r = 0; }
void     Emu8000Box::FlushBlock()      {}

#else


#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <vector>

// snd_emu8k.c je cecko a preklada se jako cecko; hlavicky i prototypy tedy
// musi mit C linkage.
extern "C" {
#include <86box/86box.h>
#include <86box/sound.h>
#include <86box/snd_emu8k.h>

// Tyhle tri nejsou v hlavicce, jen v snd_emu8k.c (stejne to resi harness.c).
void     emu8k_outw(uint16_t addr, uint16_t val, void* priv);
void     emu8k_outb(uint16_t addr, uint8_t val, void* priv);
uint16_t emu8k_inw(uint16_t addr, void* priv);
}

// ---------------------------------------------------------------------------
// Globaly a sluzby, ktere snd_emu8k.c ocekava od 86Boxu. Jsou to tytez stuby
// jako v ref86box/harness.c - kdyz se jednou vyplati, at jsou stejne.
// ---------------------------------------------------------------------------
namespace
{
    const char* g_romPath = nullptr;
}

extern "C" {

int music_pos_global     = 0;
int wavetable_pos_global = 0;

void pclog_ex(const char* fmt, va_list ap)
{
    (void) fmt;
    (void) ap;
}

void pclog(const char* fmt, ...)
{
    (void) fmt;
}

void fatal(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::exit(1);
}

FILE* rom_fopen(const char* fn, char* mode)
{
    (void) fn;
    return g_romPath ? std::fopen(g_romPath, mode) : nullptr;
}

void io_sethandler(uint16_t base, uint16_t size,
                   uint8_t (*inb)(uint16_t, void*), uint16_t (*inw)(uint16_t, void*),
                   uint32_t (*inl)(uint16_t, void*), void (*outb)(uint16_t, uint8_t, void*),
                   void (*outw)(uint16_t, uint16_t, void*), void (*outl)(uint16_t, uint32_t, void*),
                   void* priv)
{
    (void) base; (void) size; (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
}

void io_removehandler(uint16_t base, uint16_t size,
                      uint8_t (*inb)(uint16_t, void*), uint16_t (*inw)(uint16_t, void*),
                      uint32_t (*inl)(uint16_t, void*), void (*outb)(uint16_t, uint8_t, void*),
                      void (*outw)(uint16_t, uint16_t, void*), void (*outl)(uint16_t, uint32_t, void*),
                      void* priv)
{
    (void) base; (void) size; (void) inb; (void) inw; (void) inl;
    (void) outb; (void) outw; (void) outl; (void) priv;
}

} // extern "C"

// ---------------------------------------------------------------------------

struct Emu8000Box::Impl
{
    struct Write
    {
        int      offset;
        uint16_t port;
        uint16_t value;
        bool     isByte;
    };

    emu8k_t              emu{};
    std::vector<Write>   pending;
    std::vector<int32_t> fifo;          // prolozene L/R z minuleho bloku
    size_t               fifoPos = 0;
    int                  frameInBlock = 0;
    bool                 haveBlock = false;
    std::string          romPathStorage;
};

Emu8000Box::Emu8000Box()
    : m_impl(new Impl())
{
}

Emu8000Box::~Emu8000Box()
{
    if (m_impl)
    {
        if (m_ready)
            emu8k_close(&m_impl->emu);
        delete m_impl;
    }
}

bool Emu8000Box::Init(const std::string& romPath, uint16_t basePort, int ramKb, std::string& err)
{
    if (m_ready)
        return true;

    if (romPath.empty())
    {
        err = "cip 86box potrebuje wave ROM (--rom)";
        return false;
    }

    // rom_fopen si cestu vezme odsud; drzi se v Implu, aby prezila volani.
    m_impl->romPathStorage = romPath;
    g_romPath = m_impl->romPathStorage.c_str();

    if (FILE* f = std::fopen(g_romPath, "rb"))
    {
        std::fseek(f, 0, SEEK_END);
        const long bytes = std::ftell(f);
        std::fclose(f);
        if (bytes < 1048576)
        {
            err = "wave ROM '" + romPath + "' ma jen "
                + std::to_string(bytes) + " B, cip 86box vyzaduje 1 MB";
            return false;
        }
    }
    else
    {
        err = "wave ROM '" + romPath + "' nejde otevrit";
        return false;
    }

    std::memset(&m_impl->emu, 0, sizeof(m_impl->emu));
    emu8k_init(&m_impl->emu, basePort, ramKb);

    m_impl->fifo.clear();
    m_impl->pending.clear();
    m_impl->fifoPos = 0;
    m_impl->frameInBlock = 0;
    m_impl->haveBlock = false;
    m_ready = true;
    return true;
}

int16_t* Emu8000Box::Ram()
{
    return m_ready ? reinterpret_cast<int16_t*>(m_impl->emu.ram) : nullptr;
}

size_t Emu8000Box::RamWords() const
{
    if (!m_ready || !m_impl->emu.ram)
        return 0;
    return static_cast<size_t>(m_impl->emu.ram_end_addr - EMU8K_RAM_MEM_START);
}

void Emu8000Box::PortWrite(uint16_t port, uint16_t value, bool isByte)
{
    if (!m_ready)
        return;
    m_impl->pending.push_back({ m_impl->frameInBlock, port, value, isByte });
}

void Emu8000Box::FlushBlock()
{
    Impl& I = *m_impl;

    for (const Impl::Write& w : I.pending)
    {
        wavetable_pos_global = w.offset;
        if (w.isByte)
            emu8k_outb(w.port, static_cast<uint8_t>(w.value), &I.emu);
        else
            emu8k_outw(w.port, w.value, &I.emu);
    }
    I.pending.clear();

    wavetable_pos_global = kBlockFrames;
    emu8k_update(&I.emu);

    I.fifo.assign(I.emu.buffer, I.emu.buffer + kBlockFrames * 2);
    I.fifoPos   = 0;
    I.haveBlock = true;

    emu8k_reset_buffer(&I.emu);
}

void Emu8000Box::RenderFrame(int32_t& l, int32_t& r)
{
    if (!m_ready)
    {
        l = 0;
        r = 0;
        return;
    }

    Impl& I = *m_impl;

    if (I.haveBlock && I.fifoPos + 1 < I.fifo.size())
    {
        l = I.fifo[I.fifoPos++];
        r = I.fifo[I.fifoPos++];
    }
    else
    {
        l = 0;
        r = 0;
    }

    if (++I.frameInBlock == kBlockFrames)
    {
        FlushBlock();
        I.frameInBlock = 0;
    }
}

#endif  // AWE32EMU_WITH_86BOX
