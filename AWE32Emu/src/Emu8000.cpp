#include "Emu8000.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Emu8000;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    // Plny rozsah utlumu (IFATN 0xFF = 96 dB podle Programmer's Guide).
    constexpr double kFullScaleDb = kAttenMaxDb;

    // Tabulky decay/release casu v ovladacich jsou casy pro prubeh pres
    // 100 dB - to sedi na udaje v Programmer's Guide (rate 0x7F = 240 us/dB,
    // tabulka 24 ms; rate 0x01 = 470 ms/dB, tabulka 47513 ms).
    constexpr double kDecayTableSpanDb = 100.0;

    // --- prevody registrovych hodnot na cas -----------------------------
    // [ASM] Odvozeno z prevodnich tabulek v SBAWE32.DRV (Windows AWE32 MIDI
    // driver): tabulka attack casu na ds:1552 (127 polozek) a tabulka
    // decay/release casu na ds:1650 (128 polozek), obe v milisekundach.
    // Vyhledavaci rutiny sub_2BC0 (attack) a sub_2BF0 (decay) urcuji, ze
    //   attack rate r = 1..127  ->  attackTable[r-1]
    //   decay  rate r = 0..127  ->  decayTable[r]
    //
    // Obe tabulky jsou presne popsatelne jednim vzorcem: cas = base / k(i),
    // kde k je 7bitove "plovouci" kodovani - prvnich 16 hodnot 1..16, dalsich
    // 16 hodnot 17..32, a pak se s kazdou skupinou po 16 krok zdvojnasobi.
    // Overeno proti obema tabulkam bajt po bajtu (0 odchylek), takze se
    // nemusi kopirovat zadna data z ovladace.
    int RateDivisor(int index)          // index 0..127
    {
        const int group = (index >> 4) & 7;
        const int m = index & 15;
        return (group == 0) ? (m + 1) : ((m + 17) << (group - 1));
    }

    // ATKHLDV/ATKHLD, bity 6..0. Attack je celkovy cas nabehu (obalka je
    // v teto fazi linearni v amplitude). Rate 0 = "never attack" [PG].
    // Rate 1 = 11.88 s, rate 0x7F = 6 ms - presne sedi na tabulku v ovladaci.
    double AttackSeconds(int rate)
    {
        if (rate <= 0) return -1.0;      // nikdy
        return 11.878 / RateDivisor(std::min(rate, 127) - 1);
    }

    // DCYSUSV/DCYSUS, bity 6..0. Decay i release pouzivaji stejny registr
    // i stejnou tabulku - ovladac pri Note Off jen prepise DCYSUSV s bitem 15.
    //
    // Programmer's Guide udava rychlost jako cas na jeden dB, ne jako celkovy
    // cas obalky, takze to tak pocitame i tady. Vraci dB za sekundu;
    // zaporna hodnota znamena "bez decay" (rate 0).
    double DecayDbPerSecond(int rate)
    {
        if (rate <= 0) return -1.0;      // bez decay
        const double spanSeconds = 47.513 / RateDivisor(std::min(rate, 127) - 1);
        return kDecayTableSpanDb / spanSeconds;
    }

    // ATKHLDV/ATKHLD, bity 14..8: hold po 92 ms, 0x7F = bez prodlevy [PG].
    double HoldSeconds(int hold)
    {
        // Hold cipu - viz kHoldSecPerStepChip (odchylka z run5 byla
        // pomalejsi ridici takt toho behu, ne vlastnost cipu).
        return (127 - std::clamp(hold, 0, 127)) * kHoldSecPerStepChip;
    }

    // ENVVOL/ENVVAL/LFO1VAL/LFO2VAL: zpozdeni, 0x8000 = bez zpozdeni,
    // nizsi hodnoty = rostouci prodleva po 725 us [PG].
    double DelaySeconds(uint16_t value)
    {
        const int units = static_cast<int>(kDelayNone) - static_cast<int>(value);
        if (units <= 0) return 0.0;
        return units * kDelaySecPerStep;
    }

    // DCYSUSV bity 14..8: sustain level jako utlum po 0.75 dB,
    // 0x7F = bez utlumu, 0 = ticho [PG].
    double SustainDb(int level)
    {
        return (0x7F - std::clamp(level, 0, 0x7F)) * kSustainDbPerStep;
    }

    // IFATN bity 7..0: pocatecni utlum po 0.375 dB, 0xFF = 96 dB [PG].
    double AttenuationDb(int value)
    {
        return std::clamp(value, 0, 255) * kAttenDbPerStep;
    }

    // IFATN bity 15..8: pocatecni mezni kmitocet filtru.
    //
    // Programmer's Guide si tu protireci: rika "ve ctvrt pultonech, 0x00 =
    // 125 Hz" a zaroven "0xFF = 8 kHz". Ctvrt pultony (48 na oktavu) by pri
    // 255 daly jen 4966 Hz. Drzime se udanych krajnich bodu, tj. 125 Hz az
    // 8 kHz pres 255 kroku (= 42.5 kroku na oktavu), protoze:
    //   - jen tak sedi obe uvedena cisla
    //   - hloubky modulaci jsou v manualu v oktavach, takze se prepocitavaji
    //     stejne at je kroku na oktavu kolik chce (viz RenderVoice)
    //
    // Manual navic vyslovne rika: "If the Q of the channel is programmed to
    // zero and the filter cutoff to 0xFF, the filter does not alter the
    // signal." Pri ctvrt pultonech by filtr porad rezal na 5 kHz a bral
    // vysky, ktere v referencnich nahravkach jsou.
    // Kmitocet pri registru 0xFF, dopocitany ze zakladu a kroku v centech.
    inline constexpr double kCutoffTopHz =
        Emu8000::kCutoffBaseHz * 30.31287;   // 2^(255*29.3843/1200) = 7717 Hz

    double CutoffOctaves(double cutoffReg, double topHz = kCutoffTopHz,
                         double baseHz = Emu8000::kCutoffBaseHz)
    {
        // kolik oktav nad zakladem lezi dana registrova hodnota
        const double octavesTotal = std::log2(topHz / baseHz);
        return std::clamp(cutoffReg, 0.0, 255.0) / 255.0 * octavesTotal;
    }

    double CutoffHz(double octavesAboveBase, double baseHz = Emu8000::kCutoffBaseHz)
    {
        return baseHz * std::pow(2.0, octavesAboveBase);
    }

    // TREMFRQ/FM2FRQ2 bity 7..0: frekvence LFO po 0.042 Hz,
    // 0xFF = 10.72 Hz [PG]. Rada zacina na 0.01 Hz, ne na nule.
    double LfoHz(int value)
    {
        return 0.01 + std::clamp(value, 0, 255) * kLfoHzPerStep;
    }

    // LFO cipu ma TROJUHELNIKOVY prubeh, ne sinusovy. Zacina na nule,
    // stoupa k +1 ve ctvrtine periody, zpet na nulu v pulce a na -1 ve
    // tri ctvrtinach. Prepis tabulky lfotable z referencni implementace
    // 86Boxu; sinus na tomhle miste znel jinak.
    double LfoTriangle(double phase01)
    {
        double t = phase01 - std::floor(phase01);          // 0..1
        t += 0.25;                                          // posun jako v tabulce
        if (t >= 1.0) t -= 1.0;
        return (t < 0.5) ? (4.0 * t - 1.0) : (3.0 - 4.0 * t);
    }

    double DbToLinear(double db)
    {
        if (db >= kFullScaleDb) return 0.0;
        return std::pow(10.0, -db / 20.0);
    }

    // Utlum vstupu filtru podle Q, prevzato z `filter_atten` v snd_emu8k.c
    // 86Boxu (tam odvozeno z awe32faq: utlum je zhruba polovina Q v dB).
    // V 8.8 pevne radove carce, 65536 = beze zmeny.
    constexpr int32_t kFilterAtten86[16] = {
        65536, 61869, 57079, 53269, 49145, 44820, 40877, 34792,
        32845, 30653, 28607, 26392, 24630, 22463, 20487, 18470
    };

    // Mez filtru tak, jak ji 86Box plni do tabulky koeficientu:
    // zacina na 125 Hz a kazdy z 256 kroku nasobi 1.016378315
    // (= 42,66 dilku na oktavu).
    double Cutoff86Hz(int index)
    {
        double out = 125.0;
        for (int i = 0; i < std::clamp(index, 0, 255); ++i)
            out *= 1.016378315;
        return out;
    }

    inline int8_t HiSigned(uint16_t w)  { return static_cast<int8_t>(w >> 8); }
    inline int8_t LoSigned(uint16_t w)  { return static_cast<int8_t>(w & 0xFF); }
    inline int    HiByte(uint16_t w)    { return (w >> 8) & 0xFF; }
    inline int    LoByte(uint16_t w)    { return w & 0xFF; }
}

// ===========================================================================
// konstrukce, registrove pole
// ===========================================================================

Emu8000Core::Emu8000Core(uint32_t outputSampleRate)
    : m_outputRate(outputSampleRate ? outputSampleRate : kNativeSampleRate)
{
    // Vychozi presety jsou ty, ktere nastavuje ovladac: chorus 2 (Chorus 3)
    // a reverb 4 (Hall 2) - viz SBAWE32.DRV 0x60FA a 0x612D.
    m_chorus.Init(kNativeSampleRate, Emu8000Fx::kChorusDefault);
    m_reverb.Init(kNativeSampleRate);
    m_reverb.SetPreset(Emu8000Fx::kReverbDefault);
    m_eq.Init(kNativeSampleRate);
    PowerOnInit();
}

