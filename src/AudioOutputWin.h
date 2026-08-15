#pragma once
#include <cstdint>
#include <vector>

// Windows-only realtime audio vystup pres starsi, ale zavisly-na-nicem WinMM
// waveOut API (zadne externi knihovny, staci winmm.lib z Windows SDK).
// Streamuje 16-bit stereo PCM v pevne velkych blocich (viz Open()).
//
// TODO (mimo hlavni TODO seznam projektu, ale relevantni pro sekci 6 "Audio
// output vrstva"): az bude potreba nizsi latence nebo WASAPI exclusive mode,
// tohle bude misto pro vymenu backendu.
class AudioOutputWin
{
public:
    ~AudioOutputWin();

    // framesPerBuffer musi odpovidat poctu snimku predavanych do kazdeho Write() volani.
    bool Open(uint32_t sampleRate, uint32_t framesPerBuffer);

    // Blokujici zapis - pokud jsou vsechny interni buffery jeste prehravany,
    // ceka (busy-wait s Sleep(1)) na uvolneni dalsiho.
    void Write(const int16_t* interleavedStereo, uint32_t numFrames);

    // Pocka na dohrani vsech rozpracovanych bufferu a uzavre zarizeni.
    void Close();

private:
    static constexpr int kNumBuffers = 4;

    void* m_hWaveOut = nullptr; // HWAVEOUT, typ schovan aby header nemusel includovat <windows.h>
    std::vector<int16_t> m_bufferData[kNumBuffers];
    void* m_headers = nullptr;  // pole WAVEHDR, alokovano v .cpp
    uint32_t m_framesPerBuffer = 0;
    int m_currentIndex = 0;
};
