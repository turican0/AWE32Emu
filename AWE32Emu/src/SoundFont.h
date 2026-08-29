#pragma once
#include "Awe32Driver.h"
#include <cstdint>
#include <array>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Obecny loader SoundFont banky - zvlada SoundFont 1.0 (`.SBK`) i SF2 (`.sf2`).
//
// Zamerne NENI sity na zadnou konkretni banku. Rozdily obou verzi (viz
// docs/re-notes/soundfont1_sbk.md):
//
//   SF1.0                              SF2
//   -----                              ---
//   shdr 16 B (4 dwordy)               shdr 46 B vcetne jmena/sr/root key
//   jmena vzorku v chunku `snam`       jmena uvnitr shdr
//   generatory = primo registry        generatory normalizovane
//     EMU8000, casy v milisekundach      (timecents, centibely, centy)
//   adresy uz jsou hotove pro cip      adresy jsou indexy do `smpl`
//
// Vzorek, jehoz jmeno zacina '*', lezi ve wave ROM karty (SF1 konvence);
// v SF2 to same znaci priznak ROM v `sfSampleType`. Banka muze pouzivat
// oba zdroje soucasne.
// ---------------------------------------------------------------------------

namespace SoundFont
{
    enum class Version { Sf1, Sf2 };

    // Cisla generatoru podle specifikace (spolecna pro obe verze; SF1 jen
    // nektera nepouziva a jinak je interpretuje).
    namespace Gen
    {
        enum : int
        {
            StartAddrsOffset = 0, EndAddrsOffset = 1,
            StartloopAddrsOffset = 2, EndloopAddrsOffset = 3,
            StartAddrsCoarseOffset = 4,
            ModLfoToPitch = 5, VibLfoToPitch = 6, ModEnvToPitch = 7,
            InitialFilterFc = 8, InitialFilterQ = 9,
            ModLfoToFilterFc = 10, ModEnvToFilterFc = 11,
            EndAddrsCoarseOffset = 12, ModLfoToVolume = 13,
            ChorusEffectsSend = 15, ReverbEffectsSend = 16, Pan = 17,
            DelayModLFO = 21, FreqModLFO = 22, DelayVibLFO = 23, FreqVibLFO = 24,
            DelayModEnv = 25, AttackModEnv = 26, HoldModEnv = 27,
            DecayModEnv = 28, SustainModEnv = 29, ReleaseModEnv = 30,
            KeynumToModEnvHold = 31, KeynumToModEnvDecay = 32,
            DelayVolEnv = 33, AttackVolEnv = 34, HoldVolEnv = 35,
            DecayVolEnv = 36, SustainVolEnv = 37, ReleaseVolEnv = 38,
            KeynumToVolEnvHold = 39, KeynumToVolEnvDecay = 40,
            Instrument = 41, KeyRange = 43, VelRange = 44,
            StartloopAddrsCoarseOffset = 45, Keynum = 46, Velocity = 47,
            InitialAttenuation = 48, EndloopAddrsCoarseOffset = 50,
            CoarseTune = 51, FineTune = 52, SampleID = 53, SampleModes = 54,
            // 55 je v SF2 nepouzity; v SF1 bankach se objevuje s hodnotou
            // typu 6000 = zakladni nota v centech. Viz docs/re-notes.
            Sf1RootPitchCents = 55,
            ScaleTuning = 56, ExclusiveClass = 57, OverridingRootKey = 58,
            Count = 60
        };
    }

    // Sada generatoru s priznakem "bylo nastaveno" (kvuli spravnemu
    // skladani zon a defaultum).
    struct GenSet
    {
        std::array<int16_t, Gen::Count> value{};
        std::array<bool, Gen::Count> present{};

        void Set(int op, int16_t v)
        {
            if (op >= 0 && op < Gen::Count) { value[op] = v; present[op] = true; }
        }
        bool Has(int op) const { return op >= 0 && op < Gen::Count && present[op]; }
        int Get(int op, int fallback) const { return Has(op) ? value[op] : fallback; }
        // Slozeni preset zony (offsety) nad instrument zonou (absolutni).
        void AddFrom(const GenSet& other);
        void OverrideFrom(const GenSet& other);
    };

    struct Sample
    {
        std::string name;
        uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0;
        uint32_t sampleRate = 44100;
        uint8_t  originalKey = 60;
        int8_t   correction = 0;
        bool     inRom = false;
    };