uint16_t& Emu8000Core::RegRef(Port p, int reg, int voice)
{
    return m_regs[static_cast<size_t>(p)][reg & 7][voice & 0x1F];
}

uint16_t Emu8000Core::RegVal(Port p, int reg, int voice) const
{
    return m_regs[static_cast<size_t>(p)][reg & 7][voice & 0x1F];
}

// ===========================================================================
// portova uroven - presne to, co dela ovladac (viz AWEUTIL sub_10EAC)
// ===========================================================================

void Emu8000Core::SetBasePort(uint16_t sbBasePort)
{
    m_basePort = sbBasePort;
}

bool Emu8000Core::OwnsPort(uint16_t port) const
{
    const uint16_t off = static_cast<uint16_t>(port - m_basePort);
    return off == kPortData0 || off == kPortData0Hi
        || off == kPortData1 || off == kPortData1Hi
        || off == kPortData3 || off == kPortPointer;
}

bool Emu8000Core::OpenTrace(const char* path)
{
    CloseTrace();
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "# EMU8000 port write trace, 44100 Hz timebase\n");
    std::fprintf(f, "# <frame> <port hex> <value hex>\n");
    m_traceFile = f;
    m_traceFrames = 0;
    return true;
}

void Emu8000Core::CloseTrace()
{
    if (m_traceFile)
    {
        std::fclose(static_cast<FILE*>(m_traceFile));
        m_traceFile = nullptr;
    }
}

void Emu8000Core::PortOut16(uint16_t port, uint16_t value)
{
    if (m_traceFile && !m_traceOff)
        std::fprintf(static_cast<FILE*>(m_traceFile), "%llu %03X %04X\n",
                     static_cast<unsigned long long>(m_traceFrames), port, value);

    // Do 86Boxiho cipu jde presne to, co by slo na sbernici. Zpetne zapisy
    // stavu (UpdateRegistersFromState) se poznaji podle m_traceOff a
    // neposilaji se - ty si cip dela sam.
    if (m_chip == Chip::Box86 && !m_traceOff)
        m_box.PortWrite(port, value);

    const uint16_t off = static_cast<uint16_t>(port - m_basePort);
    if (off == kPortPointer)
    {
        m_pointer = value;
        return;
    }

    Port p;
    switch (off)
    {
    case kPortData0:   p = Port::Data0;   break;
    case kPortData0Hi: p = Port::Data0Hi; break;
    case kPortData1:   p = Port::Data1;   break;
    case kPortData1Hi: p = Port::Data1Hi; break;
    case kPortData3:   p = Port::Data3;   break;
    default: return;
    }

    const int reg   = (m_pointer >> 5) & 7;
    const int voice = m_pointer & 0x1F;
    // 86Box ignores an IFATN write with zero attenuation to a silent voice
    // whose DCYSUSV is 0x0080 and IP is 0 (its patch against clicks from
    // trackers that zero the registers to stop a note). Kept 1:1.
    if (p == Port::Data3 && reg == 1 && (value & 0xFF) == 0 && !m_voices[voice].playing
        && RegVal(Port::Data1, 5, voice) == kDcysusvOff && RegVal(Port::Data3, 0, voice) == 0)
        return;

    RegRef(p, reg, voice) = value;

    // A CCCA write sets the playback address immediately, as in 86Box
    // (emu8k_outw, Data1/Data2 register 0: addr.int_address = ccca & mask).
    // Before this the address was only taken at note-on, while
    // UpdateRegistersFromState wrote the voice's stale position back into
    // CCCA after every rendered block - so an address written a few frames
    // before the note-on (normal in a register replay) was lost and the note
    // started wherever the voice had stopped (AWETST25 block 33: the noise
    // note played ROM from the previous sine's loop).
    if (reg == 0 && (p == Port::Data1 || p == Port::Data1Hi))
        m_voices[voice].address = Read(Reg::CCCA, voice) & kCccaAddressMask;

    // Sample memory upload. SMALW / SMARW (register 1, voices 22 / 23; low
    // word on Data1, address bits 23..16 on Data1Hi) hold the write address;
    // every write to SMLD (Data1) or SMRD (Data1Hi) of register 1, voice 26
    // stores one sample there and advances that address. Without this a
    // register replay of a program that uploads its own samples played
    // silence (AWETST25 block 27, BULLFROG.SBK: presets 0, 3, 4 silent in our
    // render, audible on the card and in the 86Box core). Writes below the
    // DRAM start (the ROM) are ignored, as on the chip.
    if (reg == 1 && voice == Hwcf::kSMLD && (p == Port::Data1 || p == Port::Data1Hi))
    {
        static constexpr size_t kDramMaxWords = 0x1000000u - kDramOffset;
        const int ptr = (p == Port::Data1) ? Hwcf::kSMALW : Hwcf::kSMARW;
        const uint16_t hi = RegVal(Port::Data1Hi, 1, ptr);
        uint32_t addr = RegVal(Port::Data1, 1, ptr) | (static_cast<uint32_t>(hi & 0xFF) << 16);
        if (addr >= kDramOffset && addr - kDramOffset < kDramMaxWords)
        {
            const size_t idx = addr - kDramOffset;
            if (idx >= m_dram.size())
                m_dram.resize(std::min(kDramMaxWords, std::max(idx + 1, m_dram.size() * 2 + 65536)), 0);
            m_dram[idx] = static_cast<int16_t>(value);
        }
        addr = (addr + 1) & 0xFFFFFFu;
        RegRef(Port::Data1, 1, ptr)   = static_cast<uint16_t>(addr & 0xFFFF);
        RegRef(Port::Data1Hi, 1, ptr) = static_cast<uint16_t>((hi & 0xFF00) | (addr >> 16));
    }

    // Zapis IP prepocita cilovou vysku v horni pulce PTRX. Dela to **cip**,
    // ne ovladac - `SBAWE32.MDI` si ji odtud jen precte a necha (viz
    // docs/re-notes/86box_srovnani.md 14.3). 86Box to ma jako
    // `ptrx_pit_target = freqtable[ip] >> 18`, kde
    // `freqtable[c] = 2^((c - 0xE000) / 4096) * 2^32`.
    if (p == Port::Data3 && reg == 0)
    {
        // Mezivysledek musi byt 64bitovy - pro vysoke IP presahne 2^32.
        const double ratio = std::pow(2.0, (static_cast<double>(value) - 0xE000) / 4096.0);
        const uint64_t full = static_cast<uint64_t>(ratio * 65536.0 * 65536.0);
        const uint32_t target = (value == 0)
            ? 0u
            : static_cast<uint32_t>(std::min<uint64_t>(full >> 18, 0xFFFFu));
        RegRef(Port::Data0Hi, 1, voice) = static_cast<uint16_t>(target);
    }

    // Zapis DCYSUSV je podle ovladacu ten, ktery spousti envelope engine
    // ("decay/sustain parameter must be set at last"), takze na nej
    // reagujeme zmenou stavu hlasu.
    // Register side effects on the voice follow 86Box emu8k_outw 1:1:
    //   DCYSUSV (Data1 reg 5): engine on = bit 7 clear. Only the off -> on
    //     transition starts a note: LFOs reset, the volume envelope restarts
    //     when ATKHLDV bit 15 is clear, the mod envelope when ATKHLD bit 15
    //     is clear. A write to a voice that is already on only changes
    //     sustain/decay. Bit 15 = release (applied after a possible start).
    //   ATKHLDV (Data1Hi reg 4) / ATKHLD (Data1Hi reg 6) with bit 15 clear on
    //     a voice whose engine is on: restart that envelope (ATKHLDV also
    //     resets the LFOs).
    //   DCYSUS (Data1 reg 7) bit 15: release of the mod envelope.
    // The sample address comes from the CCCA write (see above); the envelope
    // and filter DSP itself stays our measured model.
    auto& vs = m_voices[voice];
    const auto restartVolEnv = [&]()
    {
        vs.volStage  = EnvStage::Delay;
        vs.volDb     = kFullScaleDb;
        vs.volLin    = 0.0;
        vs.stageTime = 0.0;
    };
    const auto restartModEnv = [&]()
    {
        vs.modStage     = EnvStage::Delay;
        vs.modLevel     = 0.0;
        vs.modStageTime = 0.0;
    };
    const auto resetLfos = [&]()
    {
        vs.lfo1Phase = 0.0;
        vs.lfo2Phase = 0.0;
        vs.lfo1Delay = DelaySeconds(RegVal(Port::Data1Hi, 5, voice));
        vs.lfo2Delay = DelaySeconds(RegVal(Port::Data1Hi, 7, voice));
    };

    if (p == Port::Data1 && reg == 5)
    {
        const bool wasOn = vs.engineOn;
        vs.engineOn = (value & kDcysusvOff) == 0;
        if (!vs.engineOn)
        {
            vs.volStage = EnvStage::Off;
            vs.modStage = EnvStage::Off;
            vs.playing = false;
        }
        else if (!wasOn)
        {
            // start noty
            vs.address   = Read(Reg::CCCA, voice) & kCccaAddressMask;
            vs.frac      = 0;
            vs.playing   = true;
            resetLfos();
            if (!(RegVal(Port::Data1Hi, 4, voice) & 0x8000))
                restartVolEnv();
            if (!(RegVal(Port::Data1Hi, 6, voice) & 0x8000))
                restartModEnv();
            // Vsech pet stavovych promennych filtru, ne jen prvni dve.
            // Hlasy se recykluji: kdyby v druhem stupni (--filter-poles 4)
            // nebo v jednopolove vetvi zustala energie z predchozi noty,
            // zacatek te nove by ji doznival - lupnuti, ktere skutecny cip
            // nedela.
            vs.filtIc1   = 0.0;
            vs.filtIc2   = 0.0;
            vs.filtIc3   = 0.0;
            vs.filtIc4   = 0.0;
            vs.filtLp1   = 0.0;
            vs.filtIc5   = 0.0;
        }
        if (vs.engineOn && (value & kDcysusvRelease))
        {
            if (vs.volStage != EnvStage::Off)
                vs.volStage = EnvStage::Release;
            if (vs.modStage != EnvStage::Off)
                vs.modStage = EnvStage::Release;
            vs.stageTime = 0.0;
            vs.modStageTime = 0.0;
        }
    }
    else if (p == Port::Data1Hi && reg == 4 && !(value & 0x8000) && vs.engineOn)
    {
        resetLfos();
        restartVolEnv();
    }
    else if (p == Port::Data1Hi && reg == 6 && !(value & 0x8000) && vs.engineOn)
    {
        restartModEnv();
    }
    else if (p == Port::Data1 && reg == 7 && (value & 0x8000) && vs.engineOn)
    {
        if (vs.modStage != EnvStage::Off)
            vs.modStage = EnvStage::Release;
        vs.modStageTime = 0.0;
    }
}

