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
        return (127 - std::clamp(hold, 0, 127)) * kHoldSecPerStep;
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

    double CutoffOctaves(double cutoffReg, double topHz = kCutoffTopHz)
    {
        // kolik oktav nad zakladem lezi dana registrova hodnota
        const double octavesTotal = std::log2(topHz / kCutoffBaseHz);
        return std::clamp(cutoffReg, 0.0, 255.0) / 255.0 * octavesTotal;
    }

    double CutoffHz(double octavesAboveBase)
    {
        return kCutoffBaseHz * std::pow(2.0, octavesAboveBase);
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
    RegRef(p, reg, voice) = value;

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
    if (p == Port::Data1 && reg == 5)
    {
        auto& vs = m_voices[voice];
        if (value & kDcysusvOff)
        {
            vs.volStage = EnvStage::Off;
            vs.modStage = EnvStage::Off;
            vs.playing = false;
        }
        else if (value & kDcysusvRelease)
        {
            if (vs.volStage != EnvStage::Off)
                vs.volStage = EnvStage::Release;
            if (vs.modStage != EnvStage::Off)
                vs.modStage = EnvStage::Release;
            vs.stageTime = 0.0;
            vs.modStageTime = 0.0;
        }
        else
        {
            // start noty
            vs.address   = Read(Reg::CCCA, voice) & kCccaAddressMask;
            vs.frac      = 0;
            vs.playing   = true;
            vs.volStage  = EnvStage::Delay;
            vs.volDb     = kFullScaleDb;
            vs.volLin    = 0.0;
            vs.stageTime = 0.0;
            vs.modStage  = EnvStage::Delay;
            vs.modLevel  = 0.0;
            vs.modStageTime = 0.0;
            vs.lfo1Phase = 0.0;
            vs.lfo2Phase = 0.0;
            vs.lfo1Delay = DelaySeconds(RegVal(Port::Data1Hi, 5, voice));
            vs.lfo2Delay = DelaySeconds(RegVal(Port::Data1Hi, 7, voice));
            vs.filtIc1   = 0.0;
            vs.filtIc2   = 0.0;
        }
    }
}

uint16_t Emu8000Core::PortIn16(uint16_t port)
{
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
    const double volAttack  = AttackSeconds(atkhldv & kAtkhldAttackMask);
    const double volHold    = HoldSeconds((atkhldv & kAtkhldHoldMask) >> 8);
    const double volDecayDb = DecayDbPerSecond(dcysusv & kDcysusvRateMask);
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
    const float gainL = static_cast<float>(std::sin(panNorm * kPi * 0.5));
    const float gainR = static_cast<float>(std::cos(panNorm * kPi * 0.5));

    // Rezonance filtru: CCCA bity 31..28, 0 = bez rezonance,
    // 15 = cca 24 dB rezonance [PG].
    const double resonanceDb = filterQ * (kResonanceMaxDb / kCccaQMax);
    const double qFactor = std::max(0.7071, std::pow(10.0, resonanceDb / 20.0));
    // Manual: pri Q = 0 a plne otevrenem filtru se signal nemeni vubec.
    const bool bypassFilter = (filterQ == 0);
    // Zvedani Q si cip vybira utlumem na vstupu filtru (viz kFilterAtten).
    // Bez toho hraji rezonancni patche vyrazne hlasiteji, nez maji.
    const double filterInputGain =
        kFilterAtten[std::clamp(filterQ, 0, 15)] / 65536.0;

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
            vs.volDb = (vs.volLin > 0.0) ? -20.0 * std::log10(vs.volLin) : kFullScaleDb;
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
            vs.volDb += (volDecayDb < 0.0 ? kFullScaleDb : volDecayDb) * dt;
            if (vs.volDb >= kFullScaleDb)
            {
                vs.volDb = kFullScaleDb;
                vs.volStage = EnvStage::Off;
                vs.playing = false;
                return;
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
            if (modAttack > 0.0) vs.modLevel += dt / modAttack;
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
            auto tap = [&](uint32_t offset) -> double
            {
                uint32_t a = vs.address + offset;
                if (loopEnd > loopStart)
                    while (a > loopEnd) a -= (loopEnd - loopStart);
                return ReadSample(a) / 32768.0;
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
        double octaves = CutoffOctaves(initialCutoff, m_filterTopHz);
        octaves += vs.modLevel * LoSigned(pefe)  / 127.0 * kPefeFilterOctaves;
        octaves += lfo1        * LoSigned(fmmod) / 127.0 * kFmmodFilterOctaves;

        double filtered;
        const double filterIn = sample * filterInputGain;
        if (bypassFilter && octaves >= CutoffOctaves(255.0, m_filterTopHz) - 1e-9)
        {
            // "If the Q of the channel is programmed to zero and the filter
            // cutoff to 0xFF, the filter does not alter the signal." [PG]
            filtered = sample;   // Q=0 a plne otevreno: beze zmeny
        }
        else
        {
            const double cutoffHz = std::clamp(CutoffHz(octaves),
                                               20.0, kNativeSampleRate * 0.49);

            // Topology-preserving state variable filter. Chamberlinova
            // varianta se pri vyssich mezich rozkmitava (podminka f + 1/Q < 2
            // pri 4 kHz a Q=0.707 uz neplati), tahle je stabilni az k Nyquistu.
            const double g = std::tan(kPi * cutoffHz / kNativeSampleRate);
            const double k = 1.0 / qFactor;
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
        // Tremolo: TREMFRQ bity 15..8, +-12 dB pri 0x7F/0x80 [PG].
        double db = vs.volDb + initialAtten;
        db -= lfo1 * HiSigned(tremfrq) * (kTremoloMaxDb / 127.0);
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

    m_waveCounter += numFrames;
    m_traceFrames += numFrames;
}

bool Emu8000Core::UseBox86Chip(const std::string& romPath, std::string& err)
{
    // 8 MB DRAM, at se vejde i velka banka; 86Box si RAM alokuje sam.
    if (!m_box.Init(romPath, m_basePort + 0x400, 8192, err))
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
