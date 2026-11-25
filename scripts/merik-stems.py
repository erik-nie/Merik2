#!/usr/bin/env python3
"""Maak uit een audiobestand + lyrics-txt automatisch een .merik (stems, MIDI, lyrics, akkoorden).

  merik-stems.py --audio song.mp3 --lyrics song.txt --out "Song.merik" [--work-dir DIR]
                 [--flac] [--vocals-out vocals.wav] [--keep-work] [--title T]

Stappen (voortgang op stdout als regels `PROGRESS <0..1> <tekst>`):
  1 inlezen          audio -> 44.1 kHz stereo wav (ffmpeg)
  2 bronscheiding    Demucs htdemucs_6s -> drums, bass, guitar, piano, other, vocals
  3 lyrics           forced alignment (torchaudio MMS_FA, CTC) van de lyrics op de zang
  4 akkoorden        beat-tracking + akkoordherkenning (librosa chroma-templates)
  5 MIDI             song.mid met tempo, lyrics (FF05) en akkoorden (marker)
  6 verpakken        .merik (zip zonder compressie, wav of flac)

Families in Merik: 1 drums, 2 bass, 3 guitars, 4 keys, 5 strings, 6 winds, 7 others.
De zang komt NIET in het .merik (alleen als wav via --vocals-out).
Benodigd (eenmalig): python 3.10+ met torch torchaudio demucs librosa soundfile mido numpy scipy, en ffmpeg.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import unicodedata
import zipfile
from pathlib import Path

DEMUCS_MODEL = "htdemucs_6s"
# demucs-bron -> (merik familie, naam)
FAMILY = {"drums": (1, "drums"), "bass": (2, "bass"), "guitar": (3, "guitars"), "piano": (4, "keys")}
OTHERS = (7, "others")


def progress(frac, text):
    print(f"PROGRESS {frac:.3f} {text}", flush=True)


def die(msg):
    print(f"ERROR {msg}", flush=True)
    sys.exit(1)


# ------------------------------------------------------------------ audio

def ffmpeg_bin():
    path = shutil.which("ffmpeg") or next((p for p in ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg")
                                           if Path(p).exists()), None)
    if not path:
        die("ffmpeg not found (install with: brew install ffmpeg)")
    return path


def decode(audio, wav):
    r = subprocess.run([ffmpeg_bin(), "-v", "error", "-y", "-i", str(audio), "-vn", "-ar", "44100", "-ac", "2",
                        "-c:a", "pcm_s16le", str(wav)], capture_output=True, text=True)
    if r.returncode:
        die("reading audio failed: " + r.stderr.strip()[-300:])


# ------------------------------------------------------------------ bronscheiding

def separate(wav, on_progress):
    import numpy as np
    import soundfile as sf
    import torch
    from demucs.apply import apply_model
    from demucs.pretrained import get_model

    model = get_model(DEMUCS_MODEL)
    model.eval()
    device = "mps" if torch.backends.mps.is_available() else ("cuda" if torch.cuda.is_available() else "cpu")
    x, sr = sf.read(str(wav), always_2d=True, dtype="float32")
    w = torch.from_numpy(x.T.copy())
    ref = w.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    w = (w - mean) / std

    # in stukken van ~60 s met voortgang; demucs doet zelf de overlap binnen een stuk.
    seg = 60 * sr
    pad = 5 * sr
    total = w.shape[1]
    out = {name: [] for name in model.sources}
    pos = 0
    while pos < total:
        a, b = max(0, pos - pad), min(total, pos + seg + pad)
        chunk = w[:, a:b]
        res = apply_model(model, chunk[None], device=device, split=True, overlap=0.25, progress=False)[0]
        res = res * std + mean
        lo, hi = pos - a, min(pos + seg, total) - a
        for i, name in enumerate(model.sources):
            out[name].append(res[i, :, lo:hi].cpu().numpy())
        pos += seg
        on_progress(min(1.0, pos / total))
    return {k: np.concatenate(v, axis=1).T for k, v in out.items()}, sr


# ------------------------------------------------------------------ lyrics

def clean_word(word):
    word = unicodedata.normalize("NFKD", word).lower()
    word = word.replace("’", "'").replace("`", "'")
    return re.sub(r"[^a-z']", "", word)


def parse_lyrics(path):
    """-> lijst regels, elke regel een lijst woorden (zichtbaar, zoals getypt)."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"[​-‏⁠﻿]", "", text)
    lines = []
    for raw in text.splitlines():
        raw = raw.strip()
        if not raw or re.fullmatch(r"\[[^\]]*\]|\([^)]*\)", raw):
            continue  # lege regels en [Verse 1]-koppen
        raw = re.sub(r"\[[^\]]*\]", "", raw)
        words = [w for w in raw.split() if clean_word(w)]
        if words:
            lines.append(words)
    return lines


