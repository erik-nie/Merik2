# Mixed playback: MIDI and audio together

A `.merik` file can hold audio for some families and leave the other families to the MIDI file. Merik plays both together, in time, and one set of family faders controls them all.

**The rule is simple: audio wins per family.** A family that has an audio stem does not play its MIDI notes; a family without a stem keeps playing from the MIDI file through the SoundFont (or your external module).

## Examples

- **A live vocal track on a MIDI backing.** Keep the MIDI file for the band and add `7-vocal.mp3` (family 7, Others). The vocal comes from the recording, the band from the SoundFont.
- **Real drums, MIDI for the rest.** Add `1-drums.flac`: the drum channel of the MIDI file stays silent and the recorded drums play instead.
- **Your best sound for the parts that matter.** Render only the families whose sound the SoundFont does not do justice (for example the piano from your favourite plug-in) and leave the rest in MIDI. See [Rendering MIDI to audio](render.md).
- **A full recording with your own lyrics.** All stems as audio, and a `song.mid` with only the lyrics, chords and tempo.

## How to build one

1. Make a folder with the MIDI file (`song.mid`, or any name; Merik renames it) and the stems you want: `7-vocal.mp3`, `1-drums.wav`, ...
2. **Settings, Tools, Create a .merik file from a folder...**, or zip them by hand ([merik-format.md](merik-format.md#making-a-merik)).

Keep the **original order of the families**. If a bass player joins live, you switch family 2 off, so don't put backing vocals there. Extra tracks belong in **7 (Others)**.

## What you see and hear

- Every family button shows **MIDI** or **AUDIO** next to its level meter, so you can see where each sound comes from.
- The **time of the audio** and the MIDI file are the same. If your audio comes from a recording with a fixed delay (a hardware module, for example), a `delay.json` in the file plays the audio earlier by that amount. Merik writes this file itself when it records a module.
- **Outputs:** each family can go to its own output under **Settings, Audio**. The MIDI families all go to the main output (or to the MIDI out).
- **Transpose** is switched off when a song has audio stems.
- **Volume per song** works on the audio stems and on the SoundFont.
- The **click** comes from the tempo of the MIDI file or the `tempo.json`, or from your own `click.wav`.

## Next

- [Rendering MIDI to audio](render.md)
- [The .merik format](merik-format.md)
