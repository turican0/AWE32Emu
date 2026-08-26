#include "SoundFont.h"
#include "Emu8000Regs.h"
#include "Awe32Driver.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

namespace SoundFont
{
namespace
{
    uint16_t RdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    uint32_t RdU32(const uint8_t* p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    std::string CStr(const uint8_t* p, size_t maxLen)
    {
        size_t n = 0;
        while (n < maxLen && p[n]) ++n;
        std::string s(reinterpret_cast<const char*>(p), n);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

    struct Chunk { size_t offset = 0; uint32_t size = 0; bool found = false; };

    // Projde RIFF strom a posbira vsechny listove chunky podle jmena.
    void WalkChunks(const std::vector<uint8_t>& buf, size_t pos, size_t end,
                    std::map<std::string, Chunk>& out)
    {
        while (pos + 8 <= end)
        {
            const std::string id(reinterpret_cast<const char*>(&buf[pos]), 4);
            const uint32_t size = RdU32(&buf[pos + 4]);
            const size_t data = pos + 8;
            if (data > end) break;

            if (id == "LIST" && data + 4 <= end)
            {
                WalkChunks(buf, data + 4, std::min(data + size, end), out);
            }
            else if (!out.count(id))
            {
                Chunk c; c.offset = data; c.size = size; c.found = true;
                out[id] = c;
            }
            pos = data + size + (size & 1);
        }
    }

    // -------------------------------------------------------------------
    // prevody jednotek
    // -------------------------------------------------------------------

    // Delitel v 7bitovem "plovoucim" kodovani rychlosti obalek EMU8000.
    // Viz docs/re-notes/emu8000_register_map.md, sekce 5.
    int RateDivisor(int index)
    {
        const int group = (index >> 4) & 7;
        const int m = index & 15;
        return (group == 0) ? (m + 1) : ((m + 17) << (group - 1));
    }

    // Inverze tabulek z ovladace: najdi nejmensi rate, jehoz cas je <=
    // pozadovanemu. Stejna logika jako sub_2BC0 / sub_2BF0 v SBAWE32.DRV.
    // Tabulka casu attacku je v `SBAWE.VXD` na offsetu **0x09118** - 128
    // polozek po 16 bitech, v celych ms. `11878 / RateDivisor(r-1)` ji po
    // zaokrouhleni reprodukuje **na vsech 127 polozkach**, takze ji sem nemusi
    // opisovat; jen se z ni musi vybirat tak, jak to dela ovladac:
    //
    //   prvni polozka, ktera je **kratsi** nez zadany cas (ostre)
    //   nulovy cas -> 0x7F, propadnuti cyklem -> 0x7E
    //
    // Overeno na ctyrech bodech ze dvou skladeb: 0 ms -> 0x7F,
    // 6 ms -> 0x7E (Georgia), 20 ms -> 100 a 1270 ms -> 10 (JUMP, presety
    // `polysynth` a `spolysynth`). Drive se vracelo `r-1` proti **presnym**
    // casum misto `r` proti zaokrouhlenym, coz na Georgii nevadilo - ta
    // doprostred tabulky vubec nesahne - ale na JUMPu delalo 1008 hlasu.
    int AttackRateFromMs(double ms)
    {
        if (ms <= 0.0) return 0x7F;
        for (int r = 1; r <= 0x7F; ++r)
            if (ms > static_cast<double>(std::lround(11878.0 / RateDivisor(r - 1))))
                return r;
        return 0x7E;
    }
    // Kdyz generator v bance neni, plati vychozi hodnota z tabulky ovladace,
    // ktera vychazi na 0x7D. Overeno na 242 notach MINUETu a na presetech
    // `organ3`, `jazzgtr`, `fretlessbs` a `piano2` v Georgii.
    constexpr int kAttackDefaultRate = 0x7D;
    // Ovladac vybira polozku, jejiz cas je **delsi nebo roven** zadanemu,
    // ne prvni kratsi. Zmereno instrukcni stopou: decay 12600 ms, keynum
    // skalovani 183 a nota 69 dava 12600 - 9*183 = 10953 ms, a ovladac
    // zapsal rate 4 (tabulka 11878 ms), ne 5 (9502 ms).
    int DecayRateFromMs(double ms)
    {
        if (ms <= 0.0) return 0x7F;
        for (int r = 1; r <= 0x7F; ++r)
            if (ms > 47513.0 / RateDivisor(r - 1)) return std::max(r - 1, 0);
        return 0x7F;
    }
    int HoldFromMs(double ms)
    {
        // Ovladac deli celociselne pres `idiv`, ktery **utina** k nule
        // (SBAWE32.DRV: `idiv -92; add 0x7F`), takze se nesmi zaokrouhlovat.
        // Priklad: hold 630 ms -> 630/92 = 6 -> 121 (0x79), ne 120.
        const int steps = static_cast<int>(ms / (Emu8000::kHoldSecPerStep * 1000.0));
        return std::clamp(127 - steps, 0, 127);
    }
    // Prevod delay registru **neni linearni v milisekundach**, jak by se
    // z kroku 32 vzorku zdalo. Ovladac pocita v timecents s pevnou radovou
    // carkou 16.16 (spolecna prevodni rutina, volana s cislem generatoru;
    // vytazeno z pameti guesta, viz runtime-dumps/SBAWE.VXD.obj1.mem):
    //
    //     cmp eax, 0xFFFFD120   ; <= -12000 -> 0x8000, tedy bez delay
    //     cmp eax, 0x156C       ; >= 5484   -> 0
    //     add eax, 0x30E4       ; + 12516
    //     mov ecx, 0x4B0        ; 1200
    //     shl eax, 0x10 / idiv ecx   ; (tc + 12516) / 1200 v 16.16
    //     ... 2^x pres mantisu a posun ...
    //     sub esi, edi          ; 0x8000 - vysledek
    //
    // Zadny **linearni** cinitel tuhle cestu nenahradi. Drivejsich
    // 2^(12516/1200)/1000 = 1,379567 bylo prolozeni, a proti kroku 725 us
    // se lisi u 19 400 z 24 000 celych milisekund - na SYNTHGM.SBK se oba
    // shodnou jen nahodou, protoze bance staci sest hodnot. Pocita se tedy
    // exponencialou tak, jak to dela ovladac.
    int DelayFromMs(double ms)
    {
        if (ms <= 0.0)
            return static_cast<int>(Emu8000::kDelayNone);

        // Prevod na timecents se **zaokrouhluje dolu**, ne k nule ani
        // matematicky. Rozhodla jedina hodnota v bance, 440 ms:
        //
        //     1200*log2(0,44) = -1421,31
        //     dolu  -> -1422 -> 2^((12516-1422)/1200) = 606,65 -> 606   ovladac
        //     k nule -> -1421 -> 2^((12516-1421)/1200) = 607,00 -> 607   my drive
        //
        // Ve stope JUMPu ma ovladac u vsech 504 dotcenych not ENVVAL 0x7DA2,
        // tedy 606 kroku. Ostatnich pet hodnot v bance vychazi stejne tak i tak.
        const int tc = static_cast<int>(std::floor(1200.0 * std::log2(ms / 1000.0)));
        if (tc <= -12000)
            return static_cast<int>(Emu8000::kDelayNone);
        if (tc >= 5484)
            return 0;

        // Pozor: ovladac tenhle mocninny vypocet dela v 16.16 pres mantisu
        // a posun. Ten kod je v casti, ktera bezi az pri nacitani banky, a
        // ta zatim neni ve vypisu pameti - muze se od `exp2` lisit o jednicku
        // na hranach. Az bude vypis, doplnit sem presnou verzi.
        const int steps = static_cast<int>(std::exp2((tc + 12516) / 1200.0));
        return std::clamp(static_cast<int>(Emu8000::kDelayNone) - steps, 0, 0x8000);
    }

    // Prepis `sub_192E` z `SBAWE.VXD` (obj 1, 0x192E) - centy -> registr IP:
    //
    //     esi = centy + 0x41A0        ; 16800, aby bylo vse kladne
    //     edi = esi / 0x4B0           ; 1200 -> oktava, orez na 15
    //     edx = esi % 0x4B0           ; zbytek v centech
    //     IP  = (edi << 12) | (edx*3 + (edx*31)/75)
    //
    // `3 + 31/75` je presne `4096/1200`, takze vzorec sam o sobe zkresleni
    // nema. Rozdil proti nasemu drivejsimu `kPitchUnity + log2(...)*4096`
    // delalo **celociselne deleni**: ovladac pocita v celych centech a utina,
    // my jsme cely retez vedli v doublech. Na Georgii to bylo +-1 u 67 not.
    int PitchFromCents(double cents)
    {
        int v = static_cast<int>(std::lround(cents)) + 0x41A0;
        if (v < 0) v = 0;
        int oct = v / 1200;
        if (oct > 15) oct = 15;
        const int rem = v % 1200;
        return (oct << 12) | (rem * 3 + (rem * 31) / 75);
    }

    double TimecentsToMs(int tc) { return std::pow(2.0, tc / 1200.0) * 1000.0; }

    // Absolutni centy (SF2) -> jednotky IFATN (ctvrt pultonu od 125 Hz).
    int FilterFcFromAbsCents(int cents)
    {
        // Musi davat tentyz registr jako cesta pres SF1, jinak tataz banka
        // ve dvou formatech zni jinak. Presne to se nam stalo: `SYNTHGM.SBK`
        // davalo u klaviru IFATN `dc30` (cutoff 220) a `SYNTHGM.SF2` `f542`
        // (cutoff 245), protoze tady byl krok 25 centu misto 29,3843
        // a zaklad 125 Hz misto 101,81 Hz. Spravna je hodnota z SF1 - ta
        // sedi proti skutecnemu ovladaci na 242 notach.
        return std::clamp(static_cast<int>(std::lround(
            (cents - Emu8000::kCutoffBaseCents) / Emu8000::kCutoffCentsStep)), 0, 255);
    }

    int8_t ClampS8(int v) { return static_cast<int8_t>(std::clamp(v, -128, 127)); }
    uint8_t ClampU8(int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); }
}

// ===========================================================================
// GenSet
// ===========================================================================

void GenSet::AddFrom(const GenSet& other)
{
    for (int i = 0; i < Gen::Count; ++i)
        if (other.present[i])
        {
            if (present[i]) value[i] = static_cast<int16_t>(value[i] + other.value[i]);
            else            { value[i] = other.value[i]; present[i] = true; }
        }
}

void GenSet::OverrideFrom(const GenSet& other)
{
    for (int i = 0; i < Gen::Count; ++i)
        if (other.present[i]) { value[i] = other.value[i]; present[i] = true; }
}

// ===========================================================================
// nacteni banky
// ===========================================================================

Bank Load(const std::string& path)
{
    Bank bank;

    std::ifstream file(path, std::ios::binary);
    if (!file) { bank.errorMessage = "Nelze otevrit soubor: " + path; return bank; }

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0)
    {
        bank.errorMessage = "Chybi RIFF hlavicka";
        return bank;
    }

