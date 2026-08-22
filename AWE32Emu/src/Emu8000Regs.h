#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Registrova mapa EMU8000 (Sound Blaster AWE32)
//
// VSE v tomto souboru je odvozeno z disassembly ovladace AWEUTIL.COM,
// viz docs/re-notes/emu8000_register_map.md. Tam je u kazde polozky
// uvedeno, jestli jde o vec potvrzenou kodem ovladace [ASM], doplnenou
// z verejne dokumentace [DOC], nebo o odhad k overeni [?].
//
// Adresovani cipu:
//   pointer registr (base+0xC02) = (regIndex << 5) | voice
//   datovy port                  = base + portOffset
//   32bit registr = zapis low wordu na port, high wordu na port+2
// ---------------------------------------------------------------------------

namespace Emu8000
{
    inline constexpr int kMaxVoices = 32;   // 32 hardwarovych hlasu

    // -- I/O porty relativne k bazi Sound Blasteru (typicky 0x220) --------
    // Odvozeno z sub_10EAC/sub_10F46 v AWEUTIL.COM.
    inline constexpr uint16_t kPortData0    = 0x400; // 0x620 - low word 32bit reg
    inline constexpr uint16_t kPortData0Hi  = 0x402; // 0x622 - high word
    inline constexpr uint16_t kPortData1    = 0x800; // 0xA20 - low word 32bit reg
    inline constexpr uint16_t kPortData1Hi  = 0x802; // 0xA22 - high word / "Data2"
    inline constexpr uint16_t kPortData3    = 0xC00; // 0xE20 - 16bit registry
    inline constexpr uint16_t kPortPointer  = 0xC02; // 0xE22 - pointer registr

    // Poradi tak, jak je pouzivame jako index do registroveho pole.
    enum class Port : int
    {
        Data0   = 0,   // 0x620
        Data0Hi = 1,   // 0x622
        Data1   = 2,   // 0xA20
        Data1Hi = 3,   // 0xA22  (v dokumentaci "Data2")
        Data3   = 4,   // 0xE20
        Count   = 5
    };

    // "sel" kodovani, presne jak ho pouziva AWEUTIL:
    //   sel = (regIndex << 12) | (portSel << 9) | voice
    // portSel: 2=Data0, 3=Data0Hi, 4=Data1, 5=Data1Hi, 6=Data3
    inline constexpr int PortSelOf(Port p)
    {
        return static_cast<int>(p) + 2;
    }
    inline constexpr uint16_t MakeSel(int regIndex, Port p, int voice)
    {
        return static_cast<uint16_t>((regIndex << 12) | (PortSelOf(p) << 9) | (voice & 0x1F));
    }
    inline constexpr int SelRegIndex(uint16_t sel) { return (sel >> 12) & 7; }
    inline constexpr int SelVoice(uint16_t sel)    { return sel & 0x1F; }

    // Pointer registr sestaveny ze sel (viz sub_10EAC).
    inline constexpr uint16_t SelToPointer(uint16_t sel)
    {
        return static_cast<uint16_t>(((sel & 0x7000) >> 7) | (sel & 0x1F));
    }

    // -- Pojmenovane registry --------------------------------------------
    // Hodnota = "sel" s voice == 0; cislo hlasu se pri pouziti pricte.
    enum class Reg : uint16_t
    {
        // Data0, 32bit
        CPF     = MakeSel(0, Port::Data0, 0),  // Current Pitch + Fractional address
        PTRX    = MakeSel(1, Port::Data0, 0),  // Pitch Target + Reverb send + aux
        CVCF    = MakeSel(2, Port::Data0, 0),  // Current Volume + Current Filter cutoff
        VTFT    = MakeSel(3, Port::Data0, 0),  // Volume Target + Filter cutoff Target
        Unk0080 = MakeSel(4, Port::Data0, 0),
        Unk0088 = MakeSel(5, Port::Data0, 0),
        PSST    = MakeSel(6, Port::Data0, 0),  // Pan + Loop Start address
        CSL     = MakeSel(7, Port::Data0, 0),  // Chorus send + Loop End address

        // Data1, 32bit / 16bit
        CCCA    = MakeSel(0, Port::Data1, 0),  // Filter Q + control + Current address
        HWCF    = MakeSel(1, Port::Data1, 0),  // "voice" slouzi jako index registru
        INIT1   = MakeSel(2, Port::Data1, 0),
        INIT3   = MakeSel(3, Port::Data1, 0),
        ENVVOL  = MakeSel(4, Port::Data1, 0),  // delay volume envelope
        DCYSUSV = MakeSel(5, Port::Data1, 0),  // decay/sustain volume envelope
        ENVVAL  = MakeSel(6, Port::Data1, 0),  // delay modulation envelope
        DCYSUS  = MakeSel(7, Port::Data1, 0),  // decay/sustain modulation envelope