def align_lyrics(vocals, sr, lines, on_progress):
    """Forced alignment (MMS_FA). Geeft per regel [(woord, start, einde)] in seconden."""
    import torch
    import torchaudio
    from torchaudio.pipelines import MMS_FA as bundle

    mono = torch.from_numpy(vocals.mean(axis=1)).float()[None]
    mono = torchaudio.functional.resample(mono, sr, int(bundle.sample_rate))
    device = "mps" if torch.backends.mps.is_available() else "cpu"
    model = bundle.get_model(with_star=False).to(device).eval()
    dictionary = bundle.get_dict(star=None)

    # emissions per venster van 30 s (attention is kwadratisch in de lengte)
    win = 30 * int(bundle.sample_rate)
    parts = []
    nwin = max(1, (mono.shape[1] + win - 1) // win)
    with torch.inference_mode():
        for i in range(nwin):
            piece = mono[:, i * win:(i + 1) * win].to(device)
            if piece.shape[1] < 4000:
                continue
            em, _ = model(piece)
            parts.append(torch.log_softmax(em, dim=-1).cpu())
            on_progress((i + 1) / nwin * 0.8)
    emission = torch.cat(parts, dim=1)
    frames = emission.shape[1]
    sec_per_frame = mono.shape[1] / bundle.sample_rate / frames

    tokens, owner = [], []  # owner = (regel, woord)
    for li, words in enumerate(lines):
        for wi, w in enumerate(words):
            for ch in clean_word(w):
                tokens.append(dictionary[ch])
                owner.append((li, wi))
    if not tokens:
        return []
    if len(tokens) > frames:
        die("the lyrics are too long for this audio")
    targets = torch.tensor([tokens], dtype=torch.int32)
    aligned, scores = torchaudio.functional.forced_align(emission, targets, blank=0)
    spans = torchaudio.functional.merge_tokens(aligned[0], scores[0].exp())
    if len(spans) != len(tokens):
        die("aligning the lyrics failed")
    timing = {}
    for span, (li, wi) in zip(spans, owner):
        s, e = span.start * sec_per_frame, span.end * sec_per_frame
        old = timing.get((li, wi))
        timing[(li, wi)] = (s, e) if old is None else (min(old[0], s), max(old[1], e))
    result = []
    for li, words in enumerate(lines):
        result.append([(w, *timing[(li, wi)]) for wi, w in enumerate(words)])
    on_progress(1.0)
    return result


# ------------------------------------------------------------------ akkoorden en tempo

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def chord_templates():
    import numpy as np
    names, mats = [], []
    shapes = [("", [0, 4, 7]), ("m", [0, 3, 7]), ("7", [0, 4, 7, 10]), ("m7", [0, 3, 7, 10]), ("maj7", [0, 4, 7, 11])]
    for root in range(12):
        for suffix, ivs in shapes:
            v = np.zeros(12)
            for k, iv in enumerate(ivs):
                v[(root + iv) % 12] = 1.0 if k < 3 else 0.6
            names.append(NOTES[root] + suffix)
            # 7e-akkoorden alleen als ze duidelijk beter passen dan de gewone drieklank
            mats.append(v / np.linalg.norm(v) * (1.0 if len(ivs) == 3 else 0.9))
    return names, np.array(mats)


def analyse_chords(harmonic, sr, on_progress):
    """-> (bpm, eerste beat in s, beat-tijden, [(tijd, akkoord)])."""
    import librosa
    import numpy as np

    y = librosa.to_mono(harmonic.T)
    y = librosa.resample(y, orig_sr=sr, target_sr=22050)
    sr2 = 22050
    hop = 512
    on_progress(0.1)
    tempo, beats = librosa.beat.beat_track(y=y, sr=sr2, hop_length=hop, units="time", tightness=200)
    tempo = float(np.atleast_1d(tempo)[0])
    # bpm fitten op alle beats (constante tempo); vouw naar 70-170
    if len(beats) > 8:
        n = np.arange(len(beats))
        slope, icpt = np.polyfit(n, beats, 1)
        tempo = 60.0 / slope
        first = float(icpt)
    else:
        first = float(beats[0]) if len(beats) else 0.0
    while tempo < 70:
        tempo *= 2
    while tempo > 170:
        tempo /= 2
    on_progress(0.4)
    chroma = librosa.feature.chroma_cqt(y=librosa.effects.harmonic(y), sr=sr2, hop_length=hop)
    names, templ = chord_templates()
    beat_len = 60.0 / tempo
    dur = len(y) / sr2
    grid = np.arange(first if first > 0 else first + beat_len, dur, beat_len)
    if first > beat_len:
        grid = np.arange(first % beat_len, dur, beat_len)
    on_progress(0.6)
    times = librosa.frames_to_time(np.arange(chroma.shape[1]), sr=sr2, hop_length=hop)
    scores = []
    for a, b in zip(grid[:-1], grid[1:]):
        m = (times >= a) & (times < b)
        v = chroma[:, m].mean(axis=1) if m.any() else np.zeros(12)
        n = np.linalg.norm(v)
        scores.append(templ @ (v / n) if n > 1e-6 else np.zeros(len(names)))
    scores = np.array(scores)
    # Viterbi met sterke voorkeur voor blijven (akkoorden wisselen zelden per tel)
    k = len(names)
    stay, move = 0.0, -0.3
    path = np.zeros(len(scores), dtype=int)
    if len(scores):
        score = scores[0].copy()
        back = np.zeros((len(scores), k), dtype=int)
        for t in range(1, len(scores)):
            best = int(np.argmax(score))
            cand_move = score[best] + move
            cand_stay = score + stay
            take_stay = cand_stay >= cand_move
            back[t] = np.where(take_stay, np.arange(k), best)
            score = np.where(take_stay, cand_stay, cand_move) + scores[t]
        path[-1] = int(np.argmax(score))
        for t in range(len(scores) - 1, 0, -1):
            path[t - 1] = back[t, path[t]]
    chords, last = [], None
    for t, c in zip(grid[:-1], path):
        # stilte (zeer lage chroma-energie) overslaan
        if names[c] != last:
            chords.append((float(t), names[c]))
            last = names[c]
    on_progress(1.0)
    return tempo, float(grid[0]) if len(grid) else 0.0, chords


# ------------------------------------------------------------------ MIDI

def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def write_midi(path, bpm, first_beat, lyric_lines, chords, title):
    """Constante tempo; als de eerste tel later dan 0 valt komt er een inleidende maat vóór."""
    ppq = 480
    events = []  # (tick, volgorde, bytes)
    lead = first_beat if first_beat > 0.05 else 0.0
    beat = 60.0 / bpm
    lead_ticks = 4 * ppq if lead else 0
    if lead:
        events.append((0, 0, b"\xff\x51\x03" + int(lead / 4 * 1e6).to_bytes(3, "big")))
        events.append((lead_ticks, 0, b"\xff\x51\x03" + int(beat * 1e6).to_bytes(3, "big")))
    else:
        events.append((0, 0, b"\xff\x51\x03" + int(beat * 1e6).to_bytes(3, "big")))
    events.append((0, 0, b"\xff\x58\x04\x04\x02\x18\x08"))

    def tick(t):
        if lead and t < lead:
            return int(t / lead * lead_ticks)
        return int(lead_ticks + (t - lead) / beat * ppq)

    name = title.encode("utf-8")
    events.append((0, 0, b"\xff\x03" + vlq(len(name)) + name))
    for t, chord in chords:
        raw = chord.encode("utf-8")
        events.append((tick(t), 1, b"\xff\x06" + vlq(len(raw)) + raw))
    for line in lyric_lines:
        for i, (word, start, _end) in enumerate(line):
            last = i == len(line) - 1
            raw = (word + ("\r" if last else " ")).encode("utf-8", errors="replace")
            events.append((tick(start), 2, b"\xff\x05" + vlq(len(raw)) + raw))
    events.sort(key=lambda e: (e[0], e[1]))
    track, prev = b"", 0
    for t, _o, data in events:
        track += vlq(t - prev) + data
        prev = t
    track += vlq(0) + b"\xff\x2f\x00"
    path.write_bytes(b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big")
                     + ppq.to_bytes(2, "big") + b"MTrk" + len(track).to_bytes(4, "big") + track)


# ------------------------------------------------------------------ pakket

def write_merik(out, entries):
    with zipfile.ZipFile(out, "w") as z:
        for name, path in entries:
            audio = Path(name).suffix.lower() in {".wav", ".flac", ".mp3"}
            z.write(path, name, zipfile.ZIP_STORED if audio else zipfile.ZIP_DEFLATED)


def to_flac(wav, flac):
    r = subprocess.run([ffmpeg_bin(), "-v", "error", "-y", "-i", str(wav), "-c:a", "flac", str(flac)],
                       capture_output=True, text=True)
    if r.returncode:
        die("making flac failed: " + r.stderr.strip()[-300:])


def load_existing(work, src):
    """Stems uit een eerdere run (drums/bass/guitars/keys/others/vocals.wav) als ze net zo lang zijn als het nummer."""
    import soundfile as sf
    need = {"drums": "1-drums.wav", "bass": "2-bass.wav", "guitar": "3-guitars.wav", "piano": "4-keys.wav",
            "other": "7-others.wav", "vocals": "vocals.wav"}
    frames = sf.info(str(src)).frames
    out, sr = {}, 44100
    for key, fname in need.items():
        path = work / fname
        if not path.exists():
            if key in ("bass", "guitar", "piano"):  # stille stems zijn niet geschreven
                out[key] = None
                continue
            return None, 44100
        info = sf.info(str(path))
        if info.frames != frames:
            return None, 44100
        data, sr = sf.read(str(path), always_2d=True, dtype="float32")
        out[key] = data
    ref = next(v for v in out.values() if v is not None)
    for key in out:
        if out[key] is None:
            out[key] = ref * 0
    return out, sr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--audio", required=True)
    ap.add_argument("--lyrics")
    ap.add_argument("--out", required=True, help="uitvoer .merik")
    ap.add_argument("--work-dir", help="tussenmap (wav, json, midi); standaard een tijdelijke map")
    ap.add_argument("--flac", action="store_true", help="stems als flac in het .merik")
    ap.add_argument("--vocals-out", help="sla de zang op als wav op dit pad (komt niet in het .merik)")
    ap.add_argument("--keep-work", action="store_true", help="tussenmap niet verwijderen")
    ap.add_argument("--reuse", action="store_true",
                    help="stems die al in de tussenmap staan met de lengte van het nummer niet opnieuw maken (met --keep-work)")
    ap.add_argument("--title")
    args = ap.parse_args()

    import numpy as np
    import soundfile as sf

    audio, out = Path(args.audio), Path(args.out)
    title = args.title or audio.stem
    import tempfile
    if args.work_dir:
        work = Path(args.work_dir)
    elif args.keep_work:
        work = out.parent / (out.stem + " - intermediate")
    else:
        work = Path(tempfile.mkdtemp(prefix="merik-stems-"))
    work.mkdir(parents=True, exist_ok=True)
    lines = parse_lyrics(args.lyrics) if args.lyrics else []

    progress(0.01, "Reading audio")
    src = work / "input.wav"
    decode(audio, src)

    sources = None
    if args.reuse:
        sources, sr = load_existing(work, src)
        if sources:
            progress(0.55, "Reusing the stems already in the intermediate folder")
    if not sources:
        progress(0.05, "Separating stems (first run downloads the model)")
        sources, sr = separate(src, lambda f: progress(0.05 + 0.5 * f, "Separating stems (first run downloads the model)"))
    stem_files = []
    others = np.zeros_like(sources["vocals"])
    for name, data in sources.items():
        if name == "vocals":
            continue
        if name in FAMILY:
            fam, label = FAMILY[name]
            if float(np.sqrt(np.mean(data ** 2))) > 1e-3:
                path = work / f"{fam}-{label}.wav"
                sf.write(str(path), data, sr, subtype="PCM_24")
                stem_files.append((fam, label, path))
        else:
            others += data
    # alles wat niet bij een eigen familie hoort (other) naar familie 7
    path = work / f"{OTHERS[0]}-{OTHERS[1]}.wav"
    sf.write(str(path), others, sr, subtype="PCM_24")
    stem_files.append((OTHERS[0], OTHERS[1], path))
    vocals = sources["vocals"]
    sf.write(str(work / "vocals.wav"), vocals, sr, subtype="PCM_24")

    lyric_lines = []
    if lines:
        progress(0.56, "Aligning lyrics (first run downloads the model)")
        lyric_lines = align_lyrics(vocals, sr, lines, lambda f: progress(0.56 + 0.2 * f, "Aligning lyrics (first run downloads the model)"))
        (work / "lyrics.json").write_text(json.dumps([
            {"time": round(l[0][1], 3), "text": " ".join(w for w, _, _ in l),
             "words": [{"time": round(s, 3), "text": w} for w, s, _ in l]} for l in lyric_lines
        ], ensure_ascii=False, indent=1), encoding="utf-8")

    progress(0.77, "Detecting tempo and chords")
    backing = sum(s for n, s in sources.items() if n not in ("vocals", "drums"))
    bpm, first, chords = analyse_chords(backing, sr, lambda f: progress(0.77 + 0.1 * f, "Detecting tempo and chords"))
    (work / "chords.json").write_text(json.dumps([{"time": round(t, 3), "chord": c} for t, c in chords],
                                                  indent=1), encoding="utf-8")

    progress(0.88, "Writing MIDI")
    write_midi(work / "song.mid", bpm, first, lyric_lines, chords, title)

    progress(0.90, "Building the .merik")
    entries = []
    for fam, label, wav in stem_files:
        if args.flac:
            flac = wav.with_suffix(".flac")
            to_flac(wav, flac)
            entries.append((flac.name, flac))
        else:
            entries.append((wav.name, wav))
    entries.append(("song.mid", work / "song.mid"))
    entries.append(("chords.json", work / "chords.json"))
    if lyric_lines:
        entries.append(("lyrics.json", work / "lyrics.json"))
    out.parent.mkdir(parents=True, exist_ok=True)
    write_merik(out, entries)
    progress(0.98, "Cleaning up")

    if args.vocals_out:
        shutil.copyfile(work / "vocals.wav", args.vocals_out)
    (work / "input.wav").unlink(missing_ok=True)
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    print(f"DONE {out}", flush=True)


if __name__ == "__main__":
    main()
