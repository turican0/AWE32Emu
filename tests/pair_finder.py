#!/usr/bin/env python
"""Obtains and **verifies** recording + MIDI pairs.

A downloaded MIDI is useless until it is proven that it really belongs to the
recording. For game music doubly so - the same piece circulates in dozens of
arrangements and the person recording used one specific one. So this script
does not end with downloading but with a marker comparison (see
`markers.py`): note onset times are extracted from the recording and the MIDI
and it looks for how many fit one offset.

Three steps, each can run separately:

    # 1) download what is in the URL list (one address per line, # is a comment)
    python tests/pair_finder.py --fetch urls.txt --stage samples3/candidates

    # 2) unpack archives and collect MIDI files
    python tests/pair_finder.py --unpack samples3/candidates

    # 3) verify everything against everything and print what matches
    python tests/pair_finder.py \\
        --audio samples3 \\
        --midi samples3/candidates SAMPLES/MIDI \\
        --out samples3/pairs.txt

The steps can also run together: `--fetch urls.txt --unpack ... --audio ... --midi ...`.

Fill the URL list yourself - deliberately there are none hard-coded here. The
script downloads only what it is given and prints what it downloaded.

The score thresholds are calibrated in `markers.py`: a right pair gives
around 0.6, noise reaches 0.25. Above 0.40 it is reported as a match.
"""
import argparse
import glob
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import markers  # noqa: E402
import match_tracks  # noqa: E402

MIDI_EXT = (".mid", ".midi", ".xmi", ".rmi")
ARCHIVE_EXT = (".zip",)
AUDIO_EXT = (".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aiff", ".aif")
CHROMA_RANK_WEIGHT = 5.0


# ---------------------------------------------------------------------------
# 1) download
# ---------------------------------------------------------------------------

def fetch(url_file, stage):
    import urllib.error
    import urllib.request

    os.makedirs(stage, exist_ok=True)
    urls = []
    for line in open(url_file, encoding="utf-8"):
        line = line.strip()
        if line and not line.startswith("#"):
            urls.append(line)
    if not urls:
        print(f"{url_file}: no URL")
        return []

    got = []
    for u in urls:
        name = os.path.basename(u.split("?")[0]) or "soubor"
        dst = os.path.join(stage, name)
        if os.path.exists(dst):
            print(f"  already downloaded: {name}")
            got.append(dst)
            continue
        try:
            req = urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=60) as r, open(dst, "wb") as f:
                shutil.copyfileobj(r, f)
            print(f"  downloaded {os.path.getsize(dst)//1024:6} kB  {name}")
            got.append(dst)
        except (urllib.error.URLError, OSError) as ex:
            print(f"  CHYBA {name}: {ex}")
    return got


# ---------------------------------------------------------------------------
# 2) unpacking
# ---------------------------------------------------------------------------

def unpack(stage):
    n = 0
    for p in sorted(glob.glob(os.path.join(stage, "**", "*"), recursive=True)):
        if not p.lower().endswith(ARCHIVE_EXT):
            continue
        out = os.path.splitext(p)[0]
        if os.path.isdir(out):
            continue
        try:
            with zipfile.ZipFile(p) as z:
                members = [m for m in z.namelist()
                           if m.lower().endswith(MIDI_EXT)]
                if not members:
                    continue
                z.extractall(out, members)
                print(f"  unpacked {len(members):4} MIDI from {os.path.basename(p)}")
                n += len(members)
        except zipfile.BadZipFile:
            print(f"  not a zip: {os.path.basename(p)}")
    print(f"unpacked {n} MIDI in total")
    return n


# ---------------------------------------------------------------------------
# 3) verification
# ---------------------------------------------------------------------------

def collect(paths, exts):
    out = []
    for p in paths:
        if os.path.isdir(p):
            for f in glob.glob(os.path.join(p, "**", "*"), recursive=True):
                if f.lower().endswith(exts) and os.path.isfile(f):
                    out.append(f)
        elif os.path.isfile(p) and p.lower().endswith(exts):
            out.append(p)
    return sorted(set(out))


