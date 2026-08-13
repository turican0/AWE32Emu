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
#include "SoundFontSbk.h"
#include "AudioOutputWin.h"
#include "WavWriter.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

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
            "  AWE32Emu.exe <soubor.mid|soubor.xmi> [--sbk <banka.sbk>] [--wav <out.wav>]\n\n"
            "Volby:\n"
            "  --sbk <soubor>   Nacte SoundFont/SBK banku (zatim jen informativne - vypise\n"
            "                   seznam RIFF chunku, napojeni na synth je TODO, viz README)\n"
            "  --wav <soubor>   Misto prehrani v realnem case zapise vystup do .wav\n";
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    std::string inputPath;
    std::string sbkPath;
    std::string wavPath;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--sbk" && i + 1 < argc)
        {
            sbkPath = argv[++i];
        }
        else if (arg == "--wav" && i + 1 < argc)
        {
            wavPath = argv[++i];
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

    // SoundFont/SBK banka - zatim jen informativni nacteni. Realne napojeni na
    // Synth (vyber sample dat podle Program Change) je TODO, viz README a
    // projektovy TODO seznam sekce 5.
    if (!sbkPath.empty())
    {
        SoundFontSbk::SbkBank bank = SoundFontSbk::Load(sbkPath);
        if (!bank.valid)
        {
            std::cerr << "Varovani: SBK banku se nepodarilo nacist: " << bank.errorMessage << "\n";
        }
        else
        {
            std::cout << "SBK banka '" << sbkPath << "' nactena, form type '" << bank.formType
                << "', " << bank.chunks.size() << " chunku nalezeno.\n";
            if (!bank.errorMessage.empty())
                std::cout << "  Poznamka: " << bank.errorMessage << "\n";
            std::cout << "  (Poznamka: vzorky z banky se zatim nenahravaji do emulovane DRAM - "
                "hraje se nahradni sinusova tabulka, viz Synth::BuildDefaultWaveform)\n";
        }
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kFramesPerBuffer = 1024;
    constexpr double kTailSeconds = 1.5; // cas navic po posledni udalosti, aby dozneli release hlasy

    Synth synth(kSampleRate);
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
        while (sequencer.HasMoreEvents())
        {
            sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
            wav.Write(block.data(), kFramesPerBuffer);
        }
        for (uint32_t i = 0; i < tailBlocks; ++i)
        {
            sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
            wav.Write(block.data(), kFramesPerBuffer);
        }
        wav.Close();
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
