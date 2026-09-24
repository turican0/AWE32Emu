#include "Synth.h"
#include <cstdio>
#include "Awe32Curves.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

using Emu8000::Reg;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Delka nahradni tabulky vzorku (pouziva se, kdyz neni nactena banka).
    constexpr uint32_t kDefaultWaveLen = 64;

    // Prevod logaritmicke vysky (registr IP) na linearni prirustek, ktery
    // ovladac zapisuje do horni pulky PTRX a CPF.
    //
    // Prepis z SBAWE.VXD, objekt 1, 0x212E. Pocita 2^(ip/4096) v pevne radove
    // carce: celociselna cast dava posun, tri nejvyssi bity zlomku pridavaji
    // 2^(1/2), 2^(1/4) a 2^(1/8) pres zlomky s jmenovatelem 10000. Nakonec
    // nasobeni 1,25 pres `esi += esi >> 2`.
    //
    //   0x102E/0x2710 = 0,41420   (2^0.5  - 1 = 0,41421)
    //   0x764 /0x2710 = 0,18920   (2^0.25 - 1 = 0,18921)
    //   0x389 /0x2710 = 0,09050   (2^0.125- 1 = 0,09051)
    uint32_t PitchIncrement(uint16_t ip)
    {
        if (ip == 0xFFFF) return 0xFFFF;

        uint32_t v = 1u << (ip >> 12);
        if (ip & 0x800) v += v * 0x102Eu / 0x2710u;
        if (ip & 0x400) v += v * 0x0764u / 0x2710u;
        if (ip & 0x200) v += v * 0x0389u / 0x2710u;
        v += v >> 2;
        return (v > 0xFFFFu) ? 0xFFFFu : v;
    }

    // AWE32 DOS SDK, midieng noteOn 0x115D..0x11EB. The same steps as
    // PitchIncrement, but in 16-bit code: `imul word` multiplies only the
    // low word of the running value (signed) and `idiv word` gives a 16-bit
    // quotient; the last step is *1.5 (`sar dx,1 / rcr ax,1 / add`) where
    // the VXD has *1.25.
    uint32_t PitchIncrementSdk(uint16_t ip)
    {
        if (ip == 0xFFFF) return 0xFFFF;

        int32_t v = 1 << (ip >> 12);
        auto step = [&v](int32_t k)
        {
            const int32_t prod = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF)) * k;
            v += static_cast<int16_t>(prod / 10000);
        };
        if (ip & 0x800) step(0x102E);
        if (ip & 0x400) step(0x0764);
        if (ip & 0x200) step(0x0389);
        v += v >> 1;
        return ((static_cast<uint32_t>(v) >> 16) != 0) ? 0xFFFFu
                                                       : (static_cast<uint32_t>(v) & 0xFFFFu);
    }

    uint16_t MakeDcysusv(int sustain, int rate)
    {
        return static_cast<uint16_t>(((sustain & 0x7F) << 8) | (rate & 0x7F));
    }
    uint16_t MakeAtkhld(int hold, int attack)
    {
        return static_cast<uint16_t>(((hold & 0x7F) << 8) | (attack & 0x7F));
    }
    int AttenDbToUnits(double db)
    {
        return std::clamp(static_cast<int>(std::lround(db / Emu8000::kAttenDbPerStep)), 0, 255);
    }
}

Synth::Synth(uint32_t sampleRate)
    : m_core(sampleRate)
{
    BuildDefaultWaveform();
}

// ---------------------------------------------------------------------------
// Nahradni vzorek v DRAM - jedna perioda sinusovky ve smycce. Prochazi celou
// skutecnou cestou cipu, jen misto realnych vzorku hraje sinus.
// ---------------------------------------------------------------------------
void Synth::BuildDefaultWaveform()
{
    if (m_core.DramSize() < kDefaultWaveLen + 8)
        m_core.ResizeDram(kDefaultWaveLen + 8);   // par vzorku navic pro interpolaci

    int16_t* dram = m_core.DramData();
    for (uint32_t i = 0; i < kDefaultWaveLen + 8; ++i)
    {
        const double phase = 2.0 * kPi * (i % kDefaultWaveLen) / kDefaultWaveLen;
        dram[i] = static_cast<int16_t>(std::sin(phase) * 30000.0);
    }

    m_fallbackStart     = Emu8000::kDramOffset;
    m_fallbackLoopStart = Emu8000::kDramOffset;
    m_fallbackLoopEnd   = Emu8000::kDramOffset + kDefaultWaveLen;
    m_fallbackUnityHz   = static_cast<double>(Emu8000Core::kNativeSampleRate) / kDefaultWaveLen;
}

// ---------------------------------------------------------------------------
// Nacitani zvukovych dat
// ---------------------------------------------------------------------------

bool Synth::LoadWaveRom(const std::string& path, std::string& error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Nelze otevrit ROM: " + path; return false; }

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    if (raw.size() < 2) { error = "ROM je prazdna: " + path; return false; }

    // Surovy dump = 16bit little-endian vzorky. Textova hlavicka ROM je
    // ulozena po slovech (proto vypada prohozene), ale vzorkova data se
    // ctou primo - viz docs/re-notes/rom_vs_sf2.md.
    std::vector<int16_t> rom(raw.size() / 2);
    std::memcpy(rom.data(), raw.data(), rom.size() * 2);

    // The widespread awe32.raw carries one extra word (0x1234) in front of
    // the ROM. A dump taken from a real AWE32 card with AWEDUMP (2026-09-14,
    // AWE32EmuData/rom/awe32rom.bin) starts with 0x0032 and equals awe32.raw
    // shifted by exactly one word (0 differing words out of 524 287).
    // Detect the extra word the same way 86Box does (emu8k_init: "1M" "GM"
    // at words 3 and 4 instead of 2 and 3) and drop it, so address A reads
    // the same word as on the card.
    if (rom.size() > 4 && static_cast<uint16_t>(rom[3]) == 0x314D
                       && static_cast<uint16_t>(rom[4]) == 0x474D)
    {
        rom.erase(rom.begin());
        rom.push_back(0);
    }

    m_core.LoadWaveRom(std::move(rom));
    return true;
}

bool Synth::LoadBank(const std::string& path, std::string& error, bool samplesInRom,
                     int midiBank)
{
    SoundFont::Bank bank = SoundFont::Load(path);
    if (!bank.valid) { error = bank.errorMessage; return false; }
    bank.samplesInRom = samplesInRom;

    if (midiBank >= 0)
        for (SoundFont::Preset& p : bank.presets)
            if (p.bank == 0) p.bank = midiBank;

    if (m_nextDramBase == 0)
    {
        const uint32_t reserve =
            (Awe32::IsDosLike(m_core.DriverVariant())) ? kDramReserveDos
                                                           : kDramReserveWin95;
        m_nextDramBase = Emu8000::kDramOffset + reserve;
    }

    LoadedBank lb;
    lb.dramBase = m_nextDramBase;

    if (!samplesInRom && !bank.sampleData.empty())
    {
        // Vzorky banky jdou do DRAM za uz nactene banky - presne jak to dela
        // ovladac, kdyz nahrava uzivatelskou banku do RAM karty.
        const size_t offset = lb.dramBase - Emu8000::kDramOffset;
        const size_t needed = offset + bank.sampleData.size() + 8;
        if (m_core.DramSize() < needed)
        {
            std::vector<int16_t> keep(m_core.DramData(), m_core.DramData() + m_core.DramSize());
            m_core.ResizeDram(needed);
            std::memcpy(m_core.DramData(), keep.data(), keep.size() * sizeof(int16_t));
        }
        std::memcpy(m_core.DramData() + offset, bank.sampleData.data(),
                    bank.sampleData.size() * sizeof(int16_t));

        // The words the driver reserves in front of the first bank are zero
        // on the card (uploads in dos_mdi.trace / bank_synth02s.trace). Our
        // fallback sine used to sit there, and SF1 addresses may point into
        // the reserve: in the MC2 intro every ch2 note starts at 0x200004 and
        // played 46 words of that full-scale sine before its sample. The
        // fallback waveform is only needed when no bank is loaded at all.
        const size_t reserveWords =
            (Awe32::IsDosLike(m_core.DriverVariant())) ? kDramReserveDos : kDramReserveWin95;
        if (offset == reserveWords)
            std::fill(m_core.DramData(), m_core.DramData() + offset, int16_t{0});

        // The Creative drivers do not upload SoundFont 1.0 samples as stored.
        // Every own sample is filtered on the way to the card:
        //   y[n] = clip16(floor((6*x[n] - x[n-1] - x[n+1]) / d))
        // d = 4 for the DOS driver (SBAWE32.MDI), d = 8 for the Win95 driver;
        // the zero padding between samples stays zero. It is a pre-emphasis
        // (unity at DC, +6 dB at Nyquist) - the ROM samples already carry it.
        // Measured on the uploads of the real drivers in 86Box traces:
        //   dos_mdi.trace (Magic Carpet 2, BULLFROG.SBK): bit exact over
        //     [start, end) of every own sample
        //   bank_synth02s.trace (Win95, SYNTH02S.SBK): same kernel with d = 8,
        //     applied over [start, end - 3) - word end - 3 stays as stored
        // Without it every DRAM sound was duller than on the card; the MC2
        // intro sounded wrong everywhere except the belltree from ROM.
        if (bank.version == SoundFont::Version::Sf1)
        {
            const bool dos = (Awe32::IsDosLike(m_core.DriverVariant()));
            const int shift = dos ? 2 : 3;
            const int32_t round = (1 << shift) - 1;
            const int16_t* src = bank.sampleData.data();
            const size_t count = bank.sampleData.size();
            int16_t* dst = m_core.DramData() + offset;
            for (const SoundFont::Sample& s : bank.samples)
            {
                if (s.inRom || s.end <= s.start) continue;
                const size_t endExcl = dos ? s.end : ((s.end >= s.start + 3) ? s.end - 3 : s.start);
                const size_t first = std::min<size_t>(s.start, count);
                const size_t last  = std::min<size_t>(endExcl, count);
                for (size_t i = first; i < last; ++i)
                {
                    const int32_t xm = (i > 0) ? src[i - 1] : 0;
                    const int32_t xp = (i + 1 < count) ? src[i + 1] : 0;
                    const int32_t num = 6 * static_cast<int32_t>(src[i]) - xm - xp;
                    const int32_t y = (num >= 0) ? (num >> shift) : -((-num + round) >> shift);
                    dst[i] = static_cast<int16_t>(std::clamp(y, -32768, 32767));
                }
            }
        }
        m_nextDramBase += static_cast<uint32_t>(bank.sampleData.size() + 8);
    }

    lb.bank = std::make_unique<SoundFont::Bank>(std::move(bank));
    m_banks.push_back(std::move(lb));
    return true;
}

