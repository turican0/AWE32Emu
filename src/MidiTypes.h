#pragma once
#include <cstdint>
#include <vector>
#include <string>

// Unified representation of MIDI events, common to both .mid (SMF) and .xmi input.
// XmiFile and MidiFile both produce std::vector<MidiEvent> sorted by absoluteTick,
// which is then consumed by the Sequencer.

enum class MidiEventType : uint8_t
{
    NoteOff,
    NoteOn,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
    TempoChange,   // meta 0x51 (SMF) / derived from XMI, value in microseconds per quarter note
    EndOfTrack
};

struct MidiEvent
{
    uint32_t absoluteTick = 0;
    MidiEventType type = MidiEventType::NoteOn;
    uint8_t channel = 0;   // 0-15, unused for TempoChange/EndOfTrack
    uint8_t data1 = 0;     // note / controller / program
    uint8_t data2 = 0;     // velocity / controller value
    uint32_t tempoUsPerQuarter = 500000; // only valid for TempoChange
};

// The result of parsing the input file (common for both Midi File and Xmi File),
// which the Sequencer plays.
struct ParsedSequence
{
    std::vector<MidiEvent> events;   // sorted by absoluteTick
    uint16_t ticksPerQuarterNote = 480;
    bool valid = false;
    std::string errorMessage;
};
