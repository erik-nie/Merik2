# Rendering MIDI files through an external sound module

Merik can play your MIDI files through a hardware sound module (for example a Boss DS-330 or a Roland Sound Canvas), record the sound, and make a `.merik` with real audio stems. The recording is done **one family at a time** (drums, bass, guitars, keys, strings, winds, others), so every family becomes its own stem, which you can mix, mute or send to a separate output in Merik. The click track is generated from the MIDI file.

This is under **Settings → Tools → Render MIDI files with an external module**.

## What you need

- A MIDI output that reaches the module (a MIDI interface, for example a MIO into the DS-330) **or** a virtual MIDI port to a program on this computer (see *Rendering a software synth* below).
- The audio of the module going into an **input of an audio device**: for example a USB mixer such as the Soundcraft Signature 12 MTK, with the module on channels 11 and 12, or a virtual audio cable.
- On macOS the first time you will be asked to allow Merik to use the microphone. That is how macOS names any audio input. Allow it, or later in *System Settings → Privacy & Security → Microphone*.

## Steps

Open **Settings → Tools → Render MIDI files...**. A window with the settings opens; no scrolling is needed.

1. **MIDI output:** the output that goes to the module (or to the host program).
2. **Audio input device:** by default the same device as the audio output of Merik (right for a mixer or interface with in- and outputs). Choose another device, for example `BlackHole 2ch`, when the audio comes in on a different device.
3. **Input channels:** the channels the module is on. A stereo pair (for example `Input 11 + Input 12 (stereo)`) records stereo; a single channel records mono.
4. **Test levels and measure delay...** opens a window:
   - *Start test sound* plays a piano (MIDI channel 1) and drums (channel 10) at the highest velocity. Watch the meter and turn the level of the module (or the input gain on your mixer) until the peaks sit in the green zone, around **-6 dBFS**. Not higher: the loudest notes of a song must not clip. The advice under the meter tells you if it is too low, good or too high.
   - *Measure delay* plays ten short hits and measures how long it takes before the sound arrives back in the recording. The result (in ms) is saved and shown in the **Delay (ms)** field, where you can also change it by hand. Measure again when you change the audio buffer size or the audio device.
5. **MIDI files:** choose one or more MIDI files. Merik shows how many passes it will record and about how long that takes.
6. **Output folder:** where the `.merik` files (and, if you want, the WAV folders) go.
7. Options:
   - *Keep the WAV files in a folder next to the .merik*: a folder `songname_wav` with the WAV files, `song.mid` and `delay.json` stays next to the `.merik`. You can use these files in a DAW, or pack them again with *Create a .merik file from a folder*.
   - *Convert to FLAC inside the .merik*: about half the size, lossless.
   - *Send a General MIDI reset before every pass* (off by default): resets the sound source to a clean General MIDI state. Useful for a hardware module that may still be in another mode. Leave it off for a software synth: with some of them (for example KQ Sampei) the drums stay silent after a reset.
8. **Start rendering.** The window now shows the progress: *Song 2 of 5*, *Pass 3 of 6* with the name of the family, a progress bar, an estimate of the time left and a log. A `.merik` is always created per song. **Stop** ends the process after cleaning up (the song that was being recorded is discarded; finished songs stay). Closing the window while rendering asks whether to stop.

## What happens

- Each MIDI channel counts for the family of its instrument at that moment (channel 10 is always drums). A family that has **no notes** in the song is skipped, so a song with four families takes four passes.
- A pass takes as long as the song (plus about 4 seconds at the start and the end), because the module plays in real time. Five passes of a four-minute song take about 25 minutes.
- Merik first sends the program changes and volumes, then the notes, exactly as in the file. At the end every note is switched off. System exclusive messages from the MIDI file are not sent (only the optional General MIDI reset).
- The WAV files are 24-bit at the sample rate of the audio device. They start at the same moment as the MIDI file. A pass that is silent or that clipped is reported in the log.
- The delay you measured is written to `delay.json` in the `.merik` (see [merik-format.md](merik-format.md#delayjson-optional)), so that during playback the audio is played earlier by that amount and stays together with the MIDI and the click.

## Good to know

- Leave the computer alone while rendering. Do not change the audio settings or the buffer size, and do not start other audio.
- Reverb and chorus of the module are recorded with each family, as the module makes them.
- The module must be set to the mode in which the songs sound right (for example General MIDI). With the reset option on, Merik sends a General MIDI reset.
- If there is no signal: check the MIDI cable, the module's audio output, the input channels in Merik, and the level. The test window shows the signal live.

## Rendering a software synth (MainStage, Logic, Bidule, a standalone plug-in)

The "module" does not have to be hardware. A program on the same computer can play a software synth (a host such as MainStage, Logic or Plogue Bidule, or a plug-in as a standalone app), and Merik records its audio through a **virtual audio device**. Merik sends the MIDI over a **virtual MIDI port**.

**macOS**

1. Virtual MIDI port: open *Audio MIDI Setup*, choose *Window → Show MIDI Studio*, double-click **IAC Driver** and tick *Device is online*.
2. Virtual audio device: install [BlackHole](https://existential.audio/blackhole/) (2 channels is enough; it is free).
3. In the host: set the MIDI input to the IAC bus, load the instrument(s) that should answer on the MIDI channels of your songs (a General MIDI style multi-timbral setup for a normal song file), and send the audio output to **BlackHole**. Give the host and BlackHole the same sample rate as your normal audio device.
4. In Merik (Render MIDI files...): *MIDI output* = the IAC bus, *Audio input device* = **BlackHole 2ch**, *Input channels* = 1 + 2. Then *Test levels and measure delay...* and start.

**Windows** works the same way with similar free tools: a virtual MIDI port such as loopMIDI, a virtual audio cable such as VB-CABLE or Voicemeeter, and a host such as Plogue Bidule, Cantabile or a standalone plug-in host. (This has not been tried here.)

If you already have all that software, why not play it live? Rendering to a `.merik` has advantages:

- You can give every family its own audio output, volume and on/off in Merik (the live software would just give one sound).
- It is lighter on the computer, and safer on stage: no plug-ins running live, so fewer chances of a crash or a dropout.
- The licence of a plug-in or a host may be tied to one computer or one operating system. The rendered `.merik` plays on any computer that runs Merik.