    std::map<std::string, Chunk> c;
    const size_t riffEnd = std::min<size_t>(buf.size(), 8 + RdU32(&buf[4]));
    WalkChunks(buf, 12, riffEnd, c);

    auto need = [&](const char* id) -> const Chunk*
    {
        auto it = c.find(id);
        return (it != c.end() && it->second.found) ? &it->second : nullptr;
    };

    const Chunk* ifil = need("ifil");
    if (!ifil || ifil->size < 2) { bank.errorMessage = "Chybi chunk ifil"; return bank; }
    const uint16_t major = RdU16(&buf[ifil->offset]);
    bank.version = (major < 2) ? Version::Sf1 : Version::Sf2;

    if (const Chunk* n = need("INAM")) bank.name = CStr(&buf[n->offset], n->size);
    if (const Chunk* r = need("irom")) bank.romName = CStr(&buf[r->offset], r->size);

    // ---- vzorkova data ----
    if (const Chunk* smpl = need("smpl"))
    {
        bank.sampleData.resize(smpl->size / 2);
        std::memcpy(bank.sampleData.data(), &buf[smpl->offset], bank.sampleData.size() * 2);
    }

    // ---- hlavicky vzorku ----
    const Chunk* shdr = need("shdr");
    if (!shdr) { bank.errorMessage = "Chybi chunk shdr"; return bank; }

