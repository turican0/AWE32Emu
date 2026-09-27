#include "AudioOutput.h"
#include "I18n.h"

#include <chrono>
#include <thread>

namespace
{
    // Discards the sound but paces the writes in real time, as a sound card
    // would.
    class AudioOutputNull final : public AudioOutput
    {
    public:
        bool Open(uint32_t sampleRate, uint32_t, std::string&) override
        {
            m_rate = sampleRate;
            m_frames = 0;
            m_start = std::chrono::steady_clock::now();
            return true;
        }

        void Write(const int16_t*, uint32_t numFrames) override
        {
            m_frames += numFrames;
            const auto due = m_start + std::chrono::microseconds(m_frames * 1000000ull / m_rate);
            std::this_thread::sleep_until(due);
        }

        void Close() override {}

    private:
        uint32_t m_rate = 44100;
        uint64_t m_frames = 0;
        std::chrono::steady_clock::time_point m_start;
    };
}

std::unique_ptr<AudioOutput> CreateAudioOutputNull()
{
    return std::make_unique<AudioOutputNull>();
}

std::vector<std::string> AudioOutputs::Available()
{
    std::vector<std::string> v;
#ifdef AWE32EMU_WITH_RTAUDIO
    v.push_back("rtaudio");
#endif
#ifdef _WIN32
    v.push_back("winmm");
#endif
    v.push_back("bass");
    v.push_back("null");
    return v;
}

std::string AudioOutputs::DefaultName()
{
#if defined(AWE32EMU_WITH_RTAUDIO)
    return "rtaudio";
#elif defined(_WIN32)
    return "winmm";
#else
    return {};
#endif
}

std::unique_ptr<AudioOutput> AudioOutputs::Create(const std::string& name, std::string& err)
{
#ifdef AWE32EMU_WITH_RTAUDIO
    if (name == "rtaudio")
        return CreateAudioOutputRt();
#endif
#ifdef _WIN32
    if (name == "winmm")
        return CreateAudioOutputWin();
#endif
    if (name == "bass")
        return CreateAudioOutputBass();
    if (name == "null")
        return CreateAudioOutputNull();

    std::string list;
    for (const std::string& n : Available())
        list += (list.empty() ? "" : ", ") + n;
    err = StrFormat(_("unknown or unavailable audio output '%s'; this build has: %s"),
                    name.c_str(), list.c_str());
    return nullptr;
}