// ---------------------------------------------------------------------------
// Sprava hlasu
// ---------------------------------------------------------------------------

int Synth::AllocateVoice()
{
    for (int i = 0; i < kUsableVoices; ++i)
        if (!m_alloc[i].inUse && !m_alloc[i].heldBySustain && !m_core.IsVoiceActive(i))
            return i;
    for (int i = 0; i < kUsableVoices; ++i)
        if (!m_alloc[i].inUse && !m_alloc[i].heldBySustain)
            return i;

    // Vse obsazene - vzit nejstarsi, prednostne ten drzeny jen pedalem.
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

void Synth::ReleaseVoice(int voice)
{
    if (Awe32::IsDosLike(m_core.DriverVariant()))
    {
        NoteOffMdi(voice);
        return;
    }
    m_core.Write(Reg::DCYSUSV, voice,
                 Emu8000::kDcysusvRelease | (m_alloc[voice].releaseRate & 0x7F));
    // `SBAWE.VXD` uvolnuje **obe** obalky - hned za DCYSUSV posila DCYSUS
    // s vlastni rychlosti (ReleaseModEnv). Zmereno v georg_win95.trace,
    // kde dvojice DCYSUSV 8029 / DCYSUS 8027 stoji u kazdeho note-offu;
    // odtud i dvojnasobny pocet zapisu do DCYSUS v census u trace_diff.
    // `SBAWE32.MDI` to nedela.
    if (m_core.DriverVariant() == Awe32::Driver::Win95)
    {
        m_core.Write(Reg::DCYSUS, voice,
                     Emu8000::kDcysusvRelease | (m_alloc[voice].releaseModRate & 0x7F));
        // SBAWE.VXD 0xC0FFA252: loop behind the sample end, CSL = end + 4,
        // PSST = end; the voice counts as finished once CCCA passes the end.
        const uint32_t end = m_alloc[voice].vxdNoteEnd;
        if (end != 0)
        {
            const uint32_t csl = m_core.ReadDriver(Reg::CSL, voice);
            m_core.Write(Reg::CSL, voice, (csl & 0xFF000000u) | (end + 4));
            const uint32_t psst = m_core.ReadDriver(Reg::PSST, voice);
            m_core.Write(Reg::PSST, voice, (psst & 0xFF000000u) | end);
            m_alloc[voice].vxdEndAddr = end;
        }
    }
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
    m_alloc[voice].vxdState = 0xFFE;
    m_alloc[voice].vxdRom = false;
}

void Synth::KillVoice(int voice)
{
    m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvOff);
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
    m_alloc[voice].vxdState = 0xFFE;
    m_alloc[voice].vxdRom = false;
}

// ---------------------------------------------------------------------------
// SBAWE32.MDI (family `dos`) voice handling, transcribed from the driver:
// allocation 0x1872, note-off 0x19BE / 0x238A, sustain 0x2576, all notes off
// 0x277C, controller reset 0x2732, volume/expression update 0x2482 / 0x2554,
// modulation update 0x25D6, channel pressure 0x2AAC, pitch bend 0x2AD8.
// The allocation reads the chip (VTFT, DCYSUSV, CCCA), so which voice it
// takes depends on the chip state, as on the card.
// ---------------------------------------------------------------------------

namespace
{
    // IP + bend offset the way SBAWE32.MDI adds them (0x20E0, 0x2B51): above
    // 0xFFFF the result is capped, a negative result wraps to 16 bits.
    uint16_t MdiAddBend(int ip, int offset)
    {
        const int32_t sum = static_cast<int32_t>(static_cast<uint16_t>(ip)) + offset;
        if (sum < 0) return static_cast<uint16_t>(sum);
        return static_cast<uint16_t>(std::min<int32_t>(sum, 0xFFFF));
    }

    // IP + bend offset in the SDK. The bend handler (AWE32PITCHBEND 0x1C4E)
    // clamps to 0..0xFFFF; the note-on (noteOn 0x0C61) only tests the high
    // word of the 32-bit sum, so a negative result becomes 0xFFFF as well.
    uint16_t SdkAddBend(int ip, int offset, bool noteOn)
    {
        const int32_t sum = static_cast<int32_t>(static_cast<uint16_t>(ip)) + offset;
        if (sum > 0xFFFF) return 0xFFFF;
        if (sum < 0) return noteOn ? 0xFFFF : 0;
        return static_cast<uint16_t>(sum);
    }
}

int Synth::AllocateVoiceMdi(uint16_t state)
{
    // Even voices first, then odd ones (voices 30/31 belong to the driver).
    // Score = volume target + 0x200 if assigned + 0x300 if held by the pedal
    // + 0x1000 if not in release. The lowest score wins and a later voice
    // wins a tie; score 0 is taken at once, and so is a voice whose playback
    // address has passed the end set by a note-off loop opening.
    uint32_t best = 0xFFFFFFFFu;
    int chosen = kUsableVoices - 1;
    bool done = false;
    for (int pass = 0; pass < 2 && !done; ++pass)
    {
        for (int v = pass; v < kUsableVoices; v += 2)
        {
            const VoiceAlloc& a = m_alloc[v];
            const uint16_t s = a.mdiState;
            if (s == 0xFFFF || s < 0x1000)
            {
                uint32_t score = m_core.ReadDriver(Reg::VTFT, v) >> 16;
                if (s != 0xFFFF)
                {
                    score += 0x200;
                    if ((s & 0xFF) == 0xFF) score += 0x300;
                }
                if (!(m_core.ReadDriver(Reg::DCYSUSV, v) & Emu8000::kDcysusvRelease))
                    score += 0x1000;
                if (score <= best)
                {
                    best = score;
                    chosen = v;
                    if (score == 0) { done = true; break; }
                }
            }
            if (a.mdiEndAddr != 0
                && (m_core.ReadDriver(Reg::CCCA, v) & Emu8000::kCccaAddressMask) >= a.mdiEndAddr)
            {
                chosen = v;
                done = true;
                break;
            }
        }
    }

    VoiceAlloc& a = m_alloc[chosen];
    a.mdiState = state;
    a.mdiEndAddr = 0;
    a.inUse = false;
    a.heldBySustain = false;
    m_core.Write(Reg::DCYSUSV, chosen, 0x807Fu);
    return chosen;
}

// ---------------------------------------------------------------------------
// AWE32 DOS SDK (family `sdk`) voice allocation, __AWE32ALLOCGCHANNEL in
// midieng.c (RAWE32L.LIB, offset 0x0478). A sibling of the MDI one with
// different details: the release test and the pedal bonus apply only to an
// assigned voice, a free voice is taken at once only when silent, the
// EARLIER voice wins a tie, and when nothing qualifies the note gets no
// voice (-1). The taken voice gets DCYSUSV 0x0080 and VTFT 0x0000FFFF.
// DOSMid trace: voices 0, 2, 4 ... first, DCYSUSV 0080 + VTFT FFFF.
// ---------------------------------------------------------------------------

int Synth::AllocateVoiceSdk(uint16_t state)
{
    uint32_t best = 0xFFFFFFFFu;
    int chosen = -1;
    bool done = false;
    for (int pass = 0; pass < 2 && !done; ++pass)
    {
        for (int v = pass; v < kUsableVoices; v += 2)
        {
            const VoiceAlloc& a = m_alloc[v];
            const uint16_t s = a.mdiState;
            if (s == 0xFFFF || s < 0x1000)
            {
                uint32_t score = m_core.ReadDriver(Reg::VTFT, v) >> 16;
                if (s == 0xFFFF)
                {
                    if (score == 0) { chosen = v; done = true; break; }
                }
                else
                {
                    score += 0x200;
                    if ((s & 0xFF) == 0xFF)
                        score += 0x300;
                    else if (!(m_core.ReadDriver(Reg::DCYSUSV, v) & Emu8000::kDcysusvRelease))
                        score += 0x1300;
                }
                if (score < best) { best = score; chosen = v; }
            }
            if (a.mdiEndAddr != 0
                && (m_core.ReadDriver(Reg::CCCA, v) & Emu8000::kCccaAddressMask) >= a.mdiEndAddr)
            {
                chosen = v;
                done = true;
                break;
            }
        }
    }
    if (chosen < 0) return -1;

    VoiceAlloc& a = m_alloc[chosen];
    a.mdiState = state;
    a.mdiEndAddr = 0;
    a.inUse = false;
    a.heldBySustain = false;
    m_core.Write(Reg::DCYSUSV, chosen, 0x0080u);
    m_core.Write(Reg::VTFT, chosen, 0x0000FFFFu);
    return chosen;
}

// ---------------------------------------------------------------------------
// SBAWE.VXD (family `win95`) voice allocation, transcribed from 0xC0FF9C68.
// ---------------------------------------------------------------------------

int Synth::AllocateVoiceVxd(uint16_t state)
{
    // Three passes over the voices in steps of three (0, 3, .. 30, then 1, 4,
    // .. 31, then 2, 5, .. 29) - that is the order the driver takes them in
    // (georg_win95.trace). Score of a voice: its volume target, plus 0x200
    // when it is assigned (0x1200 when bit 12 of the state is set), plus
    // 0x300 when it is held by the pedal, otherwise plus 0x1300 when its
    // volume envelope is not in release. The lowest score wins, a later voice
    // wins a tie, a free voice that is already silent is taken at once, and so
    // is a voice whose playback address has passed the end of its sample.
    // While at most two ROM voices are playing, ROM voices are not taken.
    // The flag means "playing a ROM sample now" - it is cleared when the voice
    // is released; kept after the note it made every voice of a ROM bank
    // untouchable and all notes of Georgia after 18 s fell on voice 0.
    int romPlaying = 0;
    for (int v = 0; v < kVxdVoices; ++v)
        if (m_alloc[v].vxdRom && m_alloc[v].vxdState != 0xFFE) ++romPlaying;
    const bool protectRom = (romPlaying <= 2);

    uint32_t best = 0xFFFFFFFFu;
    int cand = -1;
    int chosen = -1;
    for (int pass = 0; pass < 3 && chosen < 0; ++pass)
    {
        for (int v = pass; v < kVxdVoices; v += 3)
        {
            const VoiceAlloc& a = m_alloc[v];
            if (protectRom && a.vxdRom) continue;
            const uint16_t s = a.vxdState;
            if (s < 0x2000)
            {
                uint32_t score = m_core.ReadDriver(Reg::VTFT, v) >> 16;
                bool take = false;
                if (s == 0xFFE)
                {
                    take = (score == 0);
                }
                else
                {
                    score += (s & 0x1000) ? 0x1200u : 0x200u;
                    if ((s & 0xFF) == 0xFF)
                        score += 0x300u;
                    else if (!(m_core.ReadDriver(Reg::DCYSUSV, v) & Emu8000::kDcysusvRelease))
                        score += 0x1300u;
                }
                if (take) { chosen = v; break; }
                if (score <= best) { best = score; cand = v; }
            }
            if (a.vxdEndAddr != 0
                && (m_core.ReadDriver(Reg::CCCA, v) & Emu8000::kCccaAddressMask) >= a.vxdEndAddr)
            {
                chosen = v;
                break;
            }
        }
    }
    if (chosen < 0) chosen = (cand >= 0) ? cand : 0;

    VoiceAlloc& a = m_alloc[chosen];
    a.vxdState = state;
    a.vxdEndAddr = 0;
    a.vxdRom = false;
    a.inUse = false;
    a.heldBySustain = false;
    m_core.Write(Reg::DCYSUSV, chosen, 0x00FFu);
    return chosen;
}

