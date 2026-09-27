// AWE32Emu - command line player for .mid/.xmi through an EMU8000 emulation
//
// The chip is snd_emu8k.c from 86Box (src/86box, a copy of its own); the
// driver layer copies the Creative drivers (dos = SBAWE32.MDI, win95 =
// SBAWE.VXD), see docs/re-notes.
//
// Usage:
//   AWE32Emu.exe <file.mid|file.xmi> --rom awe32.raw --sf <bank.sbk|.sf2|.mdi> [--wav <out.wav>]
//
#include "MidiFile.h"
#include "XmiFile.h"
#include "Sequencer.h"
#include "Synth.h"
#include "SoundFont.h"
#include "SoundFontExport.h"
#include "AudioOutput.h"
#include "I18n.h"
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
#include <memory>

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
        std::string outputs;
        for (const std::string& n : AudioOutputs::Available())
            outputs += (outputs.empty() ? "" : ", ") + n;
        const std::string def = AudioOutputs::DefaultName();

        std::cout << _(
            "AWE32Emu - .mid/.xmi player through an EMU8000 emulation (Sound Blaster AWE32)\n\n"
            "Usage:\n"
            "  AWE32Emu.exe <file.mid|file.xmi> [options]\n\n"
            "Options:\n"
            "  --rom <file>        Wave ROM of the card (raw dump, e.g. awe32.raw)\n"
            "  --rombank <file>    A bank that only DESCRIBES the ROM contents (e.g. 1mgm.sf2)\n"
            "  --sf <file>         Bank - .sbk, .sf2 or .mdi\n"
            "                      (.mdi = the ROM GM presets compiled into the DOS driver)\n"
            "  --wav <file>        Write the output to a .wav instead of playing it live\n"
            "  --audio <output>    Output for live playback (see below)\n"
            "  --debug-voices <n>  Print the first n started voices with their registers\n"
            "  --trace <file>      Record the port writes (see docs/TESTING.md)\n"
            "  --replay <trace>    Play a port-write trace through the chip and write --wav\n"
            "                      (format as emu8k_ref; no MIDI file is given then)\n"
            "  --driver dos|win95|sdk  Creative driver variant; default win95\n"
            "                      (sdk = the AWE32 DOS SDK as DOSMid uses it)\n"
            "  --chip ours|86box   Chip core: 86box = snd_emu8k.c from 86Box with the\n"
            "                      measured corrections (default when --rom is given),\n"
            "                      ours = the older own core (default without --rom)\n"
            "  --ram <KB>          DRAM of the 86box chip in KB (onboard_ram), default 8192\n"
            "  --conf <file>       Initial state of the MIDI channels, as a game sends it\n"
            "  --dump-notes <csv>  Intermediate values at note-on, columns following the\n"
            "                      parameter block of SBAWE.VXD\n"
            "  --master-volume N   Master volume of the AIL sequencer 0..127 (default 127)\n"
            "  --sf <file>@<N>     Load the bank into MIDI bank N (selected by CC0);\n"
            "                      user banks have bank 0 in their phdr\n"
            "  --tracks 1,2,3      Only these MIDI channels (1..16); --tracks -8,-9 = all but these\n"
            "  --export-sf2 <file> Write the loaded banks (ROM included) as one .sf2\n\n");

        std::cout << StrFormat(_("Audio outputs in this build: %s (default: %s)\n"
                                 "  rtaudio = RtAudio, winmm = Windows waveOut, bass = un4seen BASS\n"
                                 "  (bass.dll / libbass.so next to the program), null = no sound, real time\n\n"),
                               outputs.c_str(), def.empty() ? _("none") : def.c_str());

        std::cout << _(
            "Options for the old own core only (--chip ours):\n"
            "  --interp linear|cubic|3point|3pointc|sinc   Interpolation (default sinc;\n"
            "                      the 86box chip uses its measured interpolation)\n"
            "  --reverb 0..7  --chorus 0..7   Effect preset\n"
            "  --rev-room --rev-damp --rev-return --cho-return   Effect tuning\n"
            "  --filter-top <Hz>   Cutoff frequency at register 0xFF (default 8000)\n"
            "  --cutoff-base <Hz>  Cutoff frequency at register 0 (default 101.81)\n"
            "  --q-cutoff-shift <oct>  Drop of the cutoff at Q=15 in octaves (default 0)\n"
            "  --q-base <x>        Base resonance of the filter (1.0 default, 0.7071 Butterworth)\n"
            "  --filter-atten <x>  Strength of the filter input attenuation (1 = chip table, 0 = none)\n"
            "  --resonance-db <x>  Resonance at Q=15 in dB (default 24)\n"
            "  --resonance-curve faq|flat  Does the resonance depend on the cutoff? (default flat)\n"
            "  --sinc-taps <n>     Number of sinc taps (even, 4-32; default 8)\n"
            "  --hold-scale <x>  --decay-scale <x>  --attack-scale <x>\n"
            "                      Scales of the envelope time constants (default 1)\n"
            "  --cutoff-map exp|lin  Register to cutoff mapping (lin = 100+31.25*reg Hz)\n"
            "  --filter-mode cham|tpt|86box  Filter structure (default cham = Chamberlin as\n"
            "                      measured on the card; tpt = the earlier bilinear; 86box = upstream)\n"
            "  --pan linear|power  Pan law (linear = a multiplier, as in the chip)\n"
            "  --loop-wrap on|off  Wrap the interpolation samples into the loop (default on)\n"
            "  --eq on|off         The card's equaliser from INIT3/INIT4 (default on; own core only)\n"
            "  --filter-poles 1|2|4  Filter slope 6/12/24 dB per octave (default 2)\n\n"
            "Banks can be given several times and are layered - later ones override earlier ones.\n"
            "Typical use:\n"
            "  --rom rom/awe32.raw --sf SBAWE32.MDI --sf sbk/BULLFROG.SBK --driver dos\n"
            "\nAll options are described with examples in docs/USAGE.md.\n");
    }
}

