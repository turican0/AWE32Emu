#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>
#include <string>
#include "Emu8000Regs.h"
#include "Emu8000Box.h"
#include "Emu8000Effects.h"
#include "Awe32InitArrays.h"
#include "Awe32Driver.h"

// ---------------------------------------------------------------------------
// Emu8000Core - register-level emulace cipu EMU8000 (Sound Blaster AWE32).
//
// Registrova mapa i inicializacni sekvence jsou odvozene z disassembly
// ovladace AWEUTIL.COM, viz docs/re-notes/emu8000_register_map.md.
//
// Trida ma zamerne DVE urovne rozhrani:
//
//   1) Portova uroven (PortOut16/PortIn16) - presne to, co dela realny
//      ovladac: OUT na pointer registr + OUT na datovy port. Tohle je
//      cesta pro napojeni reversed DOS hry, ktera si registry nastavuje
//      sama (viz README, pouziti 2).
//
//   2) Registrova uroven (WriteReg16/32, Write/Read s enumem Reg) - pro
//      nas vlastni MIDI prehravac (Synth.cpp), ktery nemusi simulovat
//      I/O porty.
//
// Cip bezi nativne na 44100 Hz; RenderBlock umi vystup i na jinou
// frekvenci (linearni resampling), ale vnitrni casovani je vzdy 44100.
// ---------------------------------------------------------------------------

class Emu8000Core
{
public:
    static constexpr int kMaxVoices = Emu8000::kMaxVoices;
    static constexpr uint32_t kNativeSampleRate = 44100;

    explicit Emu8000Core(uint32_t outputSampleRate);

    // ---- portova uroven -------------------------------------------------
    void SetBasePort(uint16_t sbBasePort);          // default 0x220
    uint16_t BasePort() const { return m_basePort; }
    bool OwnsPort(uint16_t port) const;
    void PortOut16(uint16_t port, uint16_t value);
    uint16_t PortIn16(uint16_t port);

    // ---- registrova uroven ----------------------------------------------
    void WriteReg16(uint16_t sel, uint16_t value);
    uint16_t ReadReg16(uint16_t sel) const;
    void WriteReg32(uint16_t sel, uint32_t value);   // low word, pak high word
    uint32_t ReadReg32(uint16_t sel) const;

    void Write(Emu8000::Reg r, int voice, uint32_t value);
    uint32_t Read(Emu8000::Reg r, int voice) const;

    // Inicializacni sekvence prevzata z AWEUTIL.COM (sub_12B40 a jeho
    // podrutiny), vcetne poli INIT1..INIT4 (Awe32InitArrays.h). Nase jadro
    // z init poli nic necte, ale 86Box z nich dekoduje reverb a chorus, takze
    // musi byt ve stope - viz docs/re-notes/emu8000_register_map.md.
    void PowerOnInit();

    // Ktera rodina ovladacu se emuluje. Meni osm hodnot v init polich
    // INIT3/INIT4 - viz Awe32Driver.h. Nastavit pred PowerOnInit().
    void SetDriver(Awe32::Driver d) { m_driver = d; }
    Awe32::Driver DriverVariant() const { return m_driver; }

    // ---- zvukova pamet ---------------------------------------------------
    // Adresy v registrech CCCA/PSST/CSL jsou 24bit a pocitaji se ve vzorcich.
    // Uzivatelska DRAM zacina na Emu8000::kDramOffset; nize lezi ROM karty,
    // kterou nemame (cteni vraci 0).
    void ResizeDram(size_t numSamples);
    size_t DramSize() const { return m_dram.size(); }
    int16_t* DramData() { return m_dram.data(); }
    const int16_t* DramData() const { return m_dram.data(); }

    // Wave ROM karty se mapuje od adresy 0. Emulace ji bere jako obycejnou
    // cast adresniho prostoru - hlas nepozna rozdil.
    void LoadWaveRom(std::vector<int16_t> rom) { m_rom = std::move(rom); }
    size_t RomSize() const { return m_rom.size(); }

    int16_t ReadSample(uint32_t address) const;

