#include "Emu8000.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Placeholder prevodni tabulka rate -> sekundy. Skutecny EMU8000 pouziva
    // exponencialni casove konstanty definovane v Tech Ref Manualu (TODO
    // sekce 4 - "Envelope generatory ... presne casove konstanty"). Tady je
    // jen rozumna aproximace, aby jadro fungovalo end-to-end uz ted.
    double RateToSeconds(uint16_t rate)
    {
        if (rate == 0) return 0.001;
        return std::clamp(1.0 - (static_cast<double>(rate) / 65535.0), 0.001, 1.0) * 2.0;
    }
}

Emu8000Core::Emu8000Core(uint32_t sampleRate)
    : m_sampleRate(sampleRate)
{
    // Rozumne vychozi hodnoty registru, dokud sequencer/synth vrstva
    // nezapise realne (viz Synth.cpp) - odpovida chovani "power-on defaults"
    // u realneho cipu, jen s placeholder cisly.
    for (auto& v : m_regs)
    {
        v.words[static_cast<size_t>(Emu8000Reg::VolEnvAttackRate)] = 60000;
        v.words[static_cast<size_t>(Emu8000Reg::VolEnvDecayRate)] = 60000;
        v.words[static_cast<size_t>(Emu8000Reg::VolEnvSustainLevel)] = 65535;
        v.words[static_cast<size_t>(Emu8000Reg::VolEnvReleaseRate)] = 55000;
        v.words[static_cast<size_t>(Emu8000Reg::Pan)] = 8192;
        v.words[static_cast<size_t>(Emu8000Reg::InitialAttenuation)] = 0;
    }
}

void Emu8000Core::WriteWordRegister(int voice, Emu8000Reg reg, uint16_t value)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    m_regs[voice].words[static_cast<size_t>(reg)] = value;
}

uint16_t Emu8000Core::ReadWordRegister(int voice, Emu8000Reg reg) const
{
    if (voice < 0 || voice >= kMaxVoices) return 0;
    return m_regs[voice].words[static_cast<size_t>(reg)];
}

void Emu8000Core::SetGate(int voice, bool on)
{
    if (voice < 0 || voice >= kMaxVoices) return;
    m_regs[voice].gate = on;
    auto& env = m_env[voice];
    if (on)
    {
        env.stage = EnvRuntime::Stage::Attack;
    }
    else if (env.stage != EnvRuntime::Stage::Idle)
    {
        env.stage = EnvRuntime::Stage::Release;
    }
}

bool Emu8000Core::IsActive(int voice) const
{
    if (voice < 0 || voice >= kMaxVoices) return false;
    return m_env[voice].stage != EnvRuntime::Stage::Idle;
}

double Emu8000Core::NoteOffsetToFreqHz(uint16_t pitchOffset) const
{
    // Placeholder: pitchOffset je stred na 8192 = A4 (440 Hz), 4096 jednotek
    // na oktavu. TODO: nahradit skutecnou pitch-wheel/pitch-registr logikou
    // cipu (semitone/cent rozliseni dle manualu).
    double semitoneOffset = (static_cast<double>(pitchOffset) - 8192.0) / (4096.0 / 12.0);
    return 440.0 * std::pow(2.0, semitoneOffset / 12.0);
}

void Emu8000Core::RenderBlock(int16_t* out, uint32_t numFrames)
{
    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        double mix = 0.0;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            auto& env = m_env[i];
            if (env.stage == EnvRuntime::Stage::Idle)
                continue;

            const auto& regs = m_regs[i];
            const double attackSec = RateToSeconds(regs.words[static_cast<size_t>(Emu8000Reg::VolEnvAttackRate)]);
            const double decaySec = RateToSeconds(regs.words[static_cast<size_t>(Emu8000Reg::VolEnvDecayRate)]);
            const double releaseSec = RateToSeconds(regs.words[static_cast<size_t>(Emu8000Reg::VolEnvReleaseRate)]);
            const double sustainLevel = regs.words[static_cast<size_t>(Emu8000Reg::VolEnvSustainLevel)] / 65535.0;

            switch (env.stage)
            {
            case EnvRuntime::Stage::Attack:
                env.level += 1.0 / (attackSec * m_sampleRate);
                if (env.level >= 1.0) { env.level = 1.0; env.stage = EnvRuntime::Stage::Decay; }
                break;
            case EnvRuntime::Stage::Decay:
                env.level -= (1.0 - sustainLevel) / (decaySec * m_sampleRate);
                if (env.level <= sustainLevel) { env.level = sustainLevel; env.stage = EnvRuntime::Stage::Sustain; }
                break;
            case EnvRuntime::Stage::Sustain:
                break;
            case EnvRuntime::Stage::Release:
                env.level -= 1.0 / (releaseSec * m_sampleRate);
                if (env.level <= 0.0) { env.level = 0.0; env.stage = EnvRuntime::Stage::Idle; }
                break;
            default:
                break;
            }

            if (env.stage == EnvRuntime::Stage::Idle)
                continue;

            const double freqHz = NoteOffsetToFreqHz(regs.words[static_cast<size_t>(Emu8000Reg::PitchOffset)]);
            const double atten = regs.words[static_cast<size_t>(Emu8000Reg::InitialAttenuation)] / 65535.0;

            // TODO (TODO sekce 5): sample playback ze SoundFont/SBK dat misto sinusu.
            // TODO (TODO sekce 4): low-pass filtr (FilterCutoff/FilterResonance) se
            // zatim nikde neaplikuje - registry se nactou, ale nepouzivaji.
            double sample = std::sin(env.phase) * env.level * (1.0 - atten);
            mix += sample;

            env.phase += 2.0 * kPi * freqHz / m_sampleRate;
            if (env.phase > 2.0 * kPi)
                env.phase -= 2.0 * kPi;
        }

        mix *= 0.2; // jednoduchy soft-limiter, viz puvodni Synth.cpp
        mix = std::clamp(mix, -1.0, 1.0);

        int16_t sampleI16 = static_cast<int16_t>(mix * 32767.0);
        out[frame * 2 + 0] = sampleI16; // L
        out[frame * 2 + 1] = sampleI16; // R (pan registr zatim nepouzity - TODO)
    }
}
