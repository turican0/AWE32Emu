#include "Synth.h"

Synth::Synth(uint32_t sampleRate)
    : m_core(sampleRate)
{
}

uint16_t Synth::NoteToPitchOffset(uint8_t note) const
{
    // Emu8000Core::PitchOffset: stred 8192 = A4 (MIDI nota 69), 4096
    // jednotek na oktavu (viz Emu8000.cpp NoteOffsetToFreqHz - placeholder,
    // TODO nahradit skutecnym pitch registrem dle Tech Ref Manualu).
    int semitonesFromA4 = static_cast<int>(note) - 69;
    int offset = 8192 + (semitonesFromA4 * (4096 / 12));
    if (offset < 0) offset = 0;
    if (offset > 65535) offset = 65535;
    return static_cast<uint16_t>(offset);
}

int Synth::FindFreeVoiceOrSteal()
{
    for (int i = 0; i < kMaxVoices; ++i)
        if (!m_alloc[i].inUse)
            return i;

    // Vsech 32 hlasu obsazeno - ukradnout ten nejtisejsi. Skutecny EMU8000 ma
    // vlastni voice-stealing prioritu (TODO sekce 4); tohle je jen rozumna
    // zaslepka a cte aktualni uroven obalky primo z Emu8000Core.
    int quietest = 0;
    uint16_t quietestAtten = m_core.ReadWordRegister(0, Emu8000Reg::InitialAttenuation);
    for (int i = 1; i < kMaxVoices; ++i)
    {
        uint16_t atten = m_core.ReadWordRegister(i, Emu8000Reg::InitialAttenuation);
        if (!m_core.IsActive(i))
        {
            quietest = i;
            break;
        }
        if (atten > quietestAtten)
        {
            quietest = i;
            quietestAtten = atten;
        }
    }
    return quietest;
}

void Synth::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (velocity == 0)
    {
        NoteOff(channel, note);
        return;
    }

    int idx = FindFreeVoiceOrSteal();
    m_alloc[idx] = { true, channel, note };

    m_core.WriteWordRegister(idx, Emu8000Reg::PitchOffset, NoteToPitchOffset(note));

    // TODO (TODO sekce 3): velocity by mela ovlivnit i tvar obalky/pocatecni
    // utlum, ne jen linearni attenuation - zatim jednoduchy preklad na
    // InitialAttenuation (0 = nejhlasitejsi, 65535 = ticho).
    uint16_t atten = static_cast<uint16_t>((127 - velocity) * (65535 / 127));
    m_core.WriteWordRegister(idx, Emu8000Reg::InitialAttenuation, atten);

    m_core.SetGate(idx, true);
}

void Synth::NoteOff(uint8_t channel, uint8_t note)
{
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& a = m_alloc[i];
        if (a.inUse && a.channel == channel && a.note == note)
        {
            m_core.SetGate(i, false);
            a.inUse = false; // uvolneni pro alokaci; release faze dozni v Emu8000Core
        }
    }
}

void Synth::ProgramChange(uint8_t /*channel*/, uint8_t /*program*/)
{
    // TODO: az bude napojena SoundFontSbk vrstva, tady se vybere patch/instrument
    // pro dany kanal misto ted pouziteho pevneho sinusoveho generatoru.
}

void Synth::ControlChange(uint8_t /*channel*/, uint8_t /*controller*/, uint8_t /*value*/)
{
    // TODO: sustain pedal (64), channel volume (7), pan (10), atd. -
    // az budou implementovany, budou zapisovat do Emu8000Reg::Pan a dalsich
    // registru pres m_core.WriteWordRegister(...), stejne jako NoteOn vyse.
}

void Synth::PitchBend(uint8_t /*channel*/, int16_t /*value*/)
{
    // TODO: prepocitat Emu8000Reg::PitchOffset aktivnich hlasu na danem
    // kanalu podle pitch bend range (RPN 0), pricist k hodnote z NoteToPitchOffset.
}

void Synth::RenderBlock(int16_t* out, uint32_t numFrames)
{
    m_core.RenderBlock(out, numFrames);
}
