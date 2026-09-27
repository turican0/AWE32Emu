#pragma once
#include "MidiTypes.h"
#include "Synth.h"
#include <cstdint>

// Converts the tick time line of a ParsedSequence (the same for .mid and
// .xmi) to real time through the tempo map and calls NoteOn/NoteOff/... on
// the Synth in that rhythm.
class Sequencer
{
public:
    void Load(ParsedSequence sequence);

    // True until all events of the sequence are used up
    // (voices still decaying are not counted - the caller takes care of the "tail", see main.cpp).
    bool HasMoreEvents() const;

    // Renders numFrames stereo frames, calling Synth on the way for the
    // events that fall within the block.
    void RenderBlock(Synth& synth, int16_t* out, uint32_t numFrames, uint32_t sampleRate);

    // Magic Carpet 2 registers an AIL trigger callback (NETHERW sub_8E0D0,
    // remc2 engine/Sound.cpp) that runs on every XMIDI CC119. With value 0 it
    // sends CC11 = 0 on that channel: the "war" tracks of the level music
    // stay silent until a fight fades them in. On the tester's recording of
    // level 1 channels 7-9 (CC119 0) are indeed absent.
    // Value 1 (intro) stops the sequence and sends CC0 = 1; not modelled -
    // the intro matches the game on all registers without it.
    void SetTriggerMute(bool on) { m_triggerMute = on; }

private:
    void DispatchEvent(Synth& synth, const MidiEvent& ev);
    double TicksPerSecond() const;

    ParsedSequence m_sequence;
    size_t m_nextEventIndex = 0;
    double m_currentTick = 0.0;
    uint32_t m_currentTempoUs = 500000; // 120 BPM, overwritten by the first TempoChange event
    bool m_triggerMute = false;
};