    if (bank.version == Version::Sf1)
    {
        // SF1: 16 B na zaznam, jmena v samostatnem chunku snam (20 B).
        const Chunk* snam = need("snam");
        const uint32_t count = shdr->size / 16;
        bank.samples.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* p = &buf[shdr->offset + i * 16];
            Sample s;
            s.start     = RdU32(p);
            s.end       = RdU32(p + 4);
            s.loopStart = RdU32(p + 8);
            s.loopEnd   = RdU32(p + 12);
            if (snam && (i + 1) * 20 <= snam->size)
                s.name = CStr(&buf[snam->offset + i * 20], 20);
            // Hvezdicka v nazvu = vzorek ve wave ROM. Banka, ktera nema
            // chunk `smpl` vubec (napr. SYNTHGM.SBK - popis GM banky od
            // E-mu), popisuje jen obsah ROM, takze tam jsou v ROM vsechny.
            s.inRom = bank.sampleData.empty()
                   || (!s.name.empty() && s.name[0] == '*');
            // SF1 hlavicka vzorku neobsahuje sample rate ani zakladni notu -
            // EMU8000 bezi nativne na 44100 Hz a zakladni nota se bere
            // z generatoru (OverridingRootKey / Sf1RootPitchCents).
            s.sampleRate = 44100;   // EMU8000 nativni takt
            s.originalKey = 60;
            bank.samples.push_back(std::move(s));
        }
    }
    else
    {
        const uint32_t count = shdr->size / 46;
        bank.samples.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* p = &buf[shdr->offset + i * 46];
            Sample s;
            s.name       = CStr(p, 20);
            s.start      = RdU32(p + 20);
            s.end        = RdU32(p + 24);
            s.loopStart  = RdU32(p + 28);
            s.loopEnd    = RdU32(p + 32);
            s.sampleRate = RdU32(p + 36);
            s.originalKey = p[40];
            s.correction  = static_cast<int8_t>(p[41]);
            const uint16_t type = RdU16(p + 44);
            s.inRom = (type & 0x8000) != 0 || (!s.name.empty() && s.name[0] == '*');
            if (s.sampleRate == 0) s.sampleRate = 44100;
            bank.samples.push_back(std::move(s));
        }
        if (!bank.samples.empty() && bank.samples.back().name == "EOS")
            bank.samples.pop_back();
    }

    // ---- bag / gen / instrumenty / presety ----
    const Chunk* pbag = need("pbag"); const Chunk* pgen = need("pgen");
    const Chunk* inst = need("inst"); const Chunk* ibag = need("ibag");
    const Chunk* igen = need("igen"); const Chunk* phdr = need("phdr");
    if (!pbag || !pgen || !inst || !ibag || !igen || !phdr)
    {
        bank.errorMessage = "Chybi nektery z chunku phdr/pbag/pgen/inst/ibag/igen";
        return bank;
    }

    auto genAt = [&](const Chunk* ch, uint32_t i, int& op, int16_t& val)
    {
        const uint8_t* p = &buf[ch->offset + i * 4];
        op = RdU16(p);
        val = static_cast<int16_t>(RdU16(p + 2));
    };
    auto bagGenIndex = [&](const Chunk* ch, uint32_t i) -> uint32_t
    {
        return RdU16(&buf[ch->offset + i * 4]);
    };

    // Rozdeli zony bagu na "globalni" (bez terminatoru) a normalni.
    auto readZones = [&](const Chunk* bagCh, const Chunk* genCh,
                          uint32_t bagFirst, uint32_t bagLast,
                          int terminator, GenSet& global, std::vector<Zone>& zones)
    {
        const uint32_t nGen = genCh->size / 4;
        for (uint32_t b = bagFirst; b < bagLast; ++b)
        {
            const uint32_t g0 = bagGenIndex(bagCh, b);
            const uint32_t g1 = bagGenIndex(bagCh, b + 1);
            Zone z;
            bool hasTerminator = false;
            for (uint32_t g = g0; g < g1 && g < nGen; ++g)
            {
                int op; int16_t val;
                genAt(genCh, g, op, val);
                if (op == Gen::KeyRange)
                {
                    const uint16_t raw = static_cast<uint16_t>(val);
                    z.keyLo = raw & 0xFF; z.keyHi = (raw >> 8) & 0xFF;
                }
                else if (op == Gen::VelRange)
                {
                    const uint16_t raw = static_cast<uint16_t>(val);
                    z.velLo = raw & 0xFF; z.velHi = (raw >> 8) & 0xFF;
                }
                else if (op == terminator)
                {
                    hasTerminator = true;
                    if (terminator == Gen::SampleID) z.sampleId = static_cast<uint16_t>(val);
                    else                             z.instrument = static_cast<uint16_t>(val);
                }
                z.gen.Set(op, val);
            }
            // Zona bez terminatoru na prvnim miste = globalni zona.
            if (!hasTerminator)
            {
                if (b == bagFirst) global = z.gen;
                continue;
            }
            zones.push_back(std::move(z));
        }
    };

    // instrumenty
    const uint32_t nInst = inst->size / 22;
    for (uint32_t i = 0; i + 1 < nInst; ++i)
    {
        const uint8_t* p = &buf[inst->offset + i * 22];
        Instrument in;
        in.name = CStr(p, 20);
        const uint32_t b0 = RdU16(p + 20);
        const uint32_t b1 = RdU16(p + 22 + 20);
        readZones(ibag, igen, b0, b1, Gen::SampleID, in.global, in.zones);
        bank.instruments.push_back(std::move(in));
    }

    // presety
    const uint32_t nPreset = phdr->size / 38;
    for (uint32_t i = 0; i + 1 < nPreset; ++i)
    {
        const uint8_t* p = &buf[phdr->offset + i * 38];
        Preset pr;
        pr.name    = CStr(p, 20);
        pr.program = RdU16(p + 20);
        pr.bank    = RdU16(p + 22);
        const uint32_t b0 = RdU16(p + 24);
        const uint32_t b1 = RdU16(p + 38 + 24);
        readZones(pbag, pgen, b0, b1, Gen::Instrument, pr.global, pr.zones);
        bank.presets.push_back(std::move(pr));
    }

    bank.valid = true;
    return bank;
}

