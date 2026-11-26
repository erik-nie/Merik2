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
import math
import re
import shutil
import subprocess
import sys
import unicodedata
import zipfile
from pathlib import Path

DEMUCS_MODEL = "htdemucs_6s"
LYRICS_SCORE_THRESHOLD = -4.0   # gemiddelde log-waarschijnlijkheid per letter: juiste tekst ~ -3, een andere tekst ~ -5 (MMS_FA)
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


def lyric_emissions(vocals, sr, on_progress):
    """Letter-waarschijnlijkheden (MMS_FA) van de zang: (emission, seconden per frame, dictionary)."""
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
            on_progress((i + 1) / nwin)
    emission = torch.cat(parts, dim=1)
    return emission, mono.shape[1] / bundle.sample_rate / emission.shape[1], dictionary


def align_text(emission, sec_per_frame, dictionary, lines):
    """Forced alignment van een tekst. -> (per regel [(woord, start, einde)], score) of None.
    score = gemiddelde log-waarschijnlijkheid van de letters op hun plek: de tekst die echt gezongen wordt scoort duidelijk hoger."""
    import math
    import torch
    import torchaudio
    tokens, owner = [], []  # owner = (regel, woord)
    for li, words in enumerate(lines):
        for wi, w in enumerate(words):
            for ch in clean_word(w):
                tokens.append(dictionary[ch])
                owner.append((li, wi))
    if not tokens or len(tokens) > emission.shape[1]:
        return None
    targets = torch.tensor([tokens], dtype=torch.int32)
    aligned, scores = torchaudio.functional.forced_align(emission, targets, blank=0)
    spans = torchaudio.functional.merge_tokens(aligned[0], scores[0].exp())
    if len(spans) != len(tokens):
        return None
    timing = {}
    for span, (li, wi) in zip(spans, owner):
        s, e = span.start * sec_per_frame, span.end * sec_per_frame
        old = timing.get((li, wi))
        timing[(li, wi)] = (s, e) if old is None else (min(old[0], s), max(old[1], e))
    result = []
    last = 0.0
    for li, words in enumerate(lines):
        line = []
        for wi, w in enumerate(words):
            st, en = timing.get((li, wi), (last, last))
            last = en
            line.append((w, st, en))
        result.append(line)
    score = sum(math.log(max(float(sp.score), 1e-6)) for sp in spans) / len(spans)
    return result, score


# ------------------------------------------------------------------ MIDI/KAR/TXT als bron

CHORD_RE = re.compile(r"^[A-G][#b]?(m|maj|min|dim|aug|sus|add)?[0-9()+b#-]*(/[A-G][#b]?)?$")
SECTION_RE = re.compile(r"^(intro|verse|vers|chorus|refrain|pre[- ]?chorus|bridge|solo|outro|coda|interlude|instrumental|break|hook|tag)\b", re.I)


class Candidate:
    def __init__(self, path, kind, lines, sections=None, chords=None, quarters=None, beats_per_bar=None):
        self.quarters, self.beats_per_bar = quarters or [], beats_per_bar
        self.path, self.kind, self.lines = Path(path), kind, lines
        self.sections = sections or {}   # regelindex -> naam
        self.chords = chords or []       # akkoordnamen in volgorde (alleen MIDI)


def load_text_candidate(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"[\u200b-\u200f\u2060\ufeff]", "", text)
    lines, sections, pending = [], {}, None
    for raw in text.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        m = re.fullmatch(r"\[([^\]]*)\]", raw)
        if m:
            name = m.group(1).split(":")[0].strip()
            pending = name or None
            continue
        if re.fullmatch(r"\([^)]*\)", raw):
            continue
        raw = re.sub(r"\[[^\]]*\]", "", raw)
        words = [w for w in raw.split() if clean_word(w)]
        if words:
            if pending:
                sections[len(lines)] = pending
                pending = None
            lines.append(words)
    return Candidate(path, "text", lines, sections) if lines else None