uint16_t Emu8000Core::PortIn16(uint16_t port)
{
    const uint16_t value = PortIn16Raw(port);
    // Reads go to the trace as "R <frame> <port> <value>", the format of the
    // 86Box traces, so the driver's decisions can be followed there.
    if (m_traceFile && !m_traceOff)
        std::fprintf(static_cast<FILE*>(m_traceFile), "R %llu %03X %04X\n",
                     static_cast<unsigned long long>(m_traceFrames), port, value);
    return value;
}

uint16_t Emu8000Core::PortIn16Raw(uint16_t port)
{
    // With the 86Box chip every read goes to the chip, like on the bus.
    if (m_chip == Chip::Box86)
        return m_box.PortRead(port);

    const uint16_t off = static_cast<uint16_t>(port - m_basePort);
    if (off == kPortPointer)
    {
        // Ovladace cekaji ve smyckach na prepnuti bitu 12 pointer registru
        // (AWEUTIL sub_12A20). Odvozujeme ho od wave counteru, aby se
        // hostitelsky kod nezasekl.
        return static_cast<uint16_t>((m_pointer & ~0x1000u)
                                     | ((m_waveCounter & 0x100u) ? 0x1000u : 0u));
    }

    Port p;
    switch (off)
    {
    case kPortData0:   p = Port::Data0;   break;
    case kPortData0Hi: p = Port::Data0Hi; break;
    case kPortData1:   p = Port::Data1;   break;
    case kPortData1Hi: p = Port::Data1Hi; break;
    case kPortData3:   p = Port::Data3;   break;
    default: return 0xFFFF;
    }

    const int reg   = (m_pointer >> 5) & 7;
    const int voice = m_pointer & 0x1F;

    // Detekcni registr - ovladac ocekava spodni nibble 0x0C.
    if (p == Port::Data3 && reg == 7 && voice == 0)
        return 0x000C;

    // Wave counter - volne bezici citac.
    if (p == Port::Data1Hi && reg == 1 && voice == Hwcf::kWC)
        return static_cast<uint16_t>(m_waveCounter);

    return RegVal(p, reg, voice);
}

// ===========================================================================
// registrova uroven
// ===========================================================================

namespace
{
    // sel bity 11..9 -> index v Emu8000::Port; mimo rozsah = neplatne
    inline bool PortFromSel(uint16_t sel, Port& out)
    {
        const int portSel = (sel >> 9) & 7;
        if (portSel < 2 || portSel > 6) return false;
        out = static_cast<Port>(portSel - 2);
        return true;
    }
}

namespace
{
    uint16_t PortOffset(Port p)
    {
        return p == Port::Data0   ? kPortData0
             : p == Port::Data0Hi ? kPortData0Hi
             : p == Port::Data1   ? kPortData1
             : p == Port::Data1Hi ? kPortData1Hi
                                  : kPortData3;
    }
}

void Emu8000Core::WriteReg16(uint16_t sel, uint16_t value)
{
    Port p;
    if (!PortFromSel(sel, p)) return;
    // Presne to, co dela ovladac: nejdriv pointer, pak datovy port. Diky tomu
    // je stopa z OpenTrace kompletni a da se prehrat v 86Boxu.
    PortOut16(static_cast<uint16_t>(m_basePort + kPortPointer), SelToPointer(sel));
    PortOut16(static_cast<uint16_t>(m_basePort + PortOffset(p)), value);
}

uint16_t Emu8000Core::ReadReg16(uint16_t sel) const
{
    Port p;
    if (!PortFromSel(sel, p)) return 0;
    if (p == Port::Data3 && SelRegIndex(sel) == 7 && SelVoice(sel) == 0)
        return 0x000C;
    if (p == Port::Data1Hi && SelRegIndex(sel) == 1 && SelVoice(sel) == Hwcf::kWC)
        return static_cast<uint16_t>(m_waveCounter);
    return RegVal(p, SelRegIndex(sel), SelVoice(sel));
}

void Emu8000Core::WriteReg32(uint16_t sel, uint32_t value)
{
    // Realny zapis posle low word na datovy port a high word na port+2,
    // coz u Data0 znamena Data0Hi a u Data1 pak Data1Hi ("Data2").
    Port p;
    if (!PortFromSel(sel, p)) return;

    Port hi;
    if (p == Port::Data0)      hi = Port::Data0Hi;
    else if (p == Port::Data1) hi = Port::Data1Hi;
    else { WriteReg16(sel, static_cast<uint16_t>(value)); return; }

    // Ovladac (AWEUTIL sub_10F46) posle low word na `port` a high word na
    // `port+2`, s jednim zapisem do pointeru pred tim.
    PortOut16(static_cast<uint16_t>(m_basePort + kPortPointer), SelToPointer(sel));
    PortOut16(static_cast<uint16_t>(m_basePort + PortOffset(p)),
              static_cast<uint16_t>(value & 0xFFFF));
    PortOut16(static_cast<uint16_t>(m_basePort + PortOffset(hi)),
              static_cast<uint16_t>(value >> 16));
}

uint32_t Emu8000Core::ReadReg32(uint16_t sel) const
{
    Port p;
    if (!PortFromSel(sel, p)) return 0;

    Port hi;
    if (p == Port::Data0)      hi = Port::Data0Hi;
    else if (p == Port::Data1) hi = Port::Data1Hi;
    else return ReadReg16(sel);

    const int reg   = SelRegIndex(sel);
    const int voice = SelVoice(sel);
    return (static_cast<uint32_t>(RegVal(hi, reg, voice)) << 16)
         | static_cast<uint32_t>(RegVal(p, reg, voice));
}

namespace
{
    // Ktere registry jsou opravdu 32bitove, tj. u kterych je port+2 horni
    // polovina tehoz registru a ne samostatny registr.
    //
    // Data0 (portSel 2): vsech osm registru je 32bitovych.
    // Data1 (portSel 4): 32bitove jsou jen CCCA (reg 0) a HWCF (reg 1).
    //   U reg 2..7 lezi na A22h uplne jiny registr - INIT2, INIT4, ATKHLDV,
    //   LFO1VAL, ATKHLD, LFO2VAL. 32bitovy zapis do DCYSUSV by tedy vynuloval
    //   LFO1VAL. Potvrzeno proti 86Boxu (snd_emu8k.c, case 0xA00 vs 0xA02).
    bool IsReg32(uint16_t sel)
    {
        const int portSel = (sel >> 9) & 7;
        if (portSel == 2) return true;                       // Data0
        if (portSel == 4) return ((sel >> 12) & 7) <= 1;     // Data1: CCCA, HWCF
        return false;
    }
}

void Emu8000Core::Write(Reg r, int voice, uint32_t value)
{
    const uint16_t sel = Sel(r, voice);
    if (IsReg32(sel)) WriteReg32(sel, value);
    else              WriteReg16(sel, static_cast<uint16_t>(value));
}

uint32_t Emu8000Core::Read(Reg r, int voice) const
{
    const uint16_t sel = Sel(r, voice);
    if (IsReg32(sel)) return ReadReg32(sel);
    return ReadReg16(sel);
}

uint32_t Emu8000Core::ReadDriver(Reg r, int voice)
{
    // As the drivers do it (SBAWE32.MDI 0x17B8 / 0x182C): pointer, then the
    // low word from the data port and, for a 32-bit register, the high word
    // from port + 2.
    const uint16_t sel = Sel(r, voice);
    Port p;
    if (!PortFromSel(sel, p)) return 0;
    PortOut16(static_cast<uint16_t>(m_basePort + kPortPointer), SelToPointer(sel));
    const uint16_t lo = PortIn16(static_cast<uint16_t>(m_basePort + PortOffset(p)));
    if (!IsReg32(sel)) return lo;
    const Port hi = (p == Port::Data0) ? Port::Data0Hi : Port::Data1Hi;
    const uint16_t hw = PortIn16(static_cast<uint16_t>(m_basePort + PortOffset(hi)));
    return (static_cast<uint32_t>(hw) << 16) | lo;
}

// ===========================================================================
// inicializace - prepis sekvence z AWEUTIL.COM (sub_12B40)
// ===========================================================================

