#include "AudioOutputNull.h"

#include <cstring>


AudioOutputNull::~AudioOutputNull()
{
    Close();
}

bool AudioOutputNull::Open(uint32_t sampleRate, uint32_t framesPerBuffer)
{
    return true;
}

void AudioOutputNull::Write(const int16_t* interleavedStereo, uint32_t numFrames)
{
    if (!m_hWaveOut || numFrames != m_framesPerBuffer)
        return; // TODO: podpora promenlive velikosti bloku, pokud bude potreba

    m_currentIndex = (m_currentIndex + 1) % kNumBuffers;
}

void AudioOutputNull::Close()
{
    if (!m_hWaveOut)
        return;

    m_hWaveOut = nullptr;

    m_headers = nullptr;
}
