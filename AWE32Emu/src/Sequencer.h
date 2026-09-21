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
    uint32_t m_currentTempoUs = 500000; // 120 BPM, prepsano prvni TempoChange udalosti
    bool m_triggerMute = false;
};
