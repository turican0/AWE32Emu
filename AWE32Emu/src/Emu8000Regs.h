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

    // Pitch: 0xE000 = jednotkovy prirustek (prehravani rychlosti 44100 Hz),
    // 0x1000 (4096) na oktavu. [DOC, k overeni v SBAWE32.DRV]
    inline constexpr uint16_t kPitchUnity = 0xE000;
    inline constexpr int kPitchPerOctave  = 4096;

    // CCCA
    inline constexpr uint32_t kCccaAddressMask = 0x00FFFFFFu;
    inline constexpr int      kCccaQShift      = 28;          // bity 31..28 = filter Q
    inline constexpr uint32_t kCccaControlMask = 0x0F000000u; // bity 27..24 [?]
    inline constexpr uint32_t kCccaDmaActive   = 0x08000000u; // [?]
    inline constexpr uint32_t kCccaDmaWrite    = 0x04000000u; // [?]

    // PSST / CSL: horni bajt = pan / chorus send, spodnich 24 bitu = adresa
    inline constexpr uint32_t kLoopAddressMask = 0x00FFFFFFu;
    inline constexpr int      kPanShift        = 24;
    inline constexpr int      kChorusShift     = 24;

    // PTRX: hi16 = pitch target, bity 15..8 = reverb send
    inline constexpr int kReverbShift = 8;

    // DCYSUSV / DCYSUS: bit 15 = spusteni release faze (envelope engine),
    // bity 14..8 = sustain level, bity 6..0 = decay/release rate. [DOC/?]
    inline constexpr uint16_t kDcysusvRelease   = 0x8000;
    inline constexpr uint16_t kDcysusvSustainMask = 0x7F00;
    inline constexpr uint16_t kDcysusvRateMask  = 0x007F;
    // Hodnota zapisovana pri inicializaci (envelope engine vypnuty).
    inline constexpr uint16_t kDcysusvOff       = 0x0080;

    // ATKHLDV / ATKHLD: bity 14..8 = hold, bity 6..0 = attack rate,
    // bit 15 = "attack modulation off" [?]
    inline constexpr uint16_t kAtkhldHoldMask   = 0x7F00;
    inline constexpr uint16_t kAtkhldAttackMask = 0x007F;

    // Adresni prostor zvukove pameti. ROM karty lezi pod 0x200000,
    // uzivatelska DRAM zacina na 0x200000 (adresy jsou ve vzorcich,
    // ne v bajtech). [DOC]
    inline constexpr uint32_t kDramOffset = 0x200000u;
}
