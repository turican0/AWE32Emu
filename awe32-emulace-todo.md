# AWE32 / EMU8000 emulace – plugin do hry (C++), vstup .mid / .xmi

Cíl: C++ plugin, který přijímá MIDI (.mid) a XMI (.xmi) soubory, interpretuje je jako sekvenci
MIDI eventů a syntetizuje zvuk pomocí vlastní register-accurate emulace čipu EMU8000
(reverzním inženýrstvím doplněné o oficiální dokumentaci a chování DOS ovladačů).

**Poznámka k architektuře:** EMU8000 je fixed-function ASIC bez vlastního programovatelného
mikrokódu (na rozdíl od pozdějšího EMU10K1/FX8010 ze Sound Blaster Live!). Nejde tedy o
"reverzování programu z čipu", ale o rekonstrukci jeho chování na základě registrové
specifikace + ověření/doplnění proti chování skutečného hardwaru (viz sekce 4). Disassembly
DOS ovladačů (sekce 0.1) slouží jako **doplňkový zdroj** – ukazuje, jak AWEUTIL/CTVDSK.SYS
skutečně zapisují do registrů čipu (pořadí, časování, hodnoty), což doplňuje místa, kde je
oficiální dokumentace neúplná.

---

## 0. Příprava a podklady

- [x] **AWE32/EMU8000 Programmer's Guide** (Rev. 1.00, Dave Rossum, E-mu/Creative 1994-1996) –
      **primární zdroj pravdy** pro registrovou mapu a význam bitových polí.
      <https://www.dosdays.co.uk/media/creative/emu8kpgm.pdf>
      Zpracováno v `docs/re-notes/emu8000_register_map.md`.
- [x] **Linuxový ALSA driver** – `sound/isa/sb/emu8000.c` + `include/sound/emu8000_reg.h`.
      Nezávisle potvrzuje registrovou mapu i inicializační sekvenci
      (`EMU8000_CMD(reg, chan) ((reg)<<5 | (chan))`, HWCF1=0x0059, HWCF2=0x0020,
      DCYSUSV=0x80). Bráno jako reference chování, ne jako zdroj kódu.
      <https://github.com/torvalds/linux/blob/master/sound/isa/sb/emu8000.c>
- [ ] Sehnat/najít XMI specifikaci (Miles Sound System / AIL formát, rozdíly oproti SMF)
- [ ] Sehnat referenční nahrávky ze skutečné AWE32/AWE64 karty (pokud dostupný hardware) pro A/B srovnání
- [ ] Zmapovat dostupné open-source reference (DOSBox-X zdrojáky pro SB16/MPU-401 vrstvu, TiMidity++, FluidSynth) – čistě jako inspirace pro architekturu, ne pro kopírování kódu
- [ ] Rozhodnout cílovou platformu pluginu (formát hostitelské hry/enginu – VST-like API, nebo interní audio-engine hook?)

### 0.1 Zdroje DOS ovladačů

- [ ] **VOGONS Driver Library** (vogonsdrivers.com) – hlavní komunitní archiv ovladačů pro
      vintage hardware, obsahuje kompletní "Sound Blaster AWE32 Install CD" i samostatné
      "Driver Disk" balíčky (CTVDSK.SYS, AWEUTIL, MIXERSET, CTCM apod.)
- [ ] **VOGONS fórum** (vogons.org) – vlákna s odkazy na konkrétní verze (např. AWEUTIL 1.00
      vs. 1.20 – novější verze bývá spolehlivější, stojí za to sehnat obě pro porovnání)
- [ ] **Archive.org** – ISO obrazy instalačních CD (např. SB32/AWE32 PnP CD), hledat pod
      "Sound Blaster AWE32 driver" nebo konkrétním modelem karty (CT3900, CT3980, CT4520...)
- [ ] Stáhnout víc verzí téhož souboru (ovladače se mezi revizemi karty/verzemi mírně lišily) –
      pomůže to odlišit obecné chování čipu od quirků konkrétní verze ovladače
- [ ] Ověřit integritu/checksum stažených souborů oproti více zdrojům, pokud je to možné
- [ ] Pro širší kontext: John Miles Audio Interface Library (AIL) – herní driver vrstva
      nad AWE32, relevantní hlavně pokud cílové hry XMI přehrávaly přes AIL
- [ ] Ujasnit licenční model výstupního projektu (co smí obsahovat, co ne – žádné originální ROM/patch data z karty)

## 1. Parsování vstupních souborů

### 1.1 Standard MIDI File (.mid)
- [ ] Parser hlavičky (MThd) – formát 0/1/2, počet stop, division (ticks/quarter nebo SMPTE)
- [ ] Parser stop (MTrk) – variable-length quantity dekodér
- [ ] Dekódování MIDI eventů (Note On/Off, Control Change, Program Change, Pitch Bend, Aftertouch)
- [ ] Dekódování Meta eventů (Tempo, Time Signature, Track Name, End of Track)
- [ ] Dekódování SysEx eventů (min. GM/GS/XG reset zprávy, případně Creative-specific SysEx pro AWE)
- [ ] Sloučení více stop do jedné časové osy (event queue seřazená podle tick/delta-time)