// SBAWE.VXD CC10 (0xC0FFC3F1): every voice of the channel gets a new target
// pan, the same formula and limits as at note-on, and moves towards it one
// register step at a time (0xC0FFC347): the first step at once, the next ones
// from the driver's deferred-call queue. The queue runs every ~5.15 ms -
// relax_win95.trace: 227 frames between the pan steps of a voice in 9669
// cases (189 in the next most common). Each step writes the pan byte of PSST
// and its complement (0xFF for pan 0) to the low byte of PTRX.
namespace { constexpr int32_t kVxdPanStepFrames = 227; }

void Synth::PanVxd(uint8_t channel, uint8_t value)
{
    for (int v = 0; v < kVxdVoices; ++v)
    {
        VoiceAlloc& a = m_alloc[v];
        if ((a.vxdState >> 8) != channel) continue;
        int pan = 0x17F - 2 * (a.vxdPatchPan + static_cast<int>(value));
        if (pan >= 0xFE) pan = 0xFF;
        else if (pan <= 1) pan = 0;
        a.vxdPanTarget = static_cast<uint8_t>(pan);
        PanStepVxd(v);
    }
}

void Synth::PanStepVxd(int voice)
{
    VoiceAlloc& a = m_alloc[voice];
    a.vxdPanTimer = -1;
    if (a.vxdState >= 0x2000 || a.vxdPanCur == a.vxdPanTarget) return;
    a.vxdPanCur = static_cast<uint8_t>(a.vxdPanCur + (a.vxdPanTarget > a.vxdPanCur ? 1 : -1));
    const uint32_t psst = m_core.ReadDriver(Reg::PSST, voice);
    m_core.Write(Reg::PSST, voice, (static_cast<uint32_t>(a.vxdPanCur) << 24) | (psst & 0x00FFFFFFu));
    const uint32_t aux = a.vxdPanCur ? static_cast<uint32_t>((256 - a.vxdPanCur) & 0xFF) : 0xFFu;
    const uint32_t ptrx = m_core.ReadDriver(Reg::PTRX, voice);
    m_core.Write(Reg::PTRX, voice, (ptrx & 0xFFFFFF00u) | aux);
    if (a.vxdPanCur != a.vxdPanTarget) a.vxdPanTimer = kVxdPanStepFrames;
}

// SBAWE.VXD CC1 (0xC0FFC650): FMMOD pitch depth of every voice of the
// channel = patch depth + CC1 / 30 + pressure / 30, capped at 0x7F from
// above; the low byte (filter depth) is read back and kept.
void Synth::UpdateFmmodVxd(uint8_t channel)
{
    const ChannelState& ch = m_channels[channel];
    for (int v = 0; v < kVxdVoices; ++v)
    {
        const VoiceAlloc& a = m_alloc[v];
        if ((a.vxdState >> 8) != channel) continue;
        const int depth = std::min(a.vxdFmmodDepth + ch.modWheel / 30 + ch.vxdPressureDiv30, 0x7F);
        const uint16_t fmmod = static_cast<uint16_t>(m_core.ReadDriver(Reg::FMMOD, v));
        m_core.Write(Reg::FMMOD, v, static_cast<uint16_t>(((depth & 0xFF) << 8) | (fmmod & 0xFF)));
    }
}

// SBAWE.VXD CC7 / CC11 (0xC0FFC47F): the attenuation of every voice of the
// channel is recomputed with the note-on formula (ComputeAttenuationVxd, +16
// for a "1MGM" ROM sample) and written to the low byte of IFATN.
void Synth::UpdateAttenVxd(uint8_t channel)
{
    const ChannelState& ch = m_channels[channel];
    for (int v = 0; v < kVxdVoices; ++v)
    {
        const VoiceAlloc& a = m_alloc[v];
        if ((a.vxdState >> 8) != channel) continue;
        int atten = Awe32Curves::ComputeAttenuation(
            EffectiveChannelVolume(ch.volume), a.velocity, ch.expression,
            a.vxdPatchAtten, Awe32::Driver::Win95);
        if (atten < 255 && a.vxdRom1mgm) atten = std::min(atten + 16, 255);
        const uint16_t ifatn = static_cast<uint16_t>(m_core.ReadDriver(Reg::IFATN, v));
        m_core.Write(Reg::IFATN, v, static_cast<uint16_t>((ifatn & 0xFF00) | (atten & 0xFF)));
    }
}

void Synth::NoteOffMdi(int voice)
{
    // 0x19BE: release both envelopes, modulation first. For a looped sample
    // with a tail after the loop the loop is moved behind the sample end, so
    // the tail plays out; the voice counts as finished once CCCA passes it.
    VoiceAlloc& a = m_alloc[voice];
    a.mdiState = 0xFF00;
    m_core.Write(Reg::DCYSUS,  voice, 0x8000u | a.mdiModRelease);
    m_core.Write(Reg::DCYSUSV, voice, 0x8000u | a.mdiVolRelease);
    if (a.mdiNoteEnd != 0)
    {
        const uint32_t csl = m_core.ReadDriver(Reg::CSL, voice);
        m_core.Write(Reg::CSL, voice, (csl & 0xFF000000u) | (a.mdiNoteEnd + 4));
        const uint32_t psst = m_core.ReadDriver(Reg::PSST, voice);
        m_core.Write(Reg::PSST, voice, (psst & 0xFF000000u) | a.mdiNoteEnd);
        a.mdiEndAddr = a.mdiNoteEnd;
    }
    else
    {
        a.mdiEndAddr = 0;
    }
    a.mdiState = 0xFFFF;
    a.inUse = false;
    a.heldBySustain = false;
}

void Synth::UpdateAttenMdi(uint8_t channel)
{
    // 0x2482 (CC7) / 0x2554 (CC11). The formula is not the note-on one: the
    // patch attenuation enters as * 25 / 80 before the * 8 / 3. The voice test
    // only compares the low nibble of the state's high byte, so free voices
    // (0xFFFF) are rewritten as well when the channel is 15.
    const ChannelState& ch = m_channels[channel];
    const int vol = std::clamp(EffectiveChannelVolume(ch.volume), 0, 127);
    const bool sdk = m_core.DriverVariant() == Awe32::Driver::Sdk;
    for (int v = 0; v < kUsableVoices; ++v)
    {
        const VoiceAlloc& a = m_alloc[v];
        if (((a.mdiState >> 8) & 0x0F) != channel) continue;
        int atten;
        if (vol <= 10)
        {
            atten = 0xFF;
        }
        else
        {
            const uint16_t db = static_cast<uint16_t>(
                Awe32Curves::VelocityDb(a.mdiVelocity, Awe32::Driver::Dos)
                + Awe32Curves::kChannelVolumeDb[vol]);
            const uint16_t patch = static_cast<uint16_t>(
                static_cast<uint16_t>(a.mdiPatchAtten * 0x19) / 0x50);
            const uint16_t sum = static_cast<uint16_t>(patch + db);
            atten = static_cast<uint16_t>(sum << 3) / 3;
            if (sdk)
            {
                // SDK Volume 0x15B2..0x1608: expression from 256 with >> 7,
                // then +16 for a ROM sample (card ROM id "1MGM").
                if (atten > 0xFF)
                    atten = 0xFF;
                else if (ch.expression < 0x7F)
                    atten += static_cast<uint16_t>(Awe32Curves::kExpressionDb[ch.expression] * (0x100 - atten)) >> 7;
                if (a.sdkRom)
                    atten = std::min(atten + 0x10, 0xFF);
            }
            else if (atten >= 0xFF)
                atten = 0xFF;
            else if (ch.expression < 0x7F)
                atten += Awe32Curves::kExpressionDb[ch.expression] * (0xFF - atten) / 0x7F;
        }
        const uint16_t ifatn = static_cast<uint16_t>(m_core.ReadDriver(Reg::IFATN, v));
        m_core.Write(Reg::IFATN, v, static_cast<uint16_t>((ifatn & 0xFF00) | (atten & 0xFF)));
    }
}

void Synth::UpdateFmmodMdi(uint8_t channel, int value)
{
    // 0x25D6: LFO1 -> pitch depth of the playing voices = pressure / 30 +
    // patch depth + value / 30, capped at 0x7F (no lower limit).
    ChannelState& ch = m_channels[channel];
    ch.mdiModDiv30 = static_cast<uint8_t>(value / 30);
    for (int v = 0; v < kUsableVoices; ++v)
    {
        const VoiceAlloc& a = m_alloc[v];
        const int hi = a.mdiState >> 8;
        if (hi == 0xFF || (hi & 0x0F) != channel) continue;
        int depth = ch.mdiPressureDiv30 + a.mdiFmmodDepth + value / 30;
        if (depth > 0x7F) depth = 0x7F;
        const uint16_t fmmod = static_cast<uint16_t>(m_core.ReadDriver(Reg::FMMOD, v));
        m_core.Write(Reg::FMMOD, v,
                     static_cast<uint16_t>((fmmod & 0x00FF) | ((depth << 8) & 0xFF00)));
    }
}

