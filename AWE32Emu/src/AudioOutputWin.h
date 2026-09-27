#pragma once
#include <cstdint>
#include <vector>

// Windows-only realtime audio output through the old but dependency-free
// WinMM waveOut API (no external libraries, winmm.lib from the Windows SDK is
// enough). Streams 16-bit stereo PCM in fixed-size blocks (see Open()).
//
// TODO (docs/TODO.md, section 6 "audio output layer"): when lower latency
// or WASAPI exclusive mode is needed, this is the place to swap the backend
// (see also issue #1: a portable output such as RtAudio).
class AudioOutputWin
{
public:
    ~AudioOutputWin();

    // framesPerBuffer must match the number of frames passed to every Write() call.
    bool Open(uint32_t sampleRate, uint32_t framesPerBuffer);

    // Blocking write - while all internal buffers are still playing, it waits
    // (busy-wait with Sleep(1)) for the next one to become free.
    void Write(const int16_t* interleavedStereo, uint32_t numFrames);

    // Waits until all queued buffers have played and closes the device.
    void Close();

private:
    static constexpr int kNumBuffers = 4;

    void* m_hWaveOut = nullptr; // HWAVEOUT, type hidden so the header need not include <windows.h>
    std::vector<int16_t> m_bufferData[kNumBuffers];
    void* m_headers = nullptr;  // WAVEHDR array, allocated in the .cpp
    uint32_t m_framesPerBuffer = 0;
    int m_currentIndex = 0;
};
