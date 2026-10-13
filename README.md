# Merik

**The live MIDI player built for the stage.**
Play your MIDI backing tracks through a SoundFont or your own sound modules, mix them on the fly, and keep every singer and player in the band in sync with lyrics and chords on any phone, tablet or laptop.

![Merik main window](docs/images/main-window.png)

Merik turns a folder of standard MIDI files into a gig-ready system: setlists, a live mixer, hardware control, and a lyrics-and-chords display that your band can open in a web browser. No cloud, no account, no latency from the internet. Everything runs on one machine on your own network.

---

## Why Merik

- **Made for performing.** Big, readable controls, instant song changes, a hardware-friendly workflow and a panic button that really silences everything.
- **Everyone sees the song.** Karaoke-style lyrics and a chord strip with slash chords and bass notes, in any browser on the same Wi-Fi.
- **Your sound, your gear.** Use the built-in SoundFont player, or send the music to hardware MIDI modules and software players with tight, hardware-style timing.
- **Fast to set up.** Drop in your MIDI files, drag them into setlists, press play.

---

## Features

### Playback and mixing
- **Built-in SoundFont player** powered by FluidSynth, with 256-voice polyphony and adjustable gain and reverb.
- **Eight instrument families** (Drums, Bass, Guitars, Keys, Strings, Winds, FX, Other) with a fader and an on/off switch each. Every MIDI channel is assigned to a family automatically from its instrument.
- **Per-channel switches** to mute a channel for one song or for the whole evening.
- **Normalize to the drum channel** so tracks from different sources sit at a similar level.
- **Transpose** by up to an octave in either direction, live. Drums are left alone and the chords follow along. Each song can remember its own transpose.
- **Pause, resume, seek and stop** without hanging notes, plus a **Panic** button.
- **Auto-play next song** in the setlist, or keep full manual control.

### Setlists that stay out of your way
- Any number of setlists, reorderable by drag and drop.
- Drag songs to reorder them, to move them to another setlist, or hold <kbd>Command</kbd> (<kbd>Ctrl</kbd> on Windows and Linux) to copy them. Select several songs at once.
- The same song can appear more than once.
- Right-click menus to rename (your own title, the file name is untouched), transpose, duplicate, copy, move, delete, or show a file in the Finder.
- Song length, setlist total time, and tags that show which songs have **lyrics** and which have **chords**. Missing files are flagged in red.
- Everything is remembered between sessions.

### Lyrics and chords
- Reads lyrics from standard MIDI files, including karaoke (KAR) and Midisoft files, with word-by-word highlighting and correct handling of accented characters.
- Finds chords in the places MIDI files actually hide them: **Yamaha XF** chord data, **Midisoft** chord lines, markers and text events. Slash chords and bass notes (such as F/A or C/E) are shown properly.
- Chords transpose together with the music.

### Remote display and control in your browser
- Open Merik's address on any phone, tablet or laptop on the same network and get a full-screen **lyrics and chord display** with a progress bar for the current chord, a countdown before the first line, and an adjustable text size.
- By default the page is **view only**, so the audience and the band can't change anything by accident.
- Add your secret key to the address and the same page also shows **Play / Pause, Stop, Next** and the **family mixer**.

![Merik web interface](docs/images/web-interface.png)

### Hardware: MIDI out, MIDI in and LEDs
- **MIDI out** to hardware modules, interfaces and virtual ports, or to the built-in SoundFont player. Each message is scheduled on the audio clock and handed to the system with its own timestamp, so fast runs and chords stay tight.
- **MIDI control in** for controllers such as the **Korg nanoKONTROL2**: faders set the family volumes, the R buttons switch families on and off, and the transport buttons play, stop, skip and panic.
- **Controller LEDs** show the state: Next and Stop lit, Play lit while playing and blinking while paused, R buttons lit while the family is on. (Set the nanoKONTROL2 LED mode to *External* in the Korg Kontrol Editor.)
- Every CC number can be changed in Settings.

![Merik MIDI settings](docs/images/settings-midi.png)

### Settings
- **General:** remote-control key and the playback options.
- **Audio:** SoundFont, output device, sample rate, buffer size, and the gain and reverb of the built-in sound.
- **Midi:** input, output, controller LEDs and all CC assignments.

![Merik audio settings](docs/images/settings-audio.png)

---

## Getting started

1. Start Merik and choose a General MIDI SoundFont (`.sf2`) under **Settings → Audio**.
2. Add your MIDI files with the **+** in the Songs header, or drag them onto the window.
3. Double-click a song to load it, then press **Play**.
4. For the lyrics display, press **Lyrics** in Merik, or open Merik's address (port 8888) on another device. **Settings → General** shows the full address.

You will need to bring your own SoundFont. Merik works with any General MIDI SoundFont in the `.sf2` format.

Want something to try first? The folder [`docs/demo`](docs/demo) has four small demo songs with invented lyrics and chords.

### Remote control

In **Settings → General** you can set a key. Then:

- `http://<address>:8888/` shows lyrics and chords (view only).
- `http://<address>:8888/?key=<your key>` also shows the transport buttons and the mixer.

Leave the key empty to make the web page strictly view-only. The key travels over your local network without encryption, so use this on a network you trust, not on the open internet.

---

## Building from source

Merik is written in C++20 with [JUCE](https://juce.com) for the interface and audio, [FluidSynth](https://www.fluidsynth.org) for the sound and [midifile](https://github.com/craigsapp/midifile) for reading MIDI files (included in `src/midifile`).

**macOS** (developed and tested on Apple silicon):

```sh
# Tools and the FluidSynth library
brew install cmake pkg-config fluid-synth

# Configure and build (JUCE is downloaded automatically the first time)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run
open build/Merik_artefacts/Merik.app
```

**Linux** (not yet tested):

```sh
sudo apt install build-essential cmake pkg-config libfluidsynth-dev \
    libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
    libxinerama-dev libxcursor-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requirements: a C++20 compiler, CMake 3.22 or newer, FluidSynth 2.2 or newer (found through `pkg-config`) and an internet connection for the first configure (it fetches JUCE).

Windows is not supported yet, because the remote-control server uses POSIX sockets.

### Project layout

| Folder | Contents |
|---|---|
| `src/` | Application, playback engine, parsers, web server |
| `include/` | Headers |
| `src/midifile/` | MIDI file library |
| `assets/` | Fonts and the application icon |
| `docs/` | Screenshots and demo songs |

---

## Credits

Merik uses [JUCE](https://juce.com), [FluidSynth](https://www.fluidsynth.org), [midifile](https://github.com/craigsapp/midifile) by Craig Stuart Sapp and the Noto Sans typeface. Their licenses apply to their respective parts.

Website: <https://merik.eriknie.synology.me>

Copyright © Erik Nie. All rights reserved.