void Synth::PitchBendMdi(uint8_t channel, int16_t value)
{
    // 0x2AD8: the IP offset is computed once per bend event (16-bit multiply,
    // truncating division) and kept in the channel block; a note-on uses the
    // stored offset. A range of 0 counts as 2.
    ChannelState& ch = m_channels[channel];
    ch.pitchBend = value;
    const int range = ch.pitchBendRangeSemitones ? ch.pitchBendRangeSemitones : 2;
    // The SDK (AWE32PITCHBEND 0x1BF8) divides bend * range by 24 - the
    // exact 4096/12 of the VXD - where the MDI multiplies by 0x155 / 0x2000,
    // one step lower at full bend (681 vs 682).
    const bool sdk = m_core.DriverVariant() == Awe32::Driver::Sdk;
    if (sdk)
    {
        ch.mdiBendOffset = static_cast<int16_t>((static_cast<int32_t>(value) * range) / 24);
    }
    else
    {
        const int32_t product = static_cast<int32_t>(value)
            * static_cast<int16_t>(static_cast<uint16_t>(range * 0x155));
        ch.mdiBendOffset = static_cast<int16_t>(product / 0x2000);
    }
    for (int v = 0; v < kUsableVoices; ++v)
    {
        const VoiceAlloc& a = m_alloc[v];
        const int hi = a.mdiState >> 8;
        if (hi == 0xFF || (hi & 0x0F) != channel) continue;
        m_core.Write(Reg::IP, v, sdk ? SdkAddBend(a.basePitch, ch.mdiBendOffset, false)
                                     : MdiAddBend(a.basePitch, ch.mdiBendOffset));
    }
}

void Synth::SustainMdi(uint8_t channel, uint8_t value)
{
    // 0x2576: releasing the pedal ends the voices a note-off marked as held.
    ChannelState& ch = m_channels[channel];
    if (value >= 0x40)
    {
        ch.sustain = true;
        return;
    }
    ch.sustain = false;
    for (int v = 0; v < kUsableVoices; ++v)
    {
        const uint16_t s = m_alloc[v].mdiState;
        if ((s & 0xFF) == 0xFF && (s >> 8) != 0xFF && ((s >> 8) & 0x0F) == channel)
            NoteOffMdi(v);
    }
}

void Synth::AllNotesOffMdi(uint8_t channel, bool respectSustain)
{
    // 0x277C: CC123 keeps pedal-held notes held, CC120 ends everything.
    const bool hold = respectSustain && m_channels[channel].sustain;
    for (int v = 0; v < kUsableVoices; ++v)
    {
        VoiceAlloc& a = m_alloc[v];
        const int hi = a.mdiState >> 8;
        if (hi == 0xFF || (hi & 0x0F) != channel) continue;
        if (hold) a.mdiState |= 0x00FF;
        else      NoteOffMdi(v);
    }
}

void Synth::ResetControllersMdi(uint8_t channel)
{
    // 0x2732 (CC121).
    ChannelState& ch = m_channels[channel];
    ch.mdiRpnMode = false;
    ch.mdiRpnLsb = 0;
    ch.mdiRpnMsb = 0;
    ch.mdiModDiv30 = 0;
    ch.modWheel = 0;
    SustainMdi(channel, 0);
    ch.expression = 0x7F;
    UpdateAttenMdi(channel);
    PitchBendMdi(channel, 0);
    ch.mdiPressureDiv30 = 0;
    UpdateFmmodMdi(channel, ch.mdiModDiv30 * 30);
}

bool Synth::ControlChangeMdi(uint8_t channel, uint8_t controller, uint8_t value)
{
    // Controller dispatcher 0x27DE; bank, pan, reverb and chorus are only
    // stored there, which the generic path does as well.
    ChannelState& ch = m_channels[channel];
    switch (controller)
    {
    case 1:
        ch.modWheel = value;
        UpdateFmmodMdi(channel, value);
        return true;
    case 6:
        if (ch.mdiRpnMode && ch.mdiRpnLsb == 0 && ch.mdiRpnMsb == 0)
            ch.pitchBendRangeSemitones = value;
        return true;
    case 7:
        ch.volume = value;
        UpdateAttenMdi(channel);
        return true;
    case 11:
        ch.expression = value;
        UpdateAttenMdi(channel);
        return true;
    case 64:
        SustainMdi(channel, value);
        return true;
    case 100:
        ch.mdiRpnMode = true;
        ch.mdiRpnLsb = value;
        return true;
    case 101:
        ch.mdiRpnMode = true;
        ch.mdiRpnMsb = value;
        return true;
    case 120:
        AllNotesOffMdi(channel, false);
        return true;
    case 121:
        ResetControllersMdi(channel);
        return true;
    case 123:
        AllNotesOffMdi(channel, true);
        return true;
    default:
        return false;
    }
}

void Synth::ChannelPressure(uint8_t channel, uint8_t value)
{
    if (channel >= 16) return;
    if (m_core.DriverVariant() == Awe32::Driver::Win95)
    {
        // SBAWE.VXD 0xC0FFCEAE: pressure / 30 is stored and the FMMOD depth
        // of the channel recomputed (0xC0FFC650).
        m_channels[channel].vxdPressureDiv30 = static_cast<uint8_t>(value / 30);
        UpdateFmmodVxd(channel);
        return;
    }
    // SBAWE32.MDI 0x2AAC.
    if (!Awe32::IsDosLike(m_core.DriverVariant())) return;
    ChannelState& ch = m_channels[channel];
    ch.mdiPressureDiv30 = static_cast<uint8_t>(value / 30);
    UpdateFmmodMdi(channel, ch.mdiModDiv30 * 30);
}

// SBAWE.VXD 0xC0FFAC08: every voice with the same channel + preset key and
// the same exclusive class is cut (IFATN 0x00FF, DCYSUSV 0x807F), whatever
// its state - playing, released or held by the pedal.
void Synth::KillExclusiveVxd(uint32_t key, uint8_t cls)
{
    for (int v = 0; v < kVxdVoices; ++v)
    {
        VoiceAlloc& a = m_alloc[v];
        if (a.vxdExclKey != key || a.vxdExclClass != cls) continue;
        m_core.Write(Reg::IFATN, v, 0x00FF);
        m_core.Write(Reg::DCYSUSV, v, 0x807F);
        a.inUse = false;
        a.heldBySustain = false;
        a.vxdState = 0xFFE;
        a.vxdRom = false;
        a.vxdExclKey = 0;
        a.vxdExclClass = 0;
    }
}

void Synth::StartLayers(size_t bankIndex, const std::vector<SoundFont::Region>& regions,
                        uint8_t channel, uint8_t note, uint8_t velocity, uint32_t presetId)
{
    const SoundFont::Bank& b = *m_banks[bankIndex].bank;
    std::vector<SoundFont::VoiceParams> vps;
    vps.reserve(regions.size());
    for (const SoundFont::Region& r : regions)
        vps.push_back(SoundFont::MakeVoiceParams(
            b, r, note, velocity, m_banks[bankIndex].dramBase, kRomPoolBase,
            m_core.DriverVariant()));

    if (Awe32::IsDosLike(m_core.DriverVariant()))
    {
        // SBAWE32.MDI 0x1EB2: every layer gets its voice (reserved, 0xFFFE)
        // before any register of the note is written.
        // The SDK does the same (noteOn 0x0BA3) with its own allocation,
        // which may find no voice - that layer is then not played.
        const bool sdk = m_core.DriverVariant() == Awe32::Driver::Sdk;
        std::vector<int> voices;
        voices.reserve(regions.size());
        for (size_t i = 0; i < regions.size(); ++i)
            voices.push_back(sdk ? AllocateVoiceSdk(0xFFFE) : AllocateVoiceMdi(0xFFFE));
        for (size_t i = 0; i < regions.size(); ++i)
            if (voices[i] >= 0)
                StartVoice(voices[i], channel, note, velocity, vps[i], &b, &regions[i]);
        return;
    }

    const bool vxd = m_core.DriverVariant() == Awe32::Driver::Win95;
    // SBAWE.VXD: the exclusive classes of all layers are resolved before
    // the first voice is taken. Key = channel << 24 | bank file, bank,
    // program (the driver keeps channel << 16 | preset).
    const uint32_t exclKey = (static_cast<uint32_t>(channel) << 24) | (presetId & 0xFFFFFF);
    if (vxd)
        for (const SoundFont::Region& r : regions)
        {
            const uint8_t cls = static_cast<uint8_t>(r.gen.Get(SoundFont::Gen::ExclusiveClass, 0));
            if (cls != 0) KillExclusiveVxd(exclKey, cls);
        }

    // Preset muze mit vic vrstev na jednu notu - kazda dostane hlas.
    // Rodina win95 vybira hlas jako SBAWE.VXD (cte pritom cip), ostatni
    // nasim vlastnim pravidlem.
    for (size_t i = 0; i < regions.size(); ++i)
    {
        const int voice = vxd ? AllocateVoiceVxd(0xFFFE) : AllocateVoice();
        StartVoice(voice, channel, note, velocity, vps[i], &b, &regions[i]);
        if (vxd)
        {
            m_alloc[voice].vxdExclKey = exclKey;
            m_alloc[voice].vxdExclClass = static_cast<uint8_t>(
                regions[i].gen.Get(SoundFont::Gen::ExclusiveClass, 0));
        }
    }
}

int Synth::BankNumberFor(uint8_t channel) const
{
    // Kanal 10 (index 9) je podle GM bicí. SoundFont je ma v bance 128.
    if (channel == 9) return kDrumBank;
    return m_channels[channel].bankMsb;
}

int Synth::PitchBendOffset(uint8_t channel) const
{
    const ChannelState& ch = m_channels[channel];
    const long long span =
        static_cast<long long>(ch.pitchBend) * ch.pitchBendRangeSemitones;

    // **Rodiny se lisi v tom, co maji na pulton.** Obe deli celociselne az
    // nakonec a utinaji k nule, ale konstanta je jina:
    //
    //   win95  4096/12 presne:  ohyb * rozsah * 4096 / (8192*12)
    //   dos    341 (utnute):    ohyb * rozsah * 341 / 8192
    //
    // Pro win95 to sedi na jedenacti namerenych bodech (ohyb, rozsah ->
    // presne -> ovladac), z Georgie a RELAXu:
    //
    //    8064,  2 ->  672,000 ->  672     -768, 12 -> -384,000 -> -384
    //   -4729, 12 -> -2364,50 -> -2364    -682, 12 -> -341,000 -> -341
    //    1280, 12 ->  640,000 ->  640     -512, 12 -> -256,000 -> -256
    //   -1280, 12 -> -640,000 -> -640      176, 12 ->   88,000 ->   88
    //   -1312, 12 -> -656,000 -> -656     8191,  2 ->  682,583 ->  682
    //   -6720,  2 -> -560,000 -> -560
    //
    // Pro dos je doklad plny ohyb dolu s rozsahem 12 v intru Magic Carpet 2:
    // ovladac zapsal posun -4092, kdezto 4096/12 by dalo presne -4096.
    // S 341 sedi cele intro na vsech 24 registrech.
    if (Awe32::IsDosLike(m_core.DriverVariant()))
        return static_cast<int>(span * 341 / 8192);
    return static_cast<int>(span * 4096 / (8192LL * 12));
}

