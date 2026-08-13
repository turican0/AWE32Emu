#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include "Emu8000Regs.h"

// ---------------------------------------------------------------------------
// Emu8000Core - register-level emulace cipu EMU8000 (Sound Blaster AWE32).
//
// Registrova mapa i inicializacni sekvence jsou odvozene z disassembly
// ovladace AWEUTIL.COM, viz docs/re-notes/emu8000_register_map.md.
//
// Trida ma zamerne DVE urovne rozhrani:
//
//   1) Portova uroven (PortOut16/PortIn16) - presne to, co dela realny
//      ovladac: OUT na pointer registr + OUT na datovy port. Tohle je
//      cesta pro napojeni reversed DOS hry, ktera si registry nastavuje
//      sama (viz README, pouziti 2).
//
//   2) Registrova uroven (WriteReg16/32, Write/Read s enumem Reg) - pro
//      nas vlastni MIDI prehravac (Synth.cpp), ktery nemusi simulovat
//      I/O porty.
//
// Cip bezi nativne na 44100 Hz; RenderBlock umi vystup i na jinou
// frekvenci (linearni resampling), ale vnitrni casovani je vzdy 44100.
// ---------------------------------------------------------------------------

class Emu8000Core
{
public:
    static constexpr int kMaxVoices = Emu8000::kMaxVoices;
    static constexpr uint32_t kNativeSampleRate = 44100;

    explicit Emu8000Core(uint32_t outputSampleRate);

    // ---- portova uroven -------------------------------------------------
    void SetBasePort(uint16_t sbBasePort);          // default 0x220
    uint16_t BasePort() const { return m_basePort; }
    bool OwnsPort(uint16_t port) const;
    void PortOut16(uint16_t port, uint16_t value);
    uint16_t PortIn16(uint16_t port);

    // ---- registrova uroven ----------------------------------------------
    void WriteReg16(uint16_t sel, uint16_t value);
    uint16_t ReadReg16(uint16_t sel) const;
    void WriteReg32(uint16_t sel, uint32_t value);   // low word, pak high word
    uint32_t ReadReg32(uint16_t sel) const;

    void Write(Emu8000::Reg r, int voice, uint32_t value);
    uint32_t Read(Emu8000::Reg r, int voice) const;

    // Inicializacni sekvence prevzata z AWEUTIL.COM (sub_12B40 a jeho
    // podrutiny). Init pole INIT1..INIT4 se do emulace neprenaseji, viz
    // poznamka v docs/re-notes/emu8000_register_map.md.
    void PowerOnInit();

    // ---- zvukova pamet ---------------------------------------------------
    // Adresy v registrech CCCA/PSST/CSL jsou 24bit a pocitaji se ve vzorcich.
    // Uzivatelska DRAM zacina na Emu8000::kDramOffset; nize lezi ROM karty,
    // kterou nemame (cteni vraci 0).
    void ResizeDram(size_t numSamples);
    size_t DramSize() const { return m_dram.size(); }
    int16_t* DramData() { return m_dram.data(); }
    const int16_t* DramData() const { return m_dram.data(); }
    int16_t ReadSample(uint32_t address) const;

    // ---- render ----------------------------------------------------------
    // out = interleaved stereo int16, numFrames snimku na vystupni frekvenci.
    void RenderBlock(int16_t* out, uint32_t numFrames);

    bool IsVoiceActive(int voice) const;

private:
    // Registrove pole: [port][reg][voice], vse jako 16bit slova.
    // 32bit registry = dvojice (Data0,Data0Hi) resp. (Data1,Data1Hi).
    using RegFile = std::array<std::array<std::array<uint16_t, kMaxVoices>,
                                          8>,
                               static_cast<size_t>(Emu8000::Port::Count)>;

    enum class EnvStage { Off, Delay, Attack, Hold, Decay, Sustain, Release };

    struct VoiceState
    {
        // prehravani vzorku
        uint32_t address = 0;      // celociselna cast (vzorky)
        uint32_t frac = 0;         // 16bit zlomkova cast
        bool     playing = false;

        // volume envelope
        EnvStage volStage = EnvStage::Off;
        double   volDb = 96.0;     // aktualni utlum v dB (0 = plna hlasitost)
        double   volLin = 0.0;     // linearni zisk behem attack faze
        double   stageTime = 0.0;  // sekundy stravene v aktualni fazi

        // modulation envelope
        EnvStage modStage = EnvStage::Off;
        double   modLevel = 0.0;
        double   modStageTime = 0.0;

        // LFO
        double lfo1Phase = 0.0;
        double lfo2Phase = 0.0;
        double lfo1Delay = 0.0;
        double lfo2Delay = 0.0;

        // low-pass filtr (Chamberlin SVF, viz poznamka v .cpp)
        double filtLow = 0.0;
        double filtBand = 0.0;
    };

    void RenderNative(float* outL, float* outR, uint32_t numFrames);
    void RenderVoice(int v, float* outL, float* outR, uint32_t numFrames);
    void UpdateRegistersFromState(int v);

    uint16_t& RegRef(Emu8000::Port p, int reg, int voice);
    uint16_t  RegVal(Emu8000::Port p, int reg, int voice) const;

    uint32_t m_outputRate;
    uint16_t m_basePort = 0x220;
    uint16_t m_pointer = 0;      // posledni zapis do pointer registru

    RegFile m_regs{};
    std::array<VoiceState, kMaxVoices> m_voices{};
    std::vector<int16_t> m_dram;

    // Wave counter (registr WC) - volne bezici citac vzorku. Ovladace ho
    // pouzivaji jako casovou zakladnu v cekacich smyckach (viz AWEUTIL
    // sub_127AE), takze ho musime tikat, jinak by se ovladac zasekl.
    uint32_t m_waveCounter = 0;

    // resampling na vystupni frekvenci
    std::vector<float> m_nativeL, m_nativeR;
    double m_resamplePos = 0.0;
    float m_lastL = 0.0f, m_lastR = 0.0f;
};
