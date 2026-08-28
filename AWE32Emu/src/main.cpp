// AWE32Emu - CLI pro prehravani .mid/.xmi pres emulaci EMU8000
//
// Zvukove jadro (Emu8000.cpp) uz je register-level emulace podle registrove
// mapy odvozene z ovladace AWEUTIL.COM, viz docs/re-notes. Co zatim chybi je
// obsah zvukove pameti - dokud neni napojena SoundFont/SBK banka, hraje se
// generovana sinusova tabulka nahrana do emulovane DRAM.
//
// Pouziti:
//   AWE32Emu.exe <soubor.mid|soubor.xmi> [--sbk <banka.sbk>] [--wav <out.wav>]
//
#include "MidiFile.h"
#include "XmiFile.h"
#include "Sequencer.h"
#include "Synth.h"
#include "SoundFont.h"
#include "AudioOutputWin.h"
#include "WavWriter.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <utility>

namespace
{
    std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::string GetExtension(const std::string& path)
    {
        auto dot = path.find_last_of('.');
        if (dot == std::string::npos) return "";
        return ToLower(path.substr(dot + 1));
    }

    void PrintUsage()
    {
        std::cout <<
            "AWE32Emu - prehravac .mid/.xmi pres emulaci EMU8000 (Sound Blaster AWE32)\n\n"
            "Pouziti:\n"
            "  AWE32Emu.exe <soubor.mid|soubor.xmi> [volby]\n\n"
            "Volby:\n"
            "  --rom <soubor>      Wave ROM karty (surovy dump, napr. awe32.raw)\n"
            "  --rombank <soubor>  Banka, ktera jen POPISUJE obsah ROM (napr. 1mgm.sf2)\n"
            "  --sbk <soubor>      Uzivatelska banka - .SBK (SoundFont 1.0) i .SF2\n"
            "  --wav <soubor>      Misto prehrani v realnem case zapise vystup do .wav\n"
            "  --debug-voices <n>  Vypise prvnich n spustenych hlasu i s registry\n"
            "  --trace <soubor>    Zaznam portovych zapisu (viz ref86box/README.md)\n"
            "  --driver dos|win95  Varianta ovladace Creative; vychozi je win95\n"
            "  --chip nas|86box   Jadro cipu: nase, nebo nezmeneny snd_emu8k.c\n"
            "  --conf <soubor>    Pocatecni stav MIDI kanalu, jak ho posila hra\n"
            "  --dump-notes <csv> Mezivysledky pri note-onu, sloupce podle bloku\n"
            "                      parametru v SBAWE.VXD (viz tests/patch_struct.py)\n"
            "                      z 86Boxu (vyzaduje --rom, viz Emu8000Box.h)\n"
            "                      (SBAWE.VXD). Rodiny se lisi osmi hodnotami\n"
            "                      v init polich, tabulkou velocity a vzorcem\n"
            "                      utlumu - viz src/Awe32Driver.h.\n"
            "  --master-volume N   Hlavni hlasitost sekvenceru AIL 0..127 (vychozi 127)\n"
            "  --sbk <soubor>@<N>  Nacte banku do MIDI banky N (vyber pres CC0);\n"
            "                      uzivatelske banky maji v phdr banku 0\n"
            "  --only-ch <maska>   Prehraje jen vybrane MIDI kanaly\n"
            "  --interp linear|cubic   Interpolace vzorku\n"
            "  --reverb 0..7  --chorus 0..7   Preset efektu\n"
            "  --rev-room --rev-damp --rev-return --cho-return   Ladeni efektu\n"
            "  --filter-top <Hz>   Mezni kmitocet pri registru 0xFF (vychozi 8000)\n"
            "  --filter-poles 1|2|4  Strmost filtru 6/12/24 dB na oktavu (vychozi 2)\n\n"
            "Banky lze zadat vicekrat a vrstvi se - pozdejsi prebiji drivejsi.\n"
            "Typicke pouziti:\n"
            "  --rom rom/awe32.raw --rombank rom/1mgm.sf2 --sbk sbk/BULLFROG.SBK\n";
    }
}

