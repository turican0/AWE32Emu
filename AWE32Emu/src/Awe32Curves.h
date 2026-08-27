#pragma once
#include <cstdint>
#include <algorithm>
#include "Awe32Driver.h"

// ---------------------------------------------------------------------------
// Prevodni krivky MIDI -> utlum, presne podle ovladacu Creative.
//
// Tri po sobe jdouci tabulky po 128 bajtech. Overeno ve TRECH nezavislych
// binarkach:
//   SBAWE32.MDI (Miles/AIL driver)      0x142D, 0x14AD, 0x152D
//   SBAWE32.DRV (45632 B, WINDRV kopie) ds:0592, ds:0692, ds:0612
//   SBAWE.VXD   (86054 B, ten, ktery skutecne bezel pri mereni)
//               kChannelVolumeDb 0x8F10, kExpressionDb 0x8F90,
//               kVelocityDb 0x8E90
//
// Pozor na dve veci:
//  1) V novejsim SBAWE.VXD ma kVelocityDb[0] hodnotu **99**, zatimco MDI
//     i WINDRV maji 50. Zbylych 127 bajtu je ve vsech trech shodnych.
//     Nechavame 50 (dva zdroje ze tri); velocity 0 je v MIDI note-off,
//     takze se hudebne nepouzije. Viz docs/re-notes/86box_srovnani.md 8.
//  2) Verze SBAWE32.DRV, ktera skutecne bezela pri mereni (45008 B), tyhle
//     tabulky **neobsahuje vubec** - presunuly se do VXD. Odkazy na
//     `ds:0592` plati pro kopii ve WINDRV, ne pro tu merenou.
//
// Vzorec pro vysledny utlum (registr IFATN, spodni bajt, jednotka 0.375 dB)
// je prepsany z note-on rutiny v SBAWE32.MDI na offsetu 0x2102:
//
//     if (cc7 <= 10) return 255;                       // ticho
//     atten = ( 8*(volDb[cc7] + velDb[velocity])
//               + ((3*(127 - patchAtten)) & ~7) ) / 3;
//     if (atten >= 255) return 255;
//     if (expression < 127)
//         atten += exprDb[expression] * (255 - atten) / 127;
//
// Vsimni si, ze utlum patche se pricita rovnou v jednotkach registru
// (127 - hodnota), zatimco hlasitost a velocity jsou v dB a prepocitavaji
// se pomerem 8/3 (= 1 / 0.375).
// ---------------------------------------------------------------------------

namespace Awe32Curves
{
    using Awe32::Driver;
    // MDI 0x142D / DRV ds:0592 - expression (CC11)
    inline constexpr uint8_t kExpressionDb[128] = {
        127, 108,  98,  90,  84,  80,  75,  72,  69,  66,  64,  61,  59,  57,  56,  54,
         52,  51,  49,  48,  47,  45,  44,  43,  42,  41,  40,  39,  38,  37,  36,  36,
         35,  34,  33,  33,  32,  31,  30,  30,  29,  29,  28,  27,  27,  26,  26,  25,
         24,  24,  23,  23,  22,  22,  21,  21,  21,  20,  20,  19,  19,  18,  18,  17,
         17,  17,  16,  16,  15,  15,  15,  14,  14,  14,  13,  13,  13,  12,  12,  12,
         11,  11,  11,  10,  10,  10,   9,   9,   9,   9,   8,   8,   8,   7,   7,   7,
          7,   6,   6,   6,   6,   5,   5,   5,   4,   4,   4,   4,   4,   3,   3,   3,
          3,   2,   2,   2,   2,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,
    };

    // MDI 0x14AD / DRV ds:0692 - hlasitost kanalu (CC7)
    inline constexpr uint8_t kChannelVolumeDb[128] = {
         99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  99,  43,  41,  40,  39,  38,
         37,  36,  35,  34,  33,  32,  31,  30,  30,  29,  28,  27,  27,  26,  25,  25,
         24,  23,  23,  22,  22,  21,  21,  20,  20,  19,  19,  19,  18,  18,  17,  17,
         17,  16,  16,  16,  15,  15,  15,  14,  14,  14,  14,  13,  13,  13,  12,  12,
         12,  12,  12,  11,  11,  11,  11,  10,  10,  10,  10,   9,   9,   9,   9,   9,
          8,   8,   8,   8,   8,   7,   7,   7,   7,   6,   6,   6,   6,   6,   5,   5,
          5,   5,   5,   4,   4,   4,   4,   4,   3,   3,   3,   3,   3,   2,   2,   2,
          2,   2,   2,   1,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,
    };