// ===========================================================================
// vyber zon
// ===========================================================================

const Preset* Bank::FindPreset(int bankNum, int program) const
{
    for (const Preset& p : presets)
        if (p.bank == bankNum && p.program == program) return &p;
    return nullptr;
}

std::vector<Region> Bank::Select(int bankNum, int program, int key, int velocity) const
{
    std::vector<Region> out;

    // Zadny fallback na banku 0 - o ten se stara volajici az potom, co se
    // na presnou banku zeptal VSECH nactenych bank. Jinak by uzivatelska
    // banka s presetem 0 prebila treba GM bicí v bance 128.
    const Preset* preset = FindPreset(bankNum, program);
    if (!preset) return out;

    for (const Zone& pz : preset->zones)
    {
        if (key < pz.keyLo || key > pz.keyHi) continue;
        if (velocity < pz.velLo || velocity > pz.velHi) continue;
        if (pz.instrument < 0 || pz.instrument >= static_cast<int>(instruments.size())) continue;

        const Instrument& in = instruments[pz.instrument];
        for (const Zone& iz : in.zones)
        {
            if (key < iz.keyLo || key > iz.keyHi) continue;
            if (velocity < iz.velLo || velocity > iz.velHi) continue;
            if (iz.sampleId < 0 || iz.sampleId >= static_cast<int>(samples.size())) continue;

            Region r;
            r.sample = &samples[iz.sampleId];

            // Poradi skladani podle specifikace: globalni zona instrumentu,
            // pak zona instrumentu (obe absolutni), a nakonec preset zony
            // jako offsety.
            r.gen = in.global;
            r.gen.OverrideFrom(iz.gen);
            GenSet presetGen = preset->global;
            presetGen.OverrideFrom(pz.gen);

            // Utlum SF1 se musi secist az v jednotkach registru, jinak by
            // `AddFrom` secetlo suroviny (napr. zona bicich 121 + preset 127
            // = 248) a `127 - 248` by spadlo na nulu. Zmereno na Georgii:
            // u bicich ma ovladac presne o `127 - atten zony` vic nez my
            // (zona 121 -> +6, zona 112 -> +15, ...).
            if (version == Version::Sf1)
            {
                int units = 0;
                bool any = false;
                if (r.gen.Has(Gen::InitialAttenuation))
                { units += 127 - r.gen.value[Gen::InitialAttenuation]; any = true; }
                if (presetGen.Has(Gen::InitialAttenuation))
                { units += 127 - presetGen.value[Gen::InitialAttenuation]; any = true; }
                r.sf1AttenUnits = any ? units : -1;
            }

            r.gen.AddFrom(presetGen);

            out.push_back(std::move(r));
        }
    }
    return out;
}

// ===========================================================================
// prevod na registry
// ===========================================================================