// Pocatecni stav MIDI kanalu ze souboru `--conf`.
//
// Hry casto pred prvni notou nastavi vsech sestnact kanalu na sve
// hodnoty a bez toho nas render zacina jinde nez hra. Magic Carpet 2
// napriklad posila CC7 127 (my mame vychozich 100) a CC91 40 (my 0).
//
// Zprávy se posilaji **normalni cestou** pres Synth::ControlChange
// a spol., ne obchazenim - jinak by se lisilo chovani RPN a stopa by
// neodpovidala tomu, co dela ovladac.
namespace {

struct ConfMessage
{
    enum class Kind { Control, Program, Bend } kind;
    int a = 0;
    int b = 0;
};

// `master` zustane -1, kdyz soubor hlavni hlasitost neuvadi.
bool LoadConf(const std::string& path, std::vector<ConfMessage>& out,
              int& master, std::string& err)
{
    std::ifstream f(path);
    if (!f)
    {
        err = "nelze otevrit '" + path + "'";
        return false;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line))
    {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);

        std::istringstream is(line);
        std::string word;
        if (!(is >> word))
            continue;

        ConfMessage m;
        if (word == "cc")
        {
            m.kind = ConfMessage::Kind::Control;
            if (!(is >> m.a >> m.b))
            {
                err = path + ":" + std::to_string(lineNo)
                    + ": `cc` chce cislo ridici zpravy a hodnotu";
                return false;
            }
        }
        else if (word == "program")
        {
            m.kind = ConfMessage::Kind::Program;
            if (!(is >> m.a))
            {
                err = path + ":" + std::to_string(lineNo)
                    + ": `program` chce cislo programu";
                return false;
            }
        }
        else if (word == "bend")
        {
            m.kind = ConfMessage::Kind::Bend;
            if (!(is >> m.a))
            {
                err = path + ":" + std::to_string(lineNo)
                    + ": `bend` chce hodnotu 0..16383";
                return false;
            }
        }
        else if (word == "master_volume")
        {
            if (!(is >> master) || master < 0 || master > 127)
            {
                err = path + ":" + std::to_string(lineNo)
                    + ": `master_volume` chce hodnotu 0..127";
                return false;
            }
            continue;                       // neni to zprava na kanal
        }
        else
        {
            err = path + ":" + std::to_string(lineNo)
                + ": neznamy prikaz '" + word + "'";
            return false;
        }
        out.push_back(m);
    }
    return true;
}