    // ---- render ----------------------------------------------------------
    // out = interleaved stereo int16, numFrames snimku na vystupni frekvenci.
    void RenderBlock(int16_t* out, uint32_t numFrames);

    bool IsVoiceActive(int voice) const;

    // Interpolace vzorku. `Point3` je ta, kterou uvadi dokumentace
    // ("3 Point sample interpolation", Vu, Un-official AWE32 Programming
    // Guide 1995) - a je i **vychozi**: zmereno proti 20 dvojicim
    // nahravka/MIDI (tests/tune.py), prumerne skore 5,937 proti 6,025
    // u puvodni kubicke (Catmull-Rom). Zmena zavedena 2026-09-02.
    // Dve varianty se lisi tim, ktere tri vzorky beru: `Point3` cte
    // dopredu (tapy 1,2,3), `Point3c` je soumerna kolem hraneho mista
    // (tapy 0,1,2) - v mereni jsou k nerozeznani (5,9369 vs 5,9370),
    // `Point3` se drzi jen kvuli shode s konvenci "tap(1) = aktualni
    // vzorek" pouzitou uz u Linear/Cubic.
    // `Sinc` je osmibodovy windowed-sinc - ostrejsi nez vsechny ostatni.
    // Neni to domnenka: zmereno na Hi-Octane, kde shoda se zeleznem roste
    // monotonne s ostrosti jadra (linear 4,271 -> Point3 4,187 ->
    // Cubic 3,915), a zaroven nam nad 6,4 kHz chybi energie (6,1 % proti
    // 19,1 %). Patent US 5,111,727 popisuje u G-chipu FIR navrzeny
    // Remezovym algoritmem, coz je taky **ostry** filtr - kvadraticka
    // Lagrangeova interpolace (`Point3`) je proti nemu prilis mekka.
    enum class Interp { Linear, Cubic, Point3, Point3c, Sinc };
    void SetInterpolation(Interp i) { m_interp = i; }

    // Ladici parametry filtru. Programmer's Guide si u meznich kmitoctu
    // protireci (ctvrt pultony vs "0xFF = 8 kHz") a proti skutecne karte nam
    // nad 3 kHz chybi energie, takze se to musi dat proměřit.
    //   topHz  - kmitocet pri registrove hodnote 0xFF
    //   poles  - 1, 2 nebo 4 (6, 12 nebo 24 dB na oktavu)
    void SetFilterTopHz(double hz) { m_filterTopHz = hz; }
    // Zaklad rezonance filtru: 1.0 = puvodni chovani, 0.7071 = Butterworth
    // pri Q = 0. Viz vypocet qFactor v Emu8000.cpp.
    void SetQBase(double q)       { m_qBase = q; }
    // Prevod registru IFATN(15..8) na mezni kmitocet. `false` = dosavadni
    // exponencialni (125 Hz -> 8 kHz pres 255 kroku), `true` = linearni
    // v Hz podle Vuovy prirucky:  f = 100 Hz + registr * 31,25 Hz.
    // Rozdil je velky - u registru 128 vyjde 1006 Hz proti 4100 Hz.
    void SetCutoffLinear(bool on)  { m_cutoffLinear = on; }
    // Podoba filtru. `false` = nase TPT (stabilni az k Nyquistu),
    // `true` = presne to, co dela snd_emu8k.c v 86Boxu:
    //   - koeficient w0 = sin(2*pi*fc/fs), ne tan
    //   - mez z tabulky 125 Hz * 1.016378315^index (42,66 dilku na oktavu)
    //   - vstup zeslaben podle Q tabulkou filter_atten
    //   - filtr se vynecha jen kdyz Q == 0 **a** cely 16bit cutoff je 0xFFFF
    // Ta posledni podminka je ten podstatny rozdil: ovladac zapisuje
    // cutoff<<8, tedy 0xFF00, takze 86Box filtruje i pri "plne otevreno",
    // kdezto my jsme se drzeli Programmer's Guide a filtr vypinali.
    void SetFilter86Box(bool on)   { m_filter86 = on; }
    // Krivka panoramy. `true` (vychozi) = prosta nasobicka jako v cipu:
    //   vlevo = pan/255, vpravo = (255-pan)/255
    // `false` = constant-power sin/cos, coz jsme meli driv. Uprostred
    // panoramy se lisi o 3,01 dB a je to presne ten plochy rozdil, ktery
    // se meril proti 86Boxu (3,37-3,41 dB na izolovanych notach).
    void SetPanLinear(bool on)     { m_panLinear = on; }
    // Ma se druhy (treti...) vzorek interpolace zalomit zpatky do smycky?
    // My to delame, `snd_emu8k.c` ne - jeho `EMU8K_READ` cte linearne dal
    // za konec smycky. Zalomeni je "cistsi", ale prave tim muze ubirat
    // vysoke kmitocty, kterych mame proti zeleze min (6,1 % proti 19,1 %
    // nad 6,4 kHz). Vychozi je zalomeni; vypina se `--loop-wrap off`.
    void SetLoopWrap(bool on)      { m_loopWrap = on; }
    void SetFilterPoles(int p)     { m_filterPoles = p; }
    double FilterTopHz() const     { return m_filterTopHz; }
    int    FilterPoles() const     { return m_filterPoles; }