### 1.2 XMI (.xmi)
- [ ] Parser IFF/FORM kontejneru (CAT/FORM chunk struktura)
- [ ] Parser XMI-specifických chunků (TIMB, EVNT, RBRN)
- [ ] Převod XMI delta-time (odlišné od SMF – jiná kódovací konvence) na interní tick reprezentaci
- [ ] Zpracování "Note On s embedded duration" (XMI specifikum – Note Off je odvozený, ne explicitní event)
- [ ] Podpora RBRN (branch points) pokud to hra využívá pro loopování hudby
- [ ] Volitelně: konverze XMI → interní event stream sdílený s .mid větví (jeden common sequencer model)

## 2. Sekvencer / timing engine

- [ ] Interní reprezentace event streamu nezávislá na vstupním formátu
- [ ] Tempo mapa (podpora tempo změn během přehrávání)
- [ ] Přesné plánování eventů vůči audio bufferu (sample-accurate timing, ne jen "per callback")
- [ ] Podpora loopování (pro herní hudbu smyčky nejsou výjimka, spíš pravidlo)
- [ ] API pro pause/resume/seek/stop z hostitelské hry

## 3. MIDI/MPU-401 interpretační vrstva (channel state machine)

- [ ] 16kanálový MIDI state (program, pitch bend range, volume, pan, expression, sustain, RPN/NRPN)
- [ ] GS/XG/GM kompatibilní zpracování Bank Select (MSB/LSB) – jak to dělal AWEUTIL/ovladač
- [ ] Voice allocation logika (jak skutečný EMU8000 přiděloval 32 hlasů mezi kanály při přetížení)
- [ ] Zpracování Creative-specific SysEx (pokud hra/hudba je používá)

## 4. Jádro emulace EMU8000 (register-level)

- [ ] Definice registrové mapy čipu (podle Tech Ref Manual) jako interní datové struktury
- [ ] Voice engine – 32 nezávislých hlasů, každý se svým stavem (pitch, sample pointer, loop points)
- [ ] Sample playback jádro – čtení vzorků z patch/soundfont dat, interpolace (EMU8000 používal lineární interpolaci – ověřit)
- [ ] Envelope generátory (volume envelope, modulation envelope) – přesné časové konstanty
- [ ] LFO1/LFO2 (tremolo, vibrato, filter modulation)
- [ ] Low-pass filtr s rezonancí (cutoff/Q podle registrů) – klíčové místo pro doladění sluchem/měřením
- [ ] Pan/volume mixing hlasů do stereo výstupu
- [ ] Chorus efekt (algoritmus + parametry – nejhůř zdokumentovaná část, počítat s empirickým laděním)
- [ ] Reverb efekt (totéž)
- [ ] Validace: srovnání výstupu s referenčními nahrávkami z bodu 0 (spektrální/poslechové testy)

## 5. Patch/instrument data (SoundFont vrstva)

- [ ] Loader pro SF2 (moderní náhrada za originální ROM/RAM banku karty)
- [ ] Mapování SF2 generátorů na EMU8000 registry (kde to sedí 1:1, kde ne)
- [ ] Podpora vlastní/uživatelské banky (pro emulaci nahrávání patchů do RAM přes AWEUTIL)
- [ ] Ošetření chybějících/neúplných patchů (fallback chování)

## 6. Audio output vrstva

- [ ] Mixer – sečtení 32 hlasů + efekty do finálního bufferu
- [ ] Resampling na cílový sample rate hostitelské hry/enginu
- [ ] Ošetření clippingu/limiteru (volitelné, pro "moderní" použití mimo DOS kontext)
- [ ] Multithreading/lock-free fronta mezi sekvencerem a audio callbackem (real-time bezpečnost)

## 7. Plugin API a integrace do hry (C++)

- [ ] Návrh veřejného C++ API (init, loadMidi/loadXmi, play/stop/pause, setVolume, callback pro audio buffer)
- [ ] Rozhodnutí o vláknovém modelu (audio thread vs. game thread, žádné alokace v audio callbacku)
- [ ] Build jako statická/dynamická knihovna (podle potřeb cílové hry)
- [ ] Minimalizace závislostí (aby šlo snadno zabudovat do cizího enginu)
- [ ] Konfigurace přes soubor/API (cesta k SF2 bance, výstupní sample rate, počet hlasů)
- [ ] Ukázková integrace – jednoduchý demo host (např. SDL2 audio) pro testování mimo cílovou hru

## 8. Testování a validace

- [ ] Unit testy parserů (.mid/.xmi) na sadě testovacích souborů
- [ ] Referenční sada MIDI/XMI z konkrétních DOS her pro poslechové testy
- [ ] A/B srovnání s reálným hardwarem (pokud dostupný) nebo s DOSBox-X + FluidSynth jako orientační baseline
- [ ] Zátěžové testy (32 hlasů najednou, rychlé note on/off sekvence, dlouhé smyčky)
- [ ] Testy výkonu (CPU zátěž v reálném čase na cílové platformě)