// ---------------------------------------------------------------------------
// Spusteni hlasu
// ---------------------------------------------------------------------------

void Synth::StartVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity,
                       const SoundFont::VoiceParams& vp,
                       const SoundFont::Bank* bank, const SoundFont::Region* region)
{
    const ChannelState& ch = m_channels[channel];

    // Utlum: presny prepis vzorce z note-on rutiny SBAWE32.MDI (0x2102),
    // vcetne prevodnich tabulek pro CC7, velocity a CC11. Viz Awe32Curves.h.
    const Awe32::Driver drv = m_core.DriverVariant();
    int atten = Awe32Curves::ComputeAttenuation(
        EffectiveChannelVolume(ch.volume), velocity, ch.expression,
        vp.patchAttenUnits, drv);

    // Kdyz se banka odkazuje na ROM "1MGM", ovladac pricte k utlumu 16
    // jednotek (= 6 dB). Zmereno instrukcni stopou v SBAWE.VXD (objekt 1,
    // 0x1CCB): `cmp dword ptr [edi+0x158E], 0x4D474D31` - to je ASCII "1MGM"
    // pozpatku - a pak `add ecx, 0x10` s oriznutim na 0xFF. Ve stope slo
    // ecx z 0x18 na 0x28 presne tady.
    //
    // Ovladac to jeste podminuje bajtovym priznakem (`cmp byte ptr [eax], 0`),
    // ktery jsme nerozklicovali; u vsech 242 not se vetev provedla.
    // Ten nerozklicovany bajtovy priznak je **"lezi vzorek v ROM?"**.
    // Odhalila to vymena banky v guestu: kdyz se misto SYNTHGM.SBK
    // (popisuje jen ROM) nacte SYNTH02S.SBK (ma vlastni vzorky v DRAM),
    // ovladac tech 16 jednotek **nepricte** - u vsech 242 not MINUETu
    // mel utlum presne o 16 nizsi nez my a cilovy objem proto dvojnasobny
    // (16 jednotek = 6 dB = faktor 2).
    //
    // Dava to smysl i fyzikalne: vzorky ve wave ROM jsou o 6 dB hlasitejsi
    // nez to, co ovladac sam nahraje do DRAM.
    const bool sampleInRom = region && region->sample
                          && (region->sample->inRom || bank->samplesInRom);
    // The SDK has the same rule (noteOn 0x0E82: sample address below
    // 0x200000 and the card's ROM id "1MGM"); the ROM id comes from the card,
    // not from the bank, and every AWE32 has that ROM.
    if (sampleInRom && ((drv == Awe32::Driver::Win95 && bank && bank->romName == "1MGM")
                        || drv == Awe32::Driver::Sdk))
        atten = std::min(atten + 16, 255);

    // Velocity ovlivnuje i mezni kmitocet filtru - tisi noty jsou tmavsi.
    // Prepis z SBAWE32.DRV, offset 0x021E:
    //
    //     if (kanal != 9 && attackRate < 0x7D)
    //         cutoff = (cutoff * max(velocity, 0x46) + 0x40) / 0x7F;
    //
    // Bicí (kanal 9) maji vlastni vetev a filtr se jim takhle neupravuje.
    // **Rodiny se tu lisi** a chvili jsme mely obe stejne (podle VXD),
    // coz DOSu nesedelo:
    //
    //   SBAWE32.DRV 0x021E (dos):    (cutoff * v + 0x40) / 0x7F
    //   SBAWE.VXD   0x1CF6 (win95):  (cutoff * v + 0xA0) >> 7
    //
    // Rozdil je videt jen nahore: pro cutoff 255 a velocity 127 da DOS
    // 255 (32449/127 = 255,5), zatimco VXD 254 (32545/128 = 254,3).
    // Zmereno na dvou notach kanalu 5 v intru Magic Carpet 2 - ovladac
    // mel 0xFF, my 0xFE.
    int cutoff = (vp.ifatn >> 8) & 0xFF;
    const int attackRate = vp.atkhldv & Emu8000::kAtkhldAttackMask;
    if (channel != 9 && attackRate < 0x7D)
    {
        const int v = std::max<int>(velocity, 0x46);
        // The SDK (noteOn 0x0D10) has the VXD form.
        cutoff = (drv == Awe32::Driver::Dos) ? (cutoff * v + 0x40) / 0x7F
                                             : ((cutoff * v + 0xA0) >> 7);
        cutoff = std::clamp(cutoff, 0, 255);
    }

    const uint16_t ifatn = static_cast<uint16_t>((cutoff << 8) | atten);

    // Pan z banky se kombinuje s CC10. CC10 64 = stred; v EMU8000 je
    // 0 = vpravo, takze posun jde opacnym smerem.
    // `SBAWE32.MDI` pocita panoramu jako `0x17F - 2*(chPan + patchPan)`
    // (0x22B6, s oriznutim >= 0xFE -> 0xFF a zaporne -> 0). Zkusili jsme to
    // tak, ale vyslo to **hur** - nase `bankPan` zjevne neodpovida jeho poli
    // `[si+0x22]`. Puvodni prevod sedi u 233 z 270 not, ten "presny" u zadne,
    // takze zustava tenhle, dokud se nedohleda, co je `[si+0x22]` zac.
    // Pan: `0x17F - 2*(panBanky + CC10)` s dvema mezemi. Prepis z obou
    // ovladacu, ktere ho maji doslova stejny az na spodni mez:
    //   SBAWE32.MDI 0x22B6:  ax = 0x17F; cx = [bx+6] + [si+0x22]; cx += cx;
    //                        ax -= cx;  if (ax >= 0xFE) ax = 0xFF;
    //                        if (zaporne) ax = 0
    //   SBAWE.VXD obj 1, 0x4253: tentyz vzorec, jen spodni mez je
    //                        `if (ax <= 1) ax = 0`
    // Drive se tu scitala uz hotova registrova hodnota z banky s posunem
    // od CC10; vyslo to stejne jen proto, ze v obou merenych bankach
    // generator `pan` chybi a vychozich 64 dava tentyz vysledek.
    int pan = 0x17F - 2 * (vp.patchPan + static_cast<int>(ch.pan));
    if (pan >= 0xFE) pan = 0xFF;
    else if (pan < (drv == Awe32::Driver::Dos ? 0 : 2)) pan = 0;   // SDK 0x0F49 = VXD
    const uint32_t psst = (static_cast<uint32_t>(pan) << Emu8000::kPanShift)
                        | (vp.psst & Emu8000::kLoopAddressMask);

    // Chorus a reverb: kanalovou hodnotu z CC93/CC91 ovladac nejdriv
    // **zmensi na 90 %** a teprve pak k ni pricte hodnotu z banky
    // (oriznuto na 255). Skalovani maji obe rodiny doslova stejne:
    //
    //   SBAWE32.MDI 0x242A (reverb) a 0x244C (chorus):
    //       mov ax, 0x5a; mul si; mov cx, 0x64; div cx  -> [kanal+4] / [+5]
    //   SBAWE.VXD obj 1, 0x314D (reverb) a 0x3174 (chorus):
    //       imul eax, eax, 0x5a; mov ecx, 0x64; div ecx -> [kanal+0x447] / [+0x448]
    //
    // Souctovy tvar je odecteny z MDI (0x2290 reverb, 0x230A chorus:
    // `al = [bx+4]; add ax, [si+0x20]` a oriznuti na 0xFF). U VXD ho
    // nemame primo z kodu - MINUET.MID zadne CC91 ani CC93 neposila, takze
    // to mereni nerozhodne - ale ulozena hodnota je tam pripravena stejne.
    const int chChorus = ch.chorusSend * 90 / 100;
    const int chorus = std::clamp(
        static_cast<int>((vp.csl >> Emu8000::kChorusShift) & 0xFF) + chChorus, 0, 255);
    const uint32_t csl = (static_cast<uint32_t>(chorus) << Emu8000::kChorusShift)
                       | (vp.csl & Emu8000::kLoopAddressMask);
    const int chReverb = ch.reverbSend * 90 / 100;
    const int reverb = std::clamp(static_cast<int>(vp.reverbSend) + chReverb, 0, 255);

    // `dos`: SBAWE32.MDI adds the offset stored at the last bend event
    // (see PitchBendMdi), capped above and wrapping below.
    const int pitch = (drv == Awe32::Driver::Sdk) ? SdkAddBend(vp.ip, ch.mdiBendOffset, true)
        : (drv == Awe32::Driver::Dos) ? MdiAddBend(vp.ip, ch.mdiBendOffset)
        : std::clamp(vp.ip + PitchBendOffset(channel), 0, 65535);

    // Modulacni kolecko pridava hloubku LFO1 na vysku. `SBAWE.VXD` obsluha
    // CC1 (0x34A4) deli hodnotu **tricetkou** a vysledek pricita k hloubce
    // z patche; soucet se orizne na 0x7F a jde do horniho bajtu FMMOD:
    //
    //     mov ecx, 0x1E / div ecx      ; CC1 / 30 -> 0..4
    //     add ebp, edx                 ; + hloubka z patche
    //     cmp ebp, 0x7F / shl ebp, 8
    //
    // Zmereno na RELAXu: ovladac mel 01, 02 a 04 tam, kde jsme meli nulu.
    // `dos` (SBAWE32.MDI 0x2224): CC1 / 30 + channel pressure / 30 + patch
    // depth, capped at 0x7F from above only.
    // `win95` (SBAWE.VXD 0xC0FFB1D5 / 0xC0FFBE0B): CC1 / 30 + pressure / 30
    // + patch depth, capped above; the low byte is ORed in sign-extended, so
    // a negative filter depth turns the high byte into 0xFF.
    const int modDepth = (Awe32::IsDosLike(drv))
        ? std::min(static_cast<int>(static_cast<int8_t>((vp.fmmod >> 8) & 0xFF))
                       + ch.mdiModDiv30 + ch.mdiPressureDiv30, 0x7F)
        : std::clamp(
              static_cast<int>(static_cast<int8_t>((vp.fmmod >> 8) & 0xFF))
                  + ch.modWheel / 30 + ch.vxdPressureDiv30, -128, 0x7F);
    uint16_t fmmod = static_cast<uint16_t>(
        ((static_cast<uint8_t>(modDepth)) << 8) | (vp.fmmod & 0xFF));
    if (drv == Awe32::Driver::Win95 && (vp.fmmod & 0x80))
        fmmod |= 0xFF00;

    const uint32_t reverbByte =
        static_cast<uint32_t>(std::clamp(reverb, 0, 255)) << Emu8000::kReverbShift;
    // Spodni bajt PTRX je doplnkova panorama. `SBAWE.VXD` tam dava 256 - pan
    // (pri pan 0x7F zapisuje 0x81), zmereno v poli hlasu na +0x24.
    // `SBAWE32.MDI` tam nechava **nulu** - zmereno na 341 notach z Magic
    // Carpet 2, kde PSST nese pan 0x0F a PTRX ma spodni bajt 0x00.
    // The SDK (noteOn 0x0F5B: pan ? -pan : 0xFF) has the same byte.
    const uint32_t panAux = (drv != Awe32::Driver::Dos)
        ? static_cast<uint32_t>(std::clamp(256 - pan, 0, 255))
        : 0u;
    // Pocatecni adresa se posouva o konstantu zavislou na rodine ovladace.
    const uint32_t ccca = (vp.ccca & ~Emu8000::kCccaAddressMask)
        | ((vp.sampleStart - Awe32::StartAddressOffset(drv))
           & Emu8000::kCccaAddressMask);

    if (drv == Awe32::Driver::Win95)
    {
        // Presny sled zapisu `SBAWE.VXD`, odectený z `georg_win95.trace`
        // (`tests/voice_seq.py --note N`). Overeno, ze je u vsech not a hlasu
        // stejny - viz docs/re-notes/driver_note_on.md, "Cely sled zapisu".
        //
        // Horni pulka PTRX (a stejne tak CPF) neni logaritmicke IP, ale
        // **linearni prirustek** - prepis z SBAWE.VXD (objekt 1, 0x212E).
        // `SBAWE.VXD` 0x2099 (modulacni) a 0x219C (volume): kdyz je attack
        // na maximu a zaroven neni delay, posle se na port konstanta
        // 0xBFFF misto spocitaneho delay. V bloku parametru pritom zustava
        // puvodni hodnota, takze ji nemenime ani my.
        const bool volInstant = (vp.atkhldv & 0x7F) == 0x7F && vp.envvol >= 0x8000;
        const bool modInstant = (vp.atkhld  & 0x7F) == 0x7F && vp.envval >= 0x8000;
        const uint32_t envvolReg = volInstant ? 0xBFFFu : vp.envvol;
        const uint32_t envvalReg = modInstant ? 0xBFFFu : vp.envval;
        const uint32_t increment = PitchIncrement(static_cast<uint16_t>(pitch));
        const uint32_t filterTarget = static_cast<uint32_t>(cutoff) << 8;

        // Umlceni: ovladac pise 0x00FF, ne 0x0080. VTFT jde dvakrat.
        m_core.Write(Reg::DCYSUSV, voice, 0x00FFu);
        m_core.Write(Reg::VTFT,    voice, 0x0000FFFFu);
        m_core.Write(Reg::VTFT,    voice, 0x0000FFFFu);
        m_core.Write(Reg::CVCF,    voice, 0x0000FFFFu);

        m_core.Write(Reg::ATKHLDV, voice, vp.atkhldv);
        m_core.Write(Reg::LFO1VAL, voice, vp.lfo1val);
        m_core.Write(Reg::ATKHLD,  voice, vp.atkhld);
        m_core.Write(Reg::DCYSUS,  voice, vp.dcysus);
        m_core.Write(Reg::LFO2VAL, voice, vp.lfo2val);
        m_core.Write(Reg::IP,      voice, static_cast<uint16_t>(pitch));
        m_core.Write(Reg::IFATN,   voice, ifatn);
        m_core.Write(Reg::PEFE,    voice, vp.pefe);
        m_core.Write(Reg::FMMOD,   voice, fmmod);
        m_core.Write(Reg::TREMFRQ, voice, vp.tremfrq);
        m_core.Write(Reg::FM2FRQ2, voice, vp.fm2frq2);
        m_core.Write(Reg::ENVVAL,  voice, envvalReg);
        m_core.Write(Reg::ENVVOL,  voice, envvolReg);

        // Vynulovani pred adresami; teprve na konci se sem daji prave hodnoty.
        m_core.Write(Reg::PTRX, voice, 0u);
        m_core.Write(Reg::CPF,  voice, 0u);
        m_core.Write(Reg::PSST, voice, psst);
        m_core.Write(Reg::CSL,  voice, csl);
        // CCCA dvakrat: nejdriv bez Q v hornim bajtu, pak s nim. Kdyz je Q
        // nula, jsou oba zapisy stejne - to ve stope taky sedi.
        m_core.Write(Reg::CCCA, voice, ccca & 0x00FFFFFFu);
        // Z1/Z2 (Data0 registry 5 a 4) ovladac u kazde noty nuluje. Co presne
        // znamenaji, nevime; 86Box je drzi jen jako ulozene slovo.
        m_core.Write(Reg::Unk0088, voice, 0u);   // Z1
        m_core.Write(Reg::Unk0080, voice, 0u);   // Z2
        m_core.Write(Reg::CCCA, voice, ccca);

        // Kdyz obalka nema attack ani delay, ovladac hlas nerozjizdi od nuly,
        // ale rovnou mu nastavi cilovy objem - do horni pulky VTFT i CVCF.
        // Podminka i tabulka jsou z `SBAWE.VXD` 0x219C..0x21EF, viz
        // Awe32Curves.h. Zmereno na 844 notach Georgie.
        const uint32_t volTarget =
            volInstant ? Awe32Curves::VolumeTarget(atten) : 0u;
        const uint32_t vtft = (volTarget << 16) | filterTarget;
        m_core.Write(Reg::VTFT, voice, vtft);
        m_core.Write(Reg::CVCF, voice, vtft);
        m_core.Write(Reg::PTRX, voice, (increment << 16) | reverbByte | panAux);
        m_core.Write(Reg::CPF,  voice, increment << 16);
    }
    else if (drv == Awe32::Driver::Sdk)
    {
        // AWE32 DOS SDK, midieng noteOn 0x0FB3..0x1349 - the VXD sequence
        // without the leading DCYSUSV/VTFT/CVCF (the allocation wrote
        // DCYSUSV 0x0080 and VTFT already) and with CVCF after ENVVOL.
        // Instant attack (0x10CF / 0x11EE): envelope delay 0xBFFF, and for
        // the modulation envelope the targets already include it - pitch
        // += PEFE pitch << 4 (16-bit, overflow -> 0xFFFF), filter = cutoff
        // + PEFE filter clamped to 0..255.
        const bool volInstant = (vp.atkhldv & 0x7F) == 0x7F && vp.envvol >= 0x8000;
        const bool modInstant = (vp.atkhld  & 0x7F) == 0x7F && vp.envval >= 0x8000;
        const uint32_t envvolReg = volInstant ? 0xBFFFu : vp.envvol;
        const uint32_t envvalReg = modInstant ? 0xBFFFu : vp.envval;

        uint32_t targetPitch = static_cast<uint32_t>(pitch) & 0xFFFFu;
        int filter = cutoff;
        if (modInstant)
        {
            const int16_t depth = static_cast<int16_t>(
                static_cast<int16_t>(static_cast<int8_t>((vp.pefe >> 8) & 0xFF)) << 4);
            uint32_t lo = static_cast<uint32_t>(static_cast<uint16_t>(depth)) + targetPitch;
            const uint32_t hi = ((depth < 0) ? 0xFFFFu : 0u) + (lo >> 16);
            lo &= 0xFFFFu;
            targetPitch = ((hi & 0xFFFFu) != 0) ? 0xFFFFu : lo;
            filter = std::clamp(cutoff + static_cast<int8_t>(vp.pefe & 0xFF), 0, 255);
        }
        const uint32_t increment = PitchIncrementSdk(static_cast<uint16_t>(targetPitch));
        const uint32_t volTarget = volInstant ? Awe32Curves::VolumeTarget(atten) : 0u;
        const uint32_t vtft = (volTarget << 16) | (static_cast<uint32_t>(filter) << 8);

        m_core.Write(Reg::VTFT,    voice, 0x0000FFFFu);
        m_core.Write(Reg::ATKHLDV, voice, vp.atkhldv);
        m_core.Write(Reg::LFO1VAL, voice, vp.lfo1val);
        m_core.Write(Reg::ATKHLD,  voice, vp.atkhld);
        m_core.Write(Reg::DCYSUS,  voice, vp.dcysus);
        m_core.Write(Reg::LFO2VAL, voice, vp.lfo2val);
        m_core.Write(Reg::IP,      voice, static_cast<uint16_t>(pitch));
        m_core.Write(Reg::IFATN,   voice, ifatn);
        m_core.Write(Reg::PEFE,    voice, vp.pefe);
        m_core.Write(Reg::FMMOD,   voice, fmmod);
        m_core.Write(Reg::TREMFRQ, voice, vp.tremfrq);
        m_core.Write(Reg::FM2FRQ2, voice, vp.fm2frq2);
        m_core.Write(Reg::ENVVAL,  voice, envvalReg);
        m_core.Write(Reg::ENVVOL,  voice, envvolReg);
        m_core.Write(Reg::CVCF,    voice, 0x0000FFFFu);
        m_core.Write(Reg::PTRX,    voice, 0u);
        m_core.Write(Reg::CPF,     voice, 0u);
        m_core.Write(Reg::PSST,    voice, psst);
        m_core.Write(Reg::CSL,     voice, csl);
        m_core.Write(Reg::CCCA,    voice, ccca & 0x00FFFFFFu);
        m_core.Write(Reg::Unk0088, voice, 0u);   // Z1
        m_core.Write(Reg::Unk0080, voice, 0u);   // Z2
        m_core.Write(Reg::CCCA,    voice, ccca);
        m_core.Write(Reg::VTFT,    voice, vtft);
        m_core.Write(Reg::CVCF,    voice, vtft);
        m_core.Write(Reg::PTRX,    voice, (increment << 16) | reverbByte | panAux);
        m_core.Write(Reg::CPF,     voice, increment << 16);
    }
    else
    {
        // `SBAWE32.MDI` note-on 0x1F12..0x2363, write order as in the driver
        // (confirmed on the game trace dos_mdi.trace, 260 notes of the MC2
        // intro): unit pitch, voice off, volume target 0, envelopes, pitch and
        // modulation, PTRX read-modify-write (the chip has already derived the
        // pitch target from IP, only the reverb byte is replaced), addresses.
        m_core.Write(Reg::IP,      voice, 0xE000u);
        m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvOff);
        m_core.Write(Reg::VTFT,    voice, 0x0000FFFFu);
        m_core.Write(Reg::ENVVOL,  voice, vp.envvol);
        m_core.Write(Reg::ATKHLDV, voice, vp.atkhldv);
        m_core.Write(Reg::ENVVAL,  voice, vp.envval);
        m_core.Write(Reg::ATKHLD,  voice, vp.atkhld);
        m_core.Write(Reg::DCYSUS,  voice, vp.dcysus);
        m_core.Write(Reg::IP,      voice, static_cast<uint16_t>(pitch));
        m_core.Write(Reg::IFATN,   voice, ifatn);
        m_core.Write(Reg::LFO1VAL, voice, vp.lfo1val);
        m_core.Write(Reg::LFO2VAL, voice, vp.lfo2val);
        m_core.Write(Reg::PEFE,    voice, vp.pefe);
        m_core.Write(Reg::FMMOD,   voice, fmmod);
        m_core.Write(Reg::TREMFRQ, voice, vp.tremfrq);
        m_core.Write(Reg::FM2FRQ2, voice, vp.fm2frq2);
        const uint32_t cur = m_core.ReadDriver(Reg::PTRX, voice);
        m_core.Write(Reg::PTRX, voice, (cur & 0xFFFF00FFu) | reverbByte);
        m_core.Write(Reg::PSST, voice, psst);
        m_core.Write(Reg::CSL,  voice, csl);
        m_core.Write(Reg::CCCA, voice, ccca);
    }

    m_core.Write(Reg::DCYSUSV, voice, vp.dcysusv);   // spousti notu

    VoiceAlloc& a = m_alloc[voice];
    a = VoiceAlloc{};
    a.inUse = true;
    a.channel = channel;
    a.note = note;
    a.velocity = velocity;
    a.releaseRate = vp.releaseRate ? vp.releaseRate : 0x40;
    a.releaseModRate = vp.releaseModRate;
    a.age = ++m_ageCounter;
    a.basePitch = vp.ip;
    if (Awe32::IsDosLike(drv))
    {
        // Voice block of SBAWE32.MDI (0x20A4..0x20DD, 0x2366). A looped
        // sample with at least 0x14 words after the loop end gets a note-off
        // loop opening at its end (0x2006..0x2060).
        a.mdiState      = static_cast<uint16_t>((channel << 8) | note);
        a.mdiVelocity   = velocity;
        a.mdiPatchAtten = vp.patchAttenUnits;
        a.sdkRom        = sampleInRom;
        a.mdiModRelease = vp.releaseModRate;
        a.mdiVolRelease = vp.releaseRate;
        a.mdiFmmodDepth = static_cast<int8_t>((vp.fmmod >> 8) & 0xFF);
        a.mdiNoteEnd    = (vp.looping && (vp.sampleEndAddr - vp.loopEndAddr) >= 0x14u)
                        ? vp.sampleEndAddr + 4 : 0u;
        a.mdiEndAddr    = 0;
    }
    else
    {
        a.vxdState = static_cast<uint16_t>((channel << 8) | note);
        a.vxdRom = sampleInRom;
        // SBAWE.VXD 0xC0FFB0C8: a sample without a loop gets the end for the
        // CCCA test (+0x0A) already at note-on, sample end + 4 - such a voice
        // is free for the next note as soon as it has played out. A looped
        // sample gets it only when the loop is opened at note-off (0xC0FFA29B).
        a.vxdEndAddr = vp.looping ? 0u : vp.sampleEndAddr + 4;
        a.vxdPatchPan = static_cast<uint8_t>(vp.patchPan);
        a.vxdFmmodDepth = static_cast<int8_t>((vp.fmmod >> 8) & 0xFF);
        a.vxdPatchAtten = vp.patchAttenUnits;
        a.vxdRom1mgm = bank && bank->romName == "1MGM" && sampleInRom;
        // Same rule as SBAWE32.MDI: a looped sample with at least 0x14 words
        // after the loop end gets its loop moved behind the end at note-off
        // (block +0x06 = sample end + 4, relax/georgia traces: PSST 983F, CSL 9843).
        a.vxdNoteEnd = (vp.looping && (vp.sampleEndAddr - vp.loopEndAddr) >= 0x14u)
                     ? vp.sampleEndAddr + 4 : 0u;
        a.vxdPanTarget = a.vxdPanCur = static_cast<uint8_t>(pan);
        a.vxdPanTimer = -1;
    }

    if (m_noteDump)
    {
        // Poradi a nazvy sloupcu odpovidaji poli bloku v `SBAWE.VXD`
        // (offsety v zavorce), aby se to dalo klast vedle patch_struct.py.
        std::fprintf(static_cast<FILE*>(m_noteDump),
                     "%d,%d,%d,%d,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%04X,%06X,%06X,%06X,%d,%d,%d\n",
                     static_cast<int>(channel), static_cast<int>(note),
                     static_cast<int>(velocity), voice,
                     static_cast<unsigned>((vp.ccca >> Emu8000::kCccaQShift) & 0xF), // 0x12 Q
                     static_cast<unsigned>(reverb & 0xFF),                           // 0x20
                     static_cast<unsigned>(panAux & 0xFF),                           // 0x24
                     static_cast<unsigned>(atten & 0xFF),                            // 0x26
                     static_cast<unsigned>(cutoff & 0xFF),
                     static_cast<unsigned>(vp.envval),                               // 0x32
                     static_cast<unsigned>(vp.atkhld & 0x7F),                        // 0x34
                     static_cast<unsigned>(vp.envvol),                               // 0x42
                     static_cast<unsigned>(vp.atkhldv & 0x7F),                       // 0x44
                     static_cast<unsigned>((vp.atkhldv >> 8) & 0x7F),                // 0x48
                     static_cast<unsigned>(pitch),
                     static_cast<unsigned>(ccca & Emu8000::kCccaAddressMask),
                     static_cast<unsigned>(vp.csl & Emu8000::kLoopAddressMask),
                     // Ohyb vysky se do vysledneho IP uz zapocital;
                     // pro rozbor rozdilu proti ovladaci je potreba
                     // videt i jeho vstupy a samotny prispevek.
                     static_cast<int>(ch.pitchBend),
                     static_cast<int>(ch.pitchBendRangeSemitones),
                     PitchBendOffset(channel));
    }

    if (m_debugVoices > 0)
    {
        --m_debugVoices;
        const bool rom = bank && region && region->sample
                      && (region->sample->inRom || bank->samplesInRom);
        std::cout << "  hlas " << voice
                  << " ch" << static_cast<int>(channel)
                  << " prog" << static_cast<int>(m_channels[channel].program)
                  << " nota " << static_cast<int>(note)
                  << " vel " << static_cast<int>(velocity)
                  << " | " << (region && region->sample ? region->sample->name
                                                        : std::string("(nahradni sinus)"))
                  << (rom ? " [ROM]" : "")
                  << std::hex
                  << " | adr " << (ccca & Emu8000::kCccaAddressMask)
                  << " smycka " << (vp.psst & Emu8000::kLoopAddressMask)
                  << ".." << (vp.csl & Emu8000::kLoopAddressMask)
                  << " IP " << vp.ip << " IFATN " << ifatn
                  << " ATKHLDV " << vp.atkhldv << " DCYSUSV " << vp.dcysusv
                  << std::dec << "\n";
    }
}

