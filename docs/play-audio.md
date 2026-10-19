# Playing audio files in setlists

Merik can play recorded audio in the same setlists as your MIDI files: a full band recording, or a few stems. The audio goes into a **`.merik`** file, which is a zip file holding one audio file per family (drums, bass, ...) and, if you want, the MIDI file with the lyrics, chords and tempo. Merik recognises everything by the **file names**.

A ready-made test song is in [demo/Stems Demo.merik](demo/Stems%20Demo.merik).

## What goes in a .merik

```
My Song.merik
├── song.mid          tempo, lyrics and chords (optional)
├── 1-drums.flac      audio stems: <family number>-<name>.<wav|flac|mp3|aif>
├── 2-bass.flac
├── 3-guitars.flac
├── 4-keys.flac
├── 7-vocal.mp3       "Others": a live vocal, choir, backing vocals...
├── click.wav         your own click track (optional)
├── chords.json       chords (optional)
└── lyrics.json       lyrics (optional)
```

- The **number** is the family: 1 drums, 2 bass, 3 guitars, 4 keys, 5 strings, 6 winds, 7 others, 8 click. The **name** after it is shown on the family button (the first 8 characters).
- Only the stems you have are needed; families without a stem play from the MIDI file, if there is one (see [Mixed playback](play-mixed.md)).
- Keep the stems the same length and start them at the same moment.

## Make a .merik

1. Put your stems in a folder (`1-drums.wav`, `2-bass.wav`, ...), plus the MIDI file if you have it.
2. **Settings, Tools, Create a .merik file from a folder...** and choose the folder.
3. Merik shows what it recognised and warns about problems (different lengths, no MIDI or tempo). It can convert WAV to FLAC without loss (about half the size), saves the `.merik` the right way and can add it to your setlist.

Don't have audio yet? [Render your MIDI files](render.md) into `.merik` files with Merik's own SoundFont, or with your favourite synth. To make one by hand with `zip`, see [The .merik format](merik-format.md#making-a-merik).

## Play it

Add the `.merik` to a setlist like any song (drag it in, or **+**). The song shows an **AUDIO** tag; if it has a MIDI file as well, also **MIDI**.

- The family buttons show an **AUDIO** pill and a level meter. The faders and switches work like those for MIDI families.
- **Streaming:** stems stored without compression (which is how Merik saves them) are read from disk as the song plays, so the song is ready almost at once and uses very little memory. The window title shows the memory in use.
- **Outputs:** under **Settings, Audio, Audio stems: output per family** every family can have its own output, for example the drums to a separate channel on your mixer. The click can go to yet another output.
- **Click and beat counter:** with a MIDI file or a `tempo.json` inside, Merik makes its own click and shows the beat counter on the web page. With neither, there is no click of its own (add a `click.wav` or a `tempo.json`).
- **Transpose** is not available for songs with audio stems, because the audio cannot change key.

## Lyrics and chords for audio-only songs

Add a `lyrics.json` and a `chords.json` to the `.merik` (the formats are in [merik-format.md](merik-format.md#chordsjson)), or include a `song.mid` that has them. They show on the web page just like those of a MIDI song.

## Next

- [Mixed playback](play-mixed.md): MIDI and audio together.
- [The .merik format](merik-format.md): all the details.