def load_midi_candidate(path):
    import mido
    try:
        mid = mido.MidiFile(str(path))
    except Exception:
        return None
    events = []  # (tick, soort, tekst)
    for track in mid.tracks:
        t = 0
        for msg in track:
            t += msg.time
            if msg.type in ("lyrics", "text", "marker"):
                try:
                    events.append((t, msg.type, msg.text))
                except Exception:
                    pass
    events.sort(key=lambda e: e[0])
    has_lyrics = any(k == "lyrics" for _, k, _ in events)
    lines, line_ticks, words = [], [], []
    first_tick = 0
    chords, sections = [], []
    word_open = False  # het laatste woord is nog niet afgesloten (geen spatie erna)

    def flush():
        nonlocal words, word_open
        keep = [w for w in words if clean_word(w)]
        if keep:
            lines.append(keep)
            line_ticks.append(first_tick)
        words, word_open = [], False

    for tick, kind, txt in events:
        raw = txt.replace("\x00", "")
        stripped = raw.strip()
        if kind == "marker":
            if stripped and CHORD_RE.match(stripped):
                chords.append(stripped)
            elif SECTION_RE.match(stripped):
                sections.append((tick, re.sub(r"^[0-9]+@", "", stripped).rstrip(":").strip()))
            continue
        if kind == "text" and (has_lyrics or stripped.startswith(("@", "%"))):
            if CHORD_RE.match(stripped):
                chords.append(stripped)
            continue
        for chord in re.findall(r"\[([A-G][^\]]*)\]", raw):
            chords.append(chord)
        raw = re.sub(r"\[[^\]]*\]", "", raw)
        if raw.startswith(("/", "\\", "\r", "\n")):
            flush()
        raw = raw.lstrip("/\\")
        end_line = raw.endswith(("\r", "\n"))
        piece = raw.strip("\r\n")
        if piece.strip():
            if not words:
                first_tick = tick
            starts_new = (not word_open) or piece.startswith(" ")
            if starts_new or not words:
                words.append(piece.strip())
            else:
                words[-1] += piece.strip()
            word_open = not piece.endswith(" ")
        if end_line:
            flush()
    flush()
    if not lines:
        return None
    smap = {}
    for tick, name in sections:
        for i, lt in enumerate(line_ticks):
            if lt >= tick - 10:
                smap.setdefault(i, name)
                break
    numerator = None
    for track in mid.tracks:
        for msg in track:
            if msg.type == "time_signature" and numerator is None and msg.denominator == 4 and 2 <= msg.numerator <= 4:
                numerator = msg.numerator
    quarters = [t / mid.ticks_per_beat for t in line_ticks] if len(line_ticks) == len(lines) else []
    return Candidate(path, "midi", lines, smap, chords, quarters, numerator)


def load_candidate(path):
    ext = Path(path).suffix.lower()
    try:
        if ext in (".mid", ".midi", ".kar"):
            return load_midi_candidate(path)
        return load_text_candidate(path)
    except Exception as exc:
        print(f"INFO could not read {path}: {exc}", flush=True)
        return None


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


def analyse_chords(harmonic, sr, on_progress, rhythm=None, midi_chords=None):
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
        if names[c] != last:
            chords.append((float(t), names[c]))
            last = names[c]
    info = {"fit": None, "first_beat": float(grid[0]) if len(grid) else 0.0}
    if midi_chords and len(scores):
        aligned = align_chord_sequence(midi_chords, names, grid[:-1], scores)
        if aligned:
            info["fit"] = (aligned[1], float(scores.max(axis=1).mean()))
            chords = aligned[0]
    first_beat = float(grid[0]) if len(grid) else 0.0
    beats, downbeat, conf = 4, first_beat, 0.0
    if rhythm is not None and len(grid) > 24:
        beats, downbeat, conf = find_downbeat(rhythm, sr, first_beat, tempo, chords)
    on_progress(1.0)
    return tempo, downbeat, chords, beats, conf, info


def chord_index(name, names):
    """Index van een akkoordnaam in de templates ("Am7", "F#m", "Bb", "Cmaj7", "G/B"...); grofweg dichtstbijzijnde soort."""
    m = re.match(r"^([A-G])([#b]?)(.*)$", name.strip())
    if not m:
        return None
    base = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1)]
    root = (base + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)) % 12
    q = m.group(3).split("/")[0].lower()
    minor = (q.startswith("m") and not q.startswith("maj")) or q.startswith("min") or q.startswith("dim")
    if "maj7" in q:
        suffix = "maj7"
    elif minor and "7" in q:
        suffix = "m7"
    elif minor:
        suffix = "m"
    elif "7" in q and "sus" not in q:
        suffix = "7"
    else:
        suffix = ""
    try:
        return names.index(NOTES[root] + suffix)
    except ValueError:
        return None