// Jedno inicializacni pole = 128 hodnot ve ctyrech blocich po 32; kazdy blok
// jde do jineho registru pro hlasy 0..31 (ALSA send_array()).
void Emu8000Core::SendInitArray(const uint16_t* data, const Awe32Init::AltInit* alt)
{
    // Osm hodnot v INIT3/INIT4 posila kazda rodina ovladacu jinak
    // (viz Awe32Driver.h), takze se pro Win95 prepisou.
    uint16_t buf[128];
    std::copy(data, data + 128, buf);
    if (alt && m_driver == Awe32::Driver::Win95)
        for (int i = 0; i < 8; ++i) buf[alt[i].index] = alt[i].value;

    for (int v = 0; v < 32; ++v) Write(Reg::INIT1, v, buf[v]);
    for (int v = 0; v < 32; ++v) Write(Reg::INIT2, v, buf[32 + v]);
    for (int v = 0; v < 32; ++v) Write(Reg::INIT3, v, buf[64 + v]);
    for (int v = 0; v < 32; ++v) Write(Reg::INIT4, v, buf[96 + v]);
}

void Emu8000Core::PowerOnInit()
{
    m_regs = RegFile{};
    m_voices = {};
    m_pointer = 0;
    m_waveCounter = 0;

    // krok 2-4: HWCF1/2/3
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kHWCF1), 0x0059);
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kHWCF2), 0x0020);
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kHWCF3), 0x0004);

    // krok 5 (sub_126E8): 16bitove registry vsech hlasu
    for (int v = 0; v < kMaxVoices; ++v)
    {
        Write(Reg::DCYSUSV, v, kDcysusvOff);
        Write(Reg::ATKHLD,  v, 0);
        Write(Reg::DCYSUS,  v, 0);
        Write(Reg::IP,      v, 0);
        Write(Reg::IFATN,   v, 0xFF00);
        Write(Reg::PEFE,    v, 0);
        Write(Reg::FMMOD,   v, 0);
        Write(Reg::TREMFRQ, v, 0x0018);
        Write(Reg::FM2FRQ2, v, 0x0018);
        Write(Reg::Unk6C,   v, 0);
        Write(Reg::LFO2VAL, v, 0);
        Write(Reg::LFO1VAL, v, 0);
        Write(Reg::ATKHLDV, v, 0);
        Write(Reg::ENVVOL,  v, 0);
        Write(Reg::ENVVAL,  v, 0);
    }

    // krok 6 (sub_127AE): 32bitove registry vsech hlasu.
    // VTFT i CVCF dostavaji 0x0000FFFF, ne 0xFFFFFFFF - horni polovina je
    // hlasitost (0 = ticho), spodni mezni kmitocet filtru (0xFFFF = plne
    // otevreno). Stejne to dela i SBAWE32.DRV (sub_1320).
    for (int v = 0; v < kMaxVoices; ++v)
    {
        Write(Reg::PTRX,    v, 0);
        Write(Reg::VTFT,    v, 0x0000FFFFu);
        Write(Reg::PSST,    v, 0);
        Write(Reg::CSL,     v, 0);
        Write(Reg::CPF,     v, 0);
        Write(Reg::CVCF,    v, 0x0000FFFFu);
        Write(Reg::CCCA,    v, 0);
        Write(Reg::Unk0088, v, 0);
        Write(Reg::Unk0080, v, 0);
    }

    // krok 7 (sub_1288C): SMALR/SMARR/SMALW + init pole.
    //
    // Init pole INIT1..INIT4 jsou koeficienty interniho DSP. Nase emulace je
    // nepouziva - jen se ulozi do registroveho pole - ale 86Box z nich cte
    // parametry reverbu a chorusu, takze se musi poslat, aby sla stopa
    // z OpenTrace prehrat. Poradi je podle ALSA init_arrays().
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kSMALR), 0);
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kSMARR), 0);
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kSMALW), 0);
    // AWEUTIL zapisuje SMARR podruhe, ne SMARW jako linuxovy ovladac.
    // Zmereno ze skutecneho behu, viz docs/re-notes/86box_srovnani.md.
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kSMARR), 0);

    SendInitArray(Awe32Init::kInit1);
    SendInitArray(Awe32Init::kInit2);
    SendInitArray(Awe32Init::kInit3, Awe32Init::kAltInit3Sbawe);

    WriteReg32(MakeSel(1, Port::Data1, Hwcf::kHWCF4), 0x00000000u);
    WriteReg32(MakeSel(1, Port::Data1, Hwcf::kHWCF5), 0x00000083u);
    WriteReg32(MakeSel(1, Port::Data1, Hwcf::kHWCF6), 0x00008000u);
    WriteReg32(MakeSel(1, Port::Data1, Hwcf::kHWCF7), 0x00000000u);

    SendInitArray(Awe32Init::kInit4, Awe32Init::kAltInit4Sbawe);

    // krok 8 (sub_12A20): hlasy 30 a 31 slouzi jako "DRAM refresh" kanaly.
    // Pozor na `cwd` v AWEUTILu: 0xFFE0 se znamenkove rozsiri na 0xFFFFFFE0,
    // zatimco jinde je horni pulka nulovana pres `xor dx,dx`.
    Write(Reg::PSST, 30, 0xFFFFFFE0u);
    Write(Reg::CSL,  30, 0x00FFFFE8u);
    Write(Reg::PTRX, 30, 0x00000000u);
    Write(Reg::CPF,  30, 0x00000000u);
    Write(Reg::CCCA, 30, 0x00FFFFE3u);
    Write(Reg::PSST, 31, 0x00FFFFF0u);
    Write(Reg::CSL,  31, 0x00FFFFF8u);
    Write(Reg::PTRX, 31, 0x000000FFu);
    Write(Reg::CPF,  31, 0x00008000u);
    Write(Reg::CCCA, 31, 0x00FFFFF3u);

    // Tyhle dva zapisy jsou v docs/re-notes/emu8000_register_map.md popsane
    // ("pointer=003Eh ... Data0+2=4828h, pointer=003Ch, Data1=0"), ale az
    // srovnani se skutecnym AWEUTILem v 86Boxu ukazalo, ze v inicializaci
    // opravdu jsou a kam patri. Data1 reg 1 hlas 28 je nezdokumentovany
    // registr HWCF.
    Write(Reg::PTRX, 30, 0x48280000u);
    WriteReg16(MakeSel(1, Port::Data1, 28), 0x0000);

    Write(Reg::VTFT, 30, 0xFFFFFFFFu);   // tady uz je `cwd`, tj. i horni pulka
    Write(Reg::VTFT, 31, 0xFFFFFFFFu);

    // krok 9
    WriteReg16(MakeSel(1, Port::Data1, Hwcf::kHWCF3), 0x0004);

    // SBAWE32.MDI initialises the voices again when the game loads it and
    // ends every voice with DCYSUS = DCYSUSV = 0x807F (0x398C..0x399F in the
    // per-voice loop at 0x3912). Its voice allocation reads DCYSUSV back and
    // an idle voice must show the release bit, otherwise every voice scores
    // 0x1000 and the last odd voice wins instead of voice 0.
    if (m_driver == Awe32::Driver::Dos)
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            Write(Reg::DCYSUS,  v, 0x807Fu);
            Write(Reg::DCYSUSV, v, 0x807Fu);
        }
    }
}

// ===========================================================================
// zvukova pamet
// ===========================================================================

void Emu8000Core::ResizeDram(size_t numSamples)
{
    m_dram.assign(numSamples, 0);
}

int16_t Emu8000Core::ReadSample(uint32_t address) const
{
    if (address < kDramOffset)
    {
        // Wave ROM karty. Pokud neni nactena, adresa cte ticho.
        return (address < m_rom.size()) ? m_rom[address] : 0;
    }
    const size_t idx = address - kDramOffset;
    return (idx < m_dram.size()) ? m_dram[idx] : 0;
}

bool Emu8000Core::IsVoiceActive(int voice) const
{
    if (voice < 0 || voice >= kMaxVoices) return false;
    return m_voices[voice].volStage != EnvStage::Off;
}

// ===========================================================================
// render
// ===========================================================================

