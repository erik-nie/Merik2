"""Maak een .merik-bestand: een zip met audio-stems en/of MIDI onder vaste namen.

  make-merik.py pack  uitvoer.merik --folder stems/ [--midi song.mid] [--click click.wav]
                      [--chords chords.json] [--lyrics lyrics.json]
  make-merik.py pack  uitvoer.merik --stem 1=drums.wav --stem 7=live-vocals.wav --midi song.mid
  make-merik.py demo  uitvoer.merik          (maakt een synthetisch testnummer)

Een manifest is niet nodig. Merik herkent de bestanden aan hun naam:
  1-drums.flac 2-bass 3-guitars 4-keys 5-strings 6-winds 7-others (of drums.wav, others.wav, ...)
  click.wav, song.mid, chords.json, lyrics.json
Zie docs/merik-format.md.
"""

import argparse
import json
import math
import random
import re
import shutil
import struct
import sys
import wave
import zipfile
from pathlib import Path

AUDIO = {".wav", ".flac", ".mp3", ".aif", ".aiff"}
WORDS = ("drums", "bass", "guitar", "keys", "strings", "winds", "other", "click")


def stem_name_ok(name):
    """Zelfde regels als Merik: 1..8 vooraan, of een familienaam."""
    base = Path(name).stem.lower()
    if re.match(r"^[1-8]($|[^0-9a-z])", base):
        return True
    return any(re.match(rf"^{w}($|[^a-z])", base) for w in WORDS)


def write_merik(out, files, manifest=None):
    """files: lijst (naam in zip, pad). Audio wordt zonder compressie opgeslagen."""
    with zipfile.ZipFile(out, "w") as z:
        if manifest:
            z.writestr("manifest.json", json.dumps(manifest, indent=2), zipfile.ZIP_DEFLATED)
        for name, path in files:
            kind = zipfile.ZIP_STORED if Path(name).suffix.lower() in AUDIO else zipfile.ZIP_DEFLATED
            z.write(path, name, kind)
    print(f"{out}: " + ", ".join(name for name, _ in files))


def cmd_pack(args):
    files = []
    if args.folder:
        for path in sorted(Path(args.folder).iterdir()):
            if path.suffix.lower() in AUDIO and stem_name_ok(path.name):
                files.append((path.name, path))
    for item in args.stem or []:
        family, _, name = item.partition("=")
        path = Path(name)
        if not family.isdigit() or not 1 <= int(family) <= 8:
            sys.exit(f"Family '{family}' is invalid (1-8)")
        files.append((f"{family}-{path.name}", path))
    for option, target in ((args.midi, "song.mid"), (args.click, "click" + Path(args.click or "x.wav").suffix),
                           (args.chords, "chords.json"), (args.lyrics, "lyrics.json")):
        if option:
            files.append((target, Path(option)))
    if not files:
        sys.exit("Nothing to pack: give --folder, --stem, --midi, ...")

    manifest = None
    if args.bpm or args.sig or args.offset or args.title:
        manifest = {"title": args.title or Path(args.out).stem}
        if args.bpm:
            manifest["bpm"] = args.bpm
        if args.sig:
            num, den = (int(x) for x in args.sig.split("/"))
            manifest["timeSignature"] = [num, den]
        if args.offset:
            manifest["timing"] = {"audioOffsetSeconds": args.offset}
    elif not args.midi:
        print("Note: without a MIDI file Merik assumes 120 bpm 4/4 for the click; use --bpm/--sig.")
    write_merik(args.out, files, manifest)


# ---------------------------------------------------------------- demo

RATE = 22050


def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def make_demo_midi(path, bpm, bars):
    ppq = 480
    events = []  # (tick, bytes)
    events.append((0, b"\xff\x51\x03" + struct.pack(">I", int(60_000_000 / bpm))[1:]))
    events.append((0, b"\xff\x58\x04\x04\x02\x18\x08"))
    events.append((0, b"\xc2\x19"))  # kanaal 3: gitaar (programma 25)
    events.append((0, b"\xc1\x21"))  # kanaal 2: bas (moet verdwijnen: er is een bas-stem)
    chords = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]  # Am F C G
    for bar in range(bars):
        chord = chords[bar % 4]
        for beat in range(4):
            tick = (bar * 4 + beat) * ppq
            for note in chord:
                events.append((tick, bytes([0x92, note, 80])))  # gitaar
                events.append((tick + ppq - 40, bytes([0x82, note, 0])))
            events.append((tick, bytes([0x91, chord[0] - 24, 100])))  # bas (onderdrukt)
            events.append((tick + ppq - 40, bytes([0x81, chord[0] - 24, 0])))
    events.sort(key=lambda e: e[0])
    track, last = b"", 0
    for tick, data in events:
        track += vlq(tick - last) + data
        last = tick
    track += vlq(0) + b"\xff\x2f\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppq))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 30000)) for s in samples))


