#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "Emu8000.h"
#include "SoundFont.h"

// MIDI/MPU-401 interpretacni vrstva nad register-level jadrem Emu8000Core.
//
// Synth nedrzi zadny zvukovy stav - prelozi MIDI udalost na presne ty zapisy
// do registru EMU8000, ktere by udelal ovladac na realne karte (poradi zapisu
// vcetne toho, ze DCYSUSV se zapisuje jako posledni, protoze prave on spousti
// envelope engine - viz docs/re-notes/emu8000_register_map.md).
//
// Zdroje zvuku, presne jako na realne karte:
//   - wave ROM karty (`--rom`), mapovana od adresy 0
//   - popis GM banky v ROM (`--rombank`), ktery jen rika, kde v ROM co lezi
//   - uzivatelske banky (`--sbk`), jejich vzorky se nahravaji do DRAM
//
// Banky se vrstvi: hleda se od naposledy nactene, takze uzivatelska banka
// prebije GM preset se stejnym cislem. Bez jakekoli banky hraje nahradni
// sinusova tabulka.
class Synth
{
public:
    explicit Synth(uint32_t sampleRate);

    // Wave ROM = surovy dump, 16bit little-endian vzorky.
    bool LoadWaveRom(const std::string& path, std::string& error);

    // Nacte banku (.SBK i .SF2). samplesInRom = banka jen popisuje obsah
    // wave ROM (typicky 1mgm.sf2 k awe32.raw), jeji `smpl` se ignoruje.
    // `midiBank` >= 0 presune vsechny presety banky, ktere maji cislo banky
    // 0, na tohle cislo. Uzivatelske banky (`.SBK` s vlastnimi vzorky) maji
    // totiz v `phdr` bezne banku 0 a ovladac je pri nacteni prirazuje do
    // uzivatelskeho slotu - jinak by prebily GM presety. Bicí banka 128
    // zustava, kde je.
    bool LoadBank(const std::string& path, std::string& error,
                  bool samplesInRom = false, int midiBank = -1);

    size_t BankCount() const { return m_banks.size(); }
    const SoundFont::Bank& BankAt(size_t i) const { return *m_banks[i].bank; }

    // Vypise prvnich N spustenych hlasu i s vyslednymi registry.
    void SetVoiceDebug(int count) { m_debugVoices = count; }

    // Zaznam mezivysledku pri note-onu, pojmenovany podle **bloku parametru
    // hlasu v `SBAWE.VXD`** (ukazuje na nej EBX, 0x94 B). Sloupce se schvalne
    // jmenuji jako jeho pole, aby sly postavit vedle vystupu z
    // `tests/patch_struct.py` a porovnavat 1:1.
    bool OpenNoteDump(const std::string& path);
    void CloseNoteDump();

    // Bitova maska povolenych MIDI kanalu (bit 0 = kanal 1). Slouzi
    // k izolaci jednotlivych stop pri ladeni.
    void SetChannelMask(uint16_t mask) { m_channelMask = mask; }

    // Hlavni hlasitost sekvenceru AIL (`AIL_set_XMIDI_master_volume`).
    // Neni to vec ovladace - ovladac dostane CC7 uz prenasobene - ale bez
    // ni se nedaji reprodukovat mereni z her, ktere si hlasitost hudby
    // nastavuji. Viz docs/re-notes/86box_srovnani.md 15.8.
    void SetMasterVolume(int v) { m_masterVolume = v; }

    void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void NoteOff(uint8_t channel, uint8_t note);
    void ProgramChange(uint8_t channel, uint8_t program);
    void ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
    void PitchBend(uint8_t channel, int16_t value);

    void RenderBlock(int16_t* out, uint32_t numFrames);

    Emu8000Core& Core() { return m_core; }

    static constexpr int kMaxVoices = Emu8000Core::kMaxVoices;

private:
    struct LoadedBank
    {
        std::unique_ptr<SoundFont::Bank> bank;
        uint32_t dramBase = Emu8000::kDramOffset + 50;
    };

    struct VoiceAlloc
    {
        bool inUse = false;
        bool heldBySustain = false;
        uint8_t channel = 0;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint8_t releaseRate = 0x40;
        uint8_t releaseModRate = 0;
        uint32_t age = 0;
        int      basePitch = 0;   // IP bez pitch bendu
    };

    struct ChannelState
    {
        uint8_t program = 0;
        uint8_t modWheel = 0;   // CC1, viz Synth::NoteOn
        uint8_t bankMsb = 0;         // CC0
        uint8_t bankLsb = 0;         // CC32
        uint8_t volume = 100;        // CC7
        uint8_t expression = 127;    // CC11
        uint8_t pan = 64;            // CC10
        uint8_t reverbSend = 0;      // CC91
        uint8_t chorusSend = 0;      // CC93
        bool sustain = false;        // CC64
        int16_t pitchBend = 0;
        uint8_t pitchBendRangeSemitones = 2;
    };

    int  EffectiveChannelVolume(int cc7) const
    {
        return (m_masterVolume >= 127) ? cc7 : cc7 * m_masterVolume / 127;
    }

    void BuildDefaultWaveform();
    int  AllocateVoice();
    void ReleaseVoice(int voice);
    void KillVoice(int voice);
    void RefreshChannel(uint8_t channel);
    int  BankNumberFor(uint8_t channel) const;
    int  PitchBendOffset(uint8_t channel) const;
    void StartVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity,
                    const SoundFont::VoiceParams& vp,
                    const SoundFont::Bank* bank, const SoundFont::Region* region);
    void StartFallbackVoice(int voice, uint8_t channel, uint8_t note, uint8_t velocity);

    Emu8000Core m_core;
    std::vector<LoadedBank> m_banks;
    // Prvni vzorek nezacina uplne na zacatku DRAM. Ovladac pred nej necha
    // 50 slov: CCCA ukazuje 46 slov pred zacatek vzorku (viz
    // Awe32::StartAddressOffset) a bez rezervy by mirilo jeste do ROM.
    // Zmereno proti SBAWE32.MDI - jeho prvni vzorek v DRAM zacina na
    // 0x200032 a nas na 0x200000, u vsech 52 not bicich z Magic Carpet 2
    // vychazel rozdil presne 50.
    static constexpr uint32_t kDramReserve = 50;
    uint32_t m_nextDramBase = Emu8000::kDramOffset + kDramReserve;
    int m_debugVoices = 0;
    void* m_noteDump = nullptr;   // FILE*
    uint16_t m_channelMask = 0xFFFF;
    int      m_masterVolume = 127;

    // Nahradni vzorek, kdyz zadna banka notu nepokryva.
    uint32_t m_fallbackStart = 0, m_fallbackLoopStart = 0, m_fallbackLoopEnd = 0;
    double   m_fallbackUnityHz = 0.0;

    std::array<VoiceAlloc, kMaxVoices> m_alloc{};
    std::array<ChannelState, 16> m_channels{};
    uint32_t m_ageCounter = 0;

    // Hlasy 30 a 31 zabira ovladac na DRAM refresh, pro noty zbyva 30.
    static constexpr int kUsableVoices = 30;
    // Zvukovy fond wave ROM zacina na tomto slove (viz docs/re-notes).
    static constexpr uint32_t kRomPoolBase = 495;
    // Cislo banky bicich podle GM/SoundFont konvence.
    static constexpr int kDrumBank = 128;
};
