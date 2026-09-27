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
        if (m_triggerMute && ev.data1 == 119 && ev.data2 == 0)
            synth.ControlChange(ev.channel, 11, 0);
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
    case MidiEventType::ChannelPressure:
        synth.ChannelPressure(ev.channel, ev.data1);
        break;
    case MidiEventType::PolyPressure:
    case MidiEventType::EndOfTrack:
    default:
        // TODO: PolyPressure is not used by the synth yet
        break;
    }
}

void Sequencer::RenderBlock(Synth& synth, int16_t* out, uint32_t numFrames, uint32_t sampleRate)
{
    for (uint32_t frame = 0; frame < numFrames; ++frame)
    {
        // Send out all events whose time has come before this frame is
        // rendered - the order within one tick comes from the stable sort
        // in the parser (e.g. a CC before a Note On).
        while (m_nextEventIndex < m_sequence.events.size() &&
               static_cast<double>(m_sequence.events[m_nextEventIndex].absoluteTick) <= m_currentTick)
        {
            DispatchEvent(synth, m_sequence.events[m_nextEventIndex]);
            m_nextEventIndex++;
        }

        synth.RenderBlock(out + frame * 2, 1);

        // Note: the player's clock in the guest runs 0.0153 % fast - Windows
        // programs the millisecond PIT timer with divisor 1193 instead of
        // 1193.182. Measured (0.015271 %) and predicted from the divisor
        // (0.015253 %). **We do not imitate it**: it does not move the
        // register stream (pairing goes by order, and a uniform speed change
        // rescales notes and bends alike), and the player would then play
        // faster than the MIDI file says.
        double ticksPerSample = TicksPerSecond() / static_cast<double>(sampleRate);
        m_currentTick += ticksPerSample;
    }
}
