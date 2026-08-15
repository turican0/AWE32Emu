#pragma once
#include "MidiTypes.h"
#include <string>

// Parser XMI (Miles Sound System / AIL) souboru.
// XMI kontejner je IFF FORM/CAT struktura obsahujici EVNT chunk s udalostmi.
// Zakladni rozdily oproti SMF, ktere tento parser resi:
//  - Note On v XMI obsahuje primo delku noty (misto samostatneho Note Off) -
//    prevadi se na dvojici NoteOn + NoteOff se spocitanym absoluteTick
//  - Delta-time kodovani je jine nez SMF VLQ (viz .cpp)
//  - Vychozi tempo pro XMI je 120 BPM / 60 ticku na ctvrtovou notu, pokud
//    soubor neobsahuje vlastni tempo meta udalost
// TODO (viz projektovy TODO seznam, sekce 1.2): RBRN (branch/loop body) neni
// zatim zpracovan, jen zaznamenan pro budouci pouziti.
namespace XmiFile
{
    ParsedSequence Load(const std::string& path);
}
