#pragma once
#include "AudioOutput.h"

#include <cstdint>
#include <vector>

// Windows-only realtime audio output through the old but dependency-free
// WinMM waveOut API (no external libraries, winmm.lib from the Windows SDK is
// enough). Streams 16-bit stereo PCM in fixed-size blocks (see Open()).
// Selected with --audio winmm; the portable backend is AudioOutputRt.
class AudioOutputWin final : public AudioOutput
{
public:
    ~AudioOutputWin() override;

    bool Open(uint32_t sampleRate, uint32_t framesPerBuffer, std::string& err) override;

    // Blocking write - while all internal buffers are still playing, it waits
    // (busy-wait with Sleep(1)) for the next one to become free.
    void Write(const int16_t* interleavedStereo, uint32_t numFrames) override;

    // Waits until all queued buffers have played and closes the device.
    void Close() override;

private:
    static constexpr int kNumBuffers = 4;

    void* m_hWaveOut = nullptr; // HWAVEOUT, type hidden so the header need not include <windows.h>
    std::vector<int16_t> m_bufferData[kNumBuffers];
    void* m_headers = nullptr;  // WAVEHDR array, allocated in the .cpp
    uint32_t m_framesPerBuffer = 0;
    int m_currentIndex = 0;
};