def align_chord_sequence(midi_chords, names, times, scores):
    """De akkoorden van een MIDI-bestand, in volgorde, op de audio gelegd: een monotoon pad over de tellen dat het best past."""
    import numpy as np
    entries = [(n, chord_index(n, names)) for n in midi_chords]
    entries = [(n, i) for n, i in entries if i is not None]
    M, B = len(entries), len(times)
    if M == 0 or B < M:
        return None
    NEG = -1e30
    best = np.full(M, NEG)
    best[0] = scores[0][entries[0][1]]
    back = np.zeros((B, M), dtype=bool)
    for b in range(1, B):
        stay = best
        move = np.concatenate(([NEG], best[:-1]))
        take = move > stay
        back[b] = take
        base = np.where(take, move, stay)
        best = np.where(base <= NEG / 2, NEG, base + np.array([scores[b][i] for _, i in entries]) - np.where(take, 0.0, 0.0) - 0.02 * take)
    if best[-1] <= NEG / 2:
        return None
    j, start, total = M - 1, [0] * M, 0.0
    for b in range(B - 1, -1, -1):
        total += scores[b][entries[j][1]]
        if back[b][j]:
            start[j] = b
            j -= 1
        if j < 0:
            break
    return [(float(times[start[k]]), entries[k][0]) for k in range(M)], total / B


def find_downbeat(rhythm, sr, first_beat, bpm, chords):
    """Maatsoort (3 of 4 tellen) en de eerste 1: de tel waar de basdrum en de akkoordwissels het sterkst zijn."""
    import librosa
    import numpy as np
    y = librosa.to_mono(rhythm.T) if rhythm.ndim > 1 else rhythm
    y = librosa.resample(y, orig_sr=sr, target_sr=22050)
    hop = 512
    S = librosa.power_to_db(librosa.feature.melspectrogram(y=y, sr=22050, n_fft=1024, hop_length=hop, n_mels=24, fmax=150))
    kick = librosa.onset.onset_strength(S=S, sr=22050, hop_length=hop)
    fd = hop / 22050.0
    beat = 60.0 / bpm
    count = int((len(y) / 22050.0 - first_beat) / beat)
    if count <= 24:
        return 4, first_beat, 0.0
    mean_k = max(float(kick.mean()), 1e-6)
    feature = np.zeros(count)
    for b in range(count):
        t = first_beat + b * beat
        a, z = max(0, int((t - 0.06) / fd)), min(len(kick) - 1, int((t + 0.06) / fd))
        if a <= z:
            feature[b] = kick[a:z + 1].max() / mean_k
    for t, _ in chords:
        b = int(round((t - first_beat) / beat))
        if 0 <= b < count:
            feature[b] += 2
    c = feature - feature.mean()
    corr = lambda lag: float(np.dot(c[:-lag], c[lag:])) / (count - lag)
    beats = 3 if (corr(3) > 1.3 * corr(4) and corr(3) > 0 and corr(6) > corr(8)) else 4
    means = [feature[k::beats].mean() for k in range(beats)]
    k = int(np.argmax(means))
    others = (sum(means) - means[k]) / (beats - 1)
    conf = max(0.0, min(1.0, (means[k] - others) / max(means[k], 1e-6)))
    return beats, first_beat + k * beat, conf


# ------------------------------------------------------------------ MIDI

def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def shift_for_count_in(downbeat, bpm, beats_per_bar, count_in_bars):
    """Met aftikken schuift de audio later: eerst zodat de eerste 1 op een maat valt, dan met de maten van het aftikken."""
    if count_in_bars <= 0:
        return 0.0, 0.0
    bar = beats_per_bar * 60.0 / bpm
    rest = downbeat % bar
    pad = 0.0 if rest < 0.05 or bar - rest < 0.05 else bar - rest
    return pad, count_in_bars * bar


def midi_downbeat(times, quarters, first_beat, bpm, beats_per_bar):
    """De eerste 1 volgens de MIDI: de regels staan op bekende tellen in de MIDI en op bekende tijden in de audio."""
    if len(times) != len(quarters) or len(times) < 4:
        return None
    beat = 60.0 / bpm
    votes = [0] * beats_per_bar
    for t, q in zip(times, quarters):
        b = round((t - first_beat) / beat)
        votes[(b - round(q)) % beats_per_bar] += 1
    best = max(range(beats_per_bar), key=lambda k: votes[k])
    return first_beat + best * beat, votes[best] / len(times)