void Synth::StartFallbackVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity)
{
    SoundFont::VoiceParams vp;
    vp.sampleStart = m_fallbackStart;
    vp.ccca = m_fallbackStart & Emu8000::kCccaAddressMask;
    vp.psst = (128u << Emu8000::kPanShift) | (m_fallbackLoopStart & Emu8000::kLoopAddressMask);
    vp.csl  = m_fallbackLoopEnd & Emu8000::kLoopAddressMask;

    const double freqHz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const double octaves = std::log2(freqHz / m_fallbackUnityHz);
    vp.ip = static_cast<uint16_t>(std::clamp(
        Emu8000::kPitchUnity + octaves * Emu8000::kPitchPerOctave, 0.0, 65535.0));

    const double vel = std::max(1, static_cast<int>(velocity)) / 127.0;
    vp.ifatn = static_cast<uint16_t>((0xFF << 8) | AttenDbToUnits(-40.0 * std::log10(vel)));
    vp.envvol = 0x8000; vp.atkhldv = MakeAtkhld(0x7F, 0x7F); vp.dcysusv = MakeDcysusv(0x7F, 0);
    vp.envval = 0x8000; vp.atkhld  = MakeAtkhld(0x7F, 0x7F); vp.dcysus  = MakeDcysusv(0x7F, 0);
    vp.lfo1val = vp.lfo2val = 0x8000;
    vp.releaseRate = 0x40;

    StartVoice(voice, channel, note, velocity, vp, nullptr, nullptr);
}