    struct Zone
    {
        GenSet gen;
        int keyLo = 0, keyHi = 127;
        int velLo = 0, velHi = 127;
        int sampleId = -1;     // zona instrumentu
        int instrument = -1;   // zona presetu
    };

    struct Instrument
    {
        std::string name;
        GenSet global;
        std::vector<Zone> zones;
    };

    struct Preset
    {
        std::string name;
        int bank = 0, program = 0;
        GenSet global;
        std::vector<Zone> zones;
    };

    // Jedna vrstva, ktera ma pri dane note zaznit.
    struct Region
    {
        const Sample* sample = nullptr;
        GenSet gen;
        // SF1: utlum se **neda** vzit ze slozeneho GenSetu. Kazda uroven
        // (zona instrumentu, zona presetu) prispiva `127 - v` jednotkami
        // registru a ty se scitaji; soucet surovych SF1 hodnot by dal
        // nesmysl. -1 znamena "nepocitano" (banka je SF2).
        int sf1AttenUnits = -1;
    };

    struct Bank
    {
        bool valid = false;
        std::string errorMessage;
        Version version = Version::Sf2;
        std::string name;
        std::string romName;            // INFO/irom - jakou ROM banka ceka
        // True = vzorky teto banky nelezi v jejim chunku smpl, ale ve wave
        // ROM karty. Tak se pouzije popis GM banky (napr. 1mgm.sf2) k tomu,
        // aby se hral obsah skutecne ROM (awe32.raw).
        bool samplesInRom = false;

        std::vector<int16_t> sampleData;  // chunk `smpl`
        std::vector<Sample> samples;
        std::vector<Instrument> instruments;
        std::vector<Preset> presets;

        const Preset* FindPreset(int bank, int program) const;

        // Vrati vsechny vrstvy, ktere maji pri dane note zaznit.
        // Prazdny vysledek = preset neexistuje nebo notu nepokryva.
        std::vector<Region> Select(int bank, int program, int key, int velocity) const;
    };

    Bank Load(const std::string& path);

    // -----------------------------------------------------------------------
    // Prevod jedne vrstvy na registry EMU8000.
    //
    // dramBase = adresa (ve vzorcich), kam se nahral chunk `smpl` teto banky.
    // romPoolBase = adresa zacatku zvukoveho fondu ve wave ROM.
    // -----------------------------------------------------------------------
    // Prevod centu na registr IP (`sub_192E` z ovladace). Potrebuje ho
    // i Synth, kdyz pricita ohyb vysky v centech.
    int PitchToIp(int cents);

    struct VoiceParams
    {
        uint32_t ccca = 0;        // Q + control + pocatecni adresa
        uint32_t sampleStart = 0; // surova adresa vzorku (bez posunu)
        int      patchPan = 64;   // pan patche v jednotkach ovladace (64 = stred)
        uint32_t psst = 0;        // pan + loop start
        uint32_t csl = 0;         // chorus send + loop end
        uint16_t ip = 0;          // vyska pro konkretni notu
        // Tataz vyska jeste v centech, pred prevodem na IP - hodi se
        // pri rozboru rozdilu proti ovladaci.
        int      ipCents = 0;
        uint16_t ifatn = 0xFF00;     // horni bajt = cutoff; spodni doplni Synth
        uint8_t  patchAttenUnits = 0;// utlum patche v jednotkach 0.375 dB
        uint16_t pefe = 0, fmmod = 0, tremfrq = 0, fm2frq2 = 0;
        uint16_t envvol = 0x8000, atkhldv = 0x7F7F, dcysusv = 0x7F00;
        uint16_t envval = 0x8000, atkhld = 0x7F7F, dcysus = 0x7F00;
        uint16_t lfo1val = 0x8000, lfo2val = 0x8000;
        uint8_t  reverbSend = 0;
        uint8_t  releaseRate = 0; // pro DCYSUSV pri Note Off
        uint8_t  releaseModRate = 0; // pro DCYSUS pri Note Off (jen win95)
        bool     looping = true;
    };

    VoiceParams MakeVoiceParams(const Bank& bank, const Region& region,
                                int key, int velocity,
                                uint32_t dramBase, uint32_t romPoolBase,
                                Awe32::Driver drv = Awe32::kDefaultDriver);
}
