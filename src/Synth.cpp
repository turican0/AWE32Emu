#include "Synth.h"

#include <algorithm>
#include <cmath>

using Emu8000::Reg;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Delka nahradni tabulky vzorku. Kratsi tabulka = vyssi zakladni
    // frekvence, coz je potreba proto, ze pitch registr EMU8000 umi
    // prirustek jen do +2 oktav nad jednotkou (0xE000..0xFFFF).
    constexpr uint32_t kDefaultWaveLen = 64;

    // Zapis do registru DCYSUSV pri Note Off: bit 15 = release faze.
    uint16_t MakeDcysusv(int sustain, int rate)
    {
        return static_cast<uint16_t>(((sustain & 0x7F) << 8) | (rate & 0x7F));
    }

    uint16_t MakeAtkhld(int hold, int attack)
    {
        return static_cast<uint16_t>(((hold & 0x7F) << 8) | (attack & 0x7F));
    }

    // Utlum v dB -> jednotky registru IFATN (0.375 dB na jednotku).
    int AttenDbToUnits(double db)
    {
        return std::clamp(static_cast<int>(std::lround(db / 0.375)), 0, 255);
    }
}

Synth::Synth(uint32_t sampleRate)
    : m_core(sampleRate)
{
    BuildDefaultWaveform();
}

// ---------------------------------------------------------------------------
// Nahradni vzorek v DRAM
//
// Dokud neni nactena SoundFont/SBK banka, nahrajeme do emulovane zvukove
// pameti jednu periodu sinusovky a hrajeme ji ve smycce. Neni to "zvuk
// AWE32", ale prochazi to celou skutecnou cestou cipu (adresa v CCCA,
// smycka v PSST/CSL, pitch v IP, obalka, filtr, pan), takze az se sem
// napoji realna banka, meni se jen obsah DRAM a hodnoty registru.
// ---------------------------------------------------------------------------
void Synth::BuildDefaultWaveform()
{
    // Par vzorku navic za smyckou kvuli linearni interpolaci.
    m_core.ResizeDram(kDefaultWaveLen + 8);

    int16_t* dram = m_core.DramData();
    for (uint32_t i = 0; i < kDefaultWaveLen + 8; ++i)
    {
        const double phase = 2.0 * kPi * (i % kDefaultWaveLen) / kDefaultWaveLen;
        dram[i] = static_cast<int16_t>(std::sin(phase) * 30000.0);
    }

    m_defaultSample.start     = Emu8000::kDramOffset;
    m_defaultSample.loopStart = Emu8000::kDramOffset;
    m_defaultSample.loopEnd   = Emu8000::kDramOffset + kDefaultWaveLen;
    m_defaultSample.unityFreqHz =
        static_cast<double>(Emu8000Core::kNativeSampleRate) / kDefaultWaveLen;
}

// ---------------------------------------------------------------------------
// Prevody MIDI -> registry
// ---------------------------------------------------------------------------

uint16_t Synth::ComputePitch(uint8_t channel, uint8_t note) const
{
    const ChannelState& ch = m_channels[channel];

    const double bendSemitones =
        (ch.pitchBend / 8192.0) * ch.pitchBendRangeSemitones;
    const double freqHz = 440.0 * std::pow(2.0, (note - 69 + bendSemitones) / 12.0);

    // IP: 0xE000 = jednotkovy prirustek, 4096 jednotek na oktavu.
    const double octaves = std::log2(freqHz / m_defaultSample.unityFreqHz);
    const double ip = static_cast<double>(Emu8000::kPitchUnity)
                    + octaves * Emu8000::kPitchPerOctave;

    return static_cast<uint16_t>(std::clamp(ip, 0.0, 65535.0));
}

uint16_t Synth::ComputeIfatn(uint8_t channel, uint8_t velocity) const
{
    const ChannelState& ch = m_channels[channel];

    // Velocity, CC7 a CC11 se skladaji multiplikativne. Prevod na dB je
    // ten obvykly kvadraticky (amplituda ~ (v/127)^2), tj. -40*log10.
    const double v = std::max(1, static_cast<int>(velocity)) / 127.0;
    const double vol = std::max(1, static_cast<int>(ch.volume)) / 127.0;
    const double expr = std::max(1, static_cast<int>(ch.expression)) / 127.0;

    const double db = -40.0 * std::log10(v * vol * expr);

    // Horni bajt = pocatecni mezni kmitocet filtru; nahradni patch ma filtr
    // plne otevreny (0xFF), aby zvuk nemenil, dokud ho banka nenastavi.
    return static_cast<uint16_t>((0xFF << 8) | AttenDbToUnits(db));
}