    // Ladici pristup k efektum - velikost prostoru a tlumeni reverbu nejsou
    // odvozene z hardwaru (init pole jsou DSP koeficienty), takze se overuji
    // merenim proti referencnim nahravkam.
    void SetReverbRoom(float size, float damp) { m_reverb.SetRoom(size, damp); }
    void SetReverbPreset(int p) { m_reverb.SetPreset(p); }
    void SetChorusPreset(int p) { m_chorus.SetPreset(p); }
    void SetEffectReturns(float rev, float cho) { m_reverbReturn = rev; m_chorusReturn = cho; }

    // ---- varianta cipu ---------------------------------------------------
    // `Ours` je nase vlastni jadro (float, laditelny filtr). `Box86` posila
    // tytez portove zapisy do nezmeneneho `snd_emu8k.c` z 86Boxu a zvuk bere
    // odtamtud - viz Emu8000Box.h. Nase jadro pri tom bezi dal naprazdno,
    // aby zustala stejna evidence hlasu, a tim i identicky proud registru.
    enum class Chip { Ours, Box86 };
    bool UseBox86Chip(const std::string& romPath, std::string& err);
    Chip ChipVariant() const { return m_chip; }
    // O kolik snimku je vystup cipu pozadu (u Box86 jeden blok).
    uint32_t ChipLatencyFrames() const;
    int16_t* ChipRam();
    size_t   ChipRamWords() const;

    // ---- zaznam portovych zapisu (pro srovnani s 86Boxem) ---------------
    // Zapise kazdy PortOut16 jako "<snimek> <port> <hodnota>" na nativni
    // casove ose 44100 Hz. Vysledek se da prehrat pres ref86box/emu8k_ref.exe,
    // ktery pouziva nezmeneny snd_emu8k.c z 86Boxu - viz ref86box/README.md.
    bool OpenTrace(const char* path);
    void CloseTrace();

private:
    // Registrove pole: [port][reg][voice], vse jako 16bit slova.
    // 32bit registry = dvojice (Data0,Data0Hi) resp. (Data1,Data1Hi).
    using RegFile = std::array<std::array<std::array<uint16_t, kMaxVoices>,
                                          8>,
                               static_cast<size_t>(Emu8000::Port::Count)>;

    enum class EnvStage { Off, Delay, Attack, Hold, Decay, Sustain, Release };

    struct VoiceState
    {
        // prehravani vzorku
        uint32_t address = 0;      // celociselna cast (vzorky)
        uint32_t frac = 0;         // 16bit zlomkova cast
        bool     playing = false;

        // volume envelope
        EnvStage volStage = EnvStage::Off;
        double   volDb = 96.0;     // aktualni utlum v dB (0 = plna hlasitost)
        double   volLin = 0.0;     // linearni zisk behem attack faze
        double   stageTime = 0.0;  // sekundy stravene v aktualni fazi

        // modulation envelope
        EnvStage modStage = EnvStage::Off;
        double   modLevel = 0.0;
        double   modStageTime = 0.0;

