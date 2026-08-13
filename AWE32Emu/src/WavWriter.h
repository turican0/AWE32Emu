#pragma once
#include <cstdint>
#include <fstream>
#include <string>

// Jednoduchy zapis 16bit stereo PCM do .wav.
//
// Slouzi k offline renderu (prepinac --wav) - bez nej se emulace da overit
// jen poslechem v realnem case, coz je na regresni testy a A/B srovnani
// s referencnimi nahravkami (TODO sekce 8) nepouzitelne.
class WavWriter
{
public:
    bool Open(const std::string& path, uint32_t sampleRate)
    {
        m_file.open(path, std::ios::binary);
        if (!m_file) return false;
        m_sampleRate = sampleRate;
        m_dataBytes = 0;
        WriteHeader(0);   // provizorni hlavicka, prepise se v Close()
        return true;
    }

    void Write(const int16_t* interleavedStereo, uint32_t numFrames)
    {
        const std::streamsize bytes = static_cast<std::streamsize>(numFrames) * 2 * 2;
        m_file.write(reinterpret_cast<const char*>(interleavedStereo), bytes);
        m_dataBytes += static_cast<uint32_t>(bytes);
    }

    void Close()
    {
        if (!m_file.is_open()) return;
        m_file.seekp(0, std::ios::beg);
        WriteHeader(m_dataBytes);
        m_file.close();
    }

    ~WavWriter() { Close(); }

private:
    void PutU32(uint32_t v)
    {
        const uint8_t b[4] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                               static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24) };
        m_file.write(reinterpret_cast<const char*>(b), 4);
    }
    void PutU16(uint16_t v)
    {
        const uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
        m_file.write(reinterpret_cast<const char*>(b), 2);
    }

    void WriteHeader(uint32_t dataBytes)
    {
        m_file.write("RIFF", 4);   PutU32(36 + dataBytes);
        m_file.write("WAVE", 4);
        m_file.write("fmt ", 4);   PutU32(16);
        PutU16(1);                 // PCM
        PutU16(2);                 // stereo
        PutU32(m_sampleRate);
        PutU32(m_sampleRate * 2 * 2);
        PutU16(4);                 // block align
        PutU16(16);                // bits per sample
        m_file.write("data", 4);   PutU32(dataBytes);
    }

    std::ofstream m_file;
    uint32_t m_sampleRate = 44100;
    uint32_t m_dataBytes = 0;
};
