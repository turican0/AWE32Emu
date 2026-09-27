// Realtime output through un4seen BASS (https://www.un4seen.com).
//
// BASS is closed source with its own licence, so it is neither shipped nor
// linked: the library is loaded at run time (bass.dll on Windows,
// libbass.so on Linux, libbass.dylib on macOS) from the directory of the
// executable or the system search path. The few functions used are declared
// here, the SDK is not needed to build.
#include "AudioOutput.h"
#include "I18n.h"

#include <chrono>
#include <cstdint>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define BASSCALL WINAPI
#else
#include <dlfcn.h>
#define BASSCALL
#endif

namespace
{
    // From bass.h 2.4.
    using BassDword = uint32_t;
    using BassBool = int;
    using BassStreamProc = BassDword(BASSCALL*)(BassDword, void*, BassDword, void*);
    const auto kStreamProcPush = reinterpret_cast<BassStreamProc>(static_cast<intptr_t>(-1));
    constexpr BassDword kActivePlaying = 1;
    constexpr BassDword kDwordError = 0xFFFFFFFFu;  // (DWORD)-1 = failure
    constexpr int kErrorAlready = 14;       // BASS_Init was already called

    using FnGetVersion = BassDword(BASSCALL*)();
    using FnInit = BassBool(BASSCALL*)(int device, BassDword freq, BassDword flags, void* win, const void* dsguid);
    using FnFree = BassBool(BASSCALL*)();
    using FnErrorGetCode = int(BASSCALL*)();
    using FnStreamCreate = BassDword(BASSCALL*)(BassDword freq, BassDword chans, BassDword flags, BassStreamProc proc, void* user);
    using FnStreamFree = BassBool(BASSCALL*)(BassDword handle);
    using FnStreamPutData = BassDword(BASSCALL*)(BassDword handle, const void* buffer, BassDword length);
    using FnChannelPlay = BassBool(BASSCALL*)(BassDword handle, BassBool restart);
    using FnChannelIsActive = BassDword(BASSCALL*)(BassDword handle);

    class AudioOutputBass final : public AudioOutput
    {
    public:
        ~AudioOutputBass() override
        {
            Close();
            if (m_lib)
            {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(m_lib));
#else
                dlclose(m_lib);
#endif
            }
        }

        bool Open(uint32_t sampleRate, uint32_t framesPerBuffer, std::string& err) override
        {
            if (!Load(err))
                return false;

            if (HIWORD_(m_getVersion()) != 0x0204)
            {
                err = _("BASS: version 2.4 is required");
                return false;
            }
            if (!m_init(-1, sampleRate, 0, nullptr, nullptr) && m_errorGetCode() != kErrorAlready)
            {
                err = StrFormat(_("BASS_Init failed (error %d)"), m_errorGetCode());
                return false;
            }
            m_inited = true;

            // flags 0 = 16-bit samples
            m_stream = m_streamCreate(sampleRate, 2, 0, kStreamProcPush, nullptr);
            if (!m_stream)
            {
                err = StrFormat(_("BASS_StreamCreate failed (error %d)"), m_errorGetCode());
                return false;
            }
            m_queueLimit = framesPerBuffer * 4u * 4u;    // four blocks, 4 bytes per frame
            m_started = false;
            return true;
        }

        void Write(const int16_t* interleavedStereo, uint32_t numFrames) override
        {
            if (!m_stream)
                return;
            // Keep at most four blocks queued; the same depth as waveOut.
            for (;;)
            {
                const BassDword queued = m_streamPutData(m_stream, nullptr, 0);
                if (!m_started || queued == kDwordError || queued <= m_queueLimit)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            m_streamPutData(m_stream, interleavedStereo, numFrames * 4u);
            // Start only with some data queued, so the first block does not
            // stall.
            const BassDword queued = m_streamPutData(m_stream, nullptr, 0);
            if (!m_started && queued != kDwordError && queued >= m_queueLimit / 2)
            {
                m_channelPlay(m_stream, 0);
                m_started = true;
            }
        }

        void Close() override
        {
            if (m_stream)
            {
                if (!m_started)
                    m_channelPlay(m_stream, 0);
                // A push stream stalls when its queue runs dry.
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                while (m_channelIsActive(m_stream) == kActivePlaying
                       && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                m_streamFree(m_stream);
                m_stream = 0;
            }
            if (m_inited)
            {
                m_free();
                m_inited = false;
            }
        }

    private:
        static BassDword HIWORD_(BassDword v) { return v >> 16; }

        void* Sym(const char* name)
        {
#ifdef _WIN32
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_lib), name));
#else
            return dlsym(m_lib, name);
#endif
        }

        bool Load(std::string& err)
        {
#if defined(_WIN32)
            const char* name = "bass.dll";
            m_lib = LoadLibraryA(name);
#elif defined(__APPLE__)
            const char* name = "libbass.dylib";
            m_lib = dlopen(name, RTLD_NOW);
#else
            const char* name = "libbass.so";
            m_lib = dlopen(name, RTLD_NOW);
            if (!m_lib)
                m_lib = dlopen("./libbass.so", RTLD_NOW);
#endif
            if (!m_lib)
            {
                err = StrFormat(_("cannot load %s - download BASS from https://www.un4seen.com "
                                  "and put the library next to AWE32Emu"), name);
                return false;
            }

            bool ok = true;
            auto get = [&](auto& fn, const char* sym)
            {
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(Sym(sym));
                ok = ok && fn;
            };
            get(m_getVersion, "BASS_GetVersion");
            get(m_init, "BASS_Init");
            get(m_free, "BASS_Free");
            get(m_errorGetCode, "BASS_ErrorGetCode");
            get(m_streamCreate, "BASS_StreamCreate");
            get(m_streamFree, "BASS_StreamFree");
            get(m_streamPutData, "BASS_StreamPutData");
            get(m_channelPlay, "BASS_ChannelPlay");
            get(m_channelIsActive, "BASS_ChannelIsActive");
            if (!ok)
                err = StrFormat(_("%s is not a usable BASS library"), name);
            return ok;
        }

        void* m_lib = nullptr;
        bool m_inited = false;
        bool m_started = false;
        BassDword m_stream = 0;
        BassDword m_queueLimit = 0;

        FnGetVersion m_getVersion = nullptr;
        FnInit m_init = nullptr;
        FnFree m_free = nullptr;
        FnErrorGetCode m_errorGetCode = nullptr;
        FnStreamCreate m_streamCreate = nullptr;
        FnStreamFree m_streamFree = nullptr;
        FnStreamPutData m_streamPutData = nullptr;
        FnChannelPlay m_channelPlay = nullptr;
        FnChannelIsActive m_channelIsActive = nullptr;
    };
}

std::unique_ptr<AudioOutput> CreateAudioOutputBass()
{
    return std::make_unique<AudioOutputBass>();
}
