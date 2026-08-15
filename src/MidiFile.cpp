#include "MidiFile.h"
#include "Util.h"
#include <fstream>
#include <algorithm>

namespace
{
    uint32_t ReadBE32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
    uint16_t ReadBE16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

    // Variable-length quantity according to SMF specification (7 bits per byte, MSB = "continues")
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
                // Running status - repeats the previous status byte, this is already data1
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
                // SysEx - skip content (TODO: handle AWE/GS/GM reset messages, see section 1.1 in TODO)
                uint32_t len = ReadVLQ(data, pos);
                pos += len;
            }
            else if (hiNibble == 0x80 || hiNibble == 0x90 || hiNibble == 0xA0 ||
                     hiNibble == 0xB0 || hiNibble == 0xE0)
            {
                // 2-byte messages: Note Off/On, Poly Pressure, Control Change, Pitch Bend
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
                // 1-byte messages: Program Change, Channel Pressure
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
                // Unknown/unsupported byte status - stop parsing this trace,
                // rather than continue with a desynchronized stream
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
        // format (SMF 0/1/2) is read from the header, but is not used anywhere yet -
        // merging multiple tracks works the same for format 0 and 1 (see merge below).
        uint16_t numTracks = ReadBE16(&buffer[10]);
        uint16_t division = ReadBE16(&buffer[12]);

        if (division & 0x8000)
        {
            // SMPTE format (frames/sec + ticks/frame) - TODO: support, see section 1.1
            seq.errorMessage = "SMPTE division neni zatim podporovana";
            return seq;
        }
        seq.ticksPerQuarterNote = division;

        // After MThd chunk: 4 bytes ID + 4 bytes length + headerLen data
        // (headerLen is typically 6, i.e. the first MTrk starts at offset 14).
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

        // Stable stamping by absoluteTick - events from the same tick from the same track
        // retain the original order, which is important for example for CC before Note On.
        std::stable_sort(merged.begin(), merged.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.absoluteTick < b.absoluteTick; });

        seq.events = std::move(merged);
        seq.valid = true;
        return seq;
    }
}