// ---------------------------------------------------------------------------
// Sprava hlasu
// ---------------------------------------------------------------------------

int Synth::AllocateVoice()
{
    for (int i = 0; i < kUsableVoices; ++i)
        if (!m_alloc[i].inUse)
            return i;

    // Vsechny hlasy obsazene. Prioritne bereme ty, ktere uz nikdo nedrzi
    // (jen sustain pedal), pak nejstarsi. Skutecny EMU8000 ma vlastni
    // prioritni schema - tohle je zatim rozumna nahrada.
    int best = 0;
    uint32_t bestAge = 0xFFFFFFFFu;
    bool foundSustained = false;

    for (int i = 0; i < kUsableVoices; ++i)
    {
        const bool sustained = m_alloc[i].heldBySustain;
        if (foundSustained && !sustained) continue;
        if (!foundSustained && sustained) { foundSustained = true; bestAge = 0xFFFFFFFFu; }
        if (m_alloc[i].age < bestAge) { bestAge = m_alloc[i].age; best = i; }
    }

    KillVoice(best);
    return best;
}

void Synth::ProgramVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity)
{
    const ChannelState& ch = m_channels[channel];
    const SampleRegion& smp = m_defaultSample;

    // Poradi zapisu odpovida tomu, co dela ovladac: nejdriv adresy a
    // parametry, obalky, a DCYSUSV az uplne nakonec - ten spousti
    // envelope engine, tedy samotnou notu.

    // Pan (PSST bity 31..24). Pozor na orientaci: v EMU8000 je 0 = zcela
    // vpravo a 0xFF = zcela vlevo, tedy opacne nez MIDI CC10.
    const uint32_t pan = static_cast<uint32_t>(255 - std::clamp(ch.pan * 2, 0, 255));
    m_core.Write(Reg::PSST, voice,
                 (pan << Emu8000::kPanShift) | (smp.loopStart & Emu8000::kLoopAddressMask));

    // Chorus send (CSL bity 31..24) + konec smycky
    const uint32_t chorus = static_cast<uint32_t>(std::clamp(ch.chorusSend * 2, 0, 255));
    m_core.Write(Reg::CSL, voice,
                 (chorus << Emu8000::kChorusShift) | (smp.loopEnd & Emu8000::kLoopAddressMask));

    // Filter Q (bity 31..28) + pocatecni adresa
    m_core.Write(Reg::CCCA, voice, smp.start & Emu8000::kCccaAddressMask);

    // Pitch target + reverb send
    const uint16_t pitch = ComputePitch(channel, note);
    const uint32_t reverb = static_cast<uint32_t>(std::clamp(ch.reverbSend * 2, 0, 255));
    m_core.Write(Reg::PTRX, voice,
                 (static_cast<uint32_t>(pitch) << 16) | (reverb << Emu8000::kReverbShift));

    m_core.Write(Reg::IP,      voice, pitch);
    m_core.Write(Reg::IFATN,   voice, ComputeIfatn(channel, velocity));
    m_core.Write(Reg::PEFE,    voice, 0);
    m_core.Write(Reg::FMMOD,   voice, 0);
    m_core.Write(Reg::TREMFRQ, voice, 0);
    m_core.Write(Reg::FM2FRQ2, voice, 0);

    // Stejne jako SBAWE32.DRV pri note-on: hlasitost 0, filtr plne otevreny.
    // Envelope engine si obe pulky vzapeti prepise sam.
    m_core.Write(Reg::CVCF, voice, 0x0000FFFFu);
    m_core.Write(Reg::VTFT, voice, 0x0000FFFFu);

    // Modulacni obalka - nahradni patch ji nepouziva.
    m_core.Write(Reg::ENVVAL,  voice, 0x8000);
    m_core.Write(Reg::ATKHLD,  voice, MakeAtkhld(0x7F, 0x7F));
    m_core.Write(Reg::DCYSUS,  voice, MakeDcysusv(0x7F, 0));
    m_core.Write(Reg::LFO1VAL, voice, 0x8000);
    m_core.Write(Reg::LFO2VAL, voice, 0x8000);

    // Volume obalka: bez zpozdeni, rychly nabeh, bez hold, plny sustain.
    m_core.Write(Reg::ENVVOL,  voice, 0x8000);
    m_core.Write(Reg::ATKHLDV, voice, MakeAtkhld(0x7F, 0x7F));

    // Az ted se spousti nota.
    m_core.Write(Reg::DCYSUSV, voice, MakeDcysusv(0x7F, 0));
}

