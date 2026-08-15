#include "AudioOutputWin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cstring>

#pragma comment(lib, "winmm.lib")

AudioOutputWin::~AudioOutputWin()
{
    Close();
}

bool AudioOutputWin::Open(uint32_t sampleRate, uint32_t framesPerBuffer)
{
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HWAVEOUT hwo = nullptr;
    MMRESULT res = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR)
        return false;
    m_hWaveOut = hwo;

    m_framesPerBuffer = framesPerBuffer;

    auto* headers = new WAVEHDR[kNumBuffers];
    std::memset(headers, 0, sizeof(WAVEHDR) * kNumBuffers);
    m_headers = headers;

    for (int i = 0; i < kNumBuffers; ++i)
    {
        m_bufferData[i].assign(static_cast<size_t>(framesPerBuffer) * 2, 0);
        headers[i].lpData = reinterpret_cast<LPSTR>(m_bufferData[i].data());
        headers[i].dwBufferLength = static_cast<DWORD>(m_bufferData[i].size() * sizeof(int16_t));
        waveOutPrepareHeader(hwo, &headers[i], sizeof(WAVEHDR));
        headers[i].dwFlags |= WHDR_DONE; // vsechny buffery jsou zpocatku "volne k zapisu"
    }

    m_currentIndex = 0;
    return true;
}

void AudioOutputWin::Write(const int16_t* interleavedStereo, uint32_t numFrames)
{
    if (!m_hWaveOut || numFrames != m_framesPerBuffer)
        return; // TODO: podpora promenlive velikosti bloku, pokud bude potreba

    auto* headers = reinterpret_cast<WAVEHDR*>(m_headers);
    WAVEHDR& hdr = headers[m_currentIndex];

    while (!(hdr.dwFlags & WHDR_DONE))
        Sleep(1);

    std::memcpy(hdr.lpData, interleavedStereo, static_cast<size_t>(numFrames) * 2 * sizeof(int16_t));
    hdr.dwFlags &= ~WHDR_DONE;
    waveOutWrite(reinterpret_cast<HWAVEOUT>(m_hWaveOut), &hdr, sizeof(WAVEHDR));

    m_currentIndex = (m_currentIndex + 1) % kNumBuffers;
}

void AudioOutputWin::Close()
{
    if (!m_hWaveOut)
        return;

    auto* headers = reinterpret_cast<WAVEHDR*>(m_headers);
    for (int i = 0; i < kNumBuffers; ++i)
    {
        while (!(headers[i].dwFlags & WHDR_DONE))
            Sleep(1);
        waveOutUnprepareHeader(reinterpret_cast<HWAVEOUT>(m_hWaveOut), &headers[i], sizeof(WAVEHDR));
    }

    waveOutClose(reinterpret_cast<HWAVEOUT>(m_hWaveOut));
    m_hWaveOut = nullptr;

    delete[] headers;
    m_headers = nullptr;
}
