#include "SoundFontSbk.h"
#include <fstream>
#include <cstring>

namespace
{
    uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

    // RIFF chunky jsou vzdy sude zarovnane (pokud je size liche, nasleduje 1 padding byte).
    size_t PadToEven(size_t size) { return size + (size & 1); }

    void WalkChunks(const std::vector<uint8_t>& buf, size_t start, size_t end,
                     std::vector<SoundFontSbk::ChunkInfo>& outChunks)
    {
        size_t pos = start;
        while (pos + 8 <= end)
        {
            std::string id(reinterpret_cast<const char*>(&buf[pos]), 4);
            uint32_t size = ReadLE32(&buf[pos + 4]);
            size_t dataStart = pos + 8;

            SoundFontSbk::ChunkInfo info;
            info.id = id;
            info.size = size;
            info.fileOffset = dataStart;
            outChunks.push_back(info);

            if (id == "LIST" && dataStart + 4 <= end)
            {
                // LIST chunk ma dalsi 4-bajtovy typ (napr. "pdta", "sdta", "INFO")
                // a pak vnorene sub-chunky - projit je rekurzivne pro uplny prehled.
                size_t innerStart = dataStart + 4;
                size_t innerEnd = dataStart + size;
                if (innerEnd > end) innerEnd = end;
                WalkChunks(buf, innerStart, innerEnd, outChunks);
            }

            pos = dataStart + PadToEven(size);
        }
    }
}

namespace SoundFontSbk
{
    SbkBank Load(const std::string& path)
    {
        SbkBank bank;

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            bank.errorMessage = "Nelze otevrit soubor: " + path;
            return bank;
        }

        bank.rawData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        const auto& buf = bank.rawData;

        if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0)
        {
            bank.errorMessage = "Chybi RIFF hlavicka - nejde o platny .SBK/.SF2 soubor";
            return bank;
        }

        uint32_t riffSize = ReadLE32(&buf[4]);
        bank.formType = std::string(reinterpret_cast<const char*>(&buf[8]), 4);

        if (bank.formType != "sfbk")
        {
            // Nekonci to chybou - jen upozorneni v ramci errorMessage, protoze
            // nektere starsi SBK varianty mohou mit mirne odlisne oznaceni.
            bank.errorMessage = "Neocekavany form type '" + bank.formType + "' (ocekavano 'sfbk') - pokracuji tolerantne";
        }

        size_t end = 8 + riffSize;
        if (end > buf.size()) end = buf.size();

        WalkChunks(buf, 12, end, bank.chunks);

        bank.valid = true;
        return bank;
    }
}
