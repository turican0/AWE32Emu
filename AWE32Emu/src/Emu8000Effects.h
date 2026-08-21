#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Chorus a reverb EMU8000.
//
// POZOR na rozsah teto casti: smerovani je registrove presne (send kazdeho
// hlasu se bere z PTRX bity 15..8 pro reverb a z CSL bity 31..24 pro chorus,
// pred panoramou - viz signalovy diagram v Programmer's Guide), ale samotny
// ALGORITMUS obou efektu presny neni a byt nemuze:
//
// Efektovy procesor EMU8000 je pevna funkce konfigurovana poli INIT1..INIT4.
// Ta pole jsou syrove koeficienty vnitrni DSP site, ne pojmenovane parametry,
// a jejich prehrani do emulace nedava smysl (viz docs/re-notes, sekce 4).
// Struktura te site se z ovladacu vycist neda - ovladace ji jen nakrmi daty.
//
// Implementace nize je tedy standardni chorus (modulovana zpozdovaci linka)
// a reverb (hrebenove + allpass filtry) s parametry, ktere odpovidaji
// dokumentovanemu chovani presetu AWE32. Je to zamerne oddelene, aby bylo
// jasne, co je odvozene z hardwaru a co je nahrada.
// ---------------------------------------------------------------------------

namespace Emu8000Fx
{
    // Modulovana zpozdovaci linka - jeden hlas chorusu.
    class ChorusVoice
    {
    public:
        void Init(uint32_t sampleRate, double baseDelayMs, double depthMs,
                  double rateHz, double phase)
        {
            m_sr = sampleRate;
            m_base = baseDelayMs * sampleRate / 1000.0;
            m_depth = depthMs * sampleRate / 1000.0;
            m_rate = rateHz;
            m_phase = phase;
            m_line.assign(static_cast<size_t>(m_base + m_depth) + 4, 0.0f);
            m_pos = 0;
        }

        float Process(float in, float feedback)
        {
            if (m_line.empty()) return 0.0f;

            const double delay = m_base + m_depth * std::sin(m_phase);
            m_phase += 2.0 * 3.14159265358979323846 * m_rate / m_sr;
            if (m_phase > 2.0 * 3.14159265358979323846)
                m_phase -= 2.0 * 3.14159265358979323846;

            // linearni interpolace v zpozdovaci lince
            const double read = static_cast<double>(m_pos) - delay;
            const double wrapped = read < 0 ? read + m_line.size() : read;
            const size_t i0 = static_cast<size_t>(wrapped) % m_line.size();
            const size_t i1 = (i0 + 1) % m_line.size();
            const float f = static_cast<float>(wrapped - std::floor(wrapped));
            const float out = m_line[i0] + (m_line[i1] - m_line[i0]) * f;

            m_line[m_pos] = in + out * feedback;
            m_pos = (m_pos + 1) % m_line.size();
            return out;
        }

    private:
        std::vector<float> m_line;
        size_t m_pos = 0;
        uint32_t m_sr = 44100;
        double m_base = 0.0, m_depth = 0.0, m_rate = 0.0, m_phase = 0.0;
    };

    // Hrebenovy filtr s tlumenim vysokych kmitoctu.
    class Comb
    {
    public:
        void Init(size_t samples) { m_line.assign(samples ? samples : 1, 0.0f); m_pos = 0; m_store = 0.0f; }
        float Process(float in, float feedback, float damp)
        {
            const float out = m_line[m_pos];
            m_store = out * (1.0f - damp) + m_store * damp;
            m_line[m_pos] = in + m_store * feedback;
            m_pos = (m_pos + 1) % m_line.size();
            return out;
        }
    private:
        std::vector<float> m_line;
        size_t m_pos = 0;
        float m_store = 0.0f;
    };

    class AllPass
    {
    public:
        void Init(size_t samples) { m_line.assign(samples ? samples : 1, 0.0f); m_pos = 0; }
        float Process(float in, float gain)
        {
            const float buf = m_line[m_pos];
            const float out = -in + buf;
            m_line[m_pos] = in + buf * gain;
            m_pos = (m_pos + 1) % m_line.size();
            return out;
        }
    private:
        std::vector<float> m_line;
        size_t m_pos = 0;
    };