void Emu8000Core::RenderVoice(int v, float* outL, float* outR,
                              float* sendRev, float* sendCho, uint32_t numFrames)
{
    VoiceState& vs = m_voices[v];
    if (vs.volStage == EnvStage::Off) return;

    const uint16_t atkhldv = RegVal(Port::Data1Hi, 4, v);
    const uint16_t dcysusv = RegVal(Port::Data1,   5, v);
    const uint16_t envvol  = RegVal(Port::Data1,   4, v);
    const uint16_t atkhld  = RegVal(Port::Data1Hi, 6, v);
    const uint16_t dcysus  = RegVal(Port::Data1,   7, v);
    const uint16_t envval  = RegVal(Port::Data1,   6, v);
    const uint16_t ifatn   = RegVal(Port::Data3,   1, v);
    const uint16_t pefe    = RegVal(Port::Data3,   2, v);
    const uint16_t fmmod   = RegVal(Port::Data3,   3, v);
    const uint16_t tremfrq = RegVal(Port::Data3,   4, v);
    const uint16_t fm2frq2 = RegVal(Port::Data3,   5, v);
    const uint16_t ipReg   = RegVal(Port::Data3,   0, v);

    const uint32_t ccca = Read(Reg::CCCA, v);
    const uint32_t psst = Read(Reg::PSST, v);
    const uint32_t csl  = Read(Reg::CSL,  v);

    const uint32_t loopStart = psst & kLoopAddressMask;
    const uint32_t loopEnd   = csl  & kLoopAddressMask;
    const int      panReg    = static_cast<int>(psst >> kPanShift) & 0xFF;
    const int      filterQ   = static_cast<int>(ccca >> kCccaQShift) & 0x0F;

    // Sendy do efektu: reverb z PTRX bity 15..8, chorus z CSL bity 31..24.
    const float revSend = ((Read(Reg::PTRX, v) >> kReverbShift) & 0xFF) / 255.0f;
    const float choSend = static_cast<float>((csl >> kChorusShift) & 0xFF) / 255.0f;

    // Konstanty obalek (rate registry se za behu obvykle nemeni, takze je
    // staci prevest jednou na blok).
    const double volDelay   = DelaySeconds(envvol);
    // Meritka jsou vychozi 1.0; slouzi k mereni, viz SetHoldScale.
    const double volAttackRaw = AttackSeconds(atkhldv & kAtkhldAttackMask);
    const double volAttack  = (volAttackRaw < 0.0) ? volAttackRaw
                                                   : volAttackRaw * m_attackScale;
    const double volHold    = HoldSeconds((atkhldv & kAtkhldHoldMask) >> 8)
                            * m_holdScale;
    const double volDecayRaw = DecayDbPerSecond(dcysusv & kDcysusvRateMask);
    const double volDecayDb = (volDecayRaw < 0.0) ? volDecayRaw
                                                  : volDecayRaw * m_decayScale;
    const double volSustain = SustainDb((dcysusv & kDcysusvSustainMask) >> 8);

    const double modDelay   = DelaySeconds(envval);
    const double modAttack  = AttackSeconds(atkhld & kAtkhldAttackMask);
    const double modHold    = HoldSeconds((atkhld & kAtkhldHoldMask) >> 8);
    const double modDecayDb = DecayDbPerSecond(dcysus & kDcysusvRateMask);
    const double modSustain = 1.0 - SustainDb((dcysus & kDcysusvSustainMask) >> 8) / kFullScaleDb;

    const double initialAtten = AttenuationDb(LoByte(ifatn));
    const double initialCutoff = static_cast<double>(HiByte(ifatn));

    const double lfo1Hz = LfoHz(LoByte(tremfrq));
    const double lfo2Hz = LfoHz(LoByte(fm2frq2));

    const double dt = 1.0 / kNativeSampleRate;

    // Pan: PSST bity 31..24, kde 0 = zcela VPRAVO a 0xFF = zcela VLEVO [PG].
    const double panNorm = panReg / 255.0;   // 0 = vpravo, 1 = vlevo
    // Cip ma pan jako **prostou nasobicku**, ne constant-power krivku:
    // snd_emu8k.c dela  vol_l = psst_pan, vol_r = 255 - psst_pan  a obe
    // deli 256. Uprostred panoramy tedy jde do kazdeho kanalu polovina
    // (-6 dB), kdezto sin/cos tam dava 0,7071 (-3 dB). Ten rozdil 3,01 dB
    // presne odpovida plochemu posunu, ktery se meril proti 86Boxu.
    // Constant-power zustava jako volba (`--pan power`) na porovnani.
    const float gainL = m_panLinear
        ? static_cast<float>(panNorm)
        : static_cast<float>(std::sin(panNorm * kPi * 0.5));
    const float gainR = m_panLinear
        ? static_cast<float>(1.0 - panNorm)
        : static_cast<float>(std::cos(panNorm * kPi * 0.5));

    // Rezonance filtru: CCCA bity 31..28, 0 = bez rezonance,
    // 15 = cca 24 dB rezonance [PG].
    const double resonanceDb = filterQ * (m_resonanceDb / kCccaQMax);
    // Zaklad, od ktereho rezonance stoupa. Puvodne tu bylo
    //     max(0.7071, pow(10, res/20))
    // jenze `pow` je pro res >= 0 vzdycky >= 1, takze se 0.7071 nikdy
    // neuplatnilo a Q = 0 davalo Q = 1.0, tedy hrb ~1,25 dB u meze.
    // Ma-li Q = 0 znamenat "bez rezonance", je zaklad 0.7071
    // (Butterworth) a rezonance se **nasobi**. Vychozi 1.0 nechava
    // puvodni chovani, aby se dalo merit obe.
    const double qFactor = m_qBase * std::pow(10.0, resonanceDb / 20.0);
    // Manual: pri Q = 0 a plne otevrenem filtru se signal nemeni vubec.
    const bool bypassFilter = (filterQ == 0);
    // Zvedani Q si cip vybira utlumem na vstupu filtru (viz kFilterAtten).
    // Bez toho hraji rezonancni patche vyrazne hlasiteji, nez maji.
    const double filterAttenRaw =
        kFilterAtten[std::clamp(filterQ, 0, 15)] / 65536.0;
    // `m_filterAtten` je mocnina, takze 0,5 znamena polovicni utlum v dB
    // a 0 zadny. Slouzi k mereni, jestli cip opravdu tlumi cele pasmo, nebo
    // jen srovnava spicku u meze - viz docs/re-notes/emu8000_ladeni.md.
    const double filterInputGain =
        (m_filterAtten == 1.0) ? filterAttenRaw
                               : std::pow(filterAttenRaw, m_filterAtten);

    for (uint32_t i = 0; i < numFrames; ++i)
    {
        // ---- volume envelope ------------------------------------------
        switch (vs.volStage)
        {
        case EnvStage::Delay:
            vs.volDb = kFullScaleDb;
            if ((vs.stageTime += dt) >= volDelay) { vs.stageTime = 0.0; vs.volStage = EnvStage::Attack; }
            break;
        case EnvStage::Attack:
            // Attack je linearni v amplitude, decay/release v dB - tak to
            // definuje i SoundFont a odpovida to chovani EMU8000.
            if (volAttack > 0.0) vs.volLin += dt / volAttack;
            if (vs.volLin >= 1.0) { vs.volLin = 1.0; vs.stageTime = 0.0; vs.volStage = EnvStage::Hold; }
            {
                // volLin is the attack PHASE (0..1 of the attack time). The
                // amplitude follows the shape measured on the tester's card
                // (AWETST25 block 8, rates 0x04..0x24, internal capture and
                // line-out agree within 0.05): silent up to ~0.1 T, roughly
                // linear to ~0.75 at 0.8 T, faster to 1.0 at 1.0 T. Values
                // are amplitude / level at 1.0 T, one point per 0.05 T. The
                // small overshoot after the attack (+3..6 % up to ~1.2 T)
                // is not modelled.
                static constexpr double kAttackShape[21] = {
                    0.000, 0.000, 0.005, 0.058, 0.116, 0.203, 0.280, 0.326,
                    0.372, 0.433, 0.493, 0.537, 0.580, 0.621, 0.662, 0.703,
                    0.744, 0.807, 0.899, 0.947, 1.000 };
                const double pos = std::clamp(vs.volLin, 0.0, 1.0) * 20.0;
                const int idx = std::min(static_cast<int>(pos), 19);
                const double amp = kAttackShape[idx]
                                 + (kAttackShape[idx + 1] - kAttackShape[idx]) * (pos - idx);
                vs.volDb = (amp > 0.0) ? -20.0 * std::log10(amp) : kFullScaleDb;
            }
            break;
        case EnvStage::Hold:
            vs.volDb = 0.0;
            if ((vs.stageTime += dt) >= volHold) { vs.stageTime = 0.0; vs.volStage = EnvStage::Decay; }
            break;
        case EnvStage::Decay:
            if (volDecayDb < 0.0) { vs.volStage = EnvStage::Sustain; break; }
            vs.volDb += volDecayDb * dt;
            if (vs.volDb >= volSustain) { vs.volDb = volSustain; vs.volStage = EnvStage::Sustain; }
            break;
        case EnvStage::Sustain:
            vs.volDb = volSustain;
            break;
        case EnvStage::Release:
            // Rate 0 = "bez decay"; hlas by na realnem cipu znel dal, ale
            // ovladac pri Note Off vzdy zapisuje nenulovy release rate.
            //
            // Uvolneni klesa k urovni SUSTAINU, ne k tichu. Ovladac pri
            // note-offu zapisuje sustain 0 (Synth::ReleaseVoice), takze
            // v hudbe je to totez - ale na karte se sustainem 0x7F nota
            // neklesne vubec. Zmereno 2026-09-09, blok 12 nahravky testera.
            vs.volDb += (volDecayDb < 0.0 ? kFullScaleDb : volDecayDb) * dt;
            if (volSustain >= kFullScaleDb - kSustainDbPerStep)
            {
                // Sustain 0 - tak to pise ovladac pri kazdem note-offu.
                // Chovani musi zustat presne jako drive, jinak se posune
                // okamzik uvolneni hlasu a s nim cele prideleni hlasu.
                if (vs.volDb >= kFullScaleDb)
                {
                    vs.volDb = kFullScaleDb;
                    vs.volStage = EnvStage::Off;
                    vs.playing = false;
                    return;
                }
            }
            else if (vs.volDb >= volSustain)
            {
                // Vyssi sustain: uroven se na nem zastavi a nota drzi.
                // Zmereno na karte blokem 12, kde AWETEST psal sustain 0x7F
                // a nota neklesla vubec.
                vs.volDb = volSustain;
            }
            break;
        default:
            return;
        }

        // ---- modulation envelope --------------------------------------
        switch (vs.modStage)
        {
        case EnvStage::Delay:
            if ((vs.modStageTime += dt) >= modDelay) { vs.modStageTime = 0.0; vs.modStage = EnvStage::Attack; }
            break;
        case EnvStage::Attack:
            if (modAttack > 0.0)
            {
                // The card's mod envelope attack is strongly convex (AWETST25
                // block 30: noise through the filter, PEFE 0x7F, cutoff 64,
                // rates 0x10..0x3C). The level read back from the brightness
                // is ~0.45 at 0.05 T and ~0.76 at 0.1 T for every rate (above
                // ~0.7 the measurement saturates); a linear ramp gives 0.05
                // and 0.1. Modelled as 1 - (1 - t/T)^13.5. modStageTime is
                // zero on entering the stage and serves as the attack phase.
                static constexpr double kModAttackExp = 13.5;
                vs.modStageTime += dt;
                const double x = std::min(vs.modStageTime / modAttack, 1.0);
                vs.modLevel = 1.0 - std::pow(1.0 - x, kModAttackExp);
            }
            if (vs.modLevel >= 1.0) { vs.modLevel = 1.0; vs.modStageTime = 0.0; vs.modStage = EnvStage::Hold; }
            break;
        case EnvStage::Hold:
            if ((vs.modStageTime += dt) >= modHold) { vs.modStageTime = 0.0; vs.modStage = EnvStage::Decay; }
            break;
        case EnvStage::Decay:
            if (modDecayDb < 0.0) { vs.modStage = EnvStage::Sustain; break; }
            vs.modLevel -= (modDecayDb / kFullScaleDb) * dt;
            if (vs.modLevel <= modSustain) { vs.modLevel = modSustain; vs.modStage = EnvStage::Sustain; }
            break;
        case EnvStage::Release:
            if (modDecayDb > 0.0)
                vs.modLevel = std::max(0.0, vs.modLevel - (modDecayDb / kFullScaleDb) * dt);
            break;
        default:
            break;
        }

        // ---- LFO -------------------------------------------------------
        double lfo1 = 0.0, lfo2 = 0.0;
        if (vs.lfo1Delay > 0.0) vs.lfo1Delay -= dt;
        else { lfo1 = LfoTriangle(vs.lfo1Phase); vs.lfo1Phase += lfo1Hz * dt; }
        if (vs.lfo2Delay > 0.0) vs.lfo2Delay -= dt;
        else { lfo2 = LfoTriangle(vs.lfo2Phase); vs.lfo2Phase += lfo2Hz * dt; }
        if (vs.lfo1Phase >= 1.0) vs.lfo1Phase -= 1.0;
        if (vs.lfo2Phase >= 1.0) vs.lfo2Phase -= 1.0;

        // ---- vyska tonu ------------------------------------------------
        // Hloubky podle Programmer's Guide: 0x7F = plna kladna hloubka,
        // 0x80 = plna zaporna. Vsechny tri jsou +-1 oktava.
        constexpr double kOct = kPitchPerOctave / 127.0;
        double pitch = static_cast<double>(ipReg);
        pitch += vs.modLevel * HiSigned(pefe)    * kOct * kPefePitchOctaves;
        pitch += lfo1        * HiSigned(fmmod)   * kOct * kFmmodPitchOctaves;
        pitch += lfo2        * HiSigned(fm2frq2) * kOct * kFm2PitchOctaves;

        const double increment = std::pow(2.0,
            (pitch - static_cast<double>(kPitchUnity)) / static_cast<double>(kPitchPerOctave));

        // ---- vzorek ----------------------------------------------------
        float sample = 0.0f;
        if (vs.playing)
        {
            // "the actual audio location is the point 1 word higher than this
            // value due to interpolator offset" [PG] - plati pro CCCA i pro
            // oba konce smycky.
            //
            // Druhy vzorek interpolace se musi zalomit zpatky do smycky.
            // Bez toho se na jejim konci cetla data ZA smyckou, coz delalo
            // nespojitost pri kazdem pruchodu - tedy periodicke lupnuti
            // a sirokopasmovy sum ve vysokych kmitoctech.
            // Cteni s zalomenim do smycky - bez toho by interpolace na
            // konci smycky sahala na data za ni a delala lupnuti.
            // Posun smi byt i zaporny - windowed-sinc je soumerny kolem
            // hraneho mista, takze sahá i pred aktualni vzorek.
            auto tap = [&](int offset) -> double
            {
                int64_t a = static_cast<int64_t>(vs.address) + offset;
                if (a < 0) a = 0;
                uint32_t ua = static_cast<uint32_t>(a);
                if (m_loopWrap && loopEnd > loopStart)
                    while (ua > loopEnd) ua -= (loopEnd - loopStart);
                return ReadSample(ua) / 32768.0;
            };

            const double f = vs.frac / 65536.0;
            if (m_interp == Interp::Cubic)
            {
                // Catmull-Rom pres ctyri body, stejne jako referencni
                // implementace 86Boxu. Body jsou 0,1,2,3 (ne -1..2) kvuli
                // posunu interpolatoru o jedno slovo.
                const double d0 = tap(0), d1 = tap(1), d2 = tap(2), d3 = tap(3);
                const double c0 = -0.5 * f * f * f + f * f - 0.5 * f;
                const double c1 =  1.5 * f * f * f - 2.5 * f * f + 1.0;
                const double c2 = -1.5 * f * f * f + 2.0 * f * f + 0.5 * f;
                const double c3 =  0.5 * f * f * f - 0.5 * f * f;
                sample = static_cast<float>(d0 * c0 + d1 * c1 + d2 * c2 + d3 * c3);
            }
            else if (m_interp == Interp::Sinc)
            {
                // Windowed-sinc (Blackmanovo okno) o `m_sincTaps` bodech.
                // Hrane misto lezi mezi tapy 1 a 2 (posun interpolatoru
                // o slovo), takze se bere soumerne kolem nej: pri osmi
                // bodech -2 az 5, pri sestnacti -6 az 9.
                const int    half = m_sincTaps / 2;
                const int    lo   = -(half - 2);
                const int    hi   = half + 1;
                const double sirka = static_cast<double>(m_sincTaps - 1);
                const double x = 1.0 + f;
                double acc = 0.0, norm = 0.0;
                for (int i = lo; i <= hi; ++i)
                {
                    const double d = x - static_cast<double>(i);
                    double w;
                    if (std::abs(d) < 1e-9)
                    {
                        w = 1.0;
                    }
                    else
                    {
                        const double pd = kPi * d;
                        // Blackmanovo okno pres celou sirku jadra
                        const double t = (d + sirka * 0.5) / sirka;
                        const double bw = 0.42 - 0.5 * std::cos(2.0 * kPi * t)
                                        + 0.08 * std::cos(4.0 * kPi * t);
                        w = std::sin(pd) / pd * bw;
                    }
                    acc += tap(i) * w;
                    norm += w;
                }
                sample = static_cast<float>(norm > 1e-9 ? acc / norm : acc);
            }
            else if (m_interp == Interp::Point3 || m_interp == Interp::Point3c)
            {
                // Kvadratika pres tri body (Lagrange). Dokumentace k AWE32
                // uvadi u cipu "3 Point sample interpolation", takze tohle
                // je blizs realu nez linearni i nez Catmull-Rom.
                //
                // Hrane misto lezi mezi druhym a tretim tapem (posun
                // interpolatoru o slovo), takze u dopredne varianty jsou to
                // tapy 1,2,3 a f bezi od 0 do 1 mezi tapy 1 a 2.
                const int b = (m_interp == Interp::Point3) ? 1 : 0;
                const double a0 = tap(b), a1 = tap(b + 1), a2 = tap(b + 2);
                const double g = (m_interp == Interp::Point3) ? f : (f + 1.0);
                const double l0 = 0.5 * (g - 1.0) * (g - 2.0);
                const double l1 = -g * (g - 2.0);
                const double l2 = 0.5 * g * (g - 1.0);
                sample = static_cast<float>(a0 * l0 + a1 * l1 + a2 * l2);
            }
            else
            {
                const double s0 = tap(1), s1 = tap(2);
                sample = static_cast<float>(s0 + (s1 - s0) * f);
            }

            const uint64_t step = static_cast<uint64_t>(increment * 65536.0);
            uint64_t pos = (static_cast<uint64_t>(vs.address) << 16) | vs.frac;
            pos += step;
            vs.address = static_cast<uint32_t>(pos >> 16);
            vs.frac = static_cast<uint32_t>(pos & 0xFFFF);

            if (loopEnd > loopStart && vs.address >= loopEnd)
                vs.address -= (loopEnd - loopStart);
        }

        // ---- filtr -----------------------------------------------------
        // Modulace se pocitaji rovnou v oktavach, jak je udava manual:
        // PEFE lo +-6 oktav, FMMOD lo +-3 oktavy.
        // Zaklad z registru. Dve mapovani, protoze prameny se neshoduji:
        // Programmer's Guide udava krajni body 125 Hz a 8 kHz (a k tomu si
        // protireci "ve ctvrt pultonech"), Vuova prirucka rika primo
        //     f = 100 Hz + registr * 31,25 Hz,
        // tedy **linearne v Hz**. U registru 128 je v tom faktor ctyri.
        // Modulace jsou v obou pripadech v oktavach, jak je udava manual:
        // PEFE lo +-6 oktav, FMMOD lo +-3 oktavy.
        const double baseHz = m_cutoffLinear
            ? (kCutoffLinearBaseHz + initialCutoff * kCutoffLinearStepHz)
            : CutoffHz(CutoffOctaves(initialCutoff, m_filterTopHz, m_cutoffBaseHz),
                       m_cutoffBaseHz);
        double octaves = 0.0;
        octaves += vs.modLevel * LoSigned(pefe)  / 127.0 * kPefeFilterOctaves;
        octaves += lfo1        * LoSigned(fmmod) / 127.0 * kFmmodFilterOctaves;
        // Posun meze s rostoucim Q (ladici, vychozi 0). Spolecny fit bloku 7
        // a 28 z run5 dava 0,16 oktavy dolu pri Q 15 - viz SetQCutoffShift.
        if (m_qCutoffShiftOct != 0.0)
            octaves -= m_qCutoffShiftOct * filterQ / 15.0;

        double filtered;
        const double filterIn = sample * filterInputGain;

        if (m_filter86)
        {
            // Presne to, co dela snd_emu8k.c - branev FILTER_MOOG, ktera je
            // v tom souboru skutecne aktivni (FILTER_INITIAL, kterou jsme
            // portovali driv, je tam zakomentovana #if 0 a nikdy nebezi;
            // prvni pokus proto merenim nic nezlepsil). Ctyrstupnova
            // kaskada jednopolovych filtru se zpetnou vazbou (Moog ladder).
            // Filtr se vynecha jen pri Q == 0 **a** celych 16 bitech
            // cutoffu na 0xFFFF - ovladac zapisuje cutoff<<8, takze k tomu
            // nedojde a filtr bezi i pri "plne otevreno".
            const int qidx = std::clamp(filterQ, 0, 15);
            const int cidx = std::clamp(static_cast<int>(initialCutoff), 0, 255);
            const uint16_t ctoff16 = static_cast<uint16_t>(cidx << 8);
            if (qidx == 0 && ctoff16 == 0xFFFF)
            {
                filtered = sample;
            }
            else
            {
                const double fc = Cutoff86Hz(cidx) * std::pow(2.0, octaves);
                // Dolni orez na mez registru 0 - viz TPT vetev nize.
                const double w0 = std::sin(2.0 * kPi
                                           * std::clamp(fc, Cutoff86Hz(0), kNativeSampleRate * 0.49)
                                           / kNativeSampleRate);
                const double qFactor86 = 1.0 - w0;
                const double p = w0 + 0.8 * w0 * qFactor86;
                const double coef0 = p;
                const double coef1 = p + p - 1.0;
                const double resonance = (1.0 - std::pow(2.0, -qidx * 24.0 / 90.0)) * 0.8;
                const double coef2 = resonance
                    * (1.0 + 0.5 * qFactor86 * (w0 + 5.6 * qFactor86 * qFactor86));

                // Praci delame v normalizovanych jednotkach (plny rozsah =
                // 1.0), coz je algebraicky totez jako fixed-point <<8/>>24
                // v 86Boxu - jen bez ztraty presnosti zaokrouhlenim.
                // "Dvojnasobek rozsahu" na oriznuti tam odpovida 2.0 tady.
                auto clip2 = [](double v) { return std::clamp(v, -2.0, 2.0); };

                const double x = sample - coef2 * vs.filtIc5;
                const double t1 = vs.filtIc2;
                vs.filtIc2 = clip2((x + vs.filtIc1) * coef0 - vs.filtIc2 * coef1);
                const double t2 = vs.filtIc3;
                vs.filtIc3 = clip2((vs.filtIc2 + t1) * coef0 - vs.filtIc3 * coef1);
                const double t3 = vs.filtIc4;
                vs.filtIc4 = clip2((vs.filtIc3 + t2) * coef0 - vs.filtIc4 * coef1);
                vs.filtIc5 = clip2((vs.filtIc4 + t3) * coef0 - vs.filtIc5 * coef1);
                vs.filtIc1 = clip2(x);
                filtered = vs.filtIc5;
            }
        }
        else if (bypassFilter && initialCutoff >= 255.0 - 1e-9 && octaves >= -1e-9)
        {
            // "If the Q of the channel is programmed to zero and the filter
            // cutoff to 0xFF, the filter does not alter the signal." [PG]
            filtered = sample;   // Q=0 a plne otevreno: beze zmeny
        }
        else if (m_filterCham)
        {
            // Chamberlin SVF (Dattorro form): L += F*B; H = in - L - q*B; B += F*H.
            // Cutoff comes from our map with the same lower clamp (register 0)
            // as the TPT branch below. F is capped at 1 (fc = fs/6): the fit of
            // the card sits there too, and with this damping the filter is
            // nearly flat across the band at F = 1 - which is why the card
            // barely filters at the highest cutoffs.
            const double cutoffFloorHz = m_cutoffLinear ? kCutoffLinearBaseHz
                                                        : m_cutoffBaseHz;
            const double cutoffHz = std::clamp(baseHz * std::pow(2.0, octaves),
                                               cutoffFloorHz, kNativeSampleRate / 6.0);
            const double F = 2.0 * std::sin(kPi * cutoffHz / kNativeSampleRate);
            const double qd = 1.0 / (kChamQ0 * std::pow(10.0, filterQ * kChamDbPerQ / 20.0));
            vs.filtIc1 += F * vs.filtIc2;                         // low-pass state
            const double hp = filterIn - vs.filtIc1 - qd * vs.filtIc2;
            vs.filtIc2 += F * hp;                                 // band-pass state
            filtered = vs.filtIc1;
        }
        else
        {
            // Dolni orez je mez REGISTRU 0, ne 20 Hz. Zmereno na karte
            // (run5 blok 28, PEFE -128..-64 na cutoffu 128): hloubsi
            // zavreni uz uroven nemeni, plosina sedi na mez registru 0
            // (fit 1,16 dB; s orezem 20 Hz 10,36 dB). 86Box orezava
            // `filtercut` na rozsah registru taky. Horni orez se nemeni -
            // na karte ho z mereni videt neni.
            const double cutoffFloorHz = m_cutoffLinear ? kCutoffLinearBaseHz
                                                        : m_cutoffBaseHz;
            const double cutoffHz = std::clamp(baseHz * std::pow(2.0, octaves),
                                               cutoffFloorHz, kNativeSampleRate * 0.49);

            // Topology-preserving state variable filter. Chamberlinova
            // varianta se pri vyssich mezich rozkmitava (podminka f + 1/Q < 2
            // pri 4 kHz a Q=0.707 uz neplati), tahle je stabilni az k Nyquistu.
            const double g = std::tan(kPi * cutoffHz / kNativeSampleRate);
            // S krivkou z awe32faq se rezonance pocita az tady, protoze
            // zavisi na **aktualni** mezi filtru (tedy i na modulaci).
            double qNow = qFactor;
            // Jen pro Q > 0. Radek "Coeff 0" v tabulce awe32faq uvadi pri
            // nizke mezi 5 dB, ale ve druhem sloupci "Flat" - tezko to bude
            // rezonance, kdyz Q je nula. Zmereno: kdyz se ten radek uplatnil,
            // dostalo 779 not v DANCE.MID rezonanci 2,6 dB, kterou driv
            // nemely, a obe nahravky Dance se zhorsily (4,122 -> 4,168).
            if (m_resonanceCurve && filterQ > 0)
            {
                const int qi = std::clamp(filterQ, 0, 15);
                const double t = std::clamp(
                    std::log2(cutoffHz / Emu8000::kResonanceLowHz)
                        / std::log2(Emu8000::kResonanceHighHz
                                    / Emu8000::kResonanceLowHz), 0.0, 1.0);
                const double db = Emu8000::kResonanceLowDb[qi] * (1.0 - t)
                                + Emu8000::kResonanceHighDb[qi] * t;
                qNow = m_qBase * std::pow(10.0, db / 20.0);
            }
            const double k = 1.0 / qNow;
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double a2 = g * a1;
            const double a3 = g * a2;

            if (m_filterPoles == 1)
            {
                // jednopolovy (6 dB/okt) - na porovnani, jak strmy filtr
                // skutecna karta vlastne ma
                const double a = g / (1.0 + g);
                vs.filtLp1 += a * (filterIn - vs.filtLp1);
                filtered = vs.filtLp1;
            }
            else
            {
                const double v3 = filterIn - vs.filtIc2;
                const double v1 = a1 * vs.filtIc1 + a2 * v3;
                const double v2 = vs.filtIc2 + a2 * vs.filtIc1 + a3 * v3;
                vs.filtIc1 = 2.0 * v1 - vs.filtIc1;
                vs.filtIc2 = 2.0 * v2 - vs.filtIc2;
                filtered = v2;   // low-pass vystup

                if (m_filterPoles >= 4)
                {
                    // druhy stejny stupen v kaskade = 24 dB na oktavu
                    const double w3 = filtered - vs.filtIc4;
                    const double w1 = a1 * vs.filtIc3 + a2 * w3;
                    const double w2 = vs.filtIc4 + a2 * vs.filtIc3 + a3 * w3;
                    vs.filtIc3 = 2.0 * w1 - vs.filtIc3;
                    vs.filtIc4 = 2.0 * w2 - vs.filtIc4;
                    filtered = w2;
                }
            }
        }

        // ---- hlasitost --------------------------------------------------
        // Tremolo: TREMFRQ bity 15..8. Jen utlum, a jen v pulperiode, kde
        // lfo * hloubka < 0 - viz kTremoloChipMaxDb (drive +-6 dB kolem nuly,
        // coz v druhe pulperiode zesilovalo).
        double db = vs.volDb + initialAtten;
        db += std::max(0.0, -lfo1 * HiSigned(tremfrq)) * (kTremoloChipMaxDb / 127.0);
        const double gain = DbToLinear(db);

        const float out = static_cast<float>(filtered * gain);
        outL[i] += out * gainL;
        outR[i] += out * gainR;

        // Sendy jdou z vystupu hlasu jeste pred panoramou, tedy monofonne.
        sendRev[i] += out * revSend;
        sendCho[i] += out * choSend;
    }
}

