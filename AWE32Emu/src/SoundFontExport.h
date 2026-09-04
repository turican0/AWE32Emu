#pragma once
#include "SoundFont.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Zapis nactenych bank do jednoho souboru SF2.
//
// K cemu to je: banky, se kterymi tady pracujeme, jsou vetsinou SoundFont 1.0
// (`.SBK`) a casto odkazuji do wave ROM karty. Takovou banku dnesni prehravac
// nepouzije - SBK neumi a ROM nema. Export udela z obojiho jeden samostatny
// soubor.
//
// Nejde ale o prebaleni. SF1 uklada hodnoty **v jinych jednotkach** nez SF2
// (casy v milisekundach, cutoff 0..127, utlum "127 = nic"), takze pouhe
// prepsani generatoru do SF2 obalu by v beznem prehravaci znelo spatne - to
// je znama past, kvuli ktere se prevedene SBK banky musi rucne dolaďovat.
// Tady se prevadi **vyznam**, a to podle prevodu, ktere jsou zmerene proti
// skutecnemu ovladaci Creative (viz komentare v SoundFont.cpp) - ne podle
// odhadu.
//
// Krome jednotek se resi i to, cim se dnesni SF2 stroj chova jinak nez AWE32
// (viz `ExportOptions::awe32Modulators`).
// ---------------------------------------------------------------------------

namespace SoundFont
{
    struct ExportOptions
    {
        // Vzorky lezici ve wave ROM se vypisou do souboru jako obycejna data.
        // Bez toho by banka odkazovala do pameti, kterou dnesni prehravac nema.
        bool bakeRom = true;

        // Prepsat vychozi modulatory SF2 tak, aby stroj reagoval jako AWE32:
        //   - vypnout vychozi vazbu velocity -> mez filtru (AWE32 ji nemá),
        //   - modulacni kolecko a aftertouch poslat na modLFO, ne na vibLFO,
        //   - citlivost CC91/CC93 zvednout na 40 % (SF2 ma vychozich 20 %).
        bool awe32Modulators = true;

        // Jmeno banky zapsane do INFO/INAM.
        std::string name;
    };

    // `banks` v poradi nacteni - pozdejsi prebiji drivejsi, stejne jako pri
    // prehravani. `rom` smi byt prazdna, kdyz zadna banka do ROM neodkazuje.
    bool ExportSf2(const std::vector<const Bank*>& banks,
                   const std::vector<int16_t>& rom,
                   const std::string& path,
                   const ExportOptions& opt,
                   std::string& error);
}