    // ----------------------------------------------------------------------
    // Chorus - parametry prevzate z tabulky presetu v SBAWE32.DRV (ds:0x19A2,
    // 8 presetu po 7 slovech, vychozi je cislo 2). Format zaznamu:
    //
    //   word 0  feedback      0xE600..0xE6FF, uroven ve spodnim bajtu
    //   word 1  delay_offset  ve vzorcich pri 44100 Hz
    //   word 2  lfo_depth     0xBC00..0xBCFF, hloubka ve spodnim bajtu
    //   word 3-4 delay        (dword, jde do HWCF4)
    //   word 5-6 lfo_freq     (dword, jde do HWCF5)
    //
    // Ze `lfo_freq` presetu 2 je 0x83 se da overit, ze AWEUTIL pouziva prave
    // tenhle preset - zapisuje HWCF5 = 0x83. Pomery frekvenci napric presety
    // (109 : 380 : 131 : 91 : 38) sedi na dokumentovane rychlosti
    // Chorus 1 / Chorus 2 / Chorus 3 / Feedback / Flanger, z cehoz vychazi
    // jednotka zhruba 0.0073 Hz.
    // ----------------------------------------------------------------------
    struct ChorusPreset
    {
        uint16_t feedback;      // spodni bajt = uroven zpetne vazby
        uint16_t delaySamples;  // zpozdeni ve vzorcich pri 44100 Hz
        uint16_t depth;         // spodni bajt = hloubka modulace
        uint32_t lfoFreq;       // jednotka ~0.0073 Hz
    };

    // Presne hodnoty z ds:0x19A2 v SBAWE32.DRV.
    inline constexpr ChorusPreset kChorusPresets[8] = {
        { 0xE600, 0x03F6, 0xBC2C, 0x006D },   // 0 Chorus 1
        { 0xE608, 0x031A, 0xBC6E, 0x017C },   // 1 Chorus 2
        { 0xE610, 0x031A, 0xBC84, 0x0083 },   // 2 Chorus 3  (vychozi)
        { 0xE620, 0x0269, 0xBC6E, 0x017C },   // 3 Chorus 4
        { 0xE680, 0x04D3, 0xBCA6, 0x005B },   // 4 Feedback
        { 0xE6E0, 0x044E, 0xBC37, 0x0026 },   // 5 Flanger
        { 0xE600, 0x0B06, 0xBC00, 0x0083 },   // 6 Short Delay
        { 0xE6C0, 0x0B06, 0xBC00, 0x0083 },   // 7 Short Delay + FB
    };
    inline constexpr int kChorusDefault = 2;
    inline constexpr int kReverbDefault = 4;   // Hall 2, viz SBAWE32.DRV 0x612D

    class Chorus
    {
    public:
        void Init(uint32_t sampleRate, int preset = kChorusDefault)
        {
            m_sampleRate = sampleRate;
            SetPreset(preset);
        }

        void SetPreset(int preset)
        {
            const ChorusPreset& p = kChorusPresets[std::clamp(preset, 0, 7)];

            const double delayMs = p.delaySamples * 1000.0 / 44100.0;
            const double depthMs = (p.depth & 0xFF) / 255.0 * 6.0;   // [?] rozsah
            const double rateHz = p.lfoFreq * 0.0073;
            m_feedback = (p.feedback & 0xFF) / 255.0f;

            // Druhy hlas o pul periody posunuty, aby byl vysledek siroky.
            m_voices[0].Init(m_sampleRate, delayMs, depthMs, rateHz, 0.0);
            m_voices[1].Init(m_sampleRate, delayMs * 1.4, depthMs, rateHz * 0.8, 3.14159);
        }

        void Process(float in, float& outL, float& outR)
        {
            const float a = m_voices[0].Process(in, m_feedback);
            const float b = m_voices[1].Process(in, m_feedback);
            outL = a * 0.7f + b * 0.3f;
            outR = a * 0.3f + b * 0.7f;
        }

