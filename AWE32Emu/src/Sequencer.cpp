#include "Sequencer.h"

void Sequencer::Load(ParsedSequence sequence)
{
    m_sequence = std::move(sequence);
    m_nextEventIndex = 0;
    m_currentTick = 0.0;
    m_currentTempoUs = 500000;
}

bool Sequencer::HasMoreEvents() const
{
    return m_nextEventIndex < m_sequence.events.size();
}

double Sequencer::TicksPerSecond() const
{
    double quartersPerSecond = 1000000.0 / static_cast<double>(m_currentTempoUs);
    return quartersPerSecond * static_cast<double>(m_sequence.ticksPerQuarterNote);
}

void Sequencer::DispatchEvent(Synth& synth, const MidiEvent& ev)
{
    switch (ev.type)
    {
    case MidiEventType::NoteOn:
        synth.NoteOn(ev.channel, ev.data1, ev.data2);
        break;
    case MidiEventType::NoteOff:
        synth.NoteOff(ev.channel, ev.data1);
        break;
    case MidiEventType::ProgramChange:
        synth.ProgramChange(ev.channel, ev.data1);
        break;
    case MidiEventType::ControlChange:
        synth.ControlChange(ev.channel, ev.data1, ev.data2);
        break;
    case MidiEventType::PitchBend:
    {
        int16_t bend = static_cast<int16_t>(((ev.data2 << 7) | ev.data1) - 8192);
        synth.PitchBend(ev.channel, bend);
        break;
    }
    case MidiEventType::TempoChange:
        m_currentTempoUs = ev.tempoUsPerQuarter;
        break;
    case MidiEventType::PolyPressure:
    case MidiEventType::ChannelPressure:
    case MidiEventType::EndOfTrack:
    default:
        // TODO: PolyPressure/ChannelPressure zatim synth nevyuziva (viz Synth.h TODO)
        break;
    }
}

void Sequencer::RenderBlock(Synth& synth, int16_t* out, uint32_t numFrames, uint32_t sampleRate)
{
    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        // Vypustit vsechny udalosti, jejichz cas jiz nastal, drive nez se
        // vyrenderuje tento snimek - poradi v ramci stejneho ticku je dane
        // stabilnim razenim v parseru (napr. CC pred Note On).
        while (m_nextEventIndex < m_sequence.events.size() &&
               static_cast<double>(m_sequence.events[m_nextEventIndex].absoluteTick) <= m_currentTick)
        {
            DispatchEvent(synth, m_sequence.events[m_nextEventIndex]);
            m_nextEventIndex++;
        }

        synth.RenderBlock(out + frame * 2, 1);

        // Pozn.: hodiny prehravace v guestovi jdou o 0,0153 % rychleji -
        // Windows programuji milisekundovy timer PIT delickou 1193 misto
        // 1193,182. Zmereno (0,015271 %) i predpovezeno z delicky
        // (0,015253 %). **Nenapodobujeme to**: registrovym proudem to
        // nehne (parovani je podle poradi a rovnomerna zmena rychlosti
        // preskaluje noty i ohyby stejne) a prehravac by kvuli tomu hral
        // rychleji, nez MIDI predepisuje.
        double ticksPerSample = TicksPerSecond() / static_cast<double>(sampleRate);
        m_currentTick += ticksPerSample;
    }
}