        // Data1Hi ("Data2"), 16bit
        HWCF_HI = MakeSel(1, Port::Data1Hi, 0),// SMRD (v26), WC (v27)
        INIT2   = MakeSel(2, Port::Data1Hi, 0),
        INIT4   = MakeSel(3, Port::Data1Hi, 0),
        ATKHLDV = MakeSel(4, Port::Data1Hi, 0),// attack/hold volume envelope
        LFO1VAL = MakeSel(5, Port::Data1Hi, 0),// delay LFO1
        ATKHLD  = MakeSel(6, Port::Data1Hi, 0),// attack/hold modulation envelope
        LFO2VAL = MakeSel(7, Port::Data1Hi, 0),// delay LFO2

        // Data3, 16bit
        IP      = MakeSel(0, Port::Data3, 0),  // Initial Pitch
        IFATN   = MakeSel(1, Port::Data3, 0),  // Initial Filter cutoff + Attenuation
        PEFE    = MakeSel(2, Port::Data3, 0),  // Pitch/Filter envelope amount
        FMMOD   = MakeSel(3, Port::Data3, 0),  // LFO1 -> pitch / filter
        TREMFRQ = MakeSel(4, Port::Data3, 0),  // LFO1 -> volume / frekvence LFO1
        FM2FRQ2 = MakeSel(5, Port::Data3, 0),  // LFO2 -> pitch / frekvence LFO2
        Unk6C   = MakeSel(6, Port::Data3, 0),
        ChipId  = MakeSel(7, Port::Data3, 0),  // cteni: ocekava se 0x000C
    };

    inline constexpr uint16_t Sel(Reg r, int voice = 0)
    {
        return static_cast<uint16_t>(static_cast<uint16_t>(r) | (voice & 0x1F));
    }

    // Registry na (Data1, reg 1), kde cislo hlasu je vlastne index registru.
    // Hodnoty potvrzene inicializacni sekvenci AWEUTILu.
    namespace Hwcf
    {
        inline constexpr int kHWCF4 = 9;
        inline constexpr int kHWCF5 = 10;
        inline constexpr int kHWCF6 = 13;
        inline constexpr int kHWCF7 = 14;
        inline constexpr int kSMALR = 20;  // sample memory address, left read
        inline constexpr int kSMARR = 21;  // ... right read
        inline constexpr int kSMALW = 22;  // ... left write
        inline constexpr int kSMARW = 23;  // ... right write
        inline constexpr int kSMLD  = 26;  // sample memory left data
        inline constexpr int kSMRD  = 26;  // ... right data (na Data1Hi)
        inline constexpr int kWC    = 27;  // wave counter (na Data1Hi)
        inline constexpr int kHWCF1 = 29;
        inline constexpr int kHWCF2 = 30;
        inline constexpr int kHWCF3 = 31;
    }

    // -- Vyznam bitu -----------------------------------------------------

    // IP (Initial Pitch): 0xE000 = bez posunu vysky, 0x1000 = jedna oktava.
    // Logaritmicka skala. [PG]
    inline constexpr uint16_t kPitchUnity = 0xE000;
    inline constexpr int kPitchPerOctave  = 4096;

    // CPF (Current Pitch): narozdil od IP je LINEARNI prirustek,
    // 0x4000 = bez posunu (tj. prirustek 1.0). [PG]
    inline constexpr uint16_t kCpfUnity = 0x4000;

    // CCCA: bity 31..28 = Q (0 = bez rezonance, 15 = cca 24 dB),
    // bit 27 vzdy 0, bit 26 = DMA, bit 25 = WR (1 = zapis), bit 24 = RIGHT. [PG]
    inline constexpr uint32_t kCccaAddressMask = 0x00FFFFFFu;
    inline constexpr int      kCccaQShift      = 28;
    inline constexpr uint32_t kCccaDma         = 0x04000000u;
    inline constexpr uint32_t kCccaDmaWrite    = 0x02000000u;
    inline constexpr uint32_t kCccaDmaRight    = 0x01000000u;
    inline constexpr int      kCccaQMax        = 15;
    // Utlum filtru podle Q. Rezonancni filtr EMU8000 si zvedanim Q zaroven
    // ubira na vstupu - dokumentace k NRPN uvadi, ze utlum je zhruba polovina
    // Q v dB (pro Q 12 dB tedy -6 dB). Tabulka je v amplitude, meritko 65536.
    // Odpovida tabulce filter_atten v referencni implementaci 86Boxu.
    inline constexpr int kFilterAtten[16] = {
        65536, 61869, 57079, 53269, 49145, 44820, 40877, 34792,
        32845, 30653, 28607, 26392, 24630, 22463, 20487, 18470
    };

    inline constexpr double   kResonanceMaxDb  = 24.0;

    // PSST: bity 31..24 = pan, POZOR 0 = zcela vpravo, 0xFF = zcela vlevo. [PG]
    // CSL:  bity 31..24 = chorus send (0 = nic, 0xFF = maximum). [PG]
    inline constexpr uint32_t kLoopAddressMask = 0x00FFFFFFu;
    inline constexpr int      kPanShift        = 24;
    inline constexpr int      kChorusShift     = 24;

    // PTRX: bity 31..16 = pitch target, 15..8 = reverb send, 7..0 = aux. [PG]
    inline constexpr int kReverbShift = 8;