    private:
        ChorusVoice m_voices[2];
        uint32_t m_sampleRate = 44100;
        float m_feedback = 0.06f;
    };

    // ----------------------------------------------------------------------
    // Reverb: osm hrebenovych filtru a ctyri allpass na kanal.
    // Delky jsou prvocisla v okoli klasickych hodnot, prepocitane na
    // vzorkovaci kmitocet cipu.
    // ----------------------------------------------------------------------
    class Reverb
    {
    public:
        void Init(uint32_t sampleRate)
        {
            static const int kComb[8]   = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int kAllPass[4] = { 556, 441, 341, 225 };
            const double scale = sampleRate / 44100.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const int spread = ch ? 23 : 0;   // rozprostreni pravého kanalu
                for (int i = 0; i < 8; ++i)
                    m_comb[ch][i].Init(static_cast<size_t>((kComb[i] + spread) * scale));
                for (int i = 0; i < 4; ++i)
                    m_ap[ch][i].Init(static_cast<size_t>((kAllPass[i] + spread) * scale));
            }
        }

        void Process(float in, float& outL, float& outR)
        {
            // POZOR na zisk: hrebenovy filtr se zpetnou vazbou f ma
            // stejnosmerne zesileni 1/(1-f). Pri f = 0.854 je to 6.85x,
            // takze bez vstupniho skalovani reverb nekolikanasobne zesiluje
            // a vystup klipuje. Skalovanim (1-f) se prumerny zisk banky
            // hrebenovych filtru srovna na jednicku a o mnozstvi efektu
            // pak rozhoduje jen send a navratova uroven.
            const float scaled = in * m_inputGain;

            float acc[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < 2; ++ch)
            {
                for (int i = 0; i < 8; ++i)
                    acc[ch] += m_comb[ch][i].Process(scaled, m_feedback, m_damp);
                acc[ch] *= 0.125f;
                for (int i = 0; i < 4; ++i)
                    acc[ch] = m_ap[ch][i].Process(acc[ch], 0.5f);
            }
            outL = acc[0];
            outR = acc[1];
        }

        // roomSize 0..1 (delka dozvuku), damp 0..1 (tlumeni vysokych)
        void SetRoom(float roomSize, float damp)
        {
            m_feedback = 0.7f + std::clamp(roomSize, 0.0f, 1.0f) * 0.28f;
            m_damp = std::clamp(damp, 0.0f, 1.0f);
            m_inputGain = 1.0f - m_feedback;
        }

        // Preset 0..7 podle tabulky v SBAWE32.DRV (ds:0x1A12, 8 zaznamu po
        // 28 slovech, vychozi je cislo 4). Tech 28 slov jsou koeficienty
        // vnitrni DSP site cipu - nedaji se prelozit na topologii, takze
        // z indexu odvozujeme jen velikost prostoru a tlumeni. Poradi
        // odpovida standardni sade AWE32.
        void SetPreset(int preset)
        {
            struct Room { float size, damp; };
            static const Room kRooms[8] = {
                { 0.30f, 0.55f },   // 0 Room 1
                { 0.42f, 0.50f },   // 1 Room 2
                { 0.54f, 0.45f },   // 2 Room 3
                { 0.70f, 0.35f },   // 3 Hall 1
                { 0.82f, 0.30f },   // 4 Hall 2   (vychozi)
                { 0.66f, 0.25f },   // 5 Plate
                { 0.50f, 0.60f },   // 6 Delay
                { 0.50f, 0.60f },   // 7 Panning Delay
            };
            const Room& r = kRooms[std::clamp(preset, 0, 7)];
            SetRoom(r.size, r.damp);
        }

    private:
        Comb m_comb[2][8];
        AllPass m_ap[2][4];
        float m_feedback = 0.84f;
        float m_damp = 0.35f;
        float m_inputGain = 0.16f;   // = 1 - m_feedback, viz Process()
    };
}