// Initial state of the MIDI channels from the `--conf` file.
//
// Games often set all sixteen channels to their own values before the first
// note; without that our render starts somewhere else than the game does.
// Magic Carpet 2 for example sends CC7 127 (our default is 100) and CC91 40
// (ours 0).
//
// The messages go the **normal way** through Synth::ControlChange and
// friends, not around it - otherwise RPNs would behave differently and the
// trace would not match what the driver does.
namespace {

struct ConfMessage
{
    enum class Kind { Control, Program, Bend } kind;
    int a = 0;
    int b = 0;
};

// `master` stays -1 when the file gives no master volume.
bool LoadConf(const std::string& path, std::vector<ConfMessage>& out,
              int& master, bool& triggerMute, std::string& err)
{
    std::ifstream f(path);
    if (!f)
    {
        err = StrFormat(_("cannot open '%s'"), path.c_str());
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
                err = StrFormat(_("%s:%d: `cc` wants a controller number and a value"), path.c_str(), lineNo);
                return false;
            }
        }
        else if (word == "program")
        {
            m.kind = ConfMessage::Kind::Program;
            if (!(is >> m.a))
            {
                err = StrFormat(_("%s:%d: `program` wants a program number"), path.c_str(), lineNo);
                return false;
            }
        }
        else if (word == "bend")
        {
            m.kind = ConfMessage::Kind::Bend;
            if (!(is >> m.a))
            {
                err = StrFormat(_("%s:%d: `bend` wants a value 0..16383"), path.c_str(), lineNo);
                return false;
            }
        }
        else if (word == "trigger_mute")
        {
            // see Sequencer::SetTriggerMute
            int v = 0;
            if (!(is >> v))
            {
                err = StrFormat(_("%s:%d: `trigger_mute` wants 0 or 1"), path.c_str(), lineNo);
                return false;
            }
            triggerMute = (v != 0);
            continue;
        }
        else if (word == "master_volume")
        {
            if (!(is >> master) || master < 0 || master > 127)
            {
                err = StrFormat(_("%s:%d: `master_volume` wants a value 0..127"), path.c_str(), lineNo);
                return false;
            }
            continue;                       // not a channel message
        }
        else
        {
            err = StrFormat(_("%s:%d: unknown command '%s'"), path.c_str(), lineNo, word.c_str());
            return false;
        }
        out.push_back(m);
    }
    return true;
}