    // DCYSUSV / DCYSUS [PG]:
    //   bit 15    = 0 zapisuje decay, 1 zapisuje release
    //   bity 14-8 = sustain level po 0.75 dB (0x7F = bez utlumu, 0 = ticho)
    //   bit 7     = envelope generator vypnut (u DCYSUS vzdy 0)
    //   bity 6-0  = kodovany decay/release rate (0 = bez decay)
    inline constexpr uint16_t kDcysusvRelease     = 0x8000;
    inline constexpr uint16_t kDcysusvSustainMask = 0x7F00;
    inline constexpr uint16_t kDcysusvOff         = 0x0080;
    inline constexpr uint16_t kDcysusvRateMask    = 0x007F;
    inline constexpr double   kSustainDbPerStep   = 0.75;

    // ATKHLDV / ATKHLD [PG]:
    //   bit 15    = 0 spousti attack
    //   bity 14-8 = hold po 92 ms (0x7F = bez prodlevy, 0 = 11.68 s)
    //   bit 7     = vzdy 0
    //   bity 6-0  = kodovany attack (0 = nikdy, 1 = 11.88 s, 0x7F = 6 ms)
    inline constexpr uint16_t kAtkhldHoldMask   = 0x7F00;
    inline constexpr uint16_t kAtkhldAttackMask = 0x007F;
    inline constexpr double   kHoldSecPerStep   = 0.092;

    // IFATN [PG]: bity 15-8 = pocatecni mezni kmitocet filtru po ctvrt
    // pultonu od 125 Hz, bity 7-0 = utlum po 0.375 dB (0xFF = 96 dB).
    inline constexpr double kAttenDbPerStep  = 0.375;
    inline constexpr double kAttenMaxDb      = 96.0;
    // Mezni kmitocet filtru pri registrove hodnote 0: **101,81 Hz**, a jeden
    // krok registru je 29,3843 centu (40,84 kroku na oktavu), takze registr
    // 0xFF vychazi na 7717 Hz.
    //
    // Neni to odhad z protirecici si dokumentace. `SYNTHGM.SBK` (SF1)
    // a `SYNTHGM.SF2` z DOSoveho SDK popisuji tytez presety, jen v jinych
    // jednotkach - z te dvojice jde odecist prevod primo:
    //
    //     SF1   4 -> 4602 centu       SF1  89 ->  9617
    //     SF1  54 -> 7552             SF1 101 -> 10325
    //     SF1  60 -> 7906             SF1 105 -> 10561
    //
    // Rozestupy vychazeji na presnych 59,0 centu na krok SF1 (2950/50,
    // 354/6, 1711/29, 708/12, 236/4). Extrapolace na SF1 0 dava 4366 centu
    // = 101,81 Hz - tedy tech "100 Hz", ktere uvadi i komentar v 86Boxu.
    // Registr je dvojnasobek SF1 (presneji x255/127), takze na krok registru
    // pripada 59 * 127/255 = 29,3843 centu.
    //
    // Zvlastni pripad: **SF1 127 ma v SF2 hodnotu 14400 centu** (33 kHz),
    // coz neni bod na te primce, ale "filtr dokoran".
    inline constexpr double kCutoffBaseHz     = 101.81;
    inline constexpr double kCutoffBaseCents  = 4366.0;
    inline constexpr double kCutoffCentsStep  = 29.3843;
    inline constexpr int    kCutoffPerOctave = 48;    // ctvrt pultonu

    // ENVVOL / ENVVAL / LFO1VAL / LFO2VAL [PG]: 0x8000 = bez prodlevy,
    // nizsi hodnoty = rostouci prodleva po 725 us.
    inline constexpr uint16_t kDelayNone      = 0x8000;
    inline constexpr double   kDelaySecPerStep = 0.000725;

    // Hloubky modulaci [PG]. Vsechny jsou znamenkove bajty, kde 0x7F
    // odpovida plne kladne hloubce a 0x80 plne zaporne.
    inline constexpr double kPefePitchOctaves  = 1.0;   // PEFE hi:  env -> pitch
    inline constexpr double kPefeFilterOctaves = 6.0;   // PEFE lo:  env -> filtr
    inline constexpr double kFmmodPitchOctaves = 1.0;   // FMMOD hi: LFO1 vibrato
    inline constexpr double kFmmodFilterOctaves = 3.0;  // FMMOD lo: LFO1 -> filtr
    inline constexpr double kTremoloMaxDb      = 12.0;  // TREMFRQ hi: LFO1 tremolo
    inline constexpr double kFm2PitchOctaves   = 1.0;   // FM2FRQ2 hi: LFO2 vibrato
    inline constexpr double kLfoHzPerStep      = 0.042; // 0xFF = 10.72 Hz

    // Adresni prostor zvukove pameti. ROM karty lezi pod 0x200000,
    // uzivatelska DRAM zacina na 0x200000 (adresy jsou ve vzorcich,
    // ne v bajtech).
    inline constexpr uint32_t kDramOffset = 0x200000u;

}
