#pragma once
#include <cstdint>
#include <array>
#include "Emu8000.h"

// MIDI/MPU-401 interpretacni vrstva nad register-level jadrem Emu8000Core.
//
// Synth nedrzi zadny zvukovy stav - prelozi MIDI udalost na presne ty zapisy
// do registru EMU8000, ktere by udelal ovladac na realne karte (poradi zapisu
// vcetne toho, ze DCYSUSV se zapisuje jako posledni, protoze prave on spousti
// envelope engine - viz docs/re-notes/emu8000_register_map.md).
//
// Co jeste chybi:
//   - vyber patche podle Program Change (ceka na SoundFont vrstvu); dokud
//     neni banka nactena, pouziva se generovana sinusova tabulka v DRAM
//   - RPN 0 (pitch bend range) - zatim pevnych +-2 pultony
//   - chorus/reverb efekty (sendy se do registru zapisuji, efekt zatim ne)
class Synth
{
public:
    explicit Synth(uint32_t sampleRate);

    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void NoteOff(uint8_t channel, uint8_t note);
    void ProgramChange(uint8_t channel, uint8_t program);
    void ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void PitchBend(uint8_t channel, int16_t value);

    void RenderBlock(int16_t* out, uint32_t numFrames);

    Emu8000Core& Core() { return m_core; }

    static constexpr int kMaxVoices = Emu8000Core::kMaxVoices;

private:
    // Popis jednoho vzorku v emulovane DRAM tak, jak ho potrebuji registry
    // PSST/CSL/CCCA. Adresy jsou absolutni (vcetne Emu8000::kDramOffset).
    struct SampleRegion
    {
        uint32_t start = 0;
        uint32_t loopStart = 0;
        uint32_t loopEnd = 0;
        double   unityFreqHz = 0.0;  // frekvence pri jednotkovem prirustku
    };

    struct VoiceAlloc
    {
        bool inUse = false;
        bool heldBySustain = false;
        uint8_t channel = 0;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint32_t age = 0;            // poradi prideleni, pro voice stealing
    };

    struct ChannelState
    {
        uint8_t program = 0;
        uint8_t volume = 100;        // CC7
        uint8_t expression = 127;    // CC11
        uint8_t pan = 64;            // CC10
        uint8_t reverbSend = 40;     // CC91
        uint8_t chorusSend = 0;      // CC93
        bool sustain = false;        // CC64
        int16_t pitchBend = 0;
        uint8_t pitchBendRangeSemitones = 2;
    };

    void BuildDefaultWaveform();
    int  AllocateVoice();
    void ProgramVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity);
    void ReleaseVoice(int voice);
    void KillVoice(int voice);

    uint16_t ComputePitch(uint8_t channel, uint8_t note) const;
    uint16_t ComputeIfatn(uint8_t channel, uint8_t velocity) const;
    void RefreshChannel(uint8_t channel);

    Emu8000Core m_core;
    SampleRegion m_defaultSample;
    std::array<VoiceAlloc, kMaxVoices> m_alloc{};
    std::array<ChannelState, 16> m_channels{};
    uint32_t m_ageCounter = 0;

    // EMU8000 ma 32 hlasu, ale hlasy 30 a 31 zabira ovladac na DRAM refresh
    // (viz inicializacni sekvence), takze pro noty zbyva 30 hlasu.
    static constexpr int kUsableVoices = 30;
};