void Emu8000Core::UpdateRegistersFromState(int v)
{
    // Aby hostitelsky kod, ktery si registry cte (napr. reversed hra),
    // videl smysluplny aktualni stav.
    //
    // Do stopy tohle nepatri - jsou to zpetne zapisy stavu jadra, ne akce
    // ovladace, a bylo by jich 32 na kazdy vzorek. 86Box si stejne hodnoty
    // pocita sam.
    const TraceOff noTrace(*this);

    const VoiceState& vs = m_voices[v];
    const uint32_t ccca = Read(Reg::CCCA, v);
    Write(Reg::CCCA, v, (ccca & ~kCccaAddressMask) | (vs.address & kCccaAddressMask));

    const uint16_t ifatn = RegVal(Port::Data3, 1, v);
    const double gain = DbToLinear(vs.volDb + AttenuationDb(LoByte(ifatn)));
    const uint16_t curVol = static_cast<uint16_t>(std::clamp(gain, 0.0, 1.0) * 65535.0);
    RegRef(Port::Data0Hi, 2, v) = curVol;   // CVCF hi16 = current volume
    // VTFT hi16 = volume target. The `dos` driver reads it to pick a voice,
    // so a silent voice must read 0 (86Box: vtft_vol_target).
    RegRef(Port::Data0Hi, 3, v) = (vs.volStage == EnvStage::Off) ? uint16_t{0} : curVol;

    // CPF: horni pulka je LINEARNI prirustek (0x4000 = 1.0), spodni je
    // zlomkova cast adresy [PG]. IP je oproti tomu logaritmicky.
    const double increment = std::pow(2.0,
        (static_cast<double>(RegVal(Port::Data3, 0, v)) - kPitchUnity) / kPitchPerOctave);
    const uint16_t cpfHi = static_cast<uint16_t>(
        std::clamp(increment * kCpfUnity, 0.0, 65535.0));
    RegRef(Port::Data0Hi, 0, v) = cpfHi;
    RegRef(Port::Data0,   0, v) = static_cast<uint16_t>(vs.frac);
}