// ---------------------------------------------------------------------------
// MIDI rozhrani
// ---------------------------------------------------------------------------

void Synth::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (channel >= 16 || note > 127) return;
    if (!((m_channelMask >> channel) & 1)) return;
    if (velocity == 0) { NoteOff(channel, note); return; }

    const int bankNum = BankNumberFor(channel);
    const int program = m_channels[channel].program;

    // Poradi hledani. Kazdy krok se zkousi pres VSECHNY nactene banky,
    // teprve pak se jde na dalsi - jinak by uzivatelska banka s presetem 0
    // prebila GM bicí jen proto, ze byla nactena pozdeji.
    //
    //   1) presna banka + program
    //   2) u bicich jeste banka 128, program 0 = "Standard" sada.
    //      GM bicí banka casto obsahuje jen zakladni sadu a skladba pritom
    //      posle jine cislo programu; bez tohoto kroku by se cely bicí part
    //      nahradil melodickym presetem z banky 0.
    //   3) banka 0 se stejnym programem - ale **jen u melodickych kanalu**.
    // Na bicim kanalu ovladac do banky 0 nesahne: kdyz sadu nenajde,
    // vezme rovnou prvni preset banky. Zmereno na RELAX.SBK (32 presetu
    // 0..31, zadna bicí banka) se skladbou RELAX_VX, kde ma kanal 9
    // program 16: ovladac hral u vsech 1849 not preset 0 (0x20000C),
    // kdezto my jsme brali melodicky preset 16 z banky 0.
    int chain[3];
    int chainLen = 0;
    chain[chainLen++] = bankNum;
    const bool drums = (bankNum == kDrumBank);
    if (drums) chain[chainLen++] = -1;          // znacka pro (128, program 0)
    if (bankNum != 0 && !drums) chain[chainLen++] = 0;

    for (int pass = 0; pass < chainLen; ++pass)
    {
        const bool standardKit = (chain[pass] == -1);
        const int wantBank = standardKit ? kDrumBank : chain[pass];
        const int wantProgram = standardKit ? 0 : program;

        // V ramci jednoho pruchodu se hleda od naposledy nactene banky -
        // uzivatelska banka prebije GM preset se stejnym cislem.
        for (size_t i = m_banks.size(); i-- > 0; )
        {
            const SoundFont::Bank& b = *m_banks[i].bank;
            const std::vector<SoundFont::Region> regions =
                b.Select(wantBank, wantProgram, note, velocity);
            if (regions.empty()) continue;

            StartLayers(i, regions, channel, note, velocity,
                        (static_cast<uint32_t>(i) << 16) | (wantBank << 8) | wantProgram);
            return;
        }
    }

    // Kdyz program v bance neni, ovladac sahne po **prvnim presetu banky**,
    // ne po nejake vlastni nahrade. Zmereno na RELAX.SBK (32 vokalnich
    // presetu 0..31) prehravane skladbou RELAX_VX, ktera pouziva GM programy
    // az do 122: u vsech chybejicich hral ovladac vzorek na 0x20000C, tedy
    // preset 0. My jsme misto toho pousteli nahradni sinusovku z `kDramOffset`,
    // coz bylo v CCCA videt jako 0x1FFFFC.
    for (size_t i = m_banks.size(); i-- > 0; )
    {
        const SoundFont::Bank& b = *m_banks[i].bank;
        const std::vector<SoundFont::Region> regions =
            b.Select(0, 0, note, velocity);
        if (regions.empty()) continue;
        StartLayers(i, regions, channel, note, velocity, static_cast<uint32_t>(i) << 16);
        return;
    }

    // Az kdyz nema banka ani preset 0 - to uz je banka bez pouzitelneho
    // obsahu a hraje se nahradni vzorek.
    const Awe32::Driver drv = m_core.DriverVariant();
    const int fallback = (drv == Awe32::Driver::Sdk) ? AllocateVoiceSdk(0xFFFE)
                       : (drv == Awe32::Driver::Dos) ? AllocateVoiceMdi(0xFFFE)
                       : AllocateVoice();
    if (fallback >= 0)
        StartFallbackVoice(fallback, channel, note, velocity);
}