def write_midi(path, bpm, downbeat, lyric_lines, chords, title, beats_per_bar=4, sections=(), count_in_bars=0, end_time=None):
    """Constante tempo. `downbeat` is de eerste "1" van een maat; ligt die niet op 0, dan komen er hele maten vóór,
    zodat maten, tellen en click gelijklopen met de audio. Lyrics (FF05), akkoorden en secties (markers FF06)."""
    ppq = 480
    events = []  # (tick, volgorde, bytes)
    beat = 60.0 / bpm
    pad, count_seconds = shift_for_count_in(downbeat, bpm, beats_per_bar, count_in_bars)
    moved = count_in_bars > 0
    total = pad + count_seconds
    lead = downbeat if (downbeat > 0.05 and not moved) else 0.0
    lead_bars = max(1, math.ceil(lead / beat / beats_per_bar)) if lead else 0
    lead_beats = lead_bars * beats_per_bar
    lead_ticks = lead_beats * ppq
    if lead:
        events.append((0, 0, b"\xff\x51\x03" + int(lead / lead_beats * 1e6).to_bytes(3, "big")))
        events.append((lead_ticks, 0, b"\xff\x51\x03" + int(beat * 1e6).to_bytes(3, "big")))
    else:
        events.append((0, 0, b"\xff\x51\x03" + int(beat * 1e6).to_bytes(3, "big")))
    events.append((0, 0, b"\xff\x58\x04" + bytes([beats_per_bar, 2, 24, 8])))

    def tick(t):
        if moved:
            return int(round((t + total) / beat * ppq))
        if lead and t < lead:
            return int(t / lead * lead_ticks)
        return int(lead_ticks + (t - lead) / beat * ppq)

    name = title.encode("utf-8")
    events.append((0, 0, b"\xff\x03" + vlq(len(name)) + name))
    for t, sec in sections:
        raw = sec.encode("utf-8")
        events.append((tick(t), 1, b"\xff\x06" + vlq(len(raw)) + raw))
    for t, chord in chords:
        raw = chord.encode("utf-8")
        events.append((tick(t), 2, b"\xff\x06" + vlq(len(raw)) + raw))
    for line in lyric_lines:
        for i, (word, start, _end) in enumerate(line):
            raw = (word + ("\r" if i == len(line) - 1 else " ")).encode("utf-8", errors="replace")
            events.append((tick(start), 3, b"\xff\x05" + vlq(len(raw)) + raw))
    events.sort(key=lambda e: (e[0], e[1]))
    track, prev = b"", 0
    for t, _o, data in events:
        track += vlq(t - prev) + data
        prev = t
    end_tick = max(prev, tick(end_time)) if end_time is not None else prev
    track += vlq(end_tick - prev) + b"\xff\x2f\x00"
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
    ap.add_argument("--lyrics", help="(oud) een lyrics-txt; zelfde als --source")
    ap.add_argument("--source", action="append", default=[],
                    help="kandidaat voor lyrics/akkoorden/secties: .mid, .kar of .txt (herhaalbaar); de beste wordt gekozen op de zang, MIDI heeft voorrang")
    ap.add_argument("--out", required=True, help="uitvoer .merik")
    ap.add_argument("--work-dir", help="tussenmap (wav, json, midi); standaard een tijdelijke map")
    ap.add_argument("--flac", action="store_true", help="stems als flac in het .merik")
    ap.add_argument("--vocals-out", help="sla de zang op als wav op dit pad (komt niet in het .merik)")
    ap.add_argument("--keep-work", action="store_true", help="tussenmap niet verwijderen")
    ap.add_argument("--reuse", action="store_true",
                    help="stems die al in de tussenmap staan met de lengte van het nummer niet opnieuw maken (met --keep-work)")
    ap.add_argument("--use-edited", action="store_true",
                    help="gebruik de (bewerkte) analysis.json, chords.json en lyrics.json uit de tussenmap (met --keep-work)")
    ap.add_argument("--count-in", type=int, default=0, choices=[0, 1, 2],
                    help="aftikken: 1 of 2 maten click voor het nummer; audio, lyrics en akkoorden schuiven even veel op")
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
    inventory = [f"audio: {audio.name}"]

    progress(0.01, "Reading audio")
    src = work / "input.wav"
    decode(audio, src)

    sources = None
    if args.reuse:
        sources, sr = load_existing(work, src)
        if sources:
            progress(0.55, "Reusing the stems already in the intermediate folder")
            inventory.append("stems: hergebruikt uit de tussenmap")
    if not sources:
        progress(0.05, "Separating stems (first run downloads the model)")
        sources, sr = separate(src, lambda f: progress(0.05 + 0.5 * f, "Separating stems (first run downloads the model)"))
        inventory.append("stems: gescheiden met htdemucs_6s")
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

    analysis_json, chords_json, lyrics_json = work / "analysis.json", work / "chords.json", work / "lyrics.json"
    lyric_lines, section_list = [], []
    chords, bpm, downbeat, beats_per_bar = [], 120.0, 0.0, 4
    edited = False
    if args.use_edited and args.keep_work and analysis_json.exists() and chords_json.exists():
        try:
            info = json.loads(analysis_json.read_text(encoding="utf-8"))
            bpm, downbeat, beats_per_bar = float(info["bpm"]), float(info.get("downbeat", 0)), int(info.get("beatsPerBar", 4))
            chords = sorted([(float(c["time"]), str(c["chord"])) for c in json.loads(chords_json.read_text(encoding="utf-8"))])
            if lyrics_json.exists():
                for entry in sorted(json.loads(lyrics_json.read_text(encoding="utf-8")), key=lambda e: float(e["time"])):
                    words = [(w["text"], float(w["time"]), float(w["time"])) for w in entry.get("words", [])] or \
                            [(w, float(entry["time"]), float(entry["time"])) for w in str(entry.get("text", "")).split()]
                    if words:
                        lyric_lines.append(words)
                        if entry.get("section"):
                            section_list.append((words[0][1], entry["section"]))
            edited = True
            inventory.append("bewerkte json uit de tussenmap gebruikt: analysis.json, chords.json" + (", lyrics.json" if lyric_lines else ""))
            progress(0.88, "Using the edited json of the intermediate folder")
        except Exception as exc:
            print(f"INFO edited json could not be read ({exc}): analysing again", flush=True)
            edited = False
            lyric_lines, section_list = [], []

    if not edited:
        # kandidaten voor lyrics/akkoorden/secties: MIDI/KAR eerst, dan txt; de zang beslist
        paths = list(args.source) + ([args.lyrics] if args.lyrics else [])
        candidates = [c for c in (load_candidate(p) for p in paths) if c]
        chosen, timed = None, None
        if candidates:
            progress(0.56, "Aligning lyrics (first run downloads the model)")
            emission, spf, dictionary = lyric_emissions(vocals, sr, lambda f: progress(0.56 + 0.15 * f, "Aligning lyrics (first run downloads the model)"))
            scored = []
            for c in candidates:
                r = align_text(emission, spf, dictionary, c.lines)
                if r:
                    scored.append((c, r[0], r[1]))
                    inventory.append(f"kandidaat {c.path.name}: score {r[1]:.2f}")
                else:
                    inventory.append(f"kandidaat {c.path.name}: past niet bij de zang")
            threshold = LYRICS_SCORE_THRESHOLD
            top = max((x[2] for x in scored), default=-99)
            pick = [x for x in scored if x[0].kind == "midi" and x[2] >= threshold and x[2] >= top - 0.3]
            pick = max(pick, key=lambda x: x[2]) if pick else None
            if pick is None:
                rest = [x for x in scored if x[2] >= threshold]
                pick = max(rest, key=lambda x: x[2]) if rest else None
            if pick:
                chosen, timed, _ = pick
                inventory.append(f"gekozen: {chosen.path.name} ({chosen.kind})")
                lyric_lines = timed
                for i, name in chosen.sections.items():
                    if i < len(timed) and timed[i]:
                        section_list.append((timed[i][0][1], name))
            else:
                inventory.append("geen kandidaat past bij de zang: zonder lyrics verder")

        progress(0.77, "Detecting tempo and chords")
        backing = sum(s for n, s in sources.items() if n not in ("vocals", "drums"))
        midi_chords = chosen.chords if chosen is not None and chosen.kind == "midi" else None
        bpm, downbeat, chords, beats_per_bar, conf, info = analyse_chords(
            backing, sr, lambda f: progress(0.77 + 0.1 * f, "Detecting tempo and chords"), rhythm=sources["drums"], midi_chords=midi_chords)
        inventory.append(f"tempo {bpm:.1f} bpm, maat {beats_per_bar}/4, eerste 1 op {downbeat:.2f} s (zekerheid {conf * 100:.0f}%)")
        if chosen is not None and chosen.kind == "midi" and chosen.beats_per_bar and timed:
            r = midi_downbeat([l[0][1] for l in timed if l], chosen.quarters, info["first_beat"], bpm, chosen.beats_per_bar)
            if r and r[1] >= 0.5:
                beats_per_bar, downbeat = chosen.beats_per_bar, r[0]
                inventory.append(f"maat uit de MIDI: {beats_per_bar}/4, eerste 1 op {downbeat:.2f} s ({r[1] * 100:.0f}% van de regels komt overeen)")
            else:
                inventory.append("maat uit de MIDI niet te leggen over de audio: maat uit de audio gebruikt")
        if info["fit"]:
            inventory.append(f"akkoorden: uit de MIDI over de audio gelegd (pasvorm {info['fit'][0]:.2f}, vrije keuze {info['fit'][1]:.2f})")
        else:
            inventory.append("akkoorden: herkend uit de audio")
        analysis_json.write_text(json.dumps({"title": title, "bpm": round(bpm, 2), "downbeat": round(downbeat, 3),
                                             "beatsPerBar": beats_per_bar}, indent=1), encoding="utf-8")
        chords_json.write_text(json.dumps([{"time": round(t, 3), "chord": c} for t, c in chords], indent=1), encoding="utf-8")
        if lyric_lines:
            sec_by_time = {round(t, 3): n for t, n in section_list}
            entries = []
            for l in lyric_lines:
                e = {"time": round(l[0][1], 3), "text": " ".join(w for w, _, _ in l),
                     "words": [{"time": round(st, 3), "text": w} for w, st, _ in l]}
                if round(l[0][1], 3) in sec_by_time:
                    e["section"] = sec_by_time[round(l[0][1], 3)]
                entries.append(e)
            lyrics_json.write_text(json.dumps(entries, ensure_ascii=False, indent=1), encoding="utf-8")

    progress(0.88, "Writing MIDI")
    song_seconds = len(vocals) / sr
    pad, count_seconds = shift_for_count_in(downbeat, bpm, beats_per_bar, args.count_in)
    shift = pad + count_seconds
    write_midi(work / "song.mid", bpm, downbeat, lyric_lines, chords, title, beats_per_bar, section_list,
               count_in_bars=args.count_in, end_time=song_seconds)

    progress(0.90, "Building the .merik")
    pack = Path(tempfile.mkdtemp(prefix="merik-pack-"))
    entries = []
    for fam, label, wav in stem_files:
        src_wav = wav
        if shift > 0:
            data, rate = sf.read(str(wav), always_2d=True, dtype="float32")
            src_wav = pack / wav.name
            sf.write(str(src_wav), np.concatenate([np.zeros((int(shift * rate), data.shape[1]), dtype="float32"), data]), rate, subtype="PCM_24")
        if args.flac:
            flac = (pack / wav.name).with_suffix(".flac")
            to_flac(src_wav, flac)
            entries.append((flac.name, flac))
        else:
            entries.append((wav.name, src_wav))
    if shift > 0:
        inventory.append(f"aftikken: {args.count_in} maat/maten; audio en alles {shift:.1f} s later (waarvan {pad:.1f} s om de 1 op een maat te leggen)")
    entries.append(("song.mid", work / "song.mid"))
    shifted_chords = pack / "chords.json"
    shifted_chords.write_text(json.dumps([{"time": round(t + shift, 3), "chord": c} for t, c in chords], indent=1), encoding="utf-8")
    entries.append(("chords.json", shifted_chords))
    if lyric_lines:
        shifted_lyrics = pack / "lyrics.json"
        sec_by_time = {round(t, 3): n for t, n in section_list}
        rows = []
        for l in lyric_lines:
            e = {"time": round(l[0][1] + shift, 3), "text": " ".join(w for w, _, _ in l),
                 "words": [{"time": round(st + shift, 3), "text": w} for w, st, _ in l]}
            if round(l[0][1], 3) in sec_by_time:
                e["section"] = sec_by_time[round(l[0][1], 3)]
            rows.append(e)
        shifted_lyrics.write_text(json.dumps(rows, ensure_ascii=False, indent=1), encoding="utf-8")
        entries.append(("lyrics.json", shifted_lyrics))
    out.parent.mkdir(parents=True, exist_ok=True)
    write_merik(out, entries)
    shutil.rmtree(pack, ignore_errors=True)
    inventory.append("tussenbestanden: " + ", ".join(sorted(p.name for p in work.iterdir())))
    inventory.append(f"uitvoer: {out.name}")
    for line in inventory:
        print(f"INFO {line}", flush=True)
    progress(0.98, "Cleaning up")

    if args.vocals_out:
        shutil.copyfile(work / "vocals.wav", args.vocals_out)
    (work / "input.wav").unlink(missing_ok=True)
    if not args.keep_work:
        shutil.rmtree(work, ignore_errors=True)
    print(f"DONE {out}", flush=True)


if __name__ == "__main__":
    main()
