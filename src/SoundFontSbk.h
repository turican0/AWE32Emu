#pragma once
#include <cstdint>
#include <string>
#include <vector>

// .SBK je puvodni Creative/E-mu "SoundFont 1.0" bankovy format - RIFF kontejner
// se stejnou vrchni strukturou (RIFF....sfbk) jako pozdejsi SF2, ale s jinym
// obsahem nekterych chunku. Tahle trida zatim jen precte RIFF strukturu a
// vypise seznam chunku - realne mapovani na EMU8000 patche/instrumenty patri
// do projektoveho TODO seznamu, sekce 5 "Patch/instrument data (SoundFont vrstva)".
namespace SoundFontSbk
{
    struct ChunkInfo
    {
        std::string id;      // napr. "phdr", "shdr", "smpl"...
        uint32_t size = 0;
        size_t fileOffset = 0; // offset zacatku dat chunku v souboru
    };

    struct SbkBank
    {
        bool valid = false;
        std::string errorMessage;
        std::string formType;         // ocekavano "sfbk"
        std::vector<ChunkInfo> chunks; // ploche vypsani vsech nalezenych sub-chunku
        std::vector<uint8_t> rawData;  // cely soubor v pameti pro pozdejsi realne parsovani
    };

    SbkBank Load(const std::string& path);
}
