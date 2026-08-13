#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

// Emu8000Core - zaklad register-level jadra EMU8000 (TODO seznam, sekce 4,
// krok 3 "Minimalni EMU8000 jadro bez efektu: jen voice+envelope+filtr").
//
// Toto je PUVODNI implementace navrzena podle:
//   - obecne zname architektury cipu (32 hlasu, pristup pres Pointer +
//     Data0/Data1 registry)
//   - mechanismu pristupu k registrum potvrzeneho analyzou AWEUTIL.COM,
//     viz docs/re-notes/aweutil_register_access_notes.md (POUZE mechanismus
//     - "jak se pristupuje", ne konkretni cisla registru ani zadny kod
//     prevzaty z ovladace)
//
// Presna cisla/vyznam jednotlivych registru (RegId nize) jsou zatim interni
// a POTREBUJI OVERENI proti EMU8000 Tech Ref Manualu (TODO sekce 0) - dokud
// se neprovede, jde o architekturu "register-driven voice", ne o bit-presnou
// shodu s realnym cipem. Waveform je zatim porad sinusovy placeholder
// (nahrada sample playbackem ze SoundFont je TODO sekce 5); chorus/reverb/LFO
// chybi umyslne (dalsi krok podle TODO poradi praci).

enum class Emu8000Reg : uint8_t
{
    // -- potvrzeno mechanismem (word registr) --
    PitchOffset,      // 16bit posun vysky tonu vuci zakladni note (placeholder jednotky)

    // -- volume envelope (TODO: presne casove konstanty z manualu, sekce 4) --
    VolEnvAttackRate,
    VolEnvDecayRate,
    VolEnvSustainLevel,
    VolEnvReleaseRate,

    // -- low-pass filtr (TODO: format cutoff/Q dle registru, sekce 4) --
    FilterCutoff,
    FilterResonance,

    // -- mixing --
    Pan,               // 0 = vlevo, 8192 = stred, 16383 = vpravo (placeholder skala)
    InitialAttenuation,

    Count
};

struct Emu8000VoiceRegs
{
    std::array<uint16_t, static_cast<size_t>(Emu8000Reg::Count)> words{};
    bool gate = false;   // TODO: nahradit skutecnym stavem envelope generatoru z manualu
};

class Emu8000Core
{
public:
    static constexpr int kMaxVoices = 32; // EMU8000 ma 32 hardwarovych hlasu

    explicit Emu8000Core(uint32_t sampleRate);

    // Register-level rozhrani - napodobuje pristupovy vzor Pointer+Data
    // potvrzeny v aweutil_register_access_notes.md (logicky, ne bit-presne).
    void WriteWordRegister(int voice, Emu8000Reg reg, uint16_t value);
    uint16_t ReadWordRegister(int voice, Emu8000Reg reg) const;

    // Gate - zapnuti/vypnuti hlasu (skutecny cip ma na tohle vlastni
    // registr/mechanismus spousteni envelope, viz TODO - zjednoduseno).
    void SetGate(int voice, bool on);
    bool IsActive(int voice) const;

    void RenderBlock(int16_t* out, uint32_t numFrames);

private:
    uint32_t m_sampleRate;
    std::array<Emu8000VoiceRegs, kMaxVoices> m_regs;

    // Interni stav enveloparu a filtru na hlas (odvozeny z registru pri
    // kazdem RenderBlock - neni soucasti registrove mapy cipu).
    struct EnvRuntime
    {
        enum class Stage { Idle, Attack, Decay, Sustain, Release } stage = Stage::Idle;
        double level = 0.0;
        double phase = 0.0;

        // Stav resonantniho low-pass filtru (Chamberlin state-variable
        // filter - zvoleny pro jednoduchost a stabilitu pri modulaci
        // cutoff/resonance za behu, ne proto, ze by presne odpovidal
        // topologii realneho EMU8000 filtru - TODO overit proti manualu).
        double filterLow = 0.0;
        double filterBand = 0.0;
    };
    std::array<EnvRuntime, kMaxVoices> m_env;

    double NoteOffsetToFreqHz(uint16_t pitchOffset) const;
    double ApplyFilter(EnvRuntime& env, double input, uint16_t cutoffReg, uint16_t resonanceReg) const;
};
