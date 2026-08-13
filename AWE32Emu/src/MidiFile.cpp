#include "MidiFile.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace
{
    uint32_t ReadBE32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
    uint16_t ReadBE16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

    // Variable-length quantity dle SMF specifikace (7 bitu na byte, MSB = "pokracuje")
    uint32_t ReadVLQ(const std::vector<uint8_t>& data, size_t& pos)
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

    struct TrackParseResult
    {
        std::vector<MidiEvent> events;
        bool ok = false;
    };

    TrackParseResult ParseTrack(const std::vector<uint8_t>& data)
    {
        TrackParseResult result;
        size_t pos = 0;
        uint32_t absoluteTick = 0;
        uint8_t runningStatus = 0;

        while (pos < data.size())
        {
            uint32_t delta = ReadVLQ(data, pos);
            absoluteTick += delta;

            if (pos >= data.size())
                break;

            uint8_t statusByte = data[pos];

            if (statusByte < 0x80)
            {
                // Running status - opakuje se predchozi status byte, tohle je uz data1
                statusByte = runningStatus;
            }
            else
            {
                pos++;
                runningStatus = statusByte;
            }

            uint8_t hiNibble = statusByte & 0xF0;
            uint8_t channel = statusByte & 0x0F;

            if (statusByte == 0xFF)
            {
                // Meta event
                if (pos >= data.size()) break;
                uint8_t metaType = data[pos++];
                uint32_t len = ReadVLQ(data, pos);

                if (metaType == 0x51 && len == 3 && pos + 3 <= data.size())
                {
                    uint32_t usPerQuarter = (data[pos] << 16) | (data[pos + 1] << 8) | data[pos + 2];
                    MidiEvent ev;
                    ev.absoluteTick = absoluteTick;
                    ev.type = MidiEventType::TempoChange;
                    ev.tempoUsPerQuarter = usPerQuarter;
                    result.events.push_back(ev);
                }
                else if (metaType == 0x2F)
                {
                    MidiEvent ev;
                    ev.absoluteTick = absoluteTick;
                    ev.type = MidiEventType::EndOfTrack;
                    result.events.push_back(ev);
                }
                pos += len;
            }
            else if (statusByte == 0xF0 || statusByte == 0xF7)
            {
                // SysEx - preskocit obsah (TODO: zpracovat AWE/GS/GM reset zpravy, viz sekce 1.1 v TODO)
                uint32_t len = ReadVLQ(data, pos);
                pos += len;
            }
            else if (hiNibble == 0x80 || hiNibble == 0x90 || hiNibble == 0xA0 ||
                     hiNibble == 0xB0 || hiNibble == 0xE0)
            {
                // 2-bytove zpravy: Note Off/On, Poly Pressure, Control Change, Pitch Bend
                if (pos + 2 > data.size()) break;
                uint8_t d1 = data[pos++];
                uint8_t d2 = data[pos++];

                MidiEvent ev;
                ev.absoluteTick = absoluteTick;
                ev.channel = channel;
                ev.data1 = d1;
                ev.data2 = d2;

                switch (hiNibble)
                {
                case 0x80: ev.type = MidiEventType::NoteOff; break;
                case 0x90: ev.type = (d2 == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn; break;
                case 0xA0: ev.type = MidiEventType::PolyPressure; break;
                case 0xB0: ev.type = MidiEventType::ControlChange; break;
                case 0xE0: ev.type = MidiEventType::PitchBend; break;
                }
                result.events.push_back(ev);
            }
            else if (hiNibble == 0xC0 || hiNibble == 0xD0)
            {
                // 1-bytove zpravy: Program Change, Channel Pressure
                if (pos + 1 > data.size()) break;
                uint8_t d1 = data[pos++];

                MidiEvent ev;
                ev.absoluteTick = absoluteTick;
                ev.channel = channel;
                ev.data1 = d1;
                ev.type = (hiNibble == 0xC0) ? MidiEventType::ProgramChange : MidiEventType::ChannelPressure;
                result.events.push_back(ev);
            }
            else
            {
                // Neznama/nepodporovana status byte - ukoncit parsovani teto stopy,
                // radeji nez pokracovat s desynchronizovanym streamem
                break;
            }
        }

        result.ok = true;
        return result;
    }
}

namespace MidiFile
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
        if (buffer.size() < 14 || std::memcmp(buffer.data(), "MThd", 4) != 0)
        {
            seq.errorMessage = "Chybi MThd hlavicka - nejde o platny SMF soubor";
            return seq;
        }

        uint32_t headerLen = ReadBE32(&buffer[4]);
        // format (SMF 0/1/2) se z hlavicky cte, ale zatim se nikde nevyuziva -
        // slouceni vice stop funguje stejne pro format 0 i 1 (viz merge nize).
        uint16_t numTracks = ReadBE16(&buffer[10]);
        uint16_t division = ReadBE16(&buffer[12]);

        if (division & 0x8000)
        {
            // SMPTE format (frames/sec + ticks/frame) - TODO: podpora, viz sekce 1.1
            seq.errorMessage = "SMPTE division neni zatim podporovana";
            return seq;
        }
        seq.ticksPerQuarterNote = division;

        // Za MThd chunkem: 4 bajty ID + 4 bajty delky + headerLen dat
        // (headerLen je typicky 6, tj. prvni MTrk zacina na offsetu 14).
        size_t pos = 8 + headerLen;
        std::vector<MidiEvent> merged;

        for (uint16_t t = 0; t < numTracks && pos + 8 <= buffer.size(); ++t)
        {
            if (std::memcmp(&buffer[pos], "MTrk", 4) != 0)
            {
                seq.errorMessage = "Ocekavan MTrk chunk, nenalezen (stopa " + std::to_string(t) + ")";
                return seq;
            }
            uint32_t trackLen = ReadBE32(&buffer[pos + 4]);
            size_t trackStart = pos + 8;
            size_t trackEnd = trackStart + trackLen;
            if (trackEnd > buffer.size())
            {
                seq.errorMessage = "Poskozena delka stopy " + std::to_string(t);
                return seq;
            }

            std::vector<uint8_t> trackData(buffer.begin() + trackStart, buffer.begin() + trackEnd);
            TrackParseResult tr = ParseTrack(trackData);
            merged.insert(merged.end(), tr.events.begin(), tr.events.end());

            pos = trackEnd;
        }

        // Stabilni razeni podle absoluteTick - udalosti ze stejneho ticku ze stejne stopy
        // si zachovaji puvodni poradi, coz je dulezite napr. pro CC pred Note On.
        std::stable_sort(merged.begin(), merged.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.absoluteTick < b.absoluteTick; });

        seq.events = std::move(merged);
        seq.valid = true;
        return seq;
    }
}