// Posle nactene zpravy na vsech sestnact kanalu, v poradi ze souboru.
// Na poradi zalezi: CC100/CC101 musi predchazet CC6, jinak by se rozsah
// ohybu nikam nezapsal.
void ApplyConf(Synth& synth, const std::vector<ConfMessage>& msgs)
{
    for (uint8_t ch = 0; ch < 16; ++ch)
    {
        for (const ConfMessage& m : msgs)
        {
            switch (m.kind)
            {
            case ConfMessage::Kind::Control:
                synth.ControlChange(ch, static_cast<uint8_t>(m.a),
                                    static_cast<uint8_t>(m.b));
                break;
            case ConfMessage::Kind::Program:
                synth.ProgramChange(ch, static_cast<uint8_t>(m.a));
                break;
            case ConfMessage::Kind::Bend:
                // V souboru je surova hodnota z MIDI (0..16383, stred 8192),
                // `Synth::PitchBend` ale chce odchylku se stredem v nule -
                // stejne jako Sequencer, ktery odecita tuhle konstantu.
                synth.PitchBend(ch, static_cast<int16_t>(m.a - 8192));
                break;
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    std::string inputPath;
    std::string romPath;
    std::string wavPath;
    std::string tracePath;
    Awe32::Driver driver = Awe32::kDefaultDriver;
    int masterVolume = 127;
    bool masterFromCmdline = false;
    double filterTop = -1;
    int filterPoles = -1;
    int debugVoices = 0;
    uint16_t channelMask = 0xFFFF;
    std::string interp;
    std::string chip;
    std::string noteDumpPath;
    std::string confPath;
    int revPreset = -1, choPreset = -1;
    double revRoom = -1, revDamp = -1, revReturn = -1, choReturn = -1;
    // (cesta, vzorky lezi ve wave ROM, cislo MIDI banky nebo -1) v poradi nacitani
    struct BankArg { std::string path; bool inRom; int midiBank; };
    std::vector<BankArg> bankPaths;

    // "soubor.sbk@1" nacte banku do MIDI banky 1 (CC0). Uzivatelske banky
    // maji v `phdr` bezne banku 0 a bez presunu by prebily GM presety.
    auto splitBank = [](std::string a) {
        const size_t at = a.rfind('@');
        int b = -1;
        if (at != std::string::npos && at + 1 < a.size()
            && a.find_first_not_of("0123456789", at + 1) == std::string::npos)
        {
            b = std::atoi(a.c_str() + at + 1);
            a.erase(at);
        }
        return std::make_pair(a, b);
    };

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--sbk" && i + 1 < argc)
        {
            auto [p2, b2] = splitBank(argv[++i]);
            bankPaths.push_back({p2, false, b2});
        }
        else if (arg == "--rombank" && i + 1 < argc)
        {
            auto [p2, b2] = splitBank(argv[++i]);
            bankPaths.push_back({p2, true, b2});
        }
        else if (arg == "--only-ch" && i + 1 < argc)
        {
            channelMask = 0;
            std::string list = argv[++i];
            size_t pos = 0;
            while (pos < list.size())
            {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) comma = list.size();
                const int ch = std::atoi(list.substr(pos, comma - pos).c_str());
                if (ch >= 0 && ch < 16) channelMask |= static_cast<uint16_t>(1u << ch);
                pos = comma + 1;
            }
        }
        else if (arg == "--reverb" && i + 1 < argc)      { revPreset = std::atoi(argv[++i]); }
        else if (arg == "--chorus" && i + 1 < argc)      { choPreset = std::atoi(argv[++i]); }
        else if (arg == "--rev-room" && i + 1 < argc)    { revRoom = std::atof(argv[++i]); }
        else if (arg == "--rev-damp" && i + 1 < argc)    { revDamp = std::atof(argv[++i]); }
        else if (arg == "--rev-return" && i + 1 < argc)  { revReturn = std::atof(argv[++i]); }
        else if (arg == "--cho-return" && i + 1 < argc)  { choReturn = std::atof(argv[++i]); }
        else if (arg == "--interp" && i + 1 < argc)
        {
            interp = argv[++i];
        }
        else if (arg == "--debug-voices" && i + 1 < argc)
        {
            debugVoices = std::atoi(argv[++i]);
        }
        else if (arg == "--rom" && i + 1 < argc)
        {
            romPath = argv[++i];
        }
        else if (arg == "--wav" && i + 1 < argc)
        {
            wavPath = argv[++i];
        }
        else if (arg == "--trace" && i + 1 < argc)
        {
            tracePath = argv[++i];
        }
        else if (arg == "--filter-top" && i + 1 < argc)
        {
            filterTop = std::atof(argv[++i]);
        }
        else if (arg == "--filter-poles" && i + 1 < argc)
        {
            filterPoles = std::atoi(argv[++i]);
        }
        else if (arg == "--master-volume" && i + 1 < argc)
        {
            masterVolume = std::clamp(std::atoi(argv[++i]), 0, 127);
            masterFromCmdline = true;
        }
        else if (arg == "--dump-notes" && i + 1 < argc)
        {
            noteDumpPath = argv[++i];
        }
        else if (arg == "--conf" && i + 1 < argc)
        {
            confPath = argv[++i];
        }
        else if (arg == "--chip" && i + 1 < argc)
        {
            chip = argv[++i];
        }
        else if (arg == "--driver" && i + 1 < argc)
        {
            if (!Awe32::DriverFromName(argv[++i], driver))
            {
                std::cerr << "Neznama varianta ovladace '" << argv[i]
                          << "'. Pouzij 'dos' nebo 'win95'.\n";
                return 1;
            }
        }
        else if (arg == "-h" || arg == "--help")
        {
            PrintUsage();
            return 0;
        }
        else if (inputPath.empty())
        {
            inputPath = arg;
        }
    }

    if (inputPath.empty())
    {
        std::cerr << "Chybi vstupni soubor.\n\n";
        PrintUsage();
        return 1;
    }

    std::string ext = GetExtension(inputPath);
    ParsedSequence sequence;

    if (ext == "mid" || ext == "midi")
    {
        sequence = MidiFile::Load(inputPath);
    }
    else if (ext == "xmi")
    {
        sequence = XmiFile::Load(inputPath);
    }
    else
    {
        std::cerr << "Nerozpoznana pripona '" << ext << "' - ocekavam .mid nebo .xmi\n";
        return 1;
    }

    if (!sequence.valid)
    {
        std::cerr << "Chyba pri nacitani '" << inputPath << "': " << sequence.errorMessage << "\n";
        return 1;
    }

    std::cout << "Nacteno: " << inputPath << " (" << sequence.events.size() << " udalosti, "
        << sequence.ticksPerQuarterNote << " ticku/ctvrtovou notu)\n";

    // Konfigurace se cte driv, nez se nastavi hlavni hlasitost - muze ji
    // totiz sama urcovat. Prepinac `--master-volume` ma prednost.
    std::vector<ConfMessage> confMessages;
    int confMaster = -1;
    if (!confPath.empty())
    {
        std::string err;
        if (!LoadConf(confPath, confMessages, confMaster, err))
        {
            std::cerr << "Chyba v konfiguraci: " << err << "\n";
            return 1;
        }
        if (confMaster >= 0 && !masterFromCmdline)
            masterVolume = confMaster;
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kFramesPerBuffer = 1024;
    constexpr double kTailSeconds = 1.5; // cas navic po posledni udalosti, aby dozneli release hlasy

    Synth synth(kSampleRate);

    if (!romPath.empty())
    {
        std::string err;
        if (!synth.LoadWaveRom(romPath, err))
            std::cerr << "Varovani: " << err << "\n";
        else
            std::cout << "Wave ROM '" << romPath << "' nactena ("
                      << synth.Core().RomSize() << " vzorku).\n";
    }

    // Cip z 86Boxu se musi zapnout drive, nez pujde prvni zapis na porty -
    // tedy pred SetDriver/PowerOnInit nize.
    if (chip == "86box")
    {
        std::string err;
        if (!synth.Core().UseBox86Chip(romPath, err))
        {
            std::cerr << "Cip 86box se nepodarilo zapnout: " << err << "\n";
            return 1;
        }
        std::cout << "Jadro cipu: nezmeneny snd_emu8k.c z 86Boxu (latence "
                  << synth.Core().ChipLatencyFrames() << " snimku).\n";
    }
    else if (!chip.empty() && chip != "nas")
    {
        std::cerr << "Neznamy --chip '" << chip << "'; znamé jsou nas a 86box.\n";
        return 1;
    }

    // Banky se nacitaji v poradi, v jakem byly zadany; pozdejsi prebiji
    // drivejsi. Typicky nejdriv popis GM banky v ROM, pak banka hry.
    for (const auto& [path, inRom, midiBank] : bankPaths)
    {
        std::string err;
        if (!synth.LoadBank(path, err, inRom, midiBank))
        {
            std::cerr << "Varovani: banku '" << path << "' se nepodarilo nacist: " << err << "\n";
            continue;
        }
        const SoundFont::Bank& b = synth.BankAt(synth.BankCount() - 1);
        std::cout << "Banka '" << path << "': SoundFont "
                  << (b.version == SoundFont::Version::Sf1 ? "1.0" : "2.0")
                  << ", " << b.presets.size() << " presetu, "
                  << b.instruments.size() << " instrumentu, "
                  << b.samples.size() << " vzorku";
        if (inRom) std::cout << ", vzorky ve wave ROM";
        if (midiBank >= 0) std::cout << ", MIDI banka " << midiBank;
        if (!b.romName.empty()) std::cout << ", ocekava ROM '" << b.romName << "'";
        std::cout << ".\n";

        size_t romRefs = 0;
        for (const SoundFont::Sample& sm : b.samples) if (sm.inRom) ++romRefs;
        if ((romRefs || inRom) && !synth.Core().RomSize())
            std::cerr << "Varovani: banka odkazuje vzorky do ROM, ale zadna ROM"
                         " neni nactena (--rom).\n";
    }

    // Vzorky bank lezi v nasi DRAM; cip z 86Boxu ma svoji vlastni, takze se
    // musi prekopirovat. Obe zacinaji na EMU8K_RAM_MEM_START, takze bez posunu.
    if (synth.Core().ChipVariant() == Emu8000Core::Chip::Box86)
    {
        if (int16_t* ram = synth.Core().ChipRam())
        {
            const size_t n = std::min(synth.Core().DramSize(),
                                      synth.Core().ChipRamWords());
            std::memcpy(ram, synth.Core().DramData(), n * sizeof(int16_t));
            std::cout << "Do cipu 86box nakopirovano " << n << " vzorku DRAM.\n";
        }
    }

    // Varianta ovladace musi byt nastavena pred PowerOnInit, protoze meni
    // osm hodnot v init polich.
    synth.Core().SetDriver(driver);
    synth.Core().PowerOnInit();
    std::cout << "Ovladac: " << Awe32::DriverName(driver) << "\n";

    if (debugVoices > 0) synth.SetVoiceDebug(debugVoices);
    synth.SetChannelMask(channelMask);
    synth.SetMasterVolume(masterVolume);
    if (filterTop > 0)    synth.Core().SetFilterTopHz(filterTop);
    if (filterPoles > 0)  synth.Core().SetFilterPoles(filterPoles);
    if (interp == "linear") synth.Core().SetInterpolation(Emu8000Core::Interp::Linear);
    else if (interp == "cubic") synth.Core().SetInterpolation(Emu8000Core::Interp::Cubic);
    if (revPreset >= 0) synth.Core().SetReverbPreset(revPreset);
    if (choPreset >= 0) synth.Core().SetChorusPreset(choPreset);
    if (revRoom >= 0 && revDamp >= 0)
        synth.Core().SetReverbRoom(static_cast<float>(revRoom), static_cast<float>(revDamp));
    if (revReturn >= 0 || choReturn >= 0)
        synth.Core().SetEffectReturns(static_cast<float>(revReturn < 0 ? 1.0 : revReturn),
                                      static_cast<float>(choReturn < 0 ? 0.7 : choReturn));

    // Zaznam portovych zapisu pro srovnani s 86Boxem. Vzorky uz jsou v DRAM
    // (nahravaji se memcpy, ne pres SMLD), takze se vedle stopy ulozi i obraz
    // DRAM - ref86box/emu8k_ref.exe si ho nacte pres --dram.
    if (!tracePath.empty())
    {
        if (!synth.Core().OpenTrace(tracePath.c_str()))
        {
            std::cerr << "Nepodarilo se otevrit stopu '" << tracePath << "'.\n";
            return 1;
        }
        // Inicializacni sekvence probehla uz v konstruktoru Synthu, takze by
        // ve stope chybela. Zopakujeme ji - registry se tim vrati do stejneho
        // stavu, jen ted i se zaznamem.
        synth.Core().PowerOnInit();

        const std::string dramPath = tracePath + ".dram.raw";
        if (FILE* df = std::fopen(dramPath.c_str(), "wb"))
        {
            std::fwrite(synth.Core().DramData(), sizeof(int16_t),
                        synth.Core().DramSize(), df);
            std::fclose(df);
            std::cout << "Stopa '" << tracePath << "' + DRAM "
                      << synth.Core().DramSize() << " vzorku.\n";
        }
    }

    if (!noteDumpPath.empty() && !synth.OpenNoteDump(noteDumpPath))
        std::cerr << "Nepodarilo se otevrit '" << noteDumpPath << "'.\n";

    // Az za zapnutim stopy, aby se pocatecni stav kanalu do stopy zapsal -
    // ovladac ve hre ho taky posila az po inicializaci cipu.
    if (!confPath.empty())
    {
        ApplyConf(synth, confMessages);
        std::cout << "Konfigurace '" << confPath << "': "
                  << confMessages.size() << " zprav na kazdy z 16 kanalu";
        if (confMaster >= 0)
            std::cout << ", hlavni hlasitost " << confMaster;
        std::cout << ".\n";
    }

    Sequencer sequencer;
    sequencer.Load(sequence);

    std::vector<int16_t> block(static_cast<size_t>(kFramesPerBuffer) * 2);
    const uint32_t tailBlocks =
        static_cast<uint32_t>((kTailSeconds * kSampleRate) / kFramesPerBuffer) + 1;

    // Offline render do .wav - pro regresni testy a A/B srovnani
    // s referencnimi nahravkami (TODO sekce 8) je realny cas nepouzitelny.
    if (!wavPath.empty())
    {
        WavWriter wav;
        if (!wav.Open(wavPath, kSampleRate))
        {
            std::cerr << "Nepodarilo se otevrit vystupni soubor '" << wavPath << "'.\n";
            return 1;
        }

        std::cout << "Renderuji do '" << wavPath << "'...\n";
        // Cip z 86Boxu vydava zvuk o blok pozadu; ta latence se na zacatku
        // zahodi a na konci se dorenderuje, takze soubor sedi snimek na snimek
        // s emu8k_ref.exe.
        uint32_t skip = synth.Core().ChipLatencyFrames();
        auto writeTrimmed = [&](const int16_t* buf, uint32_t frames)
        {
            if (skip >= frames) { skip -= frames; return; }
            wav.Write(buf + skip * 2, frames - skip);
            skip = 0;
        };
        while (sequencer.HasMoreEvents())
        {
            sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
            writeTrimmed(block.data(), kFramesPerBuffer);
        }
        const uint32_t extra = tailBlocks
            + (synth.Core().ChipLatencyFrames() + kFramesPerBuffer - 1) / kFramesPerBuffer;
        for (uint32_t i = 0; i < extra; ++i)
        {
            sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
            writeTrimmed(block.data(), kFramesPerBuffer);
        }
        wav.Close();
        synth.Core().CloseTrace();
        std::cout << "Hotovo.\n";
        return 0;
    }

    AudioOutputWin audioOut;
    if (!audioOut.Open(kSampleRate, kFramesPerBuffer))
    {
        std::cerr << "Nepodarilo se otevrit audio vystup (waveOutOpen selhal).\n";
        return 1;
    }

    std::cout << "Prehravam... (Ctrl+C pro preruseni)\n";

    while (sequencer.HasMoreEvents())
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut.Write(block.data(), kFramesPerBuffer);
    }

    // "Tail" - dorenderovat jeste kus ticha/doznivani po posledni udalosti,
    // aby se release faze obalky (viz Synth.h) nezarizla.
    for (uint32_t i = 0; i < tailBlocks; ++i)
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut.Write(block.data(), kFramesPerBuffer);
    }

    audioOut.Close();
    std::cout << "Hotovo.\n";
    return 0;
}
