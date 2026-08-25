#include "Synth.h"
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
    m_core.Write(Reg::DCYSUSV, voice,
                 Emu8000::kDcysusvRelease | (m_alloc[voice].releaseRate & 0x7F));
    // `SBAWE.VXD` uvolnuje **obe** obalky - hned za DCYSUSV posila DCYSUS
    // s vlastni rychlosti (ReleaseModEnv). Zmereno v georg_win95.trace,
    // kde dvojice DCYSUSV 8029 / DCYSUS 8027 stoji u kazdeho note-offu;
    // odtud i dvojnasobny pocet zapisu do DCYSUS v census u trace_diff.
    // `SBAWE32.MDI` to nedela.
    if (m_core.DriverVariant() == Awe32::Driver::Win95)
        m_core.Write(Reg::DCYSUS, voice,
                     Emu8000::kDcysusvRelease | (m_alloc[voice].releaseModRate & 0x7F));
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
}

void Synth::KillVoice(int voice)
{
    m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvOff);
    m_alloc[voice].inUse = false;
    m_alloc[voice].heldBySustain = false;
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
    const double semis = (ch.pitchBend / 8192.0) * ch.pitchBendRangeSemitones;
    return static_cast<int>(std::lround(semis * Emu8000::kPitchPerOctave / 12.0));
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
    if (drv == Awe32::Driver::Win95 && bank && bank->romName == "1MGM")
        atten = std::min(atten + 16, 255);

    // Velocity ovlivnuje i mezni kmitocet filtru - tisi noty jsou tmavsi.
    // Prepis z SBAWE32.DRV, offset 0x021E:
    //
    //     if (kanal != 9 && attackRate < 0x7D)
    //         cutoff = (cutoff * max(velocity, 0x46) + 0x40) / 0x7F;
    //
    // Bicí (kanal 9) maji vlastni vetev a filtr se jim takhle neupravuje.
    // Presna podoba z SBAWE.VXD (objekt 1, 0x1CF6):
    //     if (attack < 0x7D)
    //         cutoff = (cutoff * max(velocity, 0x46) + 0xA0) >> 7;
    // Puvodne jsme meli zaokrouhleni +0x40 a deleni 0x7F; ovladac ma +0xA0
    // a posun o 7 (tj. deleni 128).
    int cutoff = (vp.ifatn >> 8) & 0xFF;
    const int attackRate = vp.atkhldv & Emu8000::kAtkhldAttackMask;
    if (channel != 9 && attackRate < 0x7D)
    {
        const int v = std::max<int>(velocity, 0x46);
        cutoff = std::clamp((cutoff * v + 0xA0) >> 7, 0, 255);
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
    else if (pan < (drv == Awe32::Driver::Win95 ? 2 : 0)) pan = 0;
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

    const int pitch = std::clamp(vp.ip + PitchBendOffset(channel), 0, 65535);

    const uint32_t reverbByte =
        static_cast<uint32_t>(std::clamp(reverb, 0, 255)) << Emu8000::kReverbShift;
    // Spodni bajt PTRX je doplnkova panorama. `SBAWE.VXD` tam dava 256 - pan
    // (pri pan 0x7F zapisuje 0x81), zmereno v poli hlasu na +0x24.
    // `SBAWE32.MDI` tam nechava **nulu** - zmereno na 341 notach z Magic
    // Carpet 2, kde PSST nese pan 0x0F a PTRX ma spodni bajt 0x00.
    const uint32_t panAux = (drv == Awe32::Driver::Win95)
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
        m_core.Write(Reg::FMMOD,   voice, vp.fmmod);
        m_core.Write(Reg::TREMFRQ, voice, vp.tremfrq);
        m_core.Write(Reg::FM2FRQ2, voice, vp.fm2frq2);
        m_core.Write(Reg::ENVVAL,  voice, vp.envval);
        m_core.Write(Reg::ENVVOL,  voice, vp.envvol);

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
            ((vp.atkhldv & 0x7F) == 0x7F && vp.envvol == 0xBFFF)
                ? Awe32Curves::VolumeTarget(atten)
                : 0u;
        const uint32_t vtft = (volTarget << 16) | filterTarget;
        m_core.Write(Reg::VTFT, voice, vtft);
        m_core.Write(Reg::CVCF, voice, vtft);
        m_core.Write(Reg::PTRX, voice, (increment << 16) | reverbByte | panAux);
        m_core.Write(Reg::CPF,  voice, increment << 16);
    }
    else
    {
        // `SBAWE32.MDI`: jine poradi i jiny obsah. Overeno na 255 notach
        // z Magic Carpet 2, takze se tenhle blok nemeni.
        m_core.Write(Reg::DCYSUSV, voice, Emu8000::kDcysusvOff);
        m_core.Write(Reg::VTFT,    voice, 0x0000FFFFu);
        m_core.Write(Reg::PSST, voice, psst);
        m_core.Write(Reg::CSL,  voice, csl);
        m_core.Write(Reg::CCCA, voice, ccca);
        m_core.Write(Reg::IP,   voice, static_cast<uint16_t>(pitch));
        // MDI cte PTRX **az po zapisu IP**, aby v nem uz byla cilova vyska,
        // kterou si cip z IP dopocital, a prepise jen bajt s reverb sendem.
        const uint32_t cur = m_core.Read(Reg::PTRX, voice);
        m_core.Write(Reg::PTRX, voice, (cur & 0xFFFF00FFu) | reverbByte);
        m_core.Write(Reg::IFATN,   voice, ifatn);
        m_core.Write(Reg::PEFE,    voice, vp.pefe);
        m_core.Write(Reg::FMMOD,   voice, vp.fmmod);
        m_core.Write(Reg::TREMFRQ, voice, vp.tremfrq);
        m_core.Write(Reg::FM2FRQ2, voice, vp.fm2frq2);
        m_core.Write(Reg::ENVVAL,  voice, vp.envval);
        m_core.Write(Reg::ATKHLD,  voice, vp.atkhld);
        m_core.Write(Reg::DCYSUS,  voice, vp.dcysus);
        m_core.Write(Reg::LFO1VAL, voice, vp.lfo1val);
        m_core.Write(Reg::LFO2VAL, voice, vp.lfo2val);
        m_core.Write(Reg::ENVVOL,  voice, vp.envvol);
        m_core.Write(Reg::ATKHLDV, voice, vp.atkhldv);
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
    //   3) banka 0 se stejnym programem
    int chain[3];
    int chainLen = 0;
    chain[chainLen++] = bankNum;
    const bool drums = (bankNum == kDrumBank);
    if (drums) chain[chainLen++] = -1;          // znacka pro (128, program 0)
    if (bankNum != 0) chain[chainLen++] = 0;

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

            // Preset muze mit vic vrstev na jednu notu - kazda dostane hlas.
            for (const SoundFont::Region& r : regions)
            {
                const SoundFont::VoiceParams vp = SoundFont::MakeVoiceParams(
                    b, r, note, velocity, m_banks[i].dramBase, kRomPoolBase,
                    m_core.DriverVariant());
                StartVoice(AllocateVoice(), channel, note, velocity, vp, &b, &r);
            }
            return;
        }
    }

    StartFallbackVoice(AllocateVoice(), channel, note, velocity);
}

