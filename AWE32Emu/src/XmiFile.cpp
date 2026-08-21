#include "XmiFile.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace
{
    uint32_t ReadBE32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

    // XMI interval (delta-time i delka noty): bajty 0x00-0x7E ukoncuji soucet,
    // bajt 0x7F znamena "pricti 127 a pokracuj dalsim bajtem". Prvni bajt >= 0x80
    // uz neni soucasti intervalu (interval muze byt i nulovy - 0 bajtu).
    uint32_t ReadXmiInterval(const std::vector<uint8_t>& data, size_t& pos)
    {
        uint32_t value = 0;
        while (pos < data.size() && data[pos] < 0x80)
        {
            uint8_t b = data[pos];
            value += b;
            pos++;
            if (b != 0x7F)
                break;
        }
        return value;
    }

    // Standardni SMF-style VLQ - XMI meta/sysex eventy (na rozdil od delta-time
    // a note-duration) pouzivaji stejne kodovani delky jako SMF.
    uint32_t ReadSmfVLQ(const std::vector<uint8_t>& data, size_t& pos)
    {
        uint32_t value = 0;
        for (int i = 0; i < 4 && pos < data.size(); ++i)
        {
            uint8_t b = data[pos++];
            value = (value << 7) | (b & 0x7F);
            if ((b & 0x80) == 0)
                break;
        }
        return value;
    }

    // Najde prvni vyskyt 4-bajtoveho IFF tagu v bufferu a vrati offset TESNE ZA tagem
    // (tj. na zacatek 4-bajtove BE delky, ktera v IFF vzdy nasleduje). -1 pokud nenalezen.
    long FindChunk(const std::vector<uint8_t>& buf, const char* tag, size_t searchFrom = 0)
    {
        if (buf.size() < 4) return -1;
        for (size_t i = searchFrom; i + 4 <= buf.size(); ++i)
        {
            if (std::memcmp(&buf[i], tag, 4) == 0)
                return static_cast<long>(i + 4);
        }
        return -1;
    }
}

namespace XmiFile
{
    ParsedSequence Load(const std::string& path)
    {
        ParsedSequence seq;

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            seq.errorMessage = "Nelze otevrit soubor: " + path;
            return seq;
        }

        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (buffer.size() < 12 || std::memcmp(buffer.data(), "FORM", 4) != 0)
        {
            seq.errorMessage = "Chybi FORM hlavicka - nejde o platny XMI/IFF soubor";
            return seq;
        }

        // Zjednoduseni pro zakladni verzi (viz TODO v hlavicce): bereme prvni EVNT chunk
        // v souboru. Vicero-song XMI (CAT XMI s vice FORM XMID) bude potreba doresit
        // pozdeji - RBRN (loop body) mezitim jen preskocime.
        long evntLenPos = FindChunk(buffer, "EVNT");
        if (evntLenPos < 0)
        {
            seq.errorMessage = "Nenalezen EVNT chunk - soubor neni platne XMI (nebo je nepodporovana varianta)";
            return seq;
        }

        size_t pos = static_cast<size_t>(evntLenPos);
        if (pos + 4 > buffer.size())
        {
            seq.errorMessage = "Poskozeny EVNT chunk (chybi delka)";
            return seq;
        }
        uint32_t evntLen = ReadBE32(&buffer[pos]);
        pos += 4;
        size_t evntEnd = pos + evntLen;
        if (evntEnd > buffer.size())
            evntEnd = buffer.size(); // tolerantni k mirne poskozenym/orezanym souborum

        std::vector<uint8_t> data(buffer.begin() + pos, buffer.begin() + evntEnd);

        // XMI ma pevny fixni "clock": 60 ticku na ctvrtovou notu, vychozi tempo 120 BPM,
        // pokud neni v datech prepsano meta udalosti 0x51.
        seq.ticksPerQuarterNote = 60;

        std::vector<MidiEvent> events;
        MidiEvent initialTempo;
        initialTempo.absoluteTick = 0;
        initialTempo.type = MidiEventType::TempoChange;
        initialTempo.tempoUsPerQuarter = 500000; // 120 BPM
        events.push_back(initialTempo);

        size_t p = 0;
        uint32_t absoluteTick = 0;

