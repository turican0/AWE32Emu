#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

// ---------------------------------------------------------------------------
// Emu8000Box - obal nad **nezmenenym** `snd_emu8k.c` z 86Boxu.
//
// Cil je, aby cip byl v nasem projektu a v 86Boxu doslova tentyz kod. Soubor
// se proto nekopiruje ani neprepisuje - preklada se primo z datoveho adresare
// (`../AWE32EmuData/ref86box/upstream/`), stejne jako to uz dela
// `ref86box/build/emu8k_ref.exe`. Shoda tim neni vysledek peclivosti pri
// opisovani, ale konstrukce.
//
// Casovani. 86Box zene cip po blocich `WTBUFLEN` = 980 snimku: nejdriv se
// aplikuji vsechny zapisy, kazdy na svem offsetu v bloku, pak se jednim
// volanim `emu8k_update()` doreje cely blok. Nas sekvencer naproti tomu
// renderuje po jednom snimku. Krokovat cip po vzorcich nejde - efekty se
// pousti jen kdyz `num_active > 0`, coz se vyhodnocuje jednou za volani, takze
// by se chorus rozesel (viz ref86box/README.md).
//
// Reseni je **zpozdeni o jeden blok**: zapisy se sbiraji do fronty a zvuk se
// vydava az z minuleho, uz hotoveho bloku. Latence je presne 980 snimku
// (kLatencyFrames) a je konstantni - staci ji na vystupu odriznout.
// ---------------------------------------------------------------------------
class Emu8000Box
{
public:
    static constexpr int kBlockFrames   = 980;   // WTBUFLEN = 44100/45
    static constexpr int kLatencyFrames = kBlockFrames;

    Emu8000Box();
    ~Emu8000Box();
    Emu8000Box(const Emu8000Box&) = delete;
    Emu8000Box& operator=(const Emu8000Box&) = delete;

    // romPath musi ukazovat na 1 MB surovy dump wave ROM (awe32.raw) - cip si
    // ho nacte sam pres rom_fopen, presne jako v 86Boxu.
    bool Init(const std::string& romPath, uint16_t basePort, int ramKb, std::string& err);
    bool Ready() const { return m_ready; }

    // Zvukova DRAM cipu. Vzorky banky se do ni nahravaji memcpy, stejne jako
    // u naseho jadra - viz Synth::LoadBank.
    int16_t* Ram();
    size_t   RamWords() const;

    void PortWrite(uint16_t port, uint16_t value, bool isByte = false);

    // Jeden snimek na 44100 Hz. Prvnich kLatencyFrames volani vrati ticho.
    void RenderFrame(int32_t& l, int32_t& r);

private:
    void FlushBlock();

    struct Impl;
    Impl* m_impl = nullptr;
    bool  m_ready = false;
};
