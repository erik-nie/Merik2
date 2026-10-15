# The .merik format (audio stems)

A `.merik` file is a normal **zip file** with a different extension. There is **no manifest to write**: Merik recognises the files by their name. Whatever is present is used; whatever is missing falls back to the MIDI file or to Merik's own click.

```
My Song.merik
├── song.mid          MIDI: tempo, time signature, lyrics, chords and MIDI parts
├── 1-drums.flac      audio stems (wav, flac, mp3), one or more per family
├── 2-bass.flac
├── 7-live vocals.wav   a second stem in the same family is mixed in
├── click.wav         your own click track (optional)
├── chords.json       chords (optional, replaces the chords from the MIDI file)
└── lyrics.json       lyrics (optional, replaces the lyrics from the MIDI file)
```

Stems may also be placed in a `stems/` folder inside the zip. Names are not case sensitive.

## What is used, in order

| What | If the file exists | Otherwise |
|---|---|---|
| A family's sound | the audio stem(s) of that family | the MIDI parts of that family (SoundFont or MIDI out) |
| Click | `click.wav` / `click.flac` / `click.mp3` | the internal click, generated from tempo and time signature |
| Chords | `chords.json` | chords found in the MIDI file |
| Lyrics | `lyrics.json` | lyrics found in the MIDI file |
| Tempo / time signature | the MIDI file | 120 bpm, 4/4 (or `manifest.json`, see below) |

So a hybrid song is normal: for example all MIDI plus one extra stem `7-live vocals.wav`. **Audio wins per family**: a family with an audio stem does not play its MIDI notes.

## File names

**Recommended: `number-name`**, for example `1-drums.flac`, `7-void.mp3`. The number (1–7, 8 = click) selects the family and the name is shown on the family button (the first 8 characters): `7-void.mp3` shows **7 void**. A family has one name; with several stems the first named one is shown.

A file with only a family name (`drums.wav`) is accepted as well, and then the family keeps its default name.

> Keep the original order of the families: the fader or switch of a family may already be set for the evening (for example Bass off because a bassist plays live). Put extra tracks such as backing vocals in **7 (Others)**.

A stem file is recognised by a number at the start (`1`–`8`) or by the family name:

| Family | Number | Name |
|---|---|---|
| Drums | 1 | `drums` |
| Bass | 2 | `bass` |
| Guitars | 3 | `guitars` |
| Keys | 4 | `keys` |
| Strings | 5 | `strings` |
| Winds | 6 | `winds` |
| Others | 7 | `others` |
| Click | 8 | `click` |

Examples: `1-drums.flac`, `drums.wav`, `7 live vocals.mp3`, `others_choir.wav`, `click.wav`. Extensions: wav, flac, mp3, aif/aiff.

## chords.json

Times are in seconds from the start of the song.

```json
[
  { "time": 0.0, "chord": "Am" },
  { "time": 2.4, "chord": "F" },
  { "time": 4.8, "chord": "C/E" }
]
```

## lyrics.json

One entry per line. `words` is optional; with words the lyrics light up word by word, without it a line is shown at once.

```json
[
  { "time": 10.0, "text": "Hello from the demo",
    "words": [ { "time": 10.0, "text": "Hello" }, { "time": 10.6, "text": "from" },
               { "time": 11.0, "text": "the" },   { "time": 11.3, "text": "demo" } ] },
  { "time": 14.8, "text": "A line without word times" }
]
```

## manifest.json (optional)

Only needed for things the file names cannot say:

```json
{
  "bpm": 100,
  "timeSignature": [4, 4],
  "durationSeconds": 215.0,
  "timing": { "audioOffsetSeconds": 0.0 },
  "stems": [ { "family": 2, "file": "bassline.wav", "gainDb": -1.5 } ]
}
```

- `bpm`, `timeSignature`, `durationSeconds`: only used when there is **no** MIDI file (for the click and beat LEDs).
- `timing.audioOffsetSeconds`: positive means the audio starts later than the MIDI.
- `stems`: gives a file with a free name to a family (1–8), and/or a level correction in dB.

## How it plays

- The family faders and R buttons (app, web, controller) control the stems. The label above each fader shows **AUDIO** or **MIDI**, with a level meter.
- **Output per family:** Settings → Audio → "Audio stems: output per family". Each family with audio can go to the main output (default), or to its own stereo pair or mono channel of the audio device (mono plays left and right together). The click has its own output setting.
- The click (family 8) follows the MIDI tempo and time signature, so it stays in sync with the stems **if the stems start exactly at the start of the song** (bar 1 at time 0).
- **Transpose** is not available for songs with stems.
- With an external MIDI output, the audio is delayed by the same 20 ms as the MIDI so they stay together.
- Stems are decoded to memory when the song is loaded (about 11 MB per minute per stereo stem at 48 kHz).

## Stem requirements

- All stems the same length and starting at time 0. Mono or stereo; Merik converts to the sample rate of the audio device.
- Prefer FLAC (lossless). MP3 works but has a start delay that can shift timing.

## Making a .merik

Use [scripts/make-merik.py](../scripts/make-merik.py) (needs only Python 3), or just zip the files yourself (store the audio without compression for fast loading):

```bash
python3 scripts/make-merik.py pack "My Song.merik" --folder stems/ --midi song.mid
python3 scripts/make-merik.py pack "My Song.merik" --stem 1=drums.wav --stem 7=vocals.wav --midi song.mid
python3 scripts/make-merik.py pack "My Song.merik" --folder stems/ --click click.wav --chords chords.json --lyrics lyrics.json
python3 scripts/make-merik.py pack "My Song.merik" --folder stems/ --bpm 96 --sig 4/4     # no MIDI
python3 scripts/make-merik.py demo "Stems Demo.merik"                                       # synthetic test song
```

A test song is included: [docs/demo/Stems Demo.merik](demo/Stems%20Demo.merik) (drums, bass, keys, strings and others as audio, a MIDI file whose guitar part plays through the SoundFont, plus `chords.json` and `lyrics.json`).