    // MDI 0x152D / DRV ds:0612 - velocity
    inline constexpr uint8_t kVelocityDb[128] = {
         50,  49,  48,  47,  46,  45,  44,  43,  42,  42,  41,  40,  39,  38,  37,  36,
         36,  35,  34,  33,  33,  32,  31,  30,  30,  29,  28,  28,  27,  26,  26,  25,
         25,  24,  24,  23,  22,  22,  21,  21,  20,  20,  19,  19,  19,  18,  18,  17,
         17,  16,  16,  16,  15,  15,  15,  14,  14,  14,  13,  13,  13,  12,  12,  12,
         11,  11,  11,  11,  10,  10,  10,  10,   9,   9,   9,   9,   9,   8,   8,   8,
          8,   8,   7,   7,   7,   7,   7,   6,   6,   6,   6,   6,   6,   5,   5,   5,
          5,   5,   5,   5,   4,   4,   4,   4,   4,   4,   3,   3,   3,   3,   3,   3,
          2,   2,   2,   2,   2,   2,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,
    };

    // Prepis vypoctu z SBAWE32.MDI, offset 0x2102.
    // patchAttenUnits = utlum patche v jednotkach registru (0 = bez utlumu).
    // Velocity v `SBAWE.VXD` ma na indexu 0 hodnotu 99, zatimco `SBAWE32.MDI`
    // i starsi `SBAWE32.DRV` maji 50. Zbylych 127 bajtu je shodnych.
    // Velocity 0 je v MIDI note-off, takze se to hudebne neprojevi, ale
    // pro vernost to drzime oddelene.
    inline int VelocityDb(int velocity, Driver drv)
    {
        velocity = std::clamp(velocity, 0, 127);
        if (velocity == 0 && drv == Driver::Win95) return 99;
        return kVelocityDb[velocity];
    }

    // Vzorec z `SBAWE32.MDI` offset 0x2102 (rodina Dos).
    inline int ComputeAttenuationMdi(int cc7, int velocity, int expression,
                                     int patchAttenUnits)
    {
        const int db = kChannelVolumeDb[cc7] + VelocityDb(velocity, Driver::Dos);
        const int patch = (3 * std::clamp(patchAttenUnits, 0, 255)) & ~7;
        int atten = (8 * db + patch) / 3;
        if (atten >= 255) return 255;

        if (expression < 127)
            atten += kExpressionDb[expression] * (255 - atten) / 127;

        return std::clamp(atten, 0, 255);
    }

    // Vzorec z `SBAWE.VXD` objekt 1, 0x1C54..0x1CC1 (rodina Win95).
    //
    //   soucet = volDb[CC7] + velDb[velocity] + (X + 12) / 24
    //   atten  = globalniUtlum + soucet * 8 / 3          , oriznuto na 255
    //   if (expression < 127)
    //       atten += (256 - atten) * exprDb[expression] / 128
    //
    // Klicove je pole `word[esi+0x60]` = X, tedy **utlum patche v 1/20 dB**.
    // Odectene z instrukcni stopy: pro preset s `initialAttenuation` 107 tam
    // ovladac ma 150, a 150 * 0,05 dB = 7,5 dB = (127-107) * 0,375 dB. Nase
    // `patchAttenUnits` je v jednotkach registru (0,375 dB), prevod je tedy
    // `* 15 / 2`.
    //
    // Proti variante Dos jsou tri rozdily:
    //  1) utlum patche se pricita **do souctu v dB jeste pred prevodem**
    //     (a celociselnym delenim 24 se pritom orizne), ne az za nim
    //     v jednotkach registru pres `(3*p) & ~7`;
    //  2) expression deli 128 misto 127 a vychazi z 256 misto 255;
    //  3) `[edi+0x10]` je jeste jeden, globalni utlum - ve vsech 242 merenych
    //     notach byl 0, takze ho zatim nemodelujeme.
    //
    // Overeno: velocity 109 -> velDb 3, CC7 127 -> volDb 0, X 150.
    //   soucet = 0 + 3 + (150+12)/24 = 9,  atten = 9*8/3 = 24 = 0x18
    // a po pricteni 16 za ROM "1MGM" (viz Synth.cpp) vychazi 0x28, coz je
    // presne to, co ovladac zapsal.
    inline int ComputeAttenuationVxd(int cc7, int velocity, int expression,
                                     int patchAttenUnits)
    {
        const int patchTwentiethsDb = std::clamp(patchAttenUnits, 0, 255) * 15 / 2;
        const int db = kChannelVolumeDb[cc7] + VelocityDb(velocity, Driver::Win95)
                     + (patchTwentiethsDb + 12) / 24;

        int atten = (db * 8) / 3;
        if (atten >= 255) return 255;

        if (expression < 127)
            atten += (256 - atten) * kExpressionDb[expression] / 128;

        return std::clamp(atten, 0, 255);
    }