void Synth::NoteOff(uint8_t channel, uint8_t note)
{
    if (channel >= 16) return;
    if (Awe32::IsDosLike(m_core.DriverVariant()))
    {
        // SBAWE32.MDI 0x238A: every layer of the key on the channel; with the
        // pedal down the voice is only marked as held (low byte 0xFF).
        const uint16_t key = static_cast<uint16_t>((channel << 8) | note);
        for (int v = 0; v < kUsableVoices; ++v)
        {
            VoiceAlloc& a = m_alloc[v];
            if ((a.mdiState >> 8) == 0xFF || (a.mdiState & 0x0FFF) != key) continue;
            if (m_channels[channel].sustain) a.mdiState |= 0x00FF;
            else                             NoteOffMdi(v);
        }
        return;
    }
    for (int i = 0; i < NoteVoices(); ++i)
    {
        VoiceAlloc& a = m_alloc[i];
        if (!a.inUse || a.heldBySustain) continue;
        if (a.channel != channel || a.note != note) continue;

        if (m_channels[channel].sustain)
        {
            a.heldBySustain = true;
            a.vxdState = static_cast<uint16_t>((a.vxdState & 0xFF00u) | 0xFFu);
        }
        else                             ReleaseVoice(i);
    }
}

void Synth::ProgramChange(uint8_t channel, uint8_t program)
{
    if (channel >= 16) return;
    m_channels[channel].program = program;
}

void Synth::RefreshChannel(uint8_t channel)
{
    const int bend = PitchBendOffset(channel);
    const bool vxd = m_core.DriverVariant() == Awe32::Driver::Win95;
    for (int i = 0; i < NoteVoices(); ++i)
    {
        VoiceAlloc& a = m_alloc[i];
        if (vxd)
        {
            // SBAWE.VXD matches the high byte of the voice state only: a free
            // voice (0x0FFE) belongs to channel 15, so bends there move the
            // stored IP of every free voice (relax_win95.trace, 43.26 s).
            if ((a.vxdState >> 8) != channel) continue;
        }
        else if ((!a.inUse && !a.heldBySustain) || a.channel != channel) continue;

        const uint16_t pitch = static_cast<uint16_t>(std::clamp(a.basePitch + bend, 0, 65535));
        // Jen IP. PTRX se **nepise** - cilovou vysku v jeho horni pulce si
        // dopocita cip sam ze zapisu do IP (viz Emu8000Core::PortOut16).
        // Skutecny ovladac to tak dela taky: v georg_win95.trace je IP
        // 4418krat, uplne stejne jako u nas, ale PTRX tam zadne zapisy navic
        // nema. Drive se sem psalo `pitch << 16`, coz je logaritmicke IP -
        // jenze horni pulka PTRX je linearni prirustek, takze to spravnou
        // hodnotu spocitanou cipem prepisovalo necim jinym.
        m_core.Write(Reg::IP, i, pitch);
    }
}

void Synth::ControlChange(uint8_t channel, uint8_t controller, uint8_t value)
{
    if (channel >= 16) return;
    if (Awe32::IsDosLike(m_core.DriverVariant())
        && ControlChangeMdi(channel, controller, value))
        return;
    ChannelState& ch = m_channels[channel];

    switch (controller)
    {
    case 0:  ch.bankMsb = value; break;
    case 1:
        ch.modWheel = value;
        if (m_core.DriverVariant() == Awe32::Driver::Win95) UpdateFmmodVxd(channel);
        break;
    case 32: ch.bankLsb = value; break;
    case 7:
        ch.volume = value;
        if (m_core.DriverVariant() == Awe32::Driver::Win95) UpdateAttenVxd(channel);
        break;
    case 10:
        if (m_core.DriverVariant() == Awe32::Driver::Win95 && ch.pan != value)
        {
            ch.pan = value;
            PanVxd(channel, value);
        }
        ch.pan = value;
        break;
    case 11:
        ch.expression = value;
        if (m_core.DriverVariant() == Awe32::Driver::Win95) UpdateAttenVxd(channel);
        break;
    case 91: ch.reverbSend = value; break;
    case 93: ch.chorusSend = value; break;

    // Rozsah ohybu vysky se nastavuje pres RPN 0,0. Bez toho jsme meli
    // natvrdo dva pultony, coz je vychozi hodnota MIDI - jenze Miles
    // ovladac ve hre pouziva **dvanact**, a bylo to videt: ctyri noty
    // na ch6 v intru Magic Carpet 2 mely IP o 10 pultonu vys, protoze
    // na nich lezi plny ohyb dolu (u nas 2 pultony misto 12).
    case 101: ch.rpnMsb = value; break;
    case 100: ch.rpnLsb = value; break;
    case 6:
        if (ch.rpnMsb == 0 && ch.rpnLsb == 0)
            ch.pitchBendRangeSemitones = value;
        break;
    // Jemna cast rozsahu (centy) se do registru stejne nevejde -
    // ovladac pocita v celych pultonech, takze ji jen prijmeme.
    case 38: break;

    case 64:
    {
        const bool wasOn = ch.sustain;
        ch.sustain = value >= 64;
        if (wasOn && !ch.sustain)
            for (int i = 0; i < NoteVoices(); ++i)
                if (m_alloc[i].heldBySustain && m_alloc[i].channel == channel)
                    ReleaseVoice(i);
        break;
    }

    case 121:
        // SBAWE.VXD 0xC0FFC8ED: CC1 = 0 without a voice update, pedal off,
        // expression 127 (IFATN), bend to the centre (IP), pressure 0 (FMMOD).
        if (m_core.DriverVariant() != Awe32::Driver::Win95) break;
        ch.modWheel = 0;
        ControlChange(channel, 64, 0);
        ControlChange(channel, 11, 127);
        PitchBend(channel, 0);
        ChannelPressure(channel, 0);
        break;

    case 120:
        for (int i = 0; i < NoteVoices(); ++i)
            if (m_alloc[i].channel == channel) KillVoice(i);
        break;

    case 123:
        for (int i = 0; i < NoteVoices(); ++i)
            if (m_alloc[i].inUse && m_alloc[i].channel == channel) ReleaseVoice(i);
        break;

    default:
        break;
    }
}

void Synth::PitchBend(uint8_t channel, int16_t value)
{
    if (channel >= 16) return;
    if (Awe32::IsDosLike(m_core.DriverVariant()))
    {
        PitchBendMdi(channel, value);
        return;
    }
    m_channels[channel].pitchBend = value;
    RefreshChannel(channel);
}

void Synth::RenderBlock(int16_t* out, uint32_t numFrames)
{
    if (m_core.DriverVariant() == Awe32::Driver::Win95)
        for (int v = 0; v < kVxdVoices; ++v)
        {
            VoiceAlloc& a = m_alloc[v];
            if (a.vxdPanTimer < 0) continue;
            a.vxdPanTimer -= static_cast<int32_t>(numFrames);
            if (a.vxdPanTimer <= 0) PanStepVxd(v);
        }
    m_core.RenderBlock(out, numFrames);
}

bool Synth::OpenNoteDump(const std::string& path)
{
    CloseNoteDump();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "ch,note,vel,voice,Q,reverb,panAux,atten,cutoff,"
                    "envvalDelay,modAttack,envvolDelay,volAttack,volHold,ip,ccca,loopEnd,bend,bendRange,bendOffset\n");
    m_noteDump = f;
    return true;
}

void Synth::CloseNoteDump()
{
    if (m_noteDump) { std::fclose(static_cast<FILE*>(m_noteDump)); m_noteDump = nullptr; }
}