def verify(audio_paths, midi_paths, out_file, top, min_score, min_margin):
    print(f"recordings: {len(audio_paths)}, MIDI candidates: {len(midi_paths)}\n")

    # Two independent methods, because neither alone is enough:
    #   markers      - cope with a fragment, another start and repetitions, but
    #                  in dense orchestral music the match gets lost in noise
    #                  (GMNUTRAC gave a margin of only 1.1x although it is right)
    #   correlation  - sharp when the recording matches the whole piece, but
    #                  drops to zero as soon as it is just a part
    # A pair is confirmed when at least one of them proves it.
    ref = {}
    for c in midi_paths:
        try:
            m, song = markers.midi_markers(c)
            if len(m) < 12:
                continue
            env, _ = match_tracks.midi_onsets(c)
            ref[c] = (m, markers.hashes(m), len(m), song, env,
                      markers.midi_chroma(song))
        except Exception as ex:
            print(f"  skipping {os.path.basename(c)}: {ex}")
    if not ref:
        print("no usable MIDI")
        return

    lines = ["# Verified recording -> MIDI pairs (tests/pair_finder.py).",
             "# pair columns: audio, MIDI, markers, correlation, offset, scale.",
             "# 'section' row: audio_from, audio_to, midi_from, midi_to, markers, scale, offset.",
             "# time conversion: midi = scale * audio + offset.",
             "# score: the share of the recording's markers that fit one offset.",
             "# Calibration: a right pair ~0.6; noise reaches 0.25.",
             ""]
    found = 0
    for a in audio_paths:
        try:
            am = markers.audio_markers(a)
        except Exception as ex:
            print(f"{os.path.basename(a)}: cannot read ({ex})")
            continue
        if len(am) < 12:
            print(f"{os.path.basename(a)}: too few markers")
            continue
        ah = markers.hashes(am)
        try:
            achroma = markers.audio_chroma(a)
        except Exception:
            achroma = None

        try:
            import soundfile as sf
            x, sr = sf.read(a, always_2d=True, dtype="float64")
            aenv = match_tracks.onsets_from_samples(x.mean(axis=1), sr)
            adur = len(x) / sr
        except Exception:
            aenv, adur = None, 0.0

        res = []
        skipped = 0
        for c, (m, h, nm, song, menv, mchroma) in ref.items():
            # `best_alignment` refines the scale by +-1 % on top of the rough hashing
            # (`v`) - it catches a match spread by a small tempo drift (another
            # hardware clock when recording, another sample rate when
            # digitising), which the exact fingerprint triples alone miss.
            # `quick_reject` inside first cheaply checks whether the expensive
            # refinement makes sense at all - when not (with high confidence),
            # the marker score is 0 right away and it moves on; the
            # correlation (`co` below) is computed independently, because
            # markers sometimes fail in dense orchestral music.
            (hit, off, scale), v, rejected = markers.best_alignment(am, ah, m, h)
            if rejected:
                skipped += 1
                mk, off = 0.0, 0.0
            elif not v:
                continue
            else:
                mk = hit / len(am)
            chroma = (markers.chroma_score(achroma, mchroma, off, scale)
                      if achroma is not None and mk > 0 else 0.0)
            # correlation only where the lengths match - otherwise a short piece
            # would "win" by hitting a few onsets by chance
            co = 0.0
            if aenv is not None and adur > 0 and song.length > 0:
                r = song.length / adur
                if 0.75 <= r <= 1.25:
                    co = match_tracks.score(menv, aenv)[0]
            # Rhythm alone does not tell apart pieces with the same drum pattern.
            # A small tonal component therefore only orders the marker
            # candidates; the confirmation threshold stays on the marker /
            # correlation evidence.
            res.append((max(mk, co) + CHROMA_RANK_WEIGHT * chroma,
                        mk, co, chroma, v, c, song, off, scale))
        res.sort(reverse=True, key=lambda r: r[0])

        # The score alone is not enough. A right pair once gave 0.60, another
        # time only 0.35 (GMNUTRAC), while noise reached 0.25 - an absolute
        # threshold would drop good pairs. So **the margin over the second
        # best** decides too: a real match stands out over the others, a
        # random one gets lost among them.
        print(f"{os.path.basename(a)}  ({len(am)} markers)"
              + (f"  [{skipped} rejected early]" if skipped else ""))
        if not res:
            print("   no candidate")
            lines.append(f"# {os.path.basename(a)}: no candidate")
            continue

        runner = res[1][0] if len(res) > 1 else 1e-9
        margin = res[0][0] / max(runner, 1e-9)
        for i, (sc, mk, co, chroma, v, c, song, off, scale) in enumerate(res[:top]):
            # further strong peaks = the piece is in the recording several times
            # (rough, straight from hashing - `off` above is already refined)
            rep = [f"{o:+.1f} s" for cnt, o in v[1:4] if cnt > 0.6 * v[0][0]]
            note = f"  repeated at {', '.join(rep)}" if rep else ""
            ok = (i == 0 and (mk >= min_score and
                               (margin >= min_margin or chroma >= 0.05)
                              or co >= 0.40))
            tag = "  <== match" if ok else ""
            extra = f" margin {margin:.1f}x" if i == 0 else "            "
            print(f"   markers {mk:5.2f}{extra}  chroma {chroma:5.2f}  correlation {co:5.2f}  "
                  f"offset {off:+7.2f} s  {os.path.basename(c)}{note}{tag}")
            if ok:
                # Time in MIDI = scale * time in the recording + off. The single
                # statistically verified sections are printed, not just one
                # offset: a follow-up tool can then align notes without
                # guessing only in the passages where there really is a match.
                segs = markers.segments(am, m, tol=markers.REFINE_TOL)
                lines.append(f"{os.path.basename(a)}\t{c}\t{mk:.3f}\t{co:.3f}"
                             f"\t{off:+.3f}\t{scale:.6f}")
                print(f"      time conversion: MIDI = {scale:.6f} * audio {off:+.3f} s")
                if segs:
                    print("      matching sections (audio -> MIDI):")
                    for t0, t1, soff, sscale, shit in segs:
                        r0, r1 = sscale * t0 + soff, sscale * t1 + soff
                        print(f"        {t0:7.2f}-{t1:7.2f} s  ->  "
                              f"{r0:7.2f}-{r1:7.2f} s  "
                              f"({shit} markers, scale {sscale:.6f})")
                        lines.append(f"\tsection\t{t0:.3f}\t{t1:.3f}\t"
                                     f"{r0:.3f}\t{r1:.3f}\t{shit}\t{sscale:.6f}\t{soff:+.3f}")
                else:
                    print("      matching sections: cannot be separated; the conversion for the whole match applies")
                found += 1
        if not (res[0][1] >= min_score and
                (margin >= min_margin or res[0][3] >= 0.05)
                or res[0][2] >= 0.40):
            lines.append(f"# {os.path.basename(a)}: not proven "
                         f"(markers {res[0][1]:.2f} margin {margin:.1f}x, "
                         f"correlation {res[0][2]:.2f}, "
                         f"{os.path.basename(res[0][5])})")
        print()

    if out_file:
        with open(out_file, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"written {found} pairs to {out_file}")