def cmd_demo(args):
    import tempfile
    bpm, bars = 100, 8
    beat = 60.0 / bpm
    total = int(bars * 4 * beat * RATE)
    rng = random.Random(1)
    chords = [(220.0, 261.6, 329.6), (174.6, 220.0, 261.6), (130.8, 164.8, 196.0), (196.0, 246.9, 293.7)]

    drums = [0.0] * total
    bass = [0.0] * total
    keys = [0.0] * total
    strings = [0.0] * total
    others = [0.0] * total

    def add(buf, start, length, fn):
        for i in range(min(length, total - start)):
            buf[start + i] += fn(i / RATE)

    for b in range(bars * 4):
        start = int(b * beat * RATE)
        bar = b // 4
        chord = chords[bar % 4]
        if b % 2 == 0:  # kick op 1 en 3
            add(drums, start, int(0.15 * RATE),
                lambda t: 0.9 * math.sin(2 * math.pi * (110 - 300 * t) * t) * math.exp(-t * 18))
        else:  # snare op 2 en 4
            add(drums, start, int(0.15 * RATE), lambda t: 0.5 * (rng.random() * 2 - 1) * math.exp(-t * 25))
        for half in range(2):  # hi-hat
            add(drums, start + int(half * beat * RATE / 2), int(0.03 * RATE),
                lambda t: 0.2 * (rng.random() * 2 - 1) * math.exp(-t * 120))
        add(bass, start, int(beat * RATE * 0.9),
            lambda t: 0.6 * math.sin(2 * math.pi * chord[0] / 2 * t) * math.exp(-t * 3))
        add(keys, start, int(beat * RATE * 0.5),
            lambda t: 0.18 * sum(math.sin(2 * math.pi * f * t) for f in chord) * math.exp(-t * 6))
        if b % 4 == 0:
            add(strings, start, int(4 * beat * RATE),
                lambda t: 0.12 * sum(math.sin(2 * math.pi * f * 2 * t) * (0.7 + 0.3 * math.sin(6 * t)) for f in chord))
        if b % 8 == 0:  # klokje om de twee maten
            add(others, start, int(1.5 * RATE), lambda t: 0.4 * math.sin(2 * math.pi * 1318.5 * t) * math.exp(-t * 3))

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        files = []
        for family, name, data in [(1, "drums", drums), (2, "bass", bass), (4, "keys", keys),
                                   (5, "pads", strings), (7, "bell", others)]:
            wav_path = tmp / f"{family}-{name}.wav"
            write_wav(wav_path, data)
            files.append((wav_path.name, wav_path))
        midi = tmp / "song.mid"
        make_demo_midi(midi, bpm, bars)
        files.append(("song.mid", midi))

        bar = 4 * beat
        names = ["Am", "F", "C", "G"]
        chords = [{"time": round(b * bar, 3), "chord": names[b % 4]} for b in range(bars)]
        (tmp / "chords.json").write_text(json.dumps(chords, indent=1))
        files.append(("chords.json", tmp / "chords.json"))

        lines = ["Hello from the demo", "Stems are playing now", "Guitar is from MIDI", "Everything in sync"]
        lyrics = []
        for i, text in enumerate(lines):
            start = i * 2 * bar
            words = text.split()
            lyrics.append({"time": round(start, 3), "text": text,
                           "words": [{"time": round(start + k * (2 * bar - 0.3) / len(words), 3), "text": w}
                                     for k, w in enumerate(words)]})
        (tmp / "lyrics.json").write_text(json.dumps(lyrics, indent=1))
        files.append(("lyrics.json", tmp / "lyrics.json"))

        write_merik(args.out, files)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    pack = sub.add_parser("pack", help="pack stems (and optional MIDI) into a .merik")
    pack.add_argument("out")
    pack.add_argument("--folder", help="folder with 1-drums.flac, 2-bass.wav, ...")
    pack.add_argument("--stem", action="append", help="FAMILY=file (repeatable; 1-7, 8 = click)")
    pack.add_argument("--midi", help="MIDI file with tempo, lyrics, chords (optional)")
    pack.add_argument("--click", help="click track audio (replaces the internal click)")
    pack.add_argument("--chords", help="chords.json (replaces the chords from the MIDI file)")
    pack.add_argument("--lyrics", help="lyrics.json (replaces the lyrics from the MIDI file)")
    pack.add_argument("--bpm", type=float, help="optional manifest value; used without MIDI")
    pack.add_argument("--sig", help="optional manifest value, e.g. 3/4; used without MIDI")
    pack.add_argument("--title")
    pack.add_argument("--offset", type=float, default=0.0, help="seconds the audio starts later than the MIDI")
    pack.set_defaults(func=cmd_pack)

    demo = sub.add_parser("demo", help="make a synthetic test song")
    demo.add_argument("out")
    demo.set_defaults(func=cmd_demo)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
