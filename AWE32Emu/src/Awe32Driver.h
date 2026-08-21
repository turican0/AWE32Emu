#pragma once
#include <cstring>

// ---------------------------------------------------------------------------
// Varianta ovladace Creative, kterou emulujeme.
//
// Pri srovnavani se skutecnymi ovladaci bezicimi v 86Boxu se ukazalo, ze
// Creative ma dve rodiny ovladacu, ktere se v nekolika bodech **zamerne**
// lisi. Neexistuje tedy jedna spravna odpoved a nema smysl jednu variantu
// prepisovat druhou - proto jsou v kodu obe a prepinaji se prepinacem
// `--driver`.
//
// Vsechny odchylky jsou zmerene, ne odhadnute; podrobnosti a odkazy na
// konkretni offsety v disassembly jsou v docs/re-notes/86box_srovnani.md.
//
//   Dos    - `AWEUTIL.COM` (inicializace) + `SBAWE32.MDI` (Miles/AIL, note-on)
//   Win95  - `SBAWE.VXD` 86054 B, ta binarka, proti ktere je overeno
//            vsech 24 registru pri note-on na 242 notach (sekce 11)
//
// Obe varianty jsou dnes overene na **vsech registrech**: `win95` proti
// `SBAWE.VXD` (242 not, MINUET) a `dos` proti `SBAWE32.MDI` (255 not,
// Magic Carpet 2). Rozvrzeni bloku parametru vrstvy maji oba ovladace
// shodne - je to prime pole generatoru SoundFontu (sekce 16.2).
//
// Body, ve kterych se lisi:
//
// | vec | Dos | Win95 | kde |
// |---|---|---|---|
// | 8 hodnot v INIT3/INIT4 | AWEUTIL | ALSA == VXD | Awe32InitArrays.h, sekce 7.1 |
// | `kVelocityDb[0]` | 50 | 99 | Awe32Curves.h, sekce 8.1 |
// | vzorec utlumu | MDI `0x2102` | VXD `0x1C54` | Awe32Curves.h, sekce 10.2 |
// | utlum +16 pro ROM "1MGM" | ne | ano | Synth.cpp, sekce 10.2 |
// | posun adresy v CCCA | -46 | -4 | StartAddressOffset(), sekce 16.4 |
// | vychozi `initialAttenuation` | 110 | 127 | SoundFont.cpp, sekce 16.3 |
// | spodni mez panu | `< 0` | `<= 1` | Synth.cpp, sekce 16.4 |
//
// Pozor na jednu nejistotu: ze u `AWEUTIL.COM` plati tabulky a vzorec
// utlumu z `SBAWE32.MDI`, **nevime** - jeho MIDI engine (`/EM:GM`) jsme
// nikdy netrasovali. Seskupeni do rodiny "Dos" vychazi z toho, ze se v DOSu
// pouzivaji spolu, a z toho, ze `SBAWE32.MDI` a starsi `SBAWE32.DRV` maji
// tabulky bajt po bajtu shodne.
// ---------------------------------------------------------------------------

namespace Awe32
{
    enum class Driver
    {
        Dos,     // AWEUTIL.COM + SBAWE32.MDI
        Win95,   // SBAWE.VXD
    };

    // Vychozi je Win95 - proti nemu je overena cela note-on cesta.
    inline constexpr Driver kDefaultDriver = Driver::Win95;

    // O kolik slov pred zacatkem vzorku ovladac spusti prehravani (CCCA).
    // Obe rodiny maji uplne stejny rozvrzeny blok parametru vrstvy - start
    // je v obou na offsetu 0x76 - ale odecitaji jinou konstantu:
    //
    //   SBAWE32.MDI 0x1FF4:  ax:dx = [si+0x76];  sub ax, 0x2e   (46)
    //   SBAWE.VXD   0x1ECF:  eax   = [ebx+0x76]; sub eax, 4
    //
    // Neni to preklep ani nase chyba mereni: proti MDI vychazi rozdil
    // presne 42 slov u vsech not, proti VXD sedi CCCA na 242 notach.
    inline constexpr int StartAddressOffset(Driver d)
    {
        return (d == Driver::Dos) ? 46 : 4;
    }

    inline const char* DriverName(Driver d)
    {
        return (d == Driver::Dos) ? "dos" : "win95";
    }

    // Vrati false, kdyz jmeno nesedi na zadnou variantu.
    inline bool DriverFromName(const char* name, Driver& out)
    {
        if (name == nullptr) return false;
        if (std::strcmp(name, "dos") == 0)   { out = Driver::Dos;   return true; }
        if (name && std::strcmp(name, "win95") == 0) { out = Driver::Win95; return true; }
        return false;
    }
}