VoiceParams MakeVoiceParams(const Bank& bank, const Region& region,
                            int key, int velocity,
                            uint32_t dramBase, uint32_t romPoolBase,
                            Awe32::Driver drv)
{
    using namespace Emu8000;
    VoiceParams vp;
    const GenSet& g = region.gen;
    const Sample& s = *region.sample;
    const bool sf1 = (bank.version == Version::Sf1);

    // ---- adresy ---------------------------------------------------------
    // Vzorek lezi v ROM bud proto, ze ho tak oznacuje banka (hvezdicka
    // v nazvu / priznak v shdr), nebo proto, ze cela banka popisuje ROM.
    const bool inRom = s.inRom || bank.samplesInRom;
    const uint32_t base = inRom ? romPoolBase : dramBase;
    uint32_t start, loopStart, loopEnd;
    if (sf1)
    {
        // SF1 uklada uz hotove adresy pro cip (vcetne korekce na
        // interpolator) - u ROM vzorku se pouzivaji primo, u vlastnich
        // se jen posunou tam, kam se banka nahrala.
        const uint32_t off = inRom ? 0 : dramBase;
        // Offsety ze zony se musi pricist i tady. Chybelo to a projevilo se
        // to na vzorku `organwave` v presetu Organ 3, kde ma zona
        // `startloopAddrsOffset -1` i `endloopAddrsOffset -1`: ovladac psal
        // PSST F146 a CSL F17D, my F147 a F17E (232 not Georgie).
        start     = s.start + off
                  + g.Get(Gen::StartAddrsOffset, 0)
                  + 32768u * g.Get(Gen::StartAddrsCoarseOffset, 0);
        loopStart = s.loopStart + off
                  + g.Get(Gen::StartloopAddrsOffset, 0)
                  + 32768u * g.Get(Gen::StartloopAddrsCoarseOffset, 0);
        loopEnd   = s.loopEnd + off
                  + g.Get(Gen::EndloopAddrsOffset, 0)
                  + 32768u * g.Get(Gen::EndloopAddrsCoarseOffset, 0);
    }
    else
    {
        // SF2 uklada indexy; korekce -1/-2/-3 odpovida tomu, jak tytez
        // vzorky adresuje Creative ve vlastni SF1 bance (viz docs/re-notes).
        start     = base + s.start + g.Get(Gen::StartAddrsOffset, 0)
                    + 32768u * g.Get(Gen::StartAddrsCoarseOffset, 0) - 1;
        loopStart = base + s.loopStart + g.Get(Gen::StartloopAddrsOffset, 0)
                    + 32768u * g.Get(Gen::StartloopAddrsCoarseOffset, 0) - 2;
        loopEnd   = base + s.loopEnd + g.Get(Gen::EndloopAddrsOffset, 0)
                    + 32768u * g.Get(Gen::EndloopAddrsCoarseOffset, 0) - 3;
    }

    // Vychozi hodnota je **0** = jednorazovy vzorek, ne smycka. Neni to
    // odhad: oba ovladace maji tabulku vychozich hodnot generatoru, ktera se
    // pred aplikaci banky nakopiruje do bloku parametru vrstvy, a v obou je
    // na pozici generatoru 54 nula.
    //   SBAWE32.MDI 0x16AD (0x43 slov, kopiruje se na 0x1CD2)
    //   SBAWE.VXD   obj 1, 0x6D60 (tataz tabulka, stejne hodnoty)
    // Poznat to jde na bance Magic Carpet 2: presety LOOP2 a LOOP3 zadny
    // sampleModes nemaji a ovladac jim opravdu smycku pokladá az za vzorek.
    const int sampleModes = g.Get(Gen::SampleModes, 0);
    vp.looping = (sampleModes & 1) != 0;
    if (!vp.looping)
    {
        // EMU8000 umi jen smyckovat, "one-shot" rezim nema. Ovladac to resi
        // tim, ze smycku polozi do ticha ZA vzorek - format za kazdy vzorek
        // pripisuje 46 nulovych vzorku (v 1mgm.sf2 je mezera mezi koncem
        // jednoho a zacatkem dalsiho presne 46).
        //
        // Konkretni offsety +4 a +8 jsou prepsane z SBAWE32.DRV (0x02E4):
        //     loopStart = end + 4;  loopEnd = end + 8;
        const uint32_t end = (sf1 && inRom) ? s.end : (base + s.end);
        loopStart = end + 4;
        loopEnd   = end + 8;
    }

    // ---- Q + adresa -> CCCA ---------------------------------------------
    int q;
    // SF1 ma `initialFilterQ` 0..127, registr 0..15. Zmereno na Georgii proti
    // `SBAWE.VXD` (3331 not, tri ruzne hodnoty v `SYNTHGM.SBK`):
    //
    //     SF1 12 -> 1,  SF1 50 -> 6,  SF1 79 -> 9
    //
    // Puvodni `v * 15 / 127` s utinanim davalo u 50 hodnotu 5 a rozeslo se
    // na 669 notach (preset "Piano 2"). Posun o tri bity sedi na vsechny tri
    // body a je to i to, co by 16bitovy ovladac nejspis delal (`shr ax, 3`).
    //
    // Pozor: `lround(v * 15 / 127.0)` sedi na tytez tri body taky. Rozliseni
    // by prinesla nota s `initialFilterQ` 6, 14 nebo 22 - u tech se obe
    // varianty lisi. V zadne nasi stope zatim takova neni.
    if (sf1) q = g.Get(Gen::InitialFilterQ, 0) >> 3;
    else     q = static_cast<int>(std::lround(g.Get(Gen::InitialFilterQ, 0) / 10.0
                                              / kResonanceMaxDb * kCccaQMax));
    q = std::clamp(q, 0, kCccaQMax);
    // Do CCCA jde pocatecni adresa zmensena o konstantu, ktera se
    // u obou rodin ovladacu **lisi** - viz Awe32::StartAddressOffset().
    // Surova adresa se proto veze zvlast a slozi se az v Synth.
    vp.sampleStart = start;
    vp.ccca = (static_cast<uint32_t>(q) << kCccaQShift)
            | ((start - Awe32::StartAddressOffset(Awe32::kDefaultDriver))
               & kCccaAddressMask);

    // ---- pan + loop start -> PSST ---------------------------------------
    // Pan patche se drzi v jednotkach ovladace (0..127, 64 = stred, vychozi
    // hodnota z tabulky vychozich generatoru). Do registru se prevadi az
    // v Synth, protoze se tam scita s CC10. Registrova podoba nize slouzi
    // jen pro nahradni hlas a pro vypis.
    if (sf1)
        vp.patchPan = g.Get(Gen::Pan, 64);
    else
        // SF2: -500 = zcela vlevo, +500 = zcela vpravo; EMU8000 je opacne.
        vp.patchPan = std::clamp(64 + static_cast<int>(
            std::lround(g.Get(Gen::Pan, 0) * 127.0 / 1000.0)), 0, 127);
    const int pan = 0x17F - 2 * (vp.patchPan + 64);
    vp.psst = (static_cast<uint32_t>(ClampU8(pan)) << kPanShift)
            | (loopStart & kLoopAddressMask);

    // ---- chorus send + loop end -> CSL ----------------------------------
    const int chorus = sf1 ? g.Get(Gen::ChorusEffectsSend, 0)
                           : static_cast<int>(std::lround(g.Get(Gen::ChorusEffectsSend, 0) * 255.0 / 1000.0));
    // Ovladac pricita ke konci smycky 1 - ale **jen ve smyckove vetvi**.
    // U jednorazoveho vzorku pokládá smycku na `konec+4 .. konec+8` a zadnou
    // jednicku uz nepricita:
    //   SBAWE32.MDI 0x2019 (smycka: `add ax, 1`) vs 0x208B (one-shot: `add ax, 8`)
    //   SBAWE.VXD   obj 1, 0x1EE7 `inc eax` je take jen ve smyckove vetvi
    // Zmereno na 52 notach bicich z Magic Carpet 2, kde nam CSL vychazelo
    // presne o 1 vic nez ovladaci.
    vp.csl = (static_cast<uint32_t>(ClampU8(chorus)) << kChorusShift)
           | ((loopEnd + (vp.looping ? 1u : 0u)) & kLoopAddressMask);

    // Vychozi reverb send ovladace je 28: SYNTHGM.SBK zadny reverbEffectsSend
    // neobsahuje a MINUET neposila CC91, presto ovladac zapisuje 0x1C do
    // horniho bajtu spodniho slova PTRX u vsech 242 not.
    vp.reverbSend = ClampU8(sf1 ? g.Get(Gen::ReverbEffectsSend, 28)
                                : static_cast<int>(std::lround(g.Get(Gen::ReverbEffectsSend, 0) * 255.0 / 1000.0)));

    // ---- vyska tonu -> IP ------------------------------------------------
    int rootKey = g.Get(Gen::OverridingRootKey, -1);
    if (rootKey < 0) rootKey = s.originalKey;
    double cents = 0.0;
    if (sf1 && g.Has(Gen::Sf1RootPitchCents))
        cents = -(g.value[Gen::Sf1RootPitchCents] - rootKey * 100.0);
    cents += s.correction;
    cents += g.Get(Gen::CoarseTune, 0) * 100.0;
    cents += g.Get(Gen::FineTune, 0);

    const double scale = g.Get(Gen::ScaleTuning, 100) / 100.0;
    const double semis = (key - rootKey) * scale + cents / 100.0;
    // Frekvence vzorku se do centu prevede zvlast - ovladac ma tuhle slozku
    // uz zapecenou v `gen55`, my ji drzime v hlavicce vzorku.
    const double centsTotal = semis * 100.0
        + std::log2(s.sampleRate / static_cast<double>(44100)) * 1200.0;
    vp.ip = static_cast<uint16_t>(std::clamp(PitchFromCents(centsTotal), 0, 65535));

    // ---- utlum patche a filtr -> IFATN ------------------------------------
    // Utlum se sklada az v Synth vrstve podle vzorce prepsaneho z ovladace
    // (viz Awe32Curves.h) - tady jen prevedeme utlum patche na jednotky
    // registru (0.375 dB na jednotku, 0 = bez utlumu).
    if (sf1)
    {
        // SF1: 0..127, kde 127 = bez utlumu. Ovladac pocita 0x7F - v
        // a vysledek pricita rovnou v jednotkach registru.
        // Vychozi hodnota se mezi rodinami **lisi**: v tabulce vychozich
        // generatoru ma SBAWE32.MDI (0x16AD+0x60) 110, kdezto SBAWE.VXD
        // (obj 1, 0x6D60+0x60) 127.
        const int dflt = (drv == Awe32::Driver::Dos) ? 110 : 127;
        const int units = (region.sf1AttenUnits >= 0) ? region.sf1AttenUnits
                                                      : (127 - dflt);
        vp.patchAttenUnits = static_cast<uint8_t>(std::clamp(units, 0, 255));
    }
    else
    {
        // SF2: centibely (0.1 dB) -> jednotky po 0.375 dB.
        vp.patchAttenUnits = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(g.Get(Gen::InitialAttenuation, 0)
                                         / 10.0 / kAttenDbPerStep)), 0, 255));
    }

    // SF1 uklada initialFilterFc jako 0..127, registr IFATN ma cutoff
    // 8bitovy. Prevod je **prosty dvojnasobek**, chybi-li generator, plati
    // vychozi 255 z tabulky v ovladaci (MDI 0x16AD, VXD 0x6D60).
    //
    // Zmereno na Georgii proti `SBAWE.VXD`:
    //
    //     SF1 52 -> 104,  SF1 97 -> 194,  SF1 127 -> 254,  chybi -> 255
    //
    // Drive se tu pocitalo `v * 255 / 127`, aby 127 davalo 255. Ta uprava
    // byla naroubovana na spatne mereni: preset 52 'Choir Aahs' z Magic
    // Carpet 2, kde ovladac zapsal cutoff 255, **zadny `initialFilterFc`
    // nema** - slo tedy o vychozi hodnotu, ne o prevod cisla 127. Skutecna
    // 127 se objevila az u presetu "Piano 2" na Georgii a dala 254.
    int cutoff;
    if (!g.Has(Gen::InitialFilterFc)) cutoff = 255;
    else if (sf1)                     cutoff = std::clamp<int>(
                                          g.value[Gen::InitialFilterFc] * 2, 0, 255);
    else                              cutoff = FilterFcFromAbsCents(g.value[Gen::InitialFilterFc]);

    // Spodni bajt (utlum) doplni Synth podle krivek z ovladace.
    vp.ifatn = static_cast<uint16_t>(cutoff << 8);

    // ---- modulace --------------------------------------------------------
    // `sf1Scale` je nasobek pro SF1: cast generatoru ma v SBK sedmibitovy
    // rozsah, kdezto registr je osmibitovy, takze ovladac hodnotu zdvojuje.
    // Zmereno na Georgii proti `SBAWE.VXD`, 3331 not, **bez jedine vyjimky**:
    //
    //     modEnvToFilterFc  3F -> 7E,  01 -> 02   (1410 not)
    //     modLfoToFilterFc  08 -> 10              (478 not)
    //     modLfoToVolume    23 -> 46              (712 not)
    //     freqModLFO        12 -> 24              (824 not)
    //     freqVibLFO        2C -> 58              (595 not)
    //
    // Vysky (`modEnvToPitch`, `modLfoToPitch`, `vibLfoToPitch`) se naopak
    // **nezdvojuji** - u `vibLfoToPitch` sedi 03 a FF na 595 notach a
    // u `modLfoToPitch` hodnota 01 na 111 notach, takze tam by nasobeni
    // shodu rozbilo. Delici cara je tedy vyska/filtr, ne SF1/SF2.
    auto modAmount = [&](int op, double sf2Scale, int sf1Scale = 1) -> int8_t
    {
        if (!g.Has(op)) return 0;
        return sf1 ? ClampS8(g.value[op] * sf1Scale)
                   : ClampS8(static_cast<int>(std::lround(g.value[op] / sf2Scale)));
    };

    // SF2 udava hloubky v centech; EMU8000 ma +-1 oktavu na +-127,
    // tj. 1200 centu / 127 kroku = 9.45 centu na krok. Filtr ma +-6 oktav
    // (PEFE) resp. +-3 oktavy (FMMOD), tj. 56.7 resp. 28.3 centu na krok.
    const double kPitchCentsPerStep  = 1200.0 * kPefePitchOctaves / 127.0;
    const double kPefeFcCentsPerStep = 1200.0 * kPefeFilterOctaves / 127.0;
    const double kFmmodFcCentsPerStep = 1200.0 * kFmmodFilterOctaves / 127.0;
    const double kTremCbPerStep      = kTremoloMaxDb * 10.0 / 127.0;

    vp.pefe = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::ModEnvToPitch, kPitchCentsPerStep)) << 8)
        | static_cast<uint8_t>(modAmount(Gen::ModEnvToFilterFc, kPefeFcCentsPerStep, 2)));

    // Kdyz generator chybi, ovladac pouzije frekvenci LFO1 = 128. Zmereno
    // v poli hlasu na +0x2C u vsech 242 not; SYNTHGM.SBK u piana freqModLFO
    // nema, presto ovladac zapisuje TREMFRQ = 0x0080.
    // Pritomna frekvence se zdvojuje stejne jako hloubky vyse; chybejici
    // generator ale znamena rovnou registrovou hodnotu 128, ne 64x2.
    const int lfo1Freq = sf1 ? (g.Has(Gen::FreqModLFO)
                                    ? ((g.value[Gen::FreqModLFO] * 2) & 0xFF)
                                    : 128)
                             : std::clamp<int>(static_cast<int>(std::lround(
                                   8.176 * std::pow(2.0, g.Get(Gen::FreqModLFO, 0) / 1200.0)
                                   / kLfoHzPerStep)), 0, 255);
    // Pozor: ovladac vysledek **nechava pretect bajtem**, neoreze ho.
    // Zmereno na RELAXu: `freqVibLFO 132` -> 264 -> zapsano 0x08, my jsme
    // davali 0xFF.
    const int lfo2Freq = sf1 ? (g.Has(Gen::FreqVibLFO)
                                    ? ((g.value[Gen::FreqVibLFO] * 2) & 0xFF)
                                    : 0)
                             : std::clamp<int>(static_cast<int>(std::lround(
                                   8.176 * std::pow(2.0, g.Get(Gen::FreqVibLFO, 0) / 1200.0)
                                   / kLfoHzPerStep)), 0, 255);

    vp.fmmod = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::ModLfoToPitch, kPitchCentsPerStep)) << 8)
        | static_cast<uint8_t>(modAmount(Gen::ModLfoToFilterFc, kFmmodFcCentsPerStep, 2)));
    vp.tremfrq = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::ModLfoToVolume, kTremCbPerStep, 2)) << 8) | lfo1Freq);
    vp.fm2frq2 = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::VibLfoToPitch, kPitchCentsPerStep)) << 8) | lfo2Freq);

    // ---- obalky ----------------------------------------------------------
    auto timeMs = [&](int op, double defaultMs) -> double
    {
        if (!g.Has(op)) return defaultMs;
        return sf1 ? static_cast<double>(static_cast<uint16_t>(g.value[op]))
                   : TimecentsToMs(g.value[op]);
    };
    auto sustainReg = [&](int op) -> int
    {
        // Chybejici generator znamena sustain 0 (doznivani do ticha), ne 0x7F.
        // Zmereno v poli hlasu na +0x4A (DCYSUSV) a +0x3A (DCYSUS) u vsech
        // 242 not; pro klavir je doznivani spravne.
        if (!g.Has(op)) return 0;
        // SF1: uroven sustainu v celych decibelech nad tichem, registr ma
        // kroky po 0.75 dB - pomer je tedy 4/3, ne 1:1.
        //
        // Programmer's Guide k DCYSUSV: "bits 14-8 are the volume envelope
        // sustain level in 0.75dB increments, with 0x7f being no
        // attenuation". Prevod overen na bance, kterou mame v obou
        // formatech: `SYNTHGM.SBK` (SF1) a `SYNTHGM.SF2` z DOSoveho SDK
        // popisuji tytez presety, takze z nich jde odecist SF1 -> centibely:
        //
        //     SF1  99 -> 0 cB    -> registr 127     (99*4/3 = 132, orez)
        //     SF1  93 -> 23 cB   -> registr 123.9   (124.0)
        //     SF1  92 -> 34 cB   -> registr 122.5   (122.7)
        //     SF1  90 -> 55 cB   -> registr 119.7   (120.0)
        //     SF1  87 -> 86 cB   -> registr 115.5   (116.0)
        //
        // Sedi to i s merenim: u presetu 52 'Choir Aahs' ma banka sustain 99
        // a ovladac zapsal 0x7F, kdezto my jsme posilali 0x63 (= 99 syrove).
        if (sf1) return std::clamp<int>(g.value[op] * 4 / 3, 0, 0x7F);
        // SF2: centibely utlumu, 0 = plna uroven
        return std::clamp(127 - static_cast<int>(std::lround(
            g.value[op] / 10.0 / kSustainDbPerStep)), 0, 127);
    };

    // Zavislost obalky na cisle noty. Prepis z SBAWE32.DRV (0x0278):
    //     hold  += (60 - key) * keynumToHold        (nezaporne)
    //     decay -= (key - 60) * keynumToDecay       (nezaporne)
    // U SF2 jsou generatory v timecentech na klavesu, takze se uprava dela
    // jeste pred prevodem na milisekundy; u SF1 rovnou v ms jako v ovladaci.
    auto keyScaled = [&](int timeOp, int keyOp, bool subtract) -> double
    {
        const int amount = g.Get(keyOp, 0);
        if (amount == 0) return timeMs(timeOp, 0.0);
        if (sf1)
        {
            const double base = timeMs(timeOp, 0.0);
            const double delta = (subtract ? (key - 60) : (60 - key)) * amount;
            return std::max(0.0, subtract ? base - delta : base + delta);
        }
        const int tc = g.Has(timeOp) ? g.value[timeOp] : -12000;
        const int adj = tc + (subtract ? -(key - 60) : (60 - key)) * amount;
        return TimecentsToMs(adj);
    };

    vp.envvol  = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayVolEnv, 0.0)));
    vp.atkhldv = static_cast<uint16_t>(
        (HoldFromMs(keyScaled(Gen::HoldVolEnv, Gen::KeynumToVolEnvHold, false)) << 8)
        | (g.Has(Gen::AttackVolEnv)
               ? AttackRateFromMs(timeMs(Gen::AttackVolEnv, 0.0))
               : kAttackDefaultRate));
    vp.dcysusv = static_cast<uint16_t>(
        (sustainReg(Gen::SustainVolEnv) << 8)
        | DecayRateFromMs(keyScaled(Gen::DecayVolEnv, Gen::KeynumToVolEnvDecay, true)));
    vp.releaseRate = static_cast<uint8_t>(DecayRateFromMs(timeMs(Gen::ReleaseVolEnv, 0.0)));

    vp.envval = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayModEnv, 0.0)));
    vp.atkhld = static_cast<uint16_t>(
        (HoldFromMs(keyScaled(Gen::HoldModEnv, Gen::KeynumToModEnvHold, false)) << 8)
        | (g.Has(Gen::AttackModEnv)
               ? AttackRateFromMs(timeMs(Gen::AttackModEnv, 0.0))
               : kAttackDefaultRate));
    vp.dcysus = static_cast<uint16_t>(
        (sustainReg(Gen::SustainModEnv) << 8)
        | DecayRateFromMs(keyScaled(Gen::DecayModEnv, Gen::KeynumToModEnvDecay, true)));
    vp.releaseModRate = static_cast<uint8_t>(
        DecayRateFromMs(timeMs(Gen::ReleaseModEnv, 0.0)));

    // Kdyz attack vyjde na maximum (0x7F, tedy okamzity), ovladac do
    // prislusneho delay registru zapise **0xBFFF** misto 0x8000. Na zvuk to
    // nema vliv - bit 15 znamena "bez prodlevy" a spodnich 15 bitu se pak
    // ignoruje (`ENVVOL_TO_EMU_SAMPLES`) - ale ve stope to je.
    //
    // Zmereno na Georgii: 844 not presetu `shonkytonk` (druha vrstva
    // Honky-Tonk, `attackVolEnv 0`) a bicich. Odpovida to vetvi na
    // SBAWE32.DRV 0x0206, jen tam je v listingu 0xB7FF - merena hodnota je
    // 0xBFFF a plati i mimo kanal 9.
    // Pozn.: 0xBFFF do ENVVOL/ENVVAL se sem **nedosazuje**. Ovladac si
    // v bloku parametru nechava spocitany delay a konstantu posle az na
    // port (`SBAWE.VXD` 0x21AB: `push 0xBFFF`). Drzime to stejne, aby se
    // dal blok porovnavat 1:1 - viz Synth::NoteOn a tests/patch_cmp.py.

    vp.lfo1val = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayModLFO, 0.0)));
    vp.lfo2val = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayVibLFO, 0.0)));

    return vp;
}

} // namespace SoundFont
