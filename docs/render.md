# Rendering MIDI to audio

Rendering turns a MIDI file into a **`.merik`** file with real audio: one stem per family (drums, bass, guitars, keys, strings, winds, others). You get the sound you like, you can give every family its own output, and the result plays on any computer that runs Merik, without plug-ins, a hardware module or a SoundFont.

Merik does this **one family at a time**, so that you can mix, mute or route the families afterwards. A family without notes is skipped. The lyrics, chords, tempo and click of the MIDI file stay in the `.merik`.

Everything starts at **Settings, Tools, Render MIDI files...** There you choose the **Sound source**:

| | Sound source | Needs | Speed |
|---|---|---|---|
| **1** | Merik's SoundFont | nothing but a SoundFont | much faster than real time |
| **2** | A standalone audio app (for example SoundCanvas) | a virtual MIDI port and a virtual audio cable | real time |
| **3** | A plug-in in a host (Logic, MainStage, Bidule) | the same | real time |
| **4** | A hardware synth or module | a MIDI interface and an audio input | real time |

Choose one or more MIDI files and an output folder, and press **Start rendering**. A progress screen shows the song, the pass and the time left; **Stop** ends it after cleaning up. If a result with the same name exists, Merik asks before it overwrites it.

Options for all methods:

- **Keep the WAV files in a folder next to the .merik** (a folder `songname_wav`), for use in a DAW.
- **Convert to FLAC inside the .merik**: about half the size, lossless.
- **Demo: only the first 30 seconds of every track**: a quick way to hear how a sound source sounds. The result is called `Song (demo 30s).merik` so it never replaces a full render.

---

## 1. Merik's own SoundFont

The fastest way, and no hardware or other software is needed.

1. Choose a SoundFont under **Settings, Soundfont** (if you have not yet) and set the gain and reverb the way you like them. Those settings are used for the render.
2. **Settings, Tools, Render MIDI files...**, **Sound source: Merik's SoundFont**.
3. Choose the MIDI files and the output folder, and start.

Merik renders every family as fast as the computer can, without playing anything aloud (on an Apple silicon Mac about 30 times faster than real time: a 96-second song in five families took 14 seconds), and the audio starts exactly where the MIDI starts, so no delay measurement is needed. The click is not in the stems; the `.merik` plays it from the MIDI file.

The *Normalize* option (Settings, General) applies to the render as well. If a family clips, the log says so: lower the SoundFont gain and render again.

**From the command line** (macOS; for many files at once):

```sh
/Applications/Merik.app/Contents/MacOS/Merik --render-internal ~/Sounds/MySoundFont.sf2 ~/Rendered ~/Midi/*.mid
```

The first argument is the SoundFont, the second the output folder, then the MIDI files; add `--demo` for the 30-second version. It makes the `.merik` files without opening a window and uses the default sound settings.

## 2. A standalone audio app (SoundCanvas and similar)

Many software synths come as a standalone app that plays MIDI through its own sound engine, for example the Roland **Sound Canvas** (the standalone version). Merik sends it the MIDI and records its audio through a virtual audio device.

1. **Virtual MIDI port.** macOS: *Audio MIDI Setup*, *Window, Show MIDI Studio*, double-click **IAC Driver** and tick *Device is online*. Windows: a virtual port such as loopMIDI (the Windows tools have not been tried here).
2. **Virtual audio device.** macOS: [BlackHole](https://existential.audio/blackhole/) (2 channels is enough, it is free). Windows: VB-CABLE or Voicemeeter.
3. **In the app:** choose the virtual MIDI port as its MIDI input and **BlackHole** as its audio output. Give the app and the virtual device the same sample rate as your audio device. Set it up for General MIDI if your songs are General MIDI.
4. **In Merik** (Sound source: *External module or software synth*): *MIDI output* = the virtual port, *Audio input device* = **BlackHole 2ch**, *Input channels* = 1 + 2.
5. **Test levels and measure delay...**: play the test sound and set the level so that the peaks stay around -6 dBFS, then measure the delay. Merik stores the delay in the `.merik`.
6. Choose the files and start.

The details, with every option, are in [Rendering through an external module](render-external-module.md).

## 3. A plug-in in a host (Logic, MainStage, Bidule)

The same method, with a host that runs your plug-in (a VST or Audio Unit, for example the Sound Canvas VA, Kontakt or Omnisphere) instead of a standalone app.

1. Install the virtual MIDI port and the virtual audio device, as above.
2. In the host: set the **MIDI input** to the virtual port, load the instrument(s) that should answer on the MIDI channels of your songs (a General MIDI style multi-timbral setup for a normal song file), and send the **audio output** to **BlackHole**. Use the same sample rate everywhere.
3. In Merik: MIDI output = the virtual port, Audio input device = BlackHole, then test the levels, measure the delay and start.

Why render when you can play the plug-in live? Rendering gives every family its own output and volume in Merik, is lighter on the computer and safer on stage (no plug-ins running live, so fewer chances of a crash or a dropout), and the result is not tied to the computer or the operating system that your plug-in licence is.

## 4. A hardware synth or module

Any sound module or keyboard with a MIDI input and an audio output works: a SoundCanvas, a Yamaha Genos or MU2000, a Kentron, a TG100. Merik does not need to know which one it is.

1. Connect **MIDI out** of your interface to the MIDI input of the device, and its **audio out** to an input of your audio interface or digital mixer (for example channels 11 and 12 of a USB mixer).
2. In Merik: *MIDI output* = the interface, *Audio input device* = the same device (the default) or another one, *Input channels* = the pair (stereo) or one channel (mono).
3. **Test levels and measure delay...** Set the output level of the device, or the input gain, so that the peaks sit around -6 dBFS, then measure the delay.
4. Set the device to the mode in which your songs sound right (General MIDI for most song files). *Send a General MIDI reset before every pass* does that for you with a hardware module. Leave it off for a software synth: some of them lose the drums after a reset.
5. Choose the files and start.

On macOS the first time you will be asked to allow Merik to use the **microphone**; that is how macOS names any audio input.

A rendering takes as long as the song, per family (five families of a four-minute song take about 25 minutes). Leave the computer alone while it runs, and do not change the audio settings.

---

## After rendering

Add the `.merik` files to a setlist like any other song ([Playing audio files in setlists](play-audio.md)). Because the lyrics and chords of the MIDI file are in the `.merik`, the web page shows them as before. Want to keep some families as MIDI and replace only others? See [Mixed playback](play-mixed.md).