void Synth::ReleaseVoice(int voice)
{
    // Release faze: bit 15 + release rate. Ovladac na realne karte tady
    // prepise DCYSUSV, protoze tentyz registr slouzi pro decay i release.
    m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvRelease | 0x40);
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
}

void Synth::KillVoice(int voice)
{
    m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvOff);
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
}

// ---------------------------------------------------------------------------
// MIDI rozhrani
// ---------------------------------------------------------------------------

void Synth::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (channel >= 16 || note > 127) return;
    if (velocity == 0) { NoteOff(channel, note); return; }

    const int voice = AllocateVoice();
    m_alloc[voice] = { true, false, channel, note, velocity, ++m_ageCounter };
    ProgramVoice(voice, channel, note, velocity);
}

void Synth::NoteOff(uint8_t channel, uint8_t note)
{
    if (channel >= 16) return;

    for (int i = 0; i < kUsableVoices; ++i)
    {
        VoiceAlloc& a = m_alloc[i];
        if (!a.inUse || a.heldBySustain) continue;
        if (a.channel != channel || a.note != note) continue;

        if (m_channels[channel].sustain)
            a.heldBySustain = true;   // pedal drzi notu, uvolni ji az CC64 off
        else
            ReleaseVoice(i);
    }
}

void Synth::ProgramChange(uint8_t channel, uint8_t program)
{
    if (channel >= 16) return;
    m_channels[channel].program = program;
    // Vyber patche z banky prijde se SoundFont vrstvou; nahradni sinusovy
    // vzorek je pro vsechny programy stejny.
}

void Synth::RefreshChannel(uint8_t channel)
{
    for (int i = 0; i < kUsableVoices; ++i)
    {
        const VoiceAlloc& a = m_alloc[i];
        if ((!a.inUse && !a.heldBySustain) || a.channel != channel) continue;

        const uint16_t pitch = ComputePitch(channel, a.note);
        m_core.Write(Reg::IP, i, pitch);

        const uint32_t ptrx = m_core.Read(Reg::PTRX, i);
        m_core.Write(Reg::PTRX, i, (static_cast<uint32_t>(pitch) << 16) | (ptrx & 0xFFFFu));

        m_core.Write(Reg::IFATN, i, ComputeIfatn(channel, a.velocity));

        const uint32_t pan = static_cast<uint32_t>(255 - std::clamp(m_channels[channel].pan * 2, 0, 255));
        const uint32_t psst = m_core.Read(Reg::PSST, i);
        m_core.Write(Reg::PSST, i,
                     (pan << Emu8000::kPanShift) | (psst & Emu8000::kLoopAddressMask));
    }
}

void Synth::ControlChange(uint8_t channel, uint8_t controller, uint8_t value)
{
    if (channel >= 16) return;
    ChannelState& ch = m_channels[channel];

    switch (controller)
    {
    case 7:  ch.volume = value;     RefreshChannel(channel); break;
    case 10: ch.pan = value;        RefreshChannel(channel); break;
    case 11: ch.expression = value; RefreshChannel(channel); break;
    case 91: ch.reverbSend = value; break;
    case 93: ch.chorusSend = value; break;

    case 64: // sustain pedal
    {
        const bool wasOn = ch.sustain;
        ch.sustain = value >= 64;
        if (wasOn && !ch.sustain)
        {
            for (int i = 0; i < kUsableVoices; ++i)
                if (m_alloc[i].heldBySustain && m_alloc[i].channel == channel)
                    ReleaseVoice(i);
        }
        break;
    }

    case 120: // All Sound Off
        for (int i = 0; i < kUsableVoices; ++i)
            if (m_alloc[i].channel == channel) KillVoice(i);
        break;

    case 123: // All Notes Off
        for (int i = 0; i < kUsableVoices; ++i)
            if (m_alloc[i].inUse && m_alloc[i].channel == channel) ReleaseVoice(i);
        break;

    default:
        // Dalsi controllery (modulation 1, RPN/NRPN pro pitch bend range)
        // zatim neresime.
        break;
    }
}

void Synth::PitchBend(uint8_t channel, int16_t value)
{
    if (channel >= 16) return;
    m_channels[channel].pitchBend = value;
    RefreshChannel(channel);
}

void Synth::RenderBlock(int16_t* out, uint32_t numFrames)
{
    m_core.RenderBlock(out, numFrames);
}