        // LFO
        double lfo1Phase = 0.0;
        double lfo2Phase = 0.0;
        double lfo1Delay = 0.0;
        double lfo2Delay = 0.0;

        // low-pass filtr (topology-preserving SVF, viz poznamka v .cpp)
        double filtIc1 = 0.0;
        double filtIc2 = 0.0;
        double filtIc3 = 0.0;   // druhy stupen pri --filter-poles 4
        double filtIc4 = 0.0;
        double filtLp1 = 0.0;   // jednopolovy filtr pri --filter-poles 1
        double filtIc5 = 0.0;   // paty stupen pro --filter-mode 86box (FILTER_MOOG)
    };

    void RenderNative(float* outL, float* outR, uint32_t numFrames);
    void RenderVoice(int v, float* outL, float* outR, float* sendRev, float* sendCho,
                     uint32_t numFrames);
    void UpdateRegistersFromState(int v);
    void SendInitArray(const uint16_t* data, const Awe32Init::AltInit* alt = nullptr);

    uint16_t& RegRef(Emu8000::Port p, int reg, int voice);
    uint16_t  RegVal(Emu8000::Port p, int reg, int voice) const;

    uint32_t m_outputRate;
    // Zmereno na vsech 23 pouzitelnych dvojicich: sinc 5,2784, cubic 5,2856,
    // point3 5,3212. Rozdil sinc vs. cubic dela **jen** Hi-Octane (4,138 a
    // 4,128 proti 4,221 a 4,265) - na zbytku je sinc o ~0,007 horsi. Bereme
    // ho proto, ze Hi-Octane je nejcistsi material, ktery mame.
    Interp m_interp = Interp::Sinc;
    double m_filterTopHz = 8000.0;
    double m_qBase       = 1.0;
    bool   m_cutoffLinear = false;
    bool   m_filter86     = false;
    bool   m_panLinear    = true;
    bool   m_loopWrap     = true;
    int    m_filterPoles = 2;
    float m_reverbReturn = 1.0f;
    float m_chorusReturn = 0.7f;
    uint16_t m_basePort = 0x220;
    uint16_t m_pointer = 0;      // posledni zapis do pointer registru
    Awe32::Driver m_driver = Awe32::kDefaultDriver;

    Chip m_chip = Chip::Ours;
    Emu8000Box m_box;

    RegFile m_regs{};
    std::array<VoiceState, kMaxVoices> m_voices{};
    std::vector<int16_t> m_dram;
    std::vector<int16_t> m_rom;

    // Wave counter (registr WC) - volne bezici citac vzorku. Ovladace ho
    // pouzivaji jako casovou zakladnu v cekacich smyckach (viz AWEUTIL
    // sub_127AE), takze ho musime tikat, jinak by se ovladac zasekl.
    uint32_t m_waveCounter = 0;

    // Zaznam portovych zapisu (viz OpenTrace).
    void* m_traceFile = nullptr;   // FILE*, drzeno jako void* aby se sem netahal <cstdio>
    uint64_t m_traceFrames = 0;    // pocet snimku vyrenderovanych na 44100 Hz
    bool m_traceOff = false;

    // RAII prepinac pro zapisy, ktere nemaji byt ve stope (viz
    // UpdateRegistersFromState).
    struct TraceOff
    {
        Emu8000Core& c;
        bool prev;
        explicit TraceOff(Emu8000Core& core) : c(core), prev(core.m_traceOff) { c.m_traceOff = true; }
        ~TraceOff() { c.m_traceOff = prev; }
    };

    // Efektove sbernice. Send se bere z vystupu hlasu jeste pred panoramou
    // (viz signalovy diagram v Programmer's Guide), takze jsou monofonni.
    Emu8000Fx::Chorus m_chorus;
    Emu8000Fx::Reverb m_reverb;
    std::vector<float> m_sendReverb, m_sendChorus;

    // resampling na vystupni frekvenci
    std::vector<float> m_nativeL, m_nativeR;
    double m_resamplePos = 0.0;
    float m_lastL = 0.0f, m_lastR = 0.0f;
};