    inline int ComputeAttenuation(int cc7, int velocity, int expression,
                                  int patchAttenUnits, Driver drv)
    {
        cc7 = std::clamp(cc7, 0, 127);
        velocity = std::clamp(velocity, 0, 127);
        expression = std::clamp(expression, 0, 127);

        // Obe rodiny umlci kanal, kdyz je hlasitost velmi nizka.
        if (cc7 <= 10) return 255;

        return (drv == Driver::Win95)
            ? ComputeAttenuationVxd(cc7, velocity, expression, patchAttenUnits)
            : ComputeAttenuationMdi(cc7, velocity, expression, patchAttenUnits);
    }
    // ---- utlum -> linearni amplituda (cilovy objem hlasu) ----------------
    //
    // `SBAWE.VXD` obj 1, 0x21BF:
    //
    //     mov si, word [edx*2 + 0x409010]   ; tabulka[utlum & 15]
    //     ...
    //     sar eax, 4                        ; utlum >> 4
    //     shr si, cl                        ; mantisa >> (utlum >> 4)
    //
    // Tedy 6 dB na posun a 16 kroku mezi tim, coz dela 0,3763 dB na jednotku.
    // Nasli jsme ji tak, ze se z 844 not Georgie zpetne dopocitaly meze pro
    // kazdou z 16 polozek a v cele binarce vyslo **jedine** misto, ktere je
    // splnuje.
    //
    // Offsety: v souboru je tabulka na **0x8DB0** (overeno hledanim tech
    // sestnacti slov v binarce), staticky disassembler ji ukazuje na
    // 0x409010 a za behu je na 0xC10001BC. Drivejsi komentar tvrdil, ze
    // v souboru je na 0x09010 - to byla linearni adresa vydavana za offset
    // v souboru, objekt LE ale nezacina na zacatku souboru.
    //
    // Pozor: neni to `attentable` z 86Boxu (ta ma krok presne 0,375 dB
    // a zacina na 65535), takze zadny hladky vzorec na to nesedne.
    inline constexpr uint16_t kAttenToAmp16[16] = {
        60096, 57544, 55104, 52768, 50528, 48392, 46336, 44376,
        42488, 40688, 38960, 37312, 35728, 34216, 32768, 31376
    };

    // Patnact slov, ktera v binarce **predchazeji** tabulce (0x8D92..0x8DAE).
    // Ovladac je cte, kdyz je utlum zaporny: index si dela jako `utlum % 16`
    // s utinanim k nule, takze je pak taky zaporny a sahne pred tabulku.
    // Je to konec jine tabulky a posledni tri slova jsou nuly, takze utlum
    // -1 az -3 da ticho.
    inline constexpr uint16_t kAttenBeforeTable[15] = {
        1542, 1286, 1285, 1028, 1028, 772, 771, 515, 514, 258, 257, 257, 0, 0, 0
    };

    // Prepis `SBAWE.VXD` obj 1, 0xC0FFB36B..0xC0FFB398 za behu:
    //
    //     movsx eax, word [ebx+0x26]   ; utlum, **se znamenkem**
    //     cdq / xor / sub / and 0xF / xor / sub   ; index = utlum % 16 k nule
    //     mov si, word [edx*2 + tabulka]
    //     cdq / and edx,0xF / add / sar eax,4     ; posun = utlum / 16 k nule
    //     mov cl, al / shr si, cl
    //
    // Obe deleni utinaji **k nule**, ne dolu - proto `%` a `/` v C++, ne
    // `& 15` a `>> 4`. Pro utlum >= 0 je to totez, pro zaporny ne.
    //
    // Zmereno: na Georgii, JUMPu a RELAXu (14 931 not) je utlum vzdy 16..255
    // a `ComputeAttenuation*` ho stejne orezava na 0..255, takze zaporna
    // vetev dnes nenastane. Je tu kvuli shode s ovladacem, ne kvuli zvuku.
    inline uint16_t VolumeTarget(int attenUnits)
    {
        const int index = attenUnits % 16;
        const int shift = attenUnits / 16;
        const uint16_t base = (index >= 0) ? kAttenToAmp16[index]
                                           : kAttenBeforeTable[15 + index];

        // `shr si, cl` bere jen dolni bajt posunu a procesor pocet maskuje
        // na peti bitu; posun 16 a vys da u 16bitoveho registru nulu.
        const unsigned count =
            static_cast<unsigned>(static_cast<uint8_t>(shift)) & 31u;
        return (count >= 16u) ? uint16_t{0}
                              : static_cast<uint16_t>(base >> count);
    }

}
