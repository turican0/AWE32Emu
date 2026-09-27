#include "XmiFile.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace
{
    uint32_t ReadBE32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

    // XMI interval (delta time): bytes 0x00-0x7E end the sum, byte 0x7F
    // means "add 127 and go on with the next byte". The first byte >= 0x80
    // is no longer part of the interval (an interval can be zero - 0 bytes).
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

    // Standard SMF-style VLQ - XMI meta/sysex events (unlike the delta time)
    // use the same length encoding as SMF.
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

    // Finds the first 4-byte IFF tag in the buffer and returns the offset RIGHT
    // AFTER the tag (i.e. the start of the 4-byte BE length that always follows
    // in IFF). -1 when not found.
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
            seq.errorMessage = "Cannot open file: " + path;
            return seq;
        }

        std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (buffer.size() < 12 || std::memcmp(buffer.data(), "FORM", 4) != 0)
        {
            seq.errorMessage = "Missing FORM header - not a valid XMI/IFF file";
            return seq;
        }

        // Simplification (see the TODO in the header): the first EVNT chunk of
        // the file is taken. Multi-song XMI (CAT XMI with several FORM XMID) is
        // left for later - RBRN (loop body) is skipped for now.
        long evntLenPos = FindChunk(buffer, "EVNT");
        if (evntLenPos < 0)
        {
            seq.errorMessage = "No EVNT chunk - not a valid XMI file (or an unsupported variant)";
            return seq;
        }

        size_t pos = static_cast<size_t>(evntLenPos);
        if (pos + 4 > buffer.size())
        {
            seq.errorMessage = "Damaged EVNT chunk (length missing)";
            return seq;
        }
        uint32_t evntLen = ReadBE32(&buffer[pos]);
        pos += 4;
        size_t evntEnd = pos + evntLen;
        if (evntEnd > buffer.size())
            evntEnd = buffer.size(); // tolerant of slightly damaged/truncated files

        std::vector<uint8_t> data(buffer.begin() + pos, buffer.begin() + evntEnd);

        // XMI has a fixed "clock": 60 ticks per quarter note, 120 BPM by
        // default, unless meta event 0x51 in the data says otherwise.
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
                // An unexpected byte where a status should be - the stream is out of sync
                // (TODO: running status support, see the header). Better to stop cleanly.
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

                // The tempo meta event is ignored ON PURPOSE in XMI. XMI runs
                // on a fixed 120 Hz clock - the value in FF 51 only serves the
                // PPQN calculation when converting to SMF, not the playback.
                // Checked on 000_C2GAME1_w.xmi: 52755 ticks / 120 Hz = 439.6 s,
                // which matches the reference recording (441.9 s), while tempo
                // 560748 would give 493 s.
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
                // An XMI Note On also carries the note length (interval
                // encoding) - from it we derive the explicit Note Off the
                // SMF/Sequencer expects.
                if (p + 2 > data.size()) break;
                uint8_t note = data[p++];
                uint8_t velocity = data[p++];
                // NOTE: the note length is NOT encoded as a delta-time interval
                // but as a standard SMF VLQ (continuation bit 0x80). Mixing up
                // the two encodings derails the whole stream - the parser then
                // stopped after a few hundred events instead of several
                // thousand.
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
                break; // unknown status - stop parsing
            }
        }

        // A Note Off derived from the note length may come before events read
        // later - the stable sort by absoluteTick puts them in the right order
        // for the Sequencer.
        std::stable_sort(events.begin(), events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.absoluteTick < b.absoluteTick; });

        seq.events = std::move(events);
        seq.valid = true;
        return seq;
    }
}