void Emu8000Core::RenderNative(float* outL, float* outR, uint32_t numFrames)
{
    std::fill(outL, outL + numFrames, 0.0f);
    std::fill(outR, outR + numFrames, 0.0f);

    m_sendReverb.assign(numFrames, 0.0f);
    m_sendChorus.assign(numFrames, 0.0f);

    for (int v = 0; v < kMaxVoices; ++v)
    {
        RenderVoice(v, outL, outR, m_sendReverb.data(), m_sendChorus.data(), numFrames);
        UpdateRegistersFromState(v);
    }

    // Chorus jde do vystupu a zaroven doplnuje reverb, stejne jako
    // v signalovem diagramu cipu.
    //
    // Navratove urovne. Reverb si zisk normalizuje sam (viz Emu8000Effects.h),
    // takze o mnozstvi efektu rozhoduje uz jen send z registru. Hodnoty jsou
    // empiricke, ne odvozene z hardwaru.
    const float kReverbReturn = m_reverbReturn;
    const float kChorusReturn = m_chorusReturn;

    // The effect presets follow the INIT words the driver wrote.
    UpdateEffectPresets();

    for (uint32_t i = 0; i < numFrames; ++i)
    {
        float cl, cr;
        m_chorus.Process(m_sendChorus[i], cl, cr);
        cl *= kChorusReturn;
        cr *= kChorusReturn;
        outL[i] += cl;
        outR[i] += cr;
        m_sendReverb[i] += (cl + cr) * 0.5f;

        float rl, rr;
        m_reverb.Process(m_sendReverb[i], rl, rr);
        outL[i] += rl * kReverbReturn;
        outR[i] += rr * kReverbReturn;
    }

    // Ekvalizer je az za efekty, na celem vystupu cipu (Programmer's Guide:
    // bass/treble na vystupu DSP). Poloha se bere z registru INIT3/INIT4,
    // takze sedi i na jiny ovladac nebo hru, ktera EQ prestavi.
    if (m_eqOn)
    {
        UpdateEqualizer();
        for (uint32_t i = 0; i < numFrames; ++i)
            m_eq.Process(outL[i], outR[i]);
    }

    m_waveCounter += numFrames;
    m_traceFrames += numFrames;
}