void Synth::NoteOff(uint8_t channel, uint8_t note)
{
    if (channel >= 16) return;
    for (int i = 0; i < kUsableVoices; ++i)
    {
        VoiceAlloc& a = m_alloc[i];
        if (!a.inUse || a.heldBySustain) continue;
        if (a.channel != channel || a.note != note) continue;

        if (m_channels[channel].sustain) a.heldBySustain = true;
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
    for (int i = 0; i < kUsableVoices; ++i)
    {
        VoiceAlloc& a = m_alloc[i];
        if ((!a.inUse && !a.heldBySustain) || a.channel != channel) continue;

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
    ChannelState& ch = m_channels[channel];

    switch (controller)
    {
    case 0:  ch.bankMsb = value; break;
    case 32: ch.bankLsb = value; break;
    case 7:  ch.volume = value; break;
    case 10: ch.pan = value; break;
    case 11: ch.expression = value; break;
    case 91: ch.reverbSend = value; break;
    case 93: ch.chorusSend = value; break;

    case 64:
    {
        const bool wasOn = ch.sustain;
        ch.sustain = value >= 64;
        if (wasOn && !ch.sustain)
            for (int i = 0; i < kUsableVoices; ++i)
                if (m_alloc[i].heldBySustain && m_alloc[i].channel == channel)
                    ReleaseVoice(i);
        break;
    }

    case 120:
        for (int i = 0; i < kUsableVoices; ++i)
            if (m_alloc[i].channel == channel) KillVoice(i);
        break;

    case 123:
        for (int i = 0; i < kUsableVoices; ++i)
            if (m_alloc[i].inUse && m_alloc[i].channel == channel) ReleaseVoice(i);
        break;

    default:
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
