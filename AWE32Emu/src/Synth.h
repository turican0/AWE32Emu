#pragma once
#include <cstdint>
#include <array>
#include "Emu8000.h"

// MIDI/MPU-401 interpretacni vrstva (TODO seznam, sekce 3) nad register-level
// jadrem Emu8000Core (sekce 4, krok 3 "minimalni EMU8000 jadro bez efektu").
//
// Synth preklada MIDI udalosti (NoteOn/NoteOff/...) na zapisy do registru
// Emu8000Core - nedrzi uz vlastni zvukovy stav primo, jen alokaci hlasu a
// mapovani MIDI kanal/nota -> cislo hlasu. Skutecna syntéza (obalka, filtr,
// v budoucnu sample playback a efekty) zije v Emu8000Core.
//
// Stale chybi (viz hlavni TODO):
//   - sample playback ze SoundFont/SBK dat (sekce 5) misto sinusu v Emu8000Core
//   - presne casove konstanty envelope a format filter registru (sekce 4)
//   - LFO1/LFO2, chorus/reverb (sekce 4)
//   - Program Change -> vyber patch/instrument (zavisi na sekci 5)
//   - Control Change (sustain, volume, pan, RPN/NRPN) (sekce 3)
//   - Pitch Bend napojeny na PitchOffset registr podle bend range (sekce 3)
//
// API teto tridy zustava beze zmeny (NoteOn/NoteOff/ProgramChange/
// ControlChange/PitchBend/RenderBlock), takze Sequencer/main.cpp se
// touto zmenou nemuseji upravovat.
class Synth
{
public:
    explicit Synth(uint32_t sampleRate);

    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void NoteOff(uint8_t channel, uint8_t note);
    void ProgramChange(uint8_t channel, uint8_t program);     // TODO: mapovani na SoundFont patch
    void ControlChange(uint8_t channel, uint8_t controller, uint8_t value); // TODO: napr. sustain, volume, pan
    void PitchBend(uint8_t channel, int16_t value);            // TODO: aplikovat na frekvenci hlasu

    // Vyrenderuje numFrames stereo snimku (interleaved L/R int16) do out.
    void RenderBlock(int16_t* out, uint32_t numFrames);

    static constexpr int kMaxVoices = Emu8000Core::kMaxVoices;

private:
    struct VoiceAlloc
    {
        bool inUse = false;
        uint8_t channel = 0;
        uint8_t note = 0;
    };

    int FindFreeVoiceOrSteal();
    uint16_t NoteToPitchOffset(uint8_t note) const;

    Emu8000Core m_core;
    std::array<VoiceAlloc, kMaxVoices> m_alloc;
};
