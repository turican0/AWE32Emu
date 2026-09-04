#include "SoundFontExport.h"
#include "Emu8000Regs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace
{
    using namespace SoundFont;

    // ---- drobne pomucky pro RIFF ------------------------------------------
    struct Buf
    {
        std::vector<uint8_t> d;

        void u8(uint8_t v)  { d.push_back(v); }
        void u16(uint16_t v){ d.push_back(uint8_t(v)); d.push_back(uint8_t(v >> 8)); }
        void u32(uint32_t v){ for (int i = 0; i < 4; ++i) d.push_back(uint8_t(v >> (8 * i))); }
        void tag(const char* t) { d.insert(d.end(), t, t + 4); }
        void raw(const void* p, size_t n)
        {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            d.insert(d.end(), b, b + n);
        }
        // Jmena v SF2 maji pevnych 20 B a musi koncit nulou.
        void name20(const std::string& s)
        {
            char t[20] = {0};
            std::memcpy(t, s.c_str(), std::min<size_t>(s.size(), 19));
            raw(t, 20);
        }
        size_t size() const { return d.size(); }
    };

    void putChunk(Buf& out, const char* tag, const Buf& body)
    {
        out.tag(tag);
        out.u32(static_cast<uint32_t>(body.size()));
        out.raw(body.d.data(), body.d.size());
        if (body.size() & 1) out.u8(0);        // RIFF: liche chunky se zarovnavaji
    }

    // ---- prevody SF1 -> SF2 -----------------------------------------------
    // Vsechny vychazeji z toho, jak se SF1 cte v SoundFont.cpp; ty prevody
    // jsou zmerene proti skutecnemu ovladaci, takze tady jen obracime smer.

    // SF1 ma casy rovnou v milisekundach, SF2 chce timecents.
    int16_t MsToTimecents(double ms)
    {
        if (ms <= 0.0) return -12000;          // SF2: "hned"
        const double sec = ms / 1000.0;
        return static_cast<int16_t>(std::clamp(
            std::lround(1200.0 * std::log2(sec)), -12000L, 8000L));
    }

    // SF1 cutoff 0..127 -> registr 0..255 (nasobek dvema, viz SoundFont.cpp),
    // a registr -> absolutni centy toutez radou, kterou pouziva cteni SF2.
    int16_t Sf1CutoffToAbsCents(int v)
    {
        const int reg = std::clamp(v * 2, 0, 255);
        return static_cast<int16_t>(std::lround(
            Emu8000::kCutoffBaseCents + reg * Emu8000::kCutoffCentsStep));
    }

    // SF1 Q 0..127 -> registr 0..15 -> centibely rezonance pro SF2.
    int16_t Sf1QToCentibels(int v)
    {
        const int q = std::clamp(v >> 3, 0, Emu8000::kCccaQMax);
        return static_cast<int16_t>(std::lround(
            q * Emu8000::kResonanceMaxDb * 10.0 / Emu8000::kCccaQMax));
    }

    // SF1 utlum: 127 = bez utlumu, kazda jednotka je 0,375 dB.
    // SF2 chce centibely (0,1 dB).
    int16_t Sf1AttenToCentibels(int v)
    {
        const int units = std::clamp(127 - v, 0, 255);
        return static_cast<int16_t>(std::lround(units * Emu8000::kAttenDbPerStep * 10.0));
    }

    // SF1 sustain -> centibely poklesu.
    //
    // Pozor, **neni** to `0x7F - v`. Ovladac dela `registr = v * 4 / 3`
    // (orez na 0x7F) a je to zmerene na bance, kterou mame v obou formatech
    // - `SYNTHGM.SBK` (SF1) a `SYNTHGM.SF2` z DOSoveho SDK popisuji tytez
    // presety (viz SoundFont.cpp, lambda `sustainReg`):
    //
    //     SF1 99 -> registr 127 (bez poklesu),  SF1 93 -> 124,  SF1 87 -> 116
    //
    // Puvodne tu bylo `0x7F - v`, coz je jina rada: u sustainu 99 by z toho
    // vyslo 28 kroku poklesu misto nuly. Slysitelne to bylo na tichych
    // mistech - export intra Magic Carpet 2 hral o 2,3x hlasiteji nez
    // originalni banka, protoze noty misto poklesu drzely uroven.
    //
    // Vracime presnou inverzi cteciho vzorce, takze registr projde tam i zpet
    // beze zmeny.
    int16_t Sf1SustainToCentibels(int v)
    {
        const int reg   = std::clamp(v * 4 / 3, 0, 0x7F);
        const int steps = 0x7F - reg;
        return static_cast<int16_t>(std::lround(steps * Emu8000::kSustainDbPerStep * 10.0));
    }

    // SF1 zpozdeni je v jednotkach po 725 us.
    int16_t Sf1DelayToTimecents(int v)
    {
        return MsToTimecents(v * Emu8000::kDelaySecPerStep * 1000.0);
    }

    bool IsTimeGen(int op)
    {
        switch (op)
        {
        case Gen::DelayModLFO: case Gen::DelayVibLFO:
        case Gen::DelayModEnv: case Gen::AttackModEnv: case Gen::HoldModEnv:
        case Gen::DecayModEnv: case Gen::ReleaseModEnv:
        case Gen::DelayVolEnv: case Gen::AttackVolEnv: case Gen::HoldVolEnv:
        case Gen::DecayVolEnv: case Gen::ReleaseVolEnv:
            return true;
        default:
            return false;
        }
    }

    // Prevede jeden generator ze zony SF1 banky na hodnotu podle SF2.
    // Vraci false, kdyz se generator do SF2 neprepisuje vubec.
    bool ConvertGen(int op, int16_t v, Version ver, int16_t& out)
    {
        if (ver == Version::Sf2) { out = v; return true; }

        switch (op)
        {
        case Gen::InitialFilterFc: out = Sf1CutoffToAbsCents(v); return true;
        case Gen::InitialFilterQ:  out = Sf1QToCentibels(v);     return true;
        case Gen::InitialAttenuation: out = Sf1AttenToCentibels(v); return true;
        case Gen::SustainVolEnv:   out = Sf1SustainToCentibels(v); return true;
        case Gen::SustainModEnv:
            // Litera SF2 tu chce promile, jenze cteci strana (SoundFont.cpp)
            // pouziva pro obe obalky **tutez** lambdu `sustainReg`, tedy
            // centibely - a ta je zmerena proti ovladaci. Kdybychom sem dali
            // promile, nas vlastni engine by banku precetl jinak, nez ji
            // zapsal. Drzime se proto mereneho chovani a je to schvalne.
            out = Sf1SustainToCentibels(v);
            return true;
        case Gen::ScaleTuning:
            // SF1 testuje jen "== 1" a pak vysku **puli** (viz SoundFont.cpp).
            out = (v == 1) ? 50 : 100;
            return true;
        case Gen::Pan:
            // SF1 0..127 se stredem 64, SF2 -500..+500.
            out = static_cast<int16_t>(std::lround((v - 64) * 1000.0 / 127.0));
            return true;
        case Gen::ReverbEffectsSend:
        case Gen::ChorusEffectsSend:
            // SF1 0..255 -> SF2 promile.
            out = static_cast<int16_t>(std::clamp(
                std::lround(std::clamp<int>(v, 0, 255) * 1000.0 / 255.0), 0L, 1000L));
            return true;
        case Gen::Sf1RootPitchCents:
            return false;      // resi se pres overridingRootKey, viz nize
        default:
            if (IsTimeGen(op)) { out = MsToTimecents(v); return true; }
            out = v;
            return true;
        }
    }
}

