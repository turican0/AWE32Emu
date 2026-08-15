#pragma once
#include "MidiTypes.h"
#include "Synth.h"
#include <cstdint>

// Prevadi tikovou casovou osu ParsedSequence (spolecnou pro .mid i .xmi) na
// realny cas podle tempo mapy a v tomto rytmu vola NoteOn/NoteOff/... na Synth.
class Sequencer
{
public:
    void Load(ParsedSequence sequence);

    // True, dokud nejsou vycerpany vsechny udalosti v sekvenci
    // (nezohlednuje jeste dozniva­jici hlasy - o "tail" se stara volajici kod, viz main.cpp).
    bool HasMoreEvents() const;

    // Vyrenderuje numFrames stereo snimku, po ceste vola Synth pro udalosti,
    // ktere v prubehu bloku nastanou.
    void RenderBlock(Synth& synth, int16_t* out, uint32_t numFrames, uint32_t sampleRate);

private:
    void DispatchEvent(Synth& synth, const MidiEvent& ev);
    double TicksPerSecond() const;

    ParsedSequence m_sequence;
    size_t m_nextEventIndex = 0;
    double m_currentTick = 0.0;
    uint32_t m_currentTempoUs = 500000; // 120 BPM, prepsano prvni TempoChange udalosti
};