// Sends the loaded messages on all sixteen channels, in file order. The
// order matters: CC100/CC101 have to come before CC6, or the bend range
// would not be written anywhere.
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
                // The file holds the raw MIDI value (0..16383, centre 8192);
                // `Synth::PitchBend` wants the deviation around zero - the
                // same as the Sequencer, which subtracts this constant.
                synth.PitchBend(ch, static_cast<int16_t>(m.a - 8192));
                break;
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    I18n::Init(argc > 0 ? argv[0] : nullptr);

    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    std::string inputPath;
    std::string romPath;
    std::string wavPath;
    std::string audioName;
    std::string tracePath;
    std::string replayPath;
    Awe32::Driver driver = Awe32::kDefaultDriver;
    int masterVolume = 127;
    bool masterFromCmdline = false;
    double filterTop = -1;
    double qBase = -1;
    std::string cutoffMap;
    std::string filterMode;
    std::string panMode;
    std::string loopWrap;
    std::string eqMode;
    std::string exportSf2;
    double filterAtten = 1.0;
    double resonanceDb = -1.0;
    std::string resonanceCurve;
    int sincTaps = 0;
    double holdScale = 1.0, decayScale = 1.0, attackScale = 1.0;
    double cutoffBase = 0.0, qCutoffShift = 0.0;
    bool   filterAttenSet = false;
    int filterPoles = -1;
    int debugVoices = 0;
    uint16_t channelMask = 0xFFFF;
    std::string interp;
    std::string chip;
    int chipRamKb = 8192;
    std::string noteDumpPath;
    std::string confPath;
    int revPreset = -1, choPreset = -1;
    double revRoom = -1, revDamp = -1, revReturn = -1, choReturn = -1;
    // (path, samples lie in the wave ROM, MIDI bank number or -1) in load order
    struct BankArg { std::string path; bool inRom; int midiBank; };
    std::vector<BankArg> bankPaths;

    // "file.sbk@1" loads the bank into MIDI bank 1 (CC0). User banks usually
    // have bank 0 in `phdr` and without the move would override the GM presets.
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
        // --sbk is the former name of --sf, kept for old batch files
        if ((arg == "--sf" || arg == "--sbk") && i + 1 < argc)
        {
            auto [p2, b2] = splitBank(argv[++i]);
            bankPaths.push_back({p2, false, b2});
        }
        else if (arg == "--rombank" && i + 1 < argc)
        {
            auto [p2, b2] = splitBank(argv[++i]);
            bankPaths.push_back({p2, true, b2});
        }
        else if (arg == "--tracks" && i + 1 < argc)
        {
            // MIDI channels 1..16. Positive numbers form a white list
            // ("--tracks 1,2,3" plays only these), negative numbers a black
            // list ("--tracks -8,-9" plays everything except these). When both
            // are given, the white list is applied first.
            uint16_t allow = 0, deny = 0;
            std::string list = argv[++i];
            size_t pos = 0;
            while (pos < list.size())
            {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) comma = list.size();
                const int n = std::atoi(list.substr(pos, comma - pos).c_str());
                if (n >= 1 && n <= 16)   allow |= static_cast<uint16_t>(1u << (n - 1));
                if (n <= -1 && n >= -16) deny  |= static_cast<uint16_t>(1u << (-n - 1));
                pos = comma + 1;
            }
            channelMask = static_cast<uint16_t>((allow ? allow : 0xFFFFu) & ~deny);
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
        else if (arg == "--audio" && i + 1 < argc)
        {
            audioName = argv[++i];
        }
        else if (arg == "--trace" && i + 1 < argc)
        {
            tracePath = argv[++i];
        }
        else if (arg == "--replay" && i + 1 < argc)
        {
            replayPath = argv[++i];
        }
        else if (arg == "--hold-scale" && i + 1 < argc)
            holdScale = std::atof(argv[++i]);
        else if (arg == "--decay-scale" && i + 1 < argc)
            decayScale = std::atof(argv[++i]);
        else if (arg == "--attack-scale" && i + 1 < argc)
            attackScale = std::atof(argv[++i]);
        else if (arg == "--sinc-taps" && i + 1 < argc)
        {
            sincTaps = std::atoi(argv[++i]);
        }
        else if (arg == "--resonance-curve" && i + 1 < argc)
        {
            resonanceCurve = argv[++i];
        }
        else if (arg == "--resonance-db" && i + 1 < argc)
        {
            resonanceDb = std::atof(argv[++i]);
        }
        else if (arg == "--filter-atten" && i + 1 < argc)
        {
            filterAtten = std::atof(argv[++i]);
            filterAttenSet = true;
        }
        else if (arg == "--export-sf2" && i + 1 < argc)
        {
            exportSf2 = argv[++i];
        }
        else if (arg == "--loop-wrap" && i + 1 < argc)
        {
            loopWrap = argv[++i];
        }
        else if (arg == "--eq" && i + 1 < argc)
        {
            eqMode = argv[++i];
        }
        else if (arg == "--pan" && i + 1 < argc)
        {
            panMode = argv[++i];
        }
        else if (arg == "--filter-mode" && i + 1 < argc)
        {
            filterMode = argv[++i];
        }
        else if (arg == "--cutoff-map" && i + 1 < argc)
        {
            cutoffMap = argv[++i];
        }
        else if (arg == "--q-base" && i + 1 < argc)
        {
            qBase = std::atof(argv[++i]);
        }
        else if (arg == "--filter-top" && i + 1 < argc)
        {
            filterTop = std::atof(argv[++i]);
        }
        else if (arg == "--cutoff-base" && i + 1 < argc)
        {
            cutoffBase = std::atof(argv[++i]);
        }
        else if (arg == "--q-cutoff-shift" && i + 1 < argc)
        {
            qCutoffShift = std::atof(argv[++i]);
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
            if (chip == "nas") chip = "ours";   // the old (Czech) name still works
        }
        else if (arg == "--ram" && i + 1 < argc)
        {
            // Onboard DRAM of the 86Box chip in KB, as 86Box `onboard_ram`.
            chipRamKb = std::atoi(argv[++i]);
        }
        else if (arg == "--driver" && i + 1 < argc)
        {
            if (!Awe32::DriverFromName(argv[++i], driver))
            {
                std::cerr << StrFormat(_("Unknown driver variant '%s'. Use 'dos', 'win95' or 'sdk'.\n"),
                                       argv[i]);
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

    // An unknown output is reported before anything is loaded.
    if (!audioName.empty() && wavPath.empty())
    {
        const auto avail = AudioOutputs::Available();
        if (std::find(avail.begin(), avail.end(), audioName) == avail.end())
        {
            std::string err;
            AudioOutputs::Create(audioName, err);
            std::cerr << StrFormat(_("Could not open the audio output '%s': %s\n"),
                                   audioName.c_str(), err.c_str());
            return 1;
        }
    }

    // A bank export plays nothing, so no input song is needed.
    if (inputPath.empty() && exportSf2.empty() && replayPath.empty())
    {
        std::cerr << _("No input file.\n\n");
        PrintUsage();
        return 1;
    }

    ParsedSequence sequence;

    if (!inputPath.empty())
    {
        std::string ext = GetExtension(inputPath);

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
            std::cerr << StrFormat(_("Unrecognised extension '%s' - expected .mid or .xmi\n"), ext.c_str());
            return 1;
        }

        if (!sequence.valid)
        {
            std::cerr << StrFormat(_("Error loading '%s': %s\n"), inputPath.c_str(),
                               sequence.errorMessage.c_str());
            return 1;
        }

        std::cout << StrFormat(_("Loaded: %s (%zu events, %u ticks per quarter note)\n"),
                               inputPath.c_str(), sequence.events.size(),
                               static_cast<unsigned>(sequence.ticksPerQuarterNote));
    }

    // The configuration is read before the master volume is set - it may set
    // the master volume itself. `--master-volume` takes precedence.
    std::vector<ConfMessage> confMessages;
    int confMaster = -1;
    bool confTriggerMute = false;
    if (!confPath.empty())
    {
        std::string err;
        if (!LoadConf(confPath, confMessages, confMaster, confTriggerMute, err))
        {
            std::cerr << StrFormat(_("Error in the configuration: %s\n"), err.c_str());
            return 1;
        }
        if (confMaster >= 0 && !masterFromCmdline)
            masterVolume = confMaster;
    }

    constexpr uint32_t kSampleRate = 44100;
    constexpr uint32_t kFramesPerBuffer = 1024;
    constexpr double kTailSeconds = 1.5; // extra time after the last event so that released voices fade out

    Synth synth(kSampleRate);

    if (!romPath.empty())
    {
        std::string err;
        if (!synth.LoadWaveRom(romPath, err))
            std::cerr << StrFormat(_("Warning: %s\n"), err.c_str());
        else
            std::cout << StrFormat(_("Wave ROM '%s' loaded (%zu samples).\n"), romPath.c_str(),
                                   static_cast<size_t>(synth.Core().RomSize()));
    }

    // The 86Box chip has to be switched on before the first port write -
    // that is, before SetDriver/PowerOnInit below.
    // The chip is snd_emu8k.c from 86Box (with the measured corrections).
    // Our older core stays available as --chip ours and is used when no wave
    // ROM is given.
    const bool chipDefault = chip.empty();
    if (chipDefault)
        chip = romPath.empty() ? "ours" : "86box";
    if (chip == "86box")
    {
        std::string err;
        synth.Core().SetChipRamKb(chipRamKb);
        if (!synth.Core().UseBox86Chip(romPath, err))
        {
            std::cerr << StrFormat(_("Could not switch on the 86box chip: %s\n"), err.c_str());
            return 1;
        }
        std::cout << StrFormat(_("Chip core: snd_emu8k.c from 86Box (latency %u frames).\n"),
                               static_cast<unsigned>(synth.Core().ChipLatencyFrames()));
    }
    else if (!chip.empty() && chip != "ours")
    {
        std::cerr << StrFormat(_("Unknown --chip '%s'; known are ours and 86box.\n"), chip.c_str());
        return 1;
    }

    // Banks are loaded in the order given; later ones override earlier
    // ones. Typically the description of the GM bank in ROM first, then the
    // game's bank.
    // The driver variant has to be set **before** the banks are loaded: the
    // families differ in where the user DRAM starts (see Synth::DramReserve).
    // It also has to be set before PowerOnInit, because it changes eight
    // values in the init arrays.
    synth.Core().SetDriver(driver);

    for (const auto& [path, inRom, midiBank] : bankPaths)
    {
        std::string err;
        if (!synth.LoadBank(path, err, inRom, midiBank))
        {
            std::cerr << StrFormat(_("Warning: could not load bank '%s': %s\n"), path.c_str(), err.c_str());
            continue;
        }
        const SoundFont::Bank& b = synth.BankAt(synth.BankCount() - 1);
        const bool fromMdi = (b.name == "SBAWE32.MDI GM");
        std::cout << StrFormat(_("Bank '%s': %s, %zu presets, %zu instruments, %zu samples"),
                               path.c_str(),
                               fromMdi ? _("GM presets from the SBAWE32.MDI driver")
                                       : (b.version == SoundFont::Version::Sf1 ? "SoundFont 1.0" : "SoundFont 2.0"),
                               b.presets.size(), b.instruments.size(), b.samples.size());
        if (inRom) std::cout << _(", samples in the wave ROM");
        if (midiBank >= 0) std::cout << StrFormat(_(", MIDI bank %d"), midiBank);
        if (!b.romName.empty()) std::cout << StrFormat(_(", expects ROM '%s'"), b.romName.c_str());
        std::cout << ".\n";

        size_t romRefs = 0;
        for (const SoundFont::Sample& sm : b.samples) if (sm.inRom) ++romRefs;
        if ((romRefs || inRom) && !synth.Core().RomSize())
            std::cerr << _("Warning: the bank refers to samples in ROM, but no ROM"
                           " is loaded (--rom).\n");
    }

    // The SF2 export is done right after the banks are loaded - nothing is
    // rendered after it, so there is no need to wait for the sound. The ROM
    // is read again from the file for it: `Synth::LoadWaveRom` moves it on,
    // so we no longer have it.
    if (!exportSf2.empty())
    {
        std::vector<const SoundFont::Bank*> banks;
        for (size_t i = 0; i < synth.BankCount(); ++i)
            banks.push_back(&synth.BankAt(i));

        std::vector<int16_t> rom;
        if (!romPath.empty())
        {
            std::ifstream rf(romPath, std::ios::binary);
            if (rf)
            {
                std::vector<uint8_t> raw((std::istreambuf_iterator<char>(rf)),
                                          std::istreambuf_iterator<char>());
                rom.resize(raw.size() / 2);
                std::memcpy(rom.data(), raw.data(), rom.size() * 2);
            }
        }

        SoundFont::ExportOptions eo;
        eo.name = exportSf2;
        std::string err;
        if (!SoundFont::ExportSf2(banks, rom, exportSf2, eo, err))
        {
            std::cerr << StrFormat(_("SF2 export failed: %s\n"), err.c_str());
            return 1;
        }
        std::cout << StrFormat(_("Written to '%s'.\n"), exportSf2.c_str());
        return 0;
    }

    // The bank samples lie in our DRAM; the 86Box chip has its own, so they
    // have to be copied over. Both start at EMU8K_RAM_MEM_START, so no offset.
    if (synth.Core().ChipVariant() == Emu8000Core::Chip::Box86)
    {
        if (int16_t* ram = synth.Core().ChipRam())
        {
            const size_t n = std::min(synth.Core().DramSize(),
                                      synth.Core().ChipRamWords());
            std::memcpy(ram, synth.Core().DramData(), n * sizeof(int16_t));
            std::cout << StrFormat(_("Copied %zu DRAM samples into the 86box chip.\n"), n);
        }
    }

    // A replayed trace carries the guest's own initialisation; our init
    // would put different state into the chip (DRAM refresh voices 30/31,
    // chorus HWCF writes) than the VM had.
    if (replayPath.empty())
        synth.Core().PowerOnInit();
    std::cout << StrFormat(_("Driver: %s\n"), Awe32::DriverName(driver));

    if (debugVoices > 0) synth.SetVoiceDebug(debugVoices);
    synth.SetChannelMask(channelMask);
    synth.SetMasterVolume(masterVolume);
    if (filterTop > 0)    synth.Core().SetFilterTopHz(filterTop);
    if (cutoffBase > 0)   synth.Core().SetCutoffBaseHz(cutoffBase);
    if (qCutoffShift != 0.0) synth.Core().SetQCutoffShift(qCutoffShift);
    if (qBase > 0)        synth.Core().SetQBase(qBase);
    if (filterAttenSet)   synth.Core().SetFilterAtten(filterAtten);
    if (resonanceDb >= 0)  synth.Core().SetResonanceDb(resonanceDb);
    if (!resonanceCurve.empty())
        synth.Core().SetResonanceCurve(resonanceCurve == "faq");
    if (holdScale   != 1.0) synth.Core().SetHoldScale(holdScale);
    if (decayScale  != 1.0) synth.Core().SetDecayScale(decayScale);
    if (attackScale != 1.0) synth.Core().SetAttackScale(attackScale);
    if (sincTaps >= 4 && sincTaps <= 32 && (sincTaps % 2) == 0)
        synth.Core().SetSincTaps(sincTaps);
    else if (sincTaps != 0)
    {
        std::cerr << _("--sinc-taps has to be an even number from 4 to 32.\n");
        return 1;
    }
    if (eqMode == "off")         synth.Core().SetEqualizer(false);
    else if (eqMode == "on")     synth.Core().SetEqualizer(true);
    if (loopWrap == "off")       synth.Core().SetLoopWrap(false);
    else if (loopWrap == "on")   synth.Core().SetLoopWrap(true);
    if (panMode == "power")      synth.Core().SetPanLinear(false);
    else if (panMode == "linear") synth.Core().SetPanLinear(true);
    if (filterMode == "86box")   synth.Core().SetFilter86Box(true);
    else if (filterMode == "tpt") { synth.Core().SetFilter86Box(false); synth.Core().SetFilterCham(false); }
    else if (filterMode == "cham") { synth.Core().SetFilter86Box(false); synth.Core().SetFilterCham(true); }
    if (cutoffMap == "lin")      synth.Core().SetCutoffLinear(true);
    else if (cutoffMap == "exp") synth.Core().SetCutoffLinear(false);
    if (filterPoles > 0)  synth.Core().SetFilterPoles(filterPoles);
    if (interp == "linear") synth.Core().SetInterpolation(Emu8000Core::Interp::Linear);
    else if (interp == "cubic") synth.Core().SetInterpolation(Emu8000Core::Interp::Cubic);
    else if (interp == "3point") synth.Core().SetInterpolation(Emu8000Core::Interp::Point3);
    else if (interp == "3pointc") synth.Core().SetInterpolation(Emu8000Core::Interp::Point3c);
    else if (interp == "sinc") synth.Core().SetInterpolation(Emu8000Core::Interp::Sinc);
    if (revPreset >= 0) synth.Core().SetReverbPreset(revPreset);
    if (choPreset >= 0) synth.Core().SetChorusPreset(choPreset);
    if (revRoom >= 0 && revDamp >= 0)
        synth.Core().SetReverbRoom(static_cast<float>(revRoom), static_cast<float>(revDamp));
    if (revReturn >= 0 || choReturn >= 0)
        synth.Core().SetEffectReturns(static_cast<float>(revReturn < 0 ? 1.0 : revReturn),
                                      static_cast<float>(choReturn < 0 ? 0.7 : choReturn));

    // Record of the port writes, for the comparison with 86Box. The samples
    // are already in DRAM (they are loaded by memcpy, not through SMLD), so
    // an image of the DRAM is saved next to the trace - emu8k_ref.exe loads
    // it with --dram.
    if (!tracePath.empty())
    {
        if (!synth.Core().OpenTrace(tracePath.c_str()))
        {
            std::cerr << StrFormat(_("Could not open the trace '%s'.\n"), tracePath.c_str());
            return 1;
        }
        // The initialisation sequence already ran in the Synth constructor,
        // so it would be missing from the trace. It is repeated - the
        // registers return to the same state, now with the record.
        synth.Core().PowerOnInit();

        const std::string dramPath = tracePath + ".dram.raw";
        if (FILE* df = std::fopen(dramPath.c_str(), "wb"))
        {
            std::fwrite(synth.Core().DramData(), sizeof(int16_t),
                        synth.Core().DramSize(), df);
            std::fclose(df);
            std::cout << StrFormat(_("Trace '%s' + DRAM %zu samples.\n"), tracePath.c_str(),
                                   static_cast<size_t>(synth.Core().DramSize()));
        }
    }

    if (!noteDumpPath.empty() && !synth.OpenNoteDump(noteDumpPath))
        std::cerr << StrFormat(_("Could not open '%s'.\n"), noteDumpPath.c_str());

    // Only after the trace is switched on, so that the initial channel state
    // gets into the trace - the game's driver also sends it only after the
    // chip is initialised.
    if (!confPath.empty())
    {
        ApplyConf(synth, confMessages);
        std::cout << StrFormat(_("Configuration '%s': %zu messages on each of the 16 channels"),
                               confPath.c_str(), confMessages.size());
        if (confMaster >= 0)
            std::cout << StrFormat(_(", master volume %d"), confMaster);
        std::cout << ".\n";
    }

    // Replay of a port-write trace - instead of MIDI, the core gets exactly
    // the writes 86Box captured (e.g. AWETEST with the SDK's bank).
    if (!replayPath.empty())
    {
        if (wavPath.empty())
        {
            std::cerr << _("--replay needs --wav <file>.\n");
            return 1;
        }
        struct Ev { unsigned long long t; unsigned port, val; };
        std::vector<Ev> evs;
        size_t byteWrites = 0;
        if (FILE* tf = std::fopen(replayPath.c_str(), "rb"))
        {
            char line[256];
            while (std::fgets(line, sizeof(line), tf))
            {
                char* q = line;
                while (*q == ' ' || *q == '\t') ++q;
                if (*q == '#' || *q == 'R' || *q == '\r' || *q == '\n' || *q == 0) continue;
                unsigned long long t; unsigned port, val; char width = 'w';
                const int got = std::sscanf(q, "%llu %x %x %c", &t, &port, &val, &width);
                if (got < 3) continue;
                if (width == 'b' || width == 'B') { ++byteWrites; continue; }
                evs.push_back({ t, port, val });
            }
            std::fclose(tf);
        }
        else
        {
            std::cerr << StrFormat(_("Could not open the trace '%s'.\n"), replayPath.c_str());
            return 1;
        }

        WavWriter wav;
        if (!wav.Open(wavPath, kSampleRate))
        {
            std::cerr << StrFormat(_("Could not open the output file '%s'.\n"), wavPath.c_str());
            return 1;
        }
        std::vector<int16_t> buf(static_cast<size_t>(kFramesPerBuffer) * 2);
        uint32_t skip = synth.Core().ChipLatencyFrames();
        auto emit = [&](uint32_t frames)
        {
            synth.Core().RenderBlock(buf.data(), frames);
            if (skip >= frames) { skip -= frames; return; }
            wav.Write(buf.data() + skip * 2, frames - skip);
            skip = 0;
        };
        unsigned long long cur = 0;
        for (const Ev& ev : evs)
        {
            while (cur < ev.t)
            {
                const uint32_t n = static_cast<uint32_t>(
                    std::min<unsigned long long>(kFramesPerBuffer, ev.t - cur));
                emit(n);
                cur += n;
            }
            synth.Core().PortOut16(static_cast<uint16_t>(ev.port),
                                   static_cast<uint16_t>(ev.val));
        }
        const unsigned long long tail = 2ull * kSampleRate + synth.Core().ChipLatencyFrames();
        for (unsigned long long done = 0; done < tail; done += kFramesPerBuffer)
            emit(kFramesPerBuffer);
        wav.Close();
        std::cout << StrFormat(_("Trace '%s': %zu writes"), replayPath.c_str(), evs.size());
        if (byteWrites)
            std::cout << StrFormat(_(", byte writes skipped: %zu"), byteWrites);
        std::cout << StrFormat(_(", %llu frames -> '%s'.\n"), cur, wavPath.c_str());
        return 0;
    }

    Sequencer sequencer;
    sequencer.Load(sequence);
    sequencer.SetTriggerMute(confTriggerMute);

    std::vector<int16_t> block(static_cast<size_t>(kFramesPerBuffer) * 2);
    const uint32_t tailBlocks =
        static_cast<uint32_t>((kTailSeconds * kSampleRate) / kFramesPerBuffer) + 1;

    // Offline render to .wav - for regression tests and A/B comparisons with
    // reference recordings real time is of no use.
    if (!wavPath.empty())
    {
        WavWriter wav;
        if (!wav.Open(wavPath, kSampleRate))
        {
            std::cerr << StrFormat(_("Could not open the output file '%s'.\n"), wavPath.c_str());
            return 1;
        }

        std::cout << StrFormat(_("Rendering to '%s'...\n"), wavPath.c_str());
        // The 86Box chip delivers its sound one block late; that latency is
        // dropped at the start and rendered on at the end, so the file
        // matches emu8k_ref.exe frame for frame.
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
        std::cout << _("Done.\n");
        return 0;
    }

    // Live playback through the selected backend (AudioOutput.h).
    if (audioName.empty())
        audioName = AudioOutputs::DefaultName();
    if (audioName.empty())
    {
        std::cerr << _("This build has no audio output for live playback; use --wav <file>\n"
                       "or --audio bass (with libbass.so next to the program).\n");
        return 1;
    }
    std::string audioErr;
    std::unique_ptr<AudioOutput> audioOut = AudioOutputs::Create(audioName, audioErr);
    if (!audioOut || !audioOut->Open(kSampleRate, kFramesPerBuffer, audioErr))
    {
        std::cerr << StrFormat(_("Could not open the audio output '%s': %s\n"),
                               audioName.c_str(), audioErr.c_str());
        return 1;
    }

    std::cout << StrFormat(_("Playing through %s... (Ctrl+C to stop)\n"), audioName.c_str());

    while (sequencer.HasMoreEvents())
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut->Write(block.data(), kFramesPerBuffer);
    }

    // "Tail" - render a bit more silence / decay after the last event, so
    // that the release stage of the envelope (see Synth.h) is not cut off.
    for (uint32_t i = 0; i < tailBlocks; ++i)
    {
        sequencer.RenderBlock(synth, block.data(), kFramesPerBuffer, kSampleRate);
        audioOut->Write(block.data(), kFramesPerBuffer);
    }

    audioOut->Close();
    std::cout << _("Done.\n");
    return 0;
}
