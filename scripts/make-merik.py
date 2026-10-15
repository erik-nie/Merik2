#!/usr/bin/env python3
"""Maak een .merik-bestand (zip met manifest.json, optioneel song.mid en stems).

  make-merik.py pack  uitvoer.merik --folder stems/ [--midi song.mid] [--bpm 120] [--sig 4/4]
  make-merik.py pack  uitvoer.merik --stem 1=drums.wav --stem 2=bass.flac [--midi song.mid]
  make-merik.py demo  uitvoer.merik          (maakt een synthetisch testnummer)

Familienummers: 1 Drums, 2 Bass, 3 Guitars, 4 Keys, 5 Strings, 6 Winds, 7 Others.
Zie docs/merik-format.md.
"""

import argparse
import json
import math
import random
import re
import struct
import sys
import wave
import zipfile
from pathlib import Path

AUDIO = {".wav", ".flac", ".mp3"}


def write_merik(out, manifest, stems, midi_path=None):
    with zipfile.ZipFile(out, "w") as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2), zipfile.ZIP_DEFLATED)
        if midi_path:
            z.write(midi_path, "song.mid", zipfile.ZIP_DEFLATED)
        for name, path in stems:
            # FLAC en MP3 zijn al gecomprimeerd; WAV ook in de zip opslaan zonder compressie
            # houdt het openen snel.
            z.write(path, name, zipfile.ZIP_STORED)
    print(f"{out}: {len(stems)} stem(s)" + (", with MIDI" if midi_path else ", no MIDI"))


def cmd_pack(args):
    found = {}  # (family, bestand)
    if args.folder:
        for path in sorted(Path(args.folder).iterdir()):
            match = re.match(r"^([1-7])[-_ ]", path.name)
            if match and path.suffix.lower() in AUDIO:
                found[path] = int(match.group(1))
    for item in args.stem or []:
        family, _, name = item.partition("=")
        found[Path(name)] = int(family)
    if not found:
        sys.exit("No stems: use --folder (files like 1-drums.flac) or --stem 1=file.wav")

    stems, entries = [], []
    for path, family in found.items():
        if not 1 <= family <= 7:
            sys.exit(f"Family {family} is invalid (1-7)")
        name = f"stems/{path.name}"
        stems.append((name, path))
        entries.append({"family": family, "file": name, "gainDb": 0})

    num, den = (int(x) for x in args.sig.split("/"))
    manifest = {"format": 1, "title": args.title or Path(args.out).stem,
                "bpm": args.bpm, "timeSignature": [num, den],
                "timing": {"audioOffsetSeconds": args.offset}, "stems": entries}
    if not args.midi:
        manifest["durationSeconds"] = args.duration or 0
        if not args.duration:
            del manifest["durationSeconds"]  # Merik leest de lengte uit de langste stem
    write_merik(args.out, manifest, stems, args.midi)


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
        stems = []
        entries = []
        for family, name, data in [(1, "drums", drums), (2, "bass", bass), (4, "keys", keys),
                                   (5, "strings", strings), (7, "others", others)]:
            wav_path = tmp / f"{family}-{name}.wav"
            write_wav(wav_path, data)
            stems.append((f"stems/{wav_path.name}", wav_path))
            entries.append({"family": family, "file": f"stems/{wav_path.name}", "gainDb": 0})
        midi = tmp / "song.mid"
        make_demo_midi(midi, bpm, bars)
        manifest = {"format": 1, "title": "Stems Demo", "bpm": bpm, "timeSignature": [4, 4],
                    "timing": {"midi": "song.mid", "audioOffsetSeconds": 0.0}, "stems": entries}
        write_merik(args.out, manifest, stems, midi)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    pack = sub.add_parser("pack", help="pack stems (and optional MIDI) into a .merik")
    pack.add_argument("out")
    pack.add_argument("--folder", help="folder with 1-drums.flac, 2-bass.wav, ...")
    pack.add_argument("--stem", action="append", help="FAMILY=file (repeatable)")
    pack.add_argument("--midi", help="MIDI file with tempo, lyrics, chords (optional)")
    pack.add_argument("--bpm", type=float, default=120.0, help="only used without --midi")
    pack.add_argument("--sig", default="4/4", help="only used without --midi")
    pack.add_argument("--title")
    pack.add_argument("--offset", type=float, default=0.0, help="seconds the audio starts later than the MIDI")
    pack.add_argument("--duration", type=float)
    pack.set_defaults(func=cmd_pack)

    demo = sub.add_parser("demo", help="make a synthetic test song")
    demo.add_argument("out")
    demo.set_defaults(func=cmd_demo)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
