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

    // ---- small RIFF helpers ----------------------------------------------
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
        // Names in SF2 have a fixed 20 B and must end with a zero.
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
        if (body.size() & 1) out.u8(0);        // RIFF: odd chunks are padded
    }

    // ---- SF1 -> SF2 conversions -------------------------------------------
    // All of them start from how SF1 is read in SoundFont.cpp; those
    // conversions are measured against the real driver, so here we only
    // reverse the direction.

    // SF1 has times directly in milliseconds, SF2 wants timecents.
    int16_t MsToTimecents(double ms)
    {
        if (ms <= 0.0) return -12000;          // SF2: "hned"
        const double sec = ms / 1000.0;
        return static_cast<int16_t>(std::clamp(
            std::lround(1200.0 * std::log2(sec)), -12000L, 8000L));
    }

    // SF1 cutoff 0..127 -> register 0..255 (times two, see SoundFont.cpp),
    // and register -> absolute cents with the same series the SF2 reader uses.
    int16_t Sf1CutoffToAbsCents(int v)
    {
        const int reg = std::clamp(v * 2, 0, 255);
        return static_cast<int16_t>(std::lround(
            Emu8000::kCutoffBaseCents + reg * Emu8000::kCutoffCentsStep));
    }

    // SF1 Q 0..127 -> register 0..15 -> resonance centibels for SF2.
    int16_t Sf1QToCentibels(int v)
    {
        const int q = std::clamp(v >> 3, 0, Emu8000::kCccaQMax);
        return static_cast<int16_t>(std::lround(
            q * Emu8000::kResonanceMaxDb * 10.0 / Emu8000::kCccaQMax));
    }

    // SF1 attenuation: 127 = none, each unit is 0.375 dB.
    // SF2 wants centibels (0.1 dB).
    int16_t Sf1AttenToCentibels(int v)
    {
        const int units = std::clamp(127 - v, 0, 255);
        return static_cast<int16_t>(std::lround(units * Emu8000::kAttenDbPerStep * 10.0));
    }

    // SF1 sustain -> centibels of drop.
    //
    // Note, it is **not** `0x7F - v`. The driver does `register = v * 4 / 3`
    // (clipped to 0x7F), and that is measured on a bank we have in both
    // formats - `SYNTHGM.SBK` (SF1) and `SYNTHGM.SF2` of the DOS SDK describe
    // the same presets (see SoundFont.cpp, lambda `sustainReg`):
    //
    //     SF1 99 -> register 127 (no drop),  SF1 93 -> 124,  SF1 87 -> 116
    //
    // Originally there was `0x7F - v`, which is a different series: for
    // sustain 99 it would give 28 steps of drop instead of zero. It was
    // audible in quiet places - the export of the Magic Carpet 2 intro played
    // 2.3x louder than the original bank, because notes held their level
    // instead of dropping.
    //
    // We return the exact inverse of the reading formula, so the register
    // makes the round trip unchanged.
    int16_t Sf1SustainToCentibels(int v)
    {
        const int reg   = std::clamp(v * 4 / 3, 0, 0x7F);
        const int steps = 0x7F - reg;
        return static_cast<int16_t>(std::lround(steps * Emu8000::kSustainDbPerStep * 10.0));
    }

    // SF1 delay is in units of 725 us.
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

    // Converts one generator of an SF1 bank zone to its SF2 value.
    // Returns false when the generator is not carried over to SF2 at all.
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
            // The letter of SF2 wants per mille here, but the reading side
            // (SoundFont.cpp) uses the **same** lambda `sustainReg` for both
            // envelopes, i.e. centibels - and that one is measured against the
            // driver. Were we to put per mille here, our own engine would read
            // the bank differently from how it wrote it. So we follow the
            // measured behaviour, on purpose.
            out = Sf1SustainToCentibels(v);
            return true;
        case Gen::ScaleTuning:
            // SF1 tests only "== 1" and then **halves** the pitch (see SoundFont.cpp).
            out = (v == 1) ? 50 : 100;
            return true;
        case Gen::Pan:
            // SF1 0..127 with centre 64, SF2 -500..+500.
            out = static_cast<int16_t>(std::lround((v - 64) * 1000.0 / 127.0));
            return true;
        case Gen::ReverbEffectsSend:
        case Gen::ChorusEffectsSend:
            // SF1 0..255 -> SF2 per mille.
            out = static_cast<int16_t>(std::clamp(
                std::lround(std::clamp<int>(v, 0, 255) * 1000.0 / 255.0), 0L, 1000L));
            return true;
        case Gen::Sf1RootPitchCents:
            return false;      // handled through overridingRootKey, see below
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
    if (banks.empty()) { error = "no bank to export"; return false; }

    // ---- 1. collect the presets ----------------------------------------
    // A later bank overrides an earlier one, as in playback.
    struct PresetRef { const Bank* bank; const Preset* preset; };
    std::map<std::pair<int, int>, PresetRef> chosen;
    for (const Bank* b : banks)
        for (const Preset& p : b->presets)
            chosen[{p.bank, p.program}] = PresetRef{b, &p};

    if (chosen.empty()) { error = "the banks contain no preset"; return false; }

    // ---- 2. collect the instruments and samples those presets really use -
    struct SampleRef { const Bank* bank; const Sample* smp; };
    std::vector<SampleRef> outSamples;
    std::map<std::pair<const Bank*, int>, int> sampleIndex;   // (bank, id) -> new index
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

    // ---- 3. sample data --------------------------------------------------
    // SF2 prescribes 46 zero points between samples - hence the offset that
    // is visible in the addresses the driver writes too.
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
            { error = "the bank refers to ROM, but baking the ROM in is disabled"; return false; }
            if (rom.empty())
            { error = "the bank refers to the wave ROM, but no ROM was loaded (--rom)"; return false; }
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
        // The loop is stored absolute in the bank; move it to the new base.
        const uint32_t ls = (s.loopStart >= s.start) ? (s.loopStart - s.start) : 0;
        const uint32_t le = (s.loopEnd   >= s.start) ? (s.loopEnd   - s.start) : 0;
        outPos[i].loopStart = base + std::min<uint32_t>(ls, static_cast<uint32_t>(avail));
        outPos[i].loopEnd   = base + std::min<uint32_t>(le, static_cast<uint32_t>(avail));
        smpl.insert(smpl.end(), 46, 0);        // mandatory padding per SF2
    }

    // ---- 4. pdta chunks ---------------------------------------------------
    Buf phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr;

    auto writeZoneGens = [&](Buf& gens, const Bank* b, const Zone& z,
                             bool isPreset)
    {
        // Ranges come first, per the specification.
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
        // The instrument/sample pointer must be the last generator of a zone.
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

    // presets
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

    // instruments
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

    // samples
    //
    // The +1/+2/+3 offset is not cosmetic. SF1 stores chip addresses
    // directly (already with the interpolator correction), while SF2 stores
    // indices - and Creative describes the same sample 1/2/3 words
    // differently in its **own** banks. The reading side accounts for it
    // (SoundFont.cpp: `- 1`, `- 2`, `- 3` in the SF2 branch), so if we wrote
    // bare SF1 addresses here, the loop would come out **two** words shorter
    // after reloading.
    //
    // Measured: 671 notes of the Magic Carpet 2 intro through
    // `--dump-notes`. Without the compensation `ccca` and `csl` differed on
    // 668 of them (e.g. 04B63F-0498ED = 0x1D52 from the SBK against 0x1D50
    // from the export); with it all twenty columns match on all 671 notes.
    for (size_t i = 0; i < outSamples.size(); ++i)
    {
        const Sample& s = *outSamples[i].smp;
        const bool sf1 = outSamples[i].bank->version == Version::Sf1;

        // An asterisk at the start of the name is Creative's mark for "this
        // sample lies in the card's wave ROM" - our loader reads it too
        // (SoundFont.cpp: `type & 0x8000 || name[0] == '*'`). Once the sample
        // is baked into the file it is no longer in ROM and the mark must go,
        // otherwise the player would look for it in ROM again, at an address
        // with completely different data.
        //
        // It happened: the Magic Carpet 2 intro played channel 1 (a glocken
        // from ROM) 2.2x louder from the export, because it read ROM at
        // address 0x14D1F instead of the baked copy. All twenty registers
        // matched meanwhile - the error was only in **where** the samples
        // are read from.
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
        shdr.u16(1);                       // monoSample - the ROM is baked in already
    }
    shdr.name20("EOS");
    shdr.u32(0); shdr.u32(0); shdr.u32(0); shdr.u32(0); shdr.u32(0);
    shdr.u8(0); shdr.u8(0); shdr.u16(0); shdr.u16(0);

    // ---- 5. assemble the file ---------------------------------------------
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
    if (!f) { error = "cannot write '" + path + "'"; return false; }
    const uint32_t len = static_cast<uint32_t>(body.size());
    std::fwrite("RIFF", 1, 4, f);
    uint8_t l[4] = { uint8_t(len), uint8_t(len >> 8), uint8_t(len >> 16), uint8_t(len >> 24) };
    std::fwrite(l, 1, 4, f);
    std::fwrite(body.d.data(), 1, body.d.size(), f);
    std::fclose(f);

    std::printf("SF2: %zu presets, %zu instruments, %zu samples, %zu thousand sample points\n",
                chosen.size(), outInstr.size(), outSamples.size(), smpl.size() / 1000);
    return true;
}

}
