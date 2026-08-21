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
    int AttackRateFromMs(double ms)
    {
        // Nejrychlejsi attack, ktery ovladac zapisuje, je 0x7D, ne 0x7F.
        // Zmereno na ATKHLDV i ATKHLD u vsech 242 not. Souvisi to nejspis
        // s podminkou `attack < 0x7D` u velocity->cutoff (SBAWE.VXD 0x1CF6).
        if (ms <= 0.0) return 0x7D;
        for (int r = 1; r <= 0x7F; ++r)
            if (ms > 11878.0 / RateDivisor(r - 1)) return r;
        return 0x7F;
    }
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
    int DelayFromMs(double ms)
    {
        const int steps = static_cast<int>(std::lround(ms / (Emu8000::kDelaySecPerStep * 1000.0)));
        return std::clamp(static_cast<int>(Emu8000::kDelayNone) - steps, 0, 0x8000);
    }

    double TimecentsToMs(int tc) { return std::pow(2.0, tc / 1200.0) * 1000.0; }

    // Absolutni centy (SF2) -> jednotky IFATN (ctvrt pultonu od 125 Hz).
    int FilterFcFromAbsCents(int cents)
    {
        const double base = 1200.0 * std::log2(Emu8000::kCutoffBaseHz / 8.176);
        return std::clamp(static_cast<int>(std::lround((cents - base) / 25.0)), 0, 255);
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
        start     = s.start + off;
        loopStart = s.loopStart + off;
        loopEnd   = s.loopEnd + off;
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
    if (sf1) q = g.Get(Gen::InitialFilterQ, 0) * kCccaQMax / 127;
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
    const double increment = (s.sampleRate / static_cast<double>(44100))
                           * std::pow(2.0, semis / 12.0);
    vp.ip = static_cast<uint16_t>(std::clamp(
        kPitchUnity + std::log2(increment) * kPitchPerOctave, 0.0, 65535.0));

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
        vp.patchAttenUnits = static_cast<uint8_t>(
            std::clamp(127 - g.Get(Gen::InitialAttenuation, dflt), 0, 255));
    }
    else
    {
        // SF2: centibely (0.1 dB) -> jednotky po 0.375 dB.
        vp.patchAttenUnits = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(g.Get(Gen::InitialAttenuation, 0)
                                         / 10.0 / kAttenDbPerStep)), 0, 255));
    }

    // SF1 uklada initialFilterFc jako 0..127, kdezto registr IFATN ma cutoff
    // 8bitovy. Prevod je **roztazeni rozsahu**, ne prosty dvojnasobek:
    //
    //     cutoff = v * 255 / 127
    //
    // Pro v < 127 to dava presne `2*v` (proto to na 242 notach MINUETu
    // sedelo), ale pro v = 127 to dava **255**, ne 254. Zmereno na dvou
    // notach presetu 52 'Choir Aahs' ze `SYNTHGM.SBK` v Magic Carpet 2:
    // ovladac zapsal `IFATN = FF1D`, my `FE1D`. Potvrzuje to i tabulka
    // vychozich hodnot generatoru v obou ovladacich (MDI 0x16AD,
    // VXD 0x6D60), kde ma generator 8 hodnotu 255 - tedy uz prevedenou.
    // Viz docs/re-notes/86box_srovnani.md sekce 9.5 a 17.
    int cutoff;
    if (!g.Has(Gen::InitialFilterFc)) cutoff = 255;
    else if (sf1)                     cutoff = std::clamp<int>(
                                          g.value[Gen::InitialFilterFc] * 255 / 127, 0, 255);
    else                              cutoff = FilterFcFromAbsCents(g.value[Gen::InitialFilterFc]);

    // Spodni bajt (utlum) doplni Synth podle krivek z ovladace.
    vp.ifatn = static_cast<uint16_t>(cutoff << 8);

    // ---- modulace --------------------------------------------------------
    auto modAmount = [&](int op, double sf2Scale) -> int8_t
    {
        if (!g.Has(op)) return 0;
        return sf1 ? ClampS8(g.value[op])
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
        | static_cast<uint8_t>(modAmount(Gen::ModEnvToFilterFc, kPefeFcCentsPerStep)));

    // Kdyz generator chybi, ovladac pouzije frekvenci LFO1 = 128. Zmereno
    // v poli hlasu na +0x2C u vsech 242 not; SYNTHGM.SBK u piana freqModLFO
    // nema, presto ovladac zapisuje TREMFRQ = 0x0080.
    const int lfo1Freq = sf1 ? g.Get(Gen::FreqModLFO, 128)
                             : std::clamp<int>(static_cast<int>(std::lround(
                                   8.176 * std::pow(2.0, g.Get(Gen::FreqModLFO, 0) / 1200.0)
                                   / kLfoHzPerStep)), 0, 255);
    const int lfo2Freq = sf1 ? g.Get(Gen::FreqVibLFO, 0)
                             : std::clamp<int>(static_cast<int>(std::lround(
                                   8.176 * std::pow(2.0, g.Get(Gen::FreqVibLFO, 0) / 1200.0)
                                   / kLfoHzPerStep)), 0, 255);

    vp.fmmod = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::ModLfoToPitch, kPitchCentsPerStep)) << 8)
        | static_cast<uint8_t>(modAmount(Gen::ModLfoToFilterFc, kFmmodFcCentsPerStep)));
    vp.tremfrq = static_cast<uint16_t>(
        (static_cast<uint8_t>(modAmount(Gen::ModLfoToVolume, kTremCbPerStep)) << 8) | lfo1Freq);
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
        if (sf1) return std::clamp<int>(g.value[op], 0, 0x7F);
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
        | AttackRateFromMs(timeMs(Gen::AttackVolEnv, 0.0)));
    vp.dcysusv = static_cast<uint16_t>(
        (sustainReg(Gen::SustainVolEnv) << 8)
        | DecayRateFromMs(keyScaled(Gen::DecayVolEnv, Gen::KeynumToVolEnvDecay, true)));
    vp.releaseRate = static_cast<uint8_t>(DecayRateFromMs(timeMs(Gen::ReleaseVolEnv, 0.0)));

    vp.envval = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayModEnv, 0.0)));
    vp.atkhld = static_cast<uint16_t>(
        (HoldFromMs(keyScaled(Gen::HoldModEnv, Gen::KeynumToModEnvHold, false)) << 8)
        | AttackRateFromMs(timeMs(Gen::AttackModEnv, 0.0)));
    vp.dcysus = static_cast<uint16_t>(
        (sustainReg(Gen::SustainModEnv) << 8)
        | DecayRateFromMs(keyScaled(Gen::DecayModEnv, Gen::KeynumToModEnvDecay, true)));

    vp.lfo1val = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayModLFO, 0.0)));
    vp.lfo2val = static_cast<uint16_t>(DelayFromMs(timeMs(Gen::DelayVibLFO, 0.0)));

    return vp;
}

} // namespace SoundFont
