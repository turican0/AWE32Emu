#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Realtime audio output (issue #1). The player renders blocks of 16-bit
// stereo PCM and hands them to one of the backends:
//
//   winmm    Windows waveOut (AudioOutputWin.cpp), no extra library
//   rtaudio  RtAudio - WASAPI/DirectSound on Windows, ALSA/PulseAudio/JACK on
//            Linux, CoreAudio on macOS (AudioOutputRt.cpp, CMake option
//            AWE32EMU_WITH_RTAUDIO)
//   bass     un4seen BASS, loaded at run time from bass.dll / libbass.so next
//            to the executable (AudioOutputBass.cpp); the library is not
//            shipped, it has its own licence
//   null     plays nothing, but keeps real time - for tests without a sound
//            device (e.g. --trace of a live run on a CI machine)
//
// The rendering itself does not depend on the backend; --wav bypasses all of
// them.
class AudioOutput
{
public:
    virtual ~AudioOutput() = default;

    // framesPerBuffer must match the number of frames passed to every
    // Write() call. On failure `err` says why.
    virtual bool Open(uint32_t sampleRate, uint32_t framesPerBuffer, std::string& err) = 0;

    // Blocking write: waits while the backend's queue is full.
    virtual void Write(const int16_t* interleavedStereo, uint32_t numFrames) = 0;

    // Waits until everything written has played and closes the device.
    virtual void Close() = 0;
};

namespace AudioOutputs
{
    // Backends built into this binary, in order of preference.
    std::vector<std::string> Available();

    // The backend used when --audio is not given; empty when there is none
    // (a Linux build without RtAudio).
    std::string DefaultName();

    // nullptr (and `err`) for an unknown or unavailable name.
    std::unique_ptr<AudioOutput> Create(const std::string& name, std::string& err);
}

// Factories of the individual backends (each in its own .cpp).
std::unique_ptr<AudioOutput> CreateAudioOutputNull();
std::unique_ptr<AudioOutput> CreateAudioOutputBass();
#ifdef _WIN32
std::unique_ptr<AudioOutput> CreateAudioOutputWin();
#endif
#ifdef AWE32EMU_WITH_RTAUDIO
std::unique_ptr<AudioOutput> CreateAudioOutputRt();
#endif
