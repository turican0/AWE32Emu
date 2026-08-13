#include "Synth.h"
#include <algorithm>

Synth::Synth(uint32_t sampleRate)
    : m_core(sampleRate)
{
}

uint16_t Synth::NoteToPitchOffset(uint8_t note, uint8_t channel) const
{
    // Emu8000Core::PitchOffset: stred 8192 = A4 (MIDI nota 69), 4096
    // jednotek na oktavu (viz Emu8000.cpp NoteOffsetToFreqHz - placeholder,
    // TODO nahradit skutecnym pitch registrem dle Tech Ref Manualu).
    const double unitsPerSemitone = 4096.0 / 12.0;

    int semitonesFromA4 = static_cast<int>(note) - 69;
    double bendSemitones = (m_channels[channel].pitchBend / 8192.0) *
                            m_channels[channel].pitchBendRangeSemitones;

    double offset = 8192.0 + (semitonesFromA4 + bendSemitones) * unitsPerSemitone;
    return static_cast<uint16_t>(std::clamp(offset, 0.0, 65535.0));
}

int Synth::FindFreeVoiceOrSteal()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_alloc[i].inUse)
            return i;

    // Vsech 32 hlasu obsazeno - ukradnout ten nejtisejsi. Skutecny EMU8000 ma
    // vlastni voice-stealing prioritu (TODO sekce 4); tohle je jen rozumna
    // zaslepka a cte aktualni uroven obalky primo z Emu8000Core. Hlasy drzene
    // jen sustain pedalem (heldBySustain) jsou prvni na rane, protoze uz
    // notu nikdo nedrzi.
    int quietest = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (m_alloc[i].heldBySustain) { quietest = i; break; }
        if (!m_core.IsActive(i)) { quietest = i; break; }
    }
    uint16_t quietestAtten = m_core.ReadWordRegister(quietest, Emu8000Reg::InitialAttenuation);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (m_alloc[i].heldBySustain) continue;
        uint16_t atten = m_core.ReadWordRegister(i, Emu8000Reg::InitialAttenuation);
        if (atten > quietestAtten)
        {
            quietest = i;
            quietestAtten = atten;
        }
    }
    return quietest;
}

void Synth::ReleaseVoice(int idx)
{
    m_core.SetGate(idx, false);
    m_alloc[idx].inUse = false;
    m_alloc[idx].heldBySustain = false;
}

void Synth::ApplyChannelVolume(uint8_t channel)
{
    // Kombinuje MIDI velocity kazdeho hlasu s aktualni hodnotou CC7 pro dany
    // kanal. TODO: expression (CC11) by se mel nasobit stejnym zpusobem, az
    // bude implementovan.
    const auto& ch = m_channels[channel];
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& a = m_alloc[i];
        if (!a.inUse || a.channel != channel) continue;

        double velNorm = a.velocity / 127.0;
        double volNorm = ch.volume / 127.0;
        double level = velNorm * volNorm;
        uint16_t atten = static_cast<uint16_t>((1.0 - level) * 65535.0);
        m_core.WriteWordRegister(i, Emu8000Reg::InitialAttenuation, atten);
    }
}

void Synth::ApplyChannelPan(uint8_t channel)
{
    const auto& ch = m_channels[channel];
    uint16_t pan14 = static_cast<uint16_t>((ch.pan / 127.0) * 16383.0);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& a = m_alloc[i];
        if (!a.inUse || a.channel != channel) continue;
        m_core.WriteWordRegister(i, Emu8000Reg::Pan, pan14);
    }
}

void Synth::ApplyChannelPitchBend(uint8_t channel)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& a = m_alloc[i];
        if (!a.inUse || a.channel != channel) continue;
        m_core.WriteWordRegister(i, Emu8000Reg::PitchOffset, NoteToPitchOffset(a.note, channel));
    }
}

void Synth::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (velocity == 0)
    {
        NoteOff(channel, note);
        return;
    }

    int idx = FindFreeVoiceOrSteal();
    m_alloc[idx] = { true, false, channel, note, velocity };

    m_core.WriteWordRegister(idx, Emu8000Reg::PitchOffset, NoteToPitchOffset(note, channel));

    const auto& ch = m_channels[channel];
    double level = (velocity / 127.0) * (ch.volume / 127.0);
    uint16_t atten = static_cast<uint16_t>((1.0 - level) * 65535.0);
    m_core.WriteWordRegister(idx, Emu8000Reg::InitialAttenuation, atten);

    uint16_t pan14 = static_cast<uint16_t>((ch.pan / 127.0) * 16383.0);
    m_core.WriteWordRegister(idx, Emu8000Reg::Pan, pan14);

    m_core.SetGate(idx, true);
}

void Synth::NoteOff(uint8_t channel, uint8_t note)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& a = m_alloc[i];
        if (a.inUse && !a.heldBySustain && a.channel == channel && a.note == note)
        {
            if (m_channels[channel].sustain)
            {
                // Sustain pedal sepnuty - hlas necham znit, jen ho oznacim,
                // aby ho FindFreeVoiceOrSteal preferoval pri kradezi a aby ho
                // uvolnil pozdejsi ControlChange(64, sepnuto->vypnuto).
                a.heldBySustain = true;
            }
            else
            {
                ReleaseVoice(i);
            }
        }
    }
}

void Synth::ProgramChange(uint8_t channel, uint8_t program)
{
    m_channels[channel].program = program;
    // TODO: az bude napojena SoundFontSbk vrstva (sekce 5), tady se podle
    // ulozeneho cisla programu vybere patch/instrument pro dany kanal
    // misto ted pouziteho pevneho sinusoveho generatoru v Emu8000Core.
}

void Synth::ControlChange(uint8_t channel, uint8_t controller, uint8_t value)
{
    auto& ch = m_channels[channel];
    switch (controller)
    {
    case 7: // Channel Volume
        ch.volume = value;
        ApplyChannelVolume(channel);
        break;

    case 10: // Pan
        ch.pan = value;
        ApplyChannelPan(channel);
        break;

    case 64: // Sustain pedal
    {
        bool wasOn = ch.sustain;
        ch.sustain = value >= 64;
        if (wasOn && !ch.sustain)
        {
            // Pedal prave pusteny - vsechny hlasy drzene jen sustainem
            // na tomto kanalu ted skutecne pustit do release faze.
            for (int i = 0; i < kMaxVoices; ++i)
            {
                auto& a = m_alloc[i];
                if (a.inUse && a.heldBySustain && a.channel == channel)
                    ReleaseVoice(i);
            }
        }
        break;
    }

    default:
        // TODO: dalsi controllery (expression 11, modulation 1, RPN/NRPN
        // 100/101/6/38 pro pitch bend range atd.) - viz Synth.h TODO.
        break;
    }
}

void Synth::PitchBend(uint8_t channel, int16_t value)
{
    m_channels[channel].pitchBend = value;
    ApplyChannelPitchBend(channel);
}

void Synth::RenderBlock(int16_t* out, uint32_t numFrames)
{
    m_core.RenderBlock(out, numFrames);
}