void Emu8000Core::UpdateEqualizer()
{
    // Poradi slotu jako snd_emu8000_update_equalizer (alsa_emu8000_init.c):
    // INIT4 0x01, 0x11 (bass), INIT3 0x11, 0x13, 0x1B, INIT4 0x07, 0x0B, 0x0D,
    // 0x17, 0x19 (treble).
    const uint16_t v[10] = {
        RegVal(Port::Data1Hi, 3, 0x01), RegVal(Port::Data1Hi, 3, 0x11),
        RegVal(Port::Data1,   3, 0x11), RegVal(Port::Data1,   3, 0x13),
        RegVal(Port::Data1,   3, 0x1B), RegVal(Port::Data1Hi, 3, 0x07),
        RegVal(Port::Data1Hi, 3, 0x0B), RegVal(Port::Data1Hi, 3, 0x0D),
        RegVal(Port::Data1Hi, 3, 0x17), RegVal(Port::Data1Hi, 3, 0x19),
    };
    int bass = m_eq.Bass(), treble = m_eq.Treble();
    Emu8000Fx::EqIndexFromInit(v, bass, treble);
    m_eq.Set(bass, treble);
}

void Emu8000Core::UpdateEffectPresets()
{
    // INIT1/INIT2 are register 2, INIT3/INIT4 register 3; the odd ones are
    // written through Data1, the even ones through Data2 (Data1Hi here).
    const auto init = [this](int n, int slot) {
        return RegVal((n % 2) ? Port::Data1 : Port::Data1Hi, (n <= 2) ? 2 : 3, slot);
    };
    if (m_revFromRegs)
    {
        uint16_t v[28];
        for (int i = 0; i < 28; ++i)
            v[i] = init(Emu8000Fx::kReverbSlots[i].init, Emu8000Fx::kReverbSlots[i].slot);
        const int p = Emu8000Fx::ReverbPresetFromInit(v);
        if (p >= 0 && p != m_revDecoded) { m_revDecoded = p; m_reverb.SetPreset(p); }
    }
    if (m_choFromRegs)
    {
        const int p = Emu8000Fx::ChorusPresetFromInit(init(3, 0x09), init(3, 0x0C), init(4, 0x03));
        if (p >= 0 && p != m_choDecoded) { m_choDecoded = p; m_chorus.SetPreset(p); }
    }
}

bool Emu8000Core::UseBox86Chip(const std::string& romPath, std::string& err)
{
    // 86Box allocates the DRAM itself; the size decides where uploads wrap.
    if (!m_box.Init(romPath, m_basePort + 0x400, m_chipRamKb, err))
        return false;
    m_chip = Chip::Box86;
    return true;
}

uint32_t Emu8000Core::ChipLatencyFrames() const
{
    return (m_chip == Chip::Box86) ? Emu8000Box::kLatencyFrames : 0u;
}

int16_t* Emu8000Core::ChipRam()
{
    return m_box.Ram();
}

size_t Emu8000Core::ChipRamWords() const
{
    return m_box.RamWords();
}

void Emu8000Core::RenderBlock(int16_t* out, uint32_t numFrames)
{
    if (numFrames == 0) return;

    auto emit = [&](uint32_t i, float l, float r)
    {
        const float sl = std::clamp(l, -1.0f, 1.0f);
        const float sr = std::clamp(r, -1.0f, 1.0f);
        out[i * 2 + 0] = static_cast<int16_t>(sl * 32767.0f);
        out[i * 2 + 1] = static_cast<int16_t>(sr * 32767.0f);
    };

    if (m_outputRate == kNativeSampleRate)
    {
        m_nativeL.resize(numFrames);
        m_nativeR.resize(numFrames);
        RenderNative(m_nativeL.data(), m_nativeR.data(), numFrames);
        if (m_chip == Chip::Box86)
        {
            // 86Box da rovnou int32 a oreze ho na int16 - zadny prevod
            // pres float, aby vysledek sel porovnat bajt po bajtu
            // s emu8k_ref.exe.
            for (uint32_t i = 0; i < numFrames; ++i)
            {
                int32_t l = 0;
                int32_t r = 0;
                m_box.RenderFrame(l, r);
                out[i * 2 + 0] = static_cast<int16_t>(std::clamp(l, -32768, 32767));
                out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -32768, 32767));
            }
            return;
        }
        for (uint32_t i = 0; i < numFrames; ++i)
            emit(i, m_nativeL[i], m_nativeR[i]);
        return;
    }

    // Linearni resampling z nativnich 44100 Hz na vystupni frekvenci.
    // m_nativeL/R slouzi jako carry buffer mezi volanimi.
    const double ratio = static_cast<double>(kNativeSampleRate) / m_outputRate;
    const size_t needed = static_cast<size_t>(
        std::ceil(m_resamplePos + ratio * numFrames)) + 2;

    if (m_nativeL.size() < needed)
    {
        const size_t have = m_nativeL.size();
        m_nativeL.resize(needed);
        m_nativeR.resize(needed);
        RenderNative(m_nativeL.data() + have, m_nativeR.data() + have,
                     static_cast<uint32_t>(needed - have));
    }

    for (uint32_t i = 0; i < numFrames; ++i)
    {
        const double q = m_resamplePos + ratio * i;
        const size_t i0 = static_cast<size_t>(q);
        const float f = static_cast<float>(q - i0);
        const float l = m_nativeL[i0] + (m_nativeL[i0 + 1] - m_nativeL[i0]) * f;
        const float r = m_nativeR[i0] + (m_nativeR[i0 + 1] - m_nativeR[i0]) * f;
        emit(i, l, r);
    }

    m_resamplePos += ratio * numFrames;
    const size_t consumed = static_cast<size_t>(m_resamplePos);
    if (consumed > 0)
    {
        m_nativeL.erase(m_nativeL.begin(), m_nativeL.begin() + consumed);
        m_nativeR.erase(m_nativeR.begin(), m_nativeR.begin() + consumed);
        m_resamplePos -= static_cast<double>(consumed);
    }
}
