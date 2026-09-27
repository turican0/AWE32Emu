#pragma once
#include "SoundFont.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Writing the loaded banks into one SF2 file.
//
// What for: the banks we work with are mostly SoundFont 1.0 (`.SBK`) and
// often refer to the card's wave ROM. A present-day player cannot use such a
// bank - it neither reads SBK nor has the ROM. The export makes one
// self-contained file out of both.
//
// It is not a repackaging, though. SF1 stores values **in other units** than
// SF2 (times in milliseconds, cutoff 0..127, attenuation "127 = none"), so
// just copying the generators into an SF2 wrapper would sound wrong in a
// normal player - the well-known trap that makes converted SBK banks need
// manual fixing.
// Here the **meaning** is converted, following conversions measured against
// the real Creative driver (see the comments in SoundFont.cpp) - not
// estimates.
//
// Besides the units, it also handles where a present-day SF2 engine behaves
// differently from the AWE32 (see `ExportOptions::awe32Modulators`).
// ---------------------------------------------------------------------------

namespace SoundFont
{
    struct ExportOptions
    {
        // Samples lying in the wave ROM are written into the file as ordinary
        // data. Without that the bank would refer to memory a present-day
        // player does not have.
        bool bakeRom = true;

        // Override the default SF2 modulators so that the engine reacts like an AWE32:
        //   - drop the default velocity -> filter cutoff link (the AWE32 has none),
        //   - send the mod wheel and aftertouch to the modLFO, not the vibLFO,
        //   - raise the CC91/CC93 sensitivity to 40 % (SF2 defaults to 20 %).
        bool awe32Modulators = true;

        // Bank name written to INFO/INAM.
        std::string name;
    };

    // `banks` in load order - later ones override earlier ones, as in
    // playback. `rom` may be empty when no bank refers to the ROM.
    bool ExportSf2(const std::vector<const Bank*>& banks,
                   const std::vector<int16_t>& rom,
                   const std::string& path,
                   const ExportOptions& opt,
                   std::string& error);
}