## 9. Dokumentace a právní záležitosti

- [ ] Zdokumentovat zdroje registrové mapy a metodiku ověřování (co je z manuálu, co empiricky doměřeno)
- [ ] Zajistit, že v repozitáři nejsou žádná originální binární data Creative (ROM patch banky, ovladače)
- [ ] README s architekturou projektu a návodem na integraci pluginu
- [ ] Licence projektu (vlastní kód, ne odvozenina z DOS ovladače)

## 10. Disassembly DOS ovladačů v IDA (doplňkový zdroj pro sekci 4)

### 10.1 Příprava souboru
- [ ] Zjistit skutečný formát binárky – `.SYS`/`.COM` nemusí mít standardní MZ hlavičku,
      `.EXE`/TSR ji obvykle má. Zkontrolovat prvních pár bajtů (`MZ` signatura) ručně v hex editoru,
      než ho IDA vůbec otevře
- [ ] U `.COM` souborů (žádná MZ hlavička, načítá se vždy na offset 0x100) nastavit v IDA
      manuálně typ segmentu a load offset – IDA to sama neuhodne správně u všech DOS formátů
- [ ] U TSR ovladačů (`.SYS`, device driver header) identifikovat driver header strukturu
      (strategy routine, interrupt routine, device attributes) – bez ní IDA nenajde entry point

### 10.2 Nastavení IDA pro 16-bit real-mode
- [ ] Zvolit procesor **Intel 80x86** a explicitně **16-bit** analýzu (ne 32-bit – real-mode
      DOS kód used 16bit segmenty i na 386/486)
- [ ] Nastavit segmentaci ručně, pokud ji IDA nerozpozná automaticky (Segments window →
      přidat/opravit segmenty podle skutečných `seg000`, `seg001`... a jejich base adres)
- [ ] Zapnout rozpoznávání DOS/BIOS interrupt volání (IDA má vestavěné signatury pro `INT 21h`
      funkce – zkontrolovat, že se korektně komentují v Disassembly view)
- [ ] Pokud IDA nabídne FLIRT signatury pro Borland C/Turbo Assembler runtime (časté u
      Creative ovladačů z poloviny 90. let) – aplikovat, ušetří to čas u knihovních funkcí

### 10.3 Mapování na I/O porty EMU8000
- [ ] Najít v kódu všechny výskyty `OUT`/`IN` instrukcí (IDA: Search → Text, nebo projít
      cross-references na porty)
- [ ] Porovnat cílové I/O adresy s rozsahem portů EMU8000 podle Tech Ref Manual (karta má
      základní port + několik offsetů pro Data0/Data1/Pointer registry)
- [ ] Pro každý zápis zaznamenat: která funkce ho dělá, v jakém pořadí vzhledem k okolním
      zápisům, a jakou hodnotu/vzorec pro ni kód počítá (ne jen konstantu – často je to
      výsledek nějakého přepočtu z MIDI parametru)
- [ ] Pojmenovat identifikované funkce podle zjištěného účelu (např. `set_voice_pitch`,
      `write_envelope_reg`) – usnadní to orientaci při dalším procházení

### 10.4 Dynamické ověření (doplněk ke statické analýze)
- [ ] Spustit stejný ovladač v **DOSBox-X debuggeru** (vestavěný, `Alt+Pause` nebo
      `debug` v konzoli) a nastavit breakpointy na identifikované funkce
- [ ] Sledovat skutečné hodnoty zapisované do portů za běhu (logování I/O) a porovnat se
      statickou analýzou – TSR kód často obsahuje větve, které statická analýza sama
      neodhalí (např. detekce revize karty, fallback cesty)
- [ ] Zaznamenat časování mezi voláními (zvlášť u inicializační sekvence po startu ovladače)

### 10.5 Výstup do projektu
- [ ] Přepsat nalezené sekvence zápisů do čitelného pseudo-C (komentovaný, ne 1:1 kopie
      assembleru) jako referenční poznámky – slouží k ověření/doplnění registrové mapy
      v sekci 4, ne jako zdrojový kód pluginu
- [ ] Označit v poznámkách, co je "ověřeno chováním ovladače" vs. "jen podle manuálu",
      pro budoucí debugging rozdílů oproti reálnému hardwaru

---

## Navrhované pořadí prací
1. Parsery .mid/.xmi (sekce 1) + interní event model (sekce 2)
2. Základní MPU-401/channel vrstva (sekce 3)
3. Minimální EMU8000 jádro bez efektů (sekce 4, jen voice+envelope+filtr)
4. SF2 loader (sekce 5) – aby šlo vůbec něco slyšet
5. Plugin API + demo host (sekce 7) – rychlá zpětná vazba poslechem
6. Doladění chorus/reverb a jemné detaily filtru (sekce 4 dokončení)
7. Testování a validace (sekce 8), dokumentace (sekce 9)