        while (p < data.size())
        {
            absoluteTick += ReadXmiInterval(data, p);

            if (p >= data.size()) break;
            uint8_t status = data[p];
            if (status < 0x80)
            {
                // Neocekavany bajt tam, kde ma byt status - stream je desynchronizovany
                // (TODO: podpora running status, viz hlavicka .h). Radeji ukoncit cistě.
                break;
            }
            p++;

            uint8_t hiNibble = status & 0xF0;
            uint8_t channel = status & 0x0F;

            if (status == 0xFF)
            {
                if (p >= data.size()) break;
                uint8_t metaType = data[p++];
                uint32_t len = ReadSmfVLQ(data, p);

                // Tempo meta se v XMI ZAMERNE ignoruje. XMI bezi na pevnem
                // hodinovem taktu 120 Hz - hodnota v FF 51 slouzi jen pri
                // konverzi do SMF k dopoctu PPQN, ne k prehravani. Overeno na
                // 000_C2GAME1_w.xmi: 52755 ticku / 120 Hz = 439.6 s, coz sedi
                // na referencni nahravku (441.9 s), zatimco s tempem 560748
                // by vyslo 493 s.
                if (metaType == 0x2F)
                {
                    MidiEvent ev;
                    ev.absoluteTick = absoluteTick;
                    ev.type = MidiEventType::EndOfTrack;
                    events.push_back(ev);
                }
                p += len;
            }
            else if (status == 0xF0 || status == 0xF7)
            {
                uint32_t len = ReadSmfVLQ(data, p);
                p += len;
            }
            else if (hiNibble == 0x90)
            {
                // XMI Note On nese navic delku noty (interval encoding) - z toho
                // odvodime explicitni Note Off, ktery SMF/Sequencer ocekava.
                if (p + 2 > data.size()) break;
                uint8_t note = data[p++];
                uint8_t velocity = data[p++];
                // POZOR: delka noty NENI kodovana jako delta-time interval, ale
                // jako standardni SMF VLQ (pokracovaci bit 0x80). Zamena obou
                // kodovani rozhodi cely stream - parser pak skoncil po par
                // stovkach udalosti misto nekolika tisic.
                uint32_t duration = ReadSmfVLQ(data, p);

                MidiEvent onEv;
                onEv.absoluteTick = absoluteTick;
                onEv.channel = channel;
                onEv.data1 = note;
                onEv.data2 = velocity;
                onEv.type = MidiEventType::NoteOn;
                events.push_back(onEv);

                MidiEvent offEv;
                offEv.absoluteTick = absoluteTick + duration;
                offEv.channel = channel;
                offEv.data1 = note;
                offEv.data2 = 0;
                offEv.type = MidiEventType::NoteOff;
                events.push_back(offEv);
            }
            else if (hiNibble == 0x80 || hiNibble == 0xA0 || hiNibble == 0xB0 || hiNibble == 0xE0)
            {
                if (p + 2 > data.size()) break;
                uint8_t d1 = data[p++];
                uint8_t d2 = data[p++];

                MidiEvent ev;
                ev.absoluteTick = absoluteTick;
                ev.channel = channel;
                ev.data1 = d1;
                ev.data2 = d2;
                switch (hiNibble)
                {
                case 0x80: ev.type = MidiEventType::NoteOff; break;
                case 0xA0: ev.type = MidiEventType::PolyPressure; break;
                case 0xB0: ev.type = MidiEventType::ControlChange; break;
                case 0xE0: ev.type = MidiEventType::PitchBend; break;
                }
                events.push_back(ev);
            }
            else if (hiNibble == 0xC0 || hiNibble == 0xD0)
            {
                if (p + 1 > data.size()) break;
                uint8_t d1 = data[p++];

                MidiEvent ev;
                ev.absoluteTick = absoluteTick;
                ev.channel = channel;
                ev.data1 = d1;
                ev.type = (hiNibble == 0xC0) ? MidiEventType::ProgramChange : MidiEventType::ChannelPressure;
                events.push_back(ev);
            }
            else
            {
                break; // neznamy status - ukoncit parsovani
            }
        }

        // Note Off odvozene z delky noty muze v case predbihat pozdeji nactene eventy -
        // stabilni razeni podle absoluteTick to srovna do spravneho poradi pro Sequencer.
        std::stable_sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.absoluteTick < b.absoluteTick; });

        seq.events = std::move(events);
        seq.valid = true;
        return seq;
    }
}