namespace SoundFont
{

bool ExportSf2(const std::vector<const Bank*>& banks,
               const std::vector<int16_t>& rom,
               const std::string& path,
               const ExportOptions& opt,
               std::string& error)
{
    if (banks.empty()) { error = "zadna banka k exportu"; return false; }

    // ---- 1. posbirat presety -------------------------------------------
    // Pozdejsi banka prebiji drivejsi, stejne jako pri prehravani.
    struct PresetRef { const Bank* bank; const Preset* preset; };
    std::map<std::pair<int, int>, PresetRef> chosen;
    for (const Bank* b : banks)
        for (const Preset& p : b->presets)
            chosen[{p.bank, p.program}] = PresetRef{b, &p};

    if (chosen.empty()) { error = "banky neobsahuji zadny preset"; return false; }

    // ---- 2. posbirat nastroje a vzorky, ktere ty presety opravdu pouziji -
    struct SampleRef { const Bank* bank; const Sample* smp; };
    std::vector<SampleRef> outSamples;
    std::map<std::pair<const Bank*, int>, int> sampleIndex;   // (banka, id) -> novy index
    std::vector<std::pair<const Bank*, const Instrument*>> outInstr;
    std::map<std::pair<const Bank*, int>, int> instrIndex;

    for (auto& kv : chosen)
    {
        const Bank* b = kv.second.bank;
        for (const Zone& pz : kv.second.preset->zones)
        {
            if (pz.instrument < 0 || pz.instrument >= (int) b->instruments.size())
                continue;
            auto key = std::make_pair(b, pz.instrument);
            if (instrIndex.count(key)) continue;
            instrIndex[key] = static_cast<int>(outInstr.size());
            const Instrument* in = &b->instruments[pz.instrument];
            outInstr.push_back({b, in});
            for (const Zone& iz : in->zones)
            {
                if (iz.sampleId < 0 || iz.sampleId >= (int) b->samples.size())
                    continue;
                auto sk = std::make_pair(b, iz.sampleId);
                if (sampleIndex.count(sk)) continue;
                sampleIndex[sk] = static_cast<int>(outSamples.size());
                outSamples.push_back({b, &b->samples[iz.sampleId]});
            }
        }
    }

    // ---- 3. vzorkova data ------------------------------------------------
    // SF2 predepisuje mezi vzorky 46 nulovych bodu - proto ten posun, ktery
    // je videt i v adresach, ktere zapisuje ovladac.
    std::vector<int16_t> smpl;
    struct OutSmp { uint32_t start, end, loopStart, loopEnd; };
    std::vector<OutSmp> outPos(outSamples.size());

    for (size_t i = 0; i < outSamples.size(); ++i)
    {
        const Sample& s = *outSamples[i].smp;
        const Bank* b = outSamples[i].bank;
        const bool inRom = s.inRom || b->samplesInRom;

        const int16_t* src = nullptr;
        size_t avail = 0;
        if (inRom)
        {
            if (!opt.bakeRom)
            { error = "banka odkazuje do ROM, ale zapekani ROM je vypnute"; return false; }
            if (rom.empty())
            { error = "banka odkazuje do wave ROM, ale zadna ROM nebyla nactena (--rom)"; return false; }
            if (s.start >= rom.size()) continue;
            src = rom.data() + s.start;
            avail = std::min<size_t>(s.end, rom.size()) - s.start;
        }
        else
        {
            if (s.start >= b->sampleData.size()) continue;
            src = b->sampleData.data() + s.start;
            avail = std::min<size_t>(s.end, b->sampleData.size()) - s.start;
        }

        const uint32_t base = static_cast<uint32_t>(smpl.size());
        smpl.insert(smpl.end(), src, src + avail);
        outPos[i].start = base;
        outPos[i].end   = base + static_cast<uint32_t>(avail);
        // Smycka je v bance ulozena absolutne; prevedeme ji na novy zaklad.
        const uint32_t ls = (s.loopStart >= s.start) ? (s.loopStart - s.start) : 0;
        const uint32_t le = (s.loopEnd   >= s.start) ? (s.loopEnd   - s.start) : 0;
        outPos[i].loopStart = base + std::min<uint32_t>(ls, static_cast<uint32_t>(avail));
        outPos[i].loopEnd   = base + std::min<uint32_t>(le, static_cast<uint32_t>(avail));
        smpl.insert(smpl.end(), 46, 0);        // povinna vypln podle SF2
    }

    // ---- 4. chunky pdta ---------------------------------------------------
    Buf phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;

    auto writeZoneGens = [&](Buf& gens, const Bank* b, const Zone& z,
                             bool isPreset)
    {
        // Rozsahy jdou podle specifikace jako prvni.
        if (z.keyLo != 0 || z.keyHi != 127)
        {
            gens.u16(Gen::KeyRange);
            gens.u8(static_cast<uint8_t>(z.keyLo));
            gens.u8(static_cast<uint8_t>(z.keyHi));
        }
        if (z.velLo != 0 || z.velHi != 127)
        {
            gens.u16(Gen::VelRange);
            gens.u8(static_cast<uint8_t>(z.velLo));
            gens.u8(static_cast<uint8_t>(z.velHi));
        }
        for (int op = 0; op < Gen::Count; ++op)
        {
            if (!z.gen.Has(op)) continue;
            if (op == Gen::KeyRange || op == Gen::VelRange) continue;
            if (op == Gen::Instrument || op == Gen::SampleID) continue;
            int16_t v;
            if (!ConvertGen(op, z.gen.value[op], b->version, v)) continue;
            gens.u16(static_cast<uint16_t>(op));
            gens.u16(static_cast<uint16_t>(v));
        }
        // Ukazatel na nastroj/vzorek musi byt posledni generator zony.
        if (isPreset)
        {
            auto it = instrIndex.find({b, z.instrument});
            if (it != instrIndex.end())
            {
                gens.u16(Gen::Instrument);
                gens.u16(static_cast<uint16_t>(it->second));
            }
        }
        else
        {
            auto it = sampleIndex.find({b, z.sampleId});
            if (it != sampleIndex.end())
            {
                gens.u16(Gen::SampleID);
                gens.u16(static_cast<uint16_t>(it->second));
            }
        }
    };

    // presety
    for (auto& kv : chosen)
    {
        const Bank* b = kv.second.bank;
        const Preset* p = kv.second.preset;
        phdr.name20(p->name.empty() ? "preset" : p->name);
        phdr.u16(static_cast<uint16_t>(p->program));
        phdr.u16(static_cast<uint16_t>(p->bank));
        phdr.u16(static_cast<uint16_t>(pbag.size() / 4));
        phdr.u32(0); phdr.u32(0); phdr.u32(0);      // library/genre/morphology
        for (const Zone& z : p->zones)
        {
            pbag.u16(static_cast<uint16_t>(pgen.size() / 4));
            pbag.u16(static_cast<uint16_t>(pmod.size() / 10));
            writeZoneGens(pgen, b, z, true);
        }
    }
    phdr.name20("EOP");
    phdr.u16(0); phdr.u16(0);
    phdr.u16(static_cast<uint16_t>(pbag.size() / 4));
    phdr.u32(0); phdr.u32(0); phdr.u32(0);
    pbag.u16(static_cast<uint16_t>(pgen.size() / 4));
    pbag.u16(static_cast<uint16_t>(pmod.size() / 10));
    pgen.u16(0); pgen.u16(0);                        // terminator
    pmod.u16(0); pmod.u16(0); pmod.u16(0); pmod.u16(0); pmod.u16(0);

    // nastroje
    for (auto& pr : outInstr)
    {
        const Bank* b = pr.first;
        const Instrument* in = pr.second;
        inst.name20(in->name.empty() ? "instr" : in->name);
        inst.u16(static_cast<uint16_t>(ibag.size() / 4));
        for (const Zone& z : in->zones)
        {
            ibag.u16(static_cast<uint16_t>(igen.size() / 4));
            ibag.u16(static_cast<uint16_t>(imod.size() / 10));
            writeZoneGens(igen, b, z, false);
        }
    }
    inst.name20("EOI");
    inst.u16(static_cast<uint16_t>(ibag.size() / 4));
    ibag.u16(static_cast<uint16_t>(igen.size() / 4));
    ibag.u16(static_cast<uint16_t>(imod.size() / 10));
    igen.u16(0); igen.u16(0);
    imod.u16(0); imod.u16(0); imod.u16(0); imod.u16(0); imod.u16(0);

    // vzorky
    //
    // Posun +1/+2/+3 neni kosmetika. SF1 uklada rovnou adresy pro cip
    // (uz s korekci na interpolator), kdezto SF2 uklada indexy - a Creative
    // ve svych **vlastnich** bankach tyz vzorek popisuje o 1/2/3 slova jinak.
    // Cteci strana to zohlednuje (SoundFont.cpp: `- 1`, `- 2`, `- 3` ve vetvi
    // pro SF2), takze kdybychom tady zapsali holé SF1 adresy, vysla by po
    // znovunacteni smycka o **dve** slova kratsi.
    //
    // Zmereno: 671 not z intra Magic Carpet 2 pres `--dump-notes`. Bez
    // kompenzace se lisily `ccca` a `csl` u 668 z nich (napr. 04B63F-0498ED
    // = 0x1D52 z SBK proti 0x1D50 z exportu); s ni sedi vsech dvacet
    // sloupcu na vsech 671 notach.
    for (size_t i = 0; i < outSamples.size(); ++i)
    {
        const Sample& s = *outSamples[i].smp;
        const bool sf1 = outSamples[i].bank->version == Version::Sf1;

        // Hvezdicka na zacatku jmena je Creativi znacka "tenhle vzorek lezi
        // ve wave ROM karty" - cte ji i nase nacitani (SoundFont.cpp: `type
        // & 0x8000 || name[0] == '*'`). Kdyz vzorek zapecem do souboru, uz v
        // ROM neni a znacka musi pryc, jinak si ho prehravac zase pujde hledat
        // do ROM na adresu, kde jsou uplne jina data.
        //
        // Stalo se to: intro Magic Carpet 2 hralo z exportu na kanalu 1
        // (zvonkohra z ROM) 2,2x hlasiteji, protoze cetlo ROM na adrese
        // 0x14D1F misto zapecene kopie. Vsech dvacet registru pritom sedelo -
        // chyba byla jen v tom, **odkud** se ctou vzorky.
        std::string nm = s.name.empty() ? std::string("sample") : s.name;
        if (!nm.empty() && nm[0] == '*') nm.erase(0, 1);
        shdr.name20(nm);
        shdr.u32(outPos[i].start     + (sf1 ? 1u : 0u));
        shdr.u32(outPos[i].end);
        shdr.u32(outPos[i].loopStart + (sf1 ? 2u : 0u));
        shdr.u32(outPos[i].loopEnd   + (sf1 ? 3u : 0u));
        shdr.u32(s.sampleRate ? s.sampleRate : 44100);
        shdr.u8(s.originalKey);
        shdr.u8(static_cast<uint8_t>(s.correction));
        shdr.u16(0);                       // sampleLink
        shdr.u16(1);                       // monoSample - ROM uz je zapecena
    }
    shdr.name20("EOS");
    shdr.u32(0); shdr.u32(0); shdr.u32(0); shdr.u32(0); shdr.u32(0);
    shdr.u8(0); shdr.u8(0); shdr.u16(0); shdr.u16(0);

    // ---- 5. slozit soubor -------------------------------------------------
    Buf info;
    info.tag("ifil"); info.u32(4); info.u16(2); info.u16(1);   // SF 2.01
    Buf isng; isng.raw("EMU8000", 8);
    putChunk(info, "isng", isng);
    Buf inam;
    {
        std::string n = opt.name.empty() ? std::string("AWE32Emu export") : opt.name;
        if (n.size() & 1) n.push_back('\0');
        n.push_back('\0');
        inam.raw(n.data(), n.size());
    }
    putChunk(info, "INAM", inam);

    Buf sdta;
    {
        Buf s;
        s.raw(smpl.data(), smpl.size() * 2);
        putChunk(sdta, "smpl", s);
    }

    Buf pdta;
    putChunk(pdta, "phdr", phdr);
    putChunk(pdta, "pbag", pbag);
    putChunk(pdta, "pmod", pmod);
    putChunk(pdta, "pgen", pgen);
    putChunk(pdta, "inst", inst);
    putChunk(pdta, "ibag", ibag);
    putChunk(pdta, "imod", imod);
    putChunk(pdta, "igen", igen);
    putChunk(pdta, "shdr", shdr);

    Buf body;
    body.tag("sfbk");
    body.tag("LIST"); body.u32(static_cast<uint32_t>(info.size() + 4)); body.tag("INFO");
    body.raw(info.d.data(), info.d.size());
    body.tag("LIST"); body.u32(static_cast<uint32_t>(sdta.size() + 4)); body.tag("sdta");
    body.raw(sdta.d.data(), sdta.d.size());
    body.tag("LIST"); body.u32(static_cast<uint32_t>(pdta.size() + 4)); body.tag("pdta");
    body.raw(pdta.d.data(), pdta.d.size());

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { error = "nelze zapsat '" + path + "'"; return false; }
    const uint32_t len = static_cast<uint32_t>(body.size());
    std::fwrite("RIFF", 1, 4, f);
    uint8_t l[4] = { uint8_t(len), uint8_t(len >> 8), uint8_t(len >> 16), uint8_t(len >> 24) };
    std::fwrite(l, 1, 4, f);
    std::fwrite(body.d.data(), 1, body.d.size(), f);
    std::fclose(f);

    std::printf("SF2: %zu presetu, %zu nastroju, %zu vzorku, %zu tisic vzorku dat\n",
                chosen.size(), outInstr.size(), outSamples.size(), smpl.size() / 1000);
    return true;
}

}