def main():
    ap = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__)
    ap.add_argument("--fetch", metavar="URLS",
                    help="text file with the list of URLs to download")
    ap.add_argument("--stage", default="kandidati",
                    help="where to download and what to unpack")
    ap.add_argument("--unpack", nargs="?", const=True, metavar="DIR",
                    help="unpack the zips in --stage (or in the given folder)")
    ap.add_argument("--audio", nargs="*", default=[],
                    help="recordings (files or folders)")
    ap.add_argument("--midi", nargs="*", default=[],
                    help="MIDI candidates (files or folders)")
    ap.add_argument("--out", help="where to write the verified pairs")
    ap.add_argument("--top", type=int, default=3)
    ap.add_argument("--min-score", dest="minscore", type=float, default=0.30)
    ap.add_argument("--min-margin", dest="minmargin", type=float, default=1.5,
                    help="how many times the best candidate must exceed the second")
    args = ap.parse_args()

    if args.fetch:
        print(f"== downloading per {args.fetch} ==")
        fetch(args.fetch, args.stage)
        print()

    if args.unpack:
        d = args.stage if args.unpack is True else args.unpack
        print(f"== unpacking in {d} ==")
        unpack(d)
        print()

    if args.audio:
        midi_dirs = list(args.midi) or [args.stage]
        print("== verifying ==")
        verify(collect(args.audio, AUDIO_EXT),
               collect(midi_dirs, MIDI_EXT),
               args.out, args.top, args.minscore, args.minmargin)
    elif not args.fetch and not args.unpack:
        ap.print_help()


if __name__ == "__main__":
    main()
