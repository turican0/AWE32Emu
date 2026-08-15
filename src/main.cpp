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

#include <iostream>
#include <vector>
#include <algorithm>

#include "Util.h"
#include "MidiFile.h"
#include "XmiFile.h"
#include "Sequencer.h"
#include "Synth.h"
#include "SoundFontSbk.h"
#include "AudioOutput.h"
#include "WavWriter.h"

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

    void PrintUsage(std::string arg0 = "AWE32Emu")
    {
        std::cout << _("AWE32Emu - .mid/.xmi player via EMU8000 emulation (Sound Blaster AWE32)") << std::endl << std::endl
                  << _("Usage:") << std::endl
                  << _("  ") << arg0 << _(" <file.mid|file.xmi> [--sbk <bank.sbk>] [--wav <out.wav>]") << std::endl
                  << _("Options:") << std::endl
                  << _("  --sbk <file>   Load SoundFont/SBK bank (for now just informative - prints") << std::endl
                  << _("                 RIFF chunks, connecting to synth is TODO, see README)") << std::endl
                  << _("  --wav <file>   Write output to .wav instead of realtime playback") << std::endl;
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
        std::cerr << _("Input file missing.") << std::endl << std::endl;
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
        std::cerr << _("Unrecognized extension '") << ext << _("' - expecting .mid or .xmi") << std::endl;
        return 1;
    }

    if (!sequence.valid)
    {
        std::cerr << _("Error loading '") << inputPath << "': " << sequence.errorMessage << std::endl;
        return 1;
    }

    std::cout << _("Loaded: ") << inputPath << " (" << sequence.events.size() << _(" events, ")
        << sequence.ticksPerQuarterNote << _(" tick/quarter note)") << std::endl;

    // SoundFont/SBK bank - just an informative load for now. The actual
    // connection to Synth (choosing sample data according to Program Change)
    // is TODO, see README and project TODO list section 5.
    if (!sbkPath.empty())
    {
        SoundFontSbk::SbkBank bank = SoundFontSbk::Load(sbkPath);
        if (!bank.valid)
        {
            std::cerr << _("Warning: SBK Bank not found: ") << bank.errorMessage << std::endl;
        }
        else
        {
            std::cout << _("SBK Bank '") << sbkPath << _("' loaded, form type '") << bank.formType
                      << "', " << bank.chunks.size() << _(" chunk found.") << std::endl;
            if (!bank.errorMessage.empty())
                std::cout << _("  Note: ") << bank.errorMessage << std::endl;
            std::cout << _("  (Note: samples from the bank are not yet loaded into the emulated DRAM - ")
                      << _("a replacement sine table is being played, see Synth::BuildDefaultWaveform)") << std::endl;
        }
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kFramesPerBuffer = 1024;
    constexpr double kTailSeconds = 1.5; // extra time after the last event to let the release voices die down

    Synth synth(kSampleRate);
    Sequencer sequencer;
    sequencer.Load(sequence);

    std::vector<int16_t> block(static_cast<size_t>(kFramesPerBuffer) * 2);
    const uint32_t tailBlocks =
        static_cast<uint32_t>((kTailSeconds * kSampleRate) / kFramesPerBuffer) + 1;

    // Offline render to .wav - for regression tests and A/B comparisons
    // with reference recordings (TODO section 8) real time is unusable.
    if (!wavPath.empty())
    {
        WavWriter wav;
        if (!wav.Open(wavPath, kSampleRate))
        {
            std::cerr << _("Failed to open output file '") << wavPath << "'." << std::endl;
            return 1;
        }

        std::cout << _("Rendering to '") << wavPath << "'..." << std::endl;
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
        std::cout << _("Done.") << std::endl;
        return 0;
    }

#ifdef _WIN32
    AudioOutputWin audioOut;
#else
    AudioOutputNull audioOut;
#endif

    if (!audioOut.Open(kSampleRate, kFramesPerBuffer))
    {
        std::cerr << _("Failed to open audio output (waveOutOpen failed).") << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << _("Playing... (Ctrl+C to interrupt)") << std::endl;

    while (sequencer.HasMoreEvents())
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut.Write(block.data(), kFramesPerBuffer);
    }

    // "Tail" - still render a bit of silence/fading after the last event,
    // so that the release phase of the wrapper (see Synth.h) doesn't get stuck.
    for (uint32_t i = 0; i < tailBlocks; ++i)
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut.Write(block.data(), kFramesPerBuffer);
    }

    audioOut.Close();
    std::cout << _("Done.") << std::endl;
    exit(EXIT_SUCCESS);
}
