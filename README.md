# Merik

[![Downloads](https://img.shields.io/github/downloads/erik-nie/Merik2/total?label=downloads&color=1876ff)](https://github.com/erik-nie/Merik2/releases)
[![Latest release](https://img.shields.io/github/v/release/erik-nie/Merik2?include_prereleases&label=release&color=1876ff)](https://github.com/erik-nie/Merik2/releases)
[![License: AGPL v3](https://img.shields.io/badge/license-AGPL%20v3-blue)](LICENSE)
[![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-support%20Merik-ff5e5b?logo=ko-fi&logoColor=white)](https://ko-fi.com/eriknie)

**The live MIDI player built for the stage.**
Play your MIDI backing tracks through a SoundFont or your own sound modules, mix them on the fly, and keep every singer and player in the band in sync with lyrics and chords on any phone, tablet or laptop.

<p align="center">
  <img src="docs/images/main-window.png" width="680" alt="Merik main window">
</p>

Merik turns a folder of standard MIDI files into a gig-ready system: setlists, a live mixer, hardware control, and a lyrics-and-chords display that your band can open in a web browser. No cloud, no account, no latency from the internet. Everything runs on one machine on your own network.

---

## Download

**[Download Merik 0.5.1](https://github.com/erik-nie/Merik2/releases/tag/v0.5.1)** for macOS or Windows. All releases are listed on the [releases page](https://github.com/erik-nie/Merik2/releases).

| System | File | Requirements |
|---|---|---|
| **macOS, Apple silicon** (M1 and newer) | `Merik-0.5.1-arm64.dmg` | macOS 14 or later |
| **macOS, Intel** | `Merik-0.5.1-intel.dmg` | Intel Mac, macOS 15 or later |
| **Windows** | `Merik-0.5.1-windows-x64.zip` | Windows 10 or 11, 64-bit |

**Install on macOS** (both versions)

1. Open the DMG and drag **Merik** onto **Applications**.
2. The app is **not notarized**, so macOS blocks the first start. Right-click **Merik.app**, choose **Open** and confirm. If it is still blocked, run this in Terminal:
   ```sh
   xattr -dr com.apple.quarantine /Applications/Merik.app
   ```

**Install on Windows**

1. Extract the zip to a folder and run `Merik.exe`.
2. The build is not code-signed, so SmartScreen may warn about an unknown app. Click **More info**, then **Run anyway**.
3. Allow Merik on private networks when Windows asks, to use the lyrics and remote control web page.

**First start (both systems):** choose a General MIDI SoundFont (`.sf2`) under **Settings → Soundfont**. Merik does not include one. `MatrixSF_v2.1.5.sf2` is a great free option, but many others exist.

Prefer to build it yourself? See [Building from source](#building-from-source).

---

## Support Merik

Merik is free and made in spare time. If it helps your band or your gigs, a coffee keeps the development going:

<p align="center">
  <a href="https://ko-fi.com/eriknie" target="_blank"><img height="40" style="border:0px;height:40px;" src="https://storage.ko-fi.com/cdn/kofi3.png?v=3" alt="Support Merik on Ko-fi"></a>
</p>

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
- **Eight families** (seven instrument families plus the click) with a fader and an on/off switch each: balance the whole band with eight controls instead of sixteen channels. See [How the family mixer works](#how-the-family-mixer-works).
- **Click track** generated from the tempo and time signature of the song, sample-accurate with the music. Send it to its own output (for example the drummer's in-ear mix) while the audience only hears the band. Volume and on/off live on family 8.
- **Audio stems** in a `.merik` file next to or instead of the MIDI parts: play a recorded band, mix MIDI and audio per family, or add a live vocal track. See [Audio stems (.merik)](#audio-stems-merik).
- **Per-channel switches** to mute a channel for one song or for the whole evening.
- **Normalize to the drum channel** so tracks from different sources sit at a similar level.
- **Transpose** by up to an octave in either direction, live. Drums are left alone and the chords follow along. Each song can remember its own transpose.
- **Pause, resume, seek and stop** without hanging notes, plus a **Panic** button.
- **Auto-play next song** in the setlist, or keep full manual control.

### How the family mixer works

A MIDI file has up to 16 channels, each playing its own instrument, and every file lays them out differently. Instead of making you chase 16 channel faders, Merik **groups the channels by the kind of instrument** into eight *families*. Each family has one fader and one on/off switch, at the top of the main window.

**Channels join a family automatically.** Merik looks at the instrument (the General MIDI program) that each channel plays and puts the channel in the matching family. If a song changes instrument halfway, the channel moves with it. The table at the bottom of the main window shows, per channel, the instrument and the family it belongs to.

| Family | What goes in |
|---|---|
| **1 Drums** | MIDI channel 10, the percussion channel |
| **2 Bass** | Basses (GM programs 33–40) |
| **3 Guitars** | Guitars (25–32) |
| **4 Keys** | Pianos, chromatic percussion and organs (1–24), synth leads and pads (81–96), ethnic and percussive instruments (105–120) |
| **5 Strings** | Strings and string ensembles (41–56) |
| **6 Winds** | Brass, reeds and pipes (57–80) |
| **7 Others** | Synth effects (97–104) and sound effects (121–128), and the place for extra audio stems such as a live vocal |
| **8 Click** | The click track: its fader is the click volume and its switch turns the click on or off. No MIDI channels are placed here. |

**What the controls do**
- The **fader** (0–127) scales the volume of every channel in the family, live. The volume written in the file is the starting point, so a fader at 64 plays the family at about half of what the file says. This works for the built-in sound and for external MIDI devices alike.
- The **name button** above the fader (or the **R button** on a controller) switches the whole family on or off. A switched-off family keeps playing silently, so it comes back instantly and in time.
- In the channel table, the **Active** column fine-tunes a single channel: a click cycles *green* (on), *orange* (off for this song only, back on for the next song) and *red* (off until you change it).
- **Normalize** can level the channels against the drum channel, so files that were mixed differently start from a similar balance.

**Typical uses**
- Playing with a live drummer? Switch the Drums family off.
- A singer wants space? Pull the Winds or Keys fader down, or switch that family off for the song.
- A guitarist wants to play the solo? Mute just the guitar channel for this song with the orange state.

The same eight faders and switches (the last one is the click) are available on a MIDI controller such as the nanoKONTROL2 and, with the remote key, on the web page.

### Audio stems (.merik)

A **.merik** file lets a song carry recorded audio next to (or instead of) the MIDI parts. It is a normal zip file; there is nothing to configure or to write. Merik recognises the files by their **name**, and whatever is missing falls back to the MIDI file or to Merik's own click.

```
My Song.merik
├── song.mid            MIDI: tempo, time signature, lyrics, chords, MIDI parts
├── 1-drums.flac        audio stems:  <family number>-<name>.<wav|flac|mp3>
├── 2-bass.flac
├── 7-void.mp3          Others: a live vocal, a choir, backing vocals...
├── click.wav           your own click track (optional)
├── chords.json         chords (optional)
└── lyrics.json         lyrics (optional)
```

**Naming of stems: `number-name`.** The number (1–7, and 8 for the click) decides the family, the name is shown on the family button (first 8 characters): `1-drums.flac` shows **1 drums**, `7-void.mp3` shows **7 void**. A file called just `drums.wav` is accepted too (family by name: drums, bass, guitars, keys, strings, winds, others, click), but `number-name` is recommended. Stems can also live in a `stems/` folder. Several stems in one family are mixed together.

> **Keep the original order of the families.** A family fader or switch may have been set for the whole evening: if a bass player joins live, the Bass family (2) is switched off, so don't put backing vocals on 2. Put extra tracks in **7 (Others)**, which is meant for that.

**What is used, in order of priority**

| What | If the file exists | Otherwise |
|---|---|---|
| A family's sound | the audio stem(s) of that family (**audio wins**, the MIDI notes of that family are not played) | the MIDI parts of that family |
| Click | `click.wav` / `.flac` / `.mp3` | the internal click, from tempo and time signature |
| Chords | `chords.json` | the chords found in the MIDI file |
| Lyrics | `lyrics.json` | the lyrics found in the MIDI file |
| Tempo and time signature | the MIDI file | 120 bpm, 4/4 (or an optional `manifest.json`) |

So these are all valid: a song that is only MIDI; a song that is only stems; MIDI plus one live-vocal stem `7-void.mp3`; MIDI for the guitar while the other families come from stems (a hybrid).

**Examples**
- *Karaoke version with stems from the internet:* `1-drums.mp3`, `2-bass.mp3`, `4-keys.mp3`, `7-others.mp3` and the `song.mid` for lyrics and chords.
- *Your band's backing track from a DAW:* export each family as a FLAC with the same start point, add `click.wav` and the MIDI file, and zip them.
- *A MIDI song with a live vocal stem:* `song.mid` and `7-void.mp3`.

**Fast loading, low memory:** when the audio is stored without compression in the zip, Merik streams the stems from disk: the song is ready in a fraction of a second and uses about 17 MB per stem instead of the whole song. Compressed zips are loaded completely, with a progress screen. See [Making a .merik](#making-a-merik-from-a-set-of-audio-files).

Each family can be sent to **its own output** (Settings → Audio), as a stereo pair or a single mono channel, and the click to another. Each family button shows **AUDIO** or **MIDI** with a level meter, and the song list tags the file with **AUDIO** and/or **MIDI**. Transpose is not available for songs with stems.

### Making a .merik from a set of audio files

**Let Merik do it.** In **Settings → General → Create a .merik file from a folder...**:

1. Choose the folder with your stems (`1-drums.wav`, `2-bass.flac`, ..., optionally `song.mid`, `click.wav`, `chords.json`, `lyrics.json`).
2. Merik shows what it recognised and what it ignored, and warns about stems of different lengths, a missing MIDI file or a missing tempo.
3. Tick *Convert WAV to FLAC* if you want (lossless, about half the size).
4. Choose a name and Merik saves the `.merik`, without compression, so it loads instantly. Afterwards you can add it straight to the current setlist.

This is **the recommended way**: it is much more stable than doing it by hand, because Merik checks the file names, compares the stem lengths, converts WAV to FLAC without loss and stores the zip the right way, which removes the common mistakes (a compressed zip, an extra folder, a wrongly named stem).

A ready-made test song is in [docs/demo/Stems Demo.merik](docs/demo/Stems%20Demo.merik). The complete description (lyrics and chord JSON formats, the optional manifest, stem requirements) is in [docs/merik-format.md](docs/merik-format.md).

#### The manual way (advanced)

For the command line, scripts, or to see what happens inside: a `.merik` is just a zip. Doing it by hand works, but it is easier to get wrong: one wrong flag or file name and the song loads slowly or a stem is not recognised. Prefer the automatic way above.

The one thing that matters: **store the audio without compression** (`zip -0`). Audio files (wav, flac, mp3) are already compressed or do not shrink much, and a stored zip is the only kind Merik can **stream**: the song is ready in a fraction of a second and uses very little memory. A normal compressed zip has to be unpacked completely first, which is slow.

Put the stems in a folder, named `1-drums.wav`, `2-bass.wav`, ... (see naming above), optionally with `song.mid`, `click.wav`, `chords.json`, `lyrics.json`.

**macOS** (Terminal, `cd` to the folder):

```sh
zip -0 -j "songname.merik" *.wav song.mid
# or, when there is only audio:
zip -0 -j "songname.merik" *.wav
```

`-0` means "store, don't compress" and `-j` leaves out the folder name. This takes a second or two and the result loads almost instantly. **Don't use right-click → Compress in Finder**: it compresses the audio (slow to make, slow to load, Merik can't stream it) and adds an extra folder.

**Windows** (PowerShell, in the folder). The simplest is the `tar` that ships with Windows 10 and 11:

```powershell
tar --format zip --options zip:compression=store -cf "songname.merik" *.wav song.mid
```

or with PowerShell's own zip command (it needs the name `.zip` first):

```powershell
Compress-Archive -Path *.wav,song.mid -CompressionLevel NoCompression -DestinationPath songname.zip
Rename-Item songname.zip songname.merik
```

or with [7-Zip](https://www.7-zip.org/): `7z a -tzip -mx=0 songname.merik *.wav song.mid`. Don't use right-click → *Send to → Compressed folder* in Explorer: that compresses the audio.

(Leave out `song.mid` if you have none. The Windows commands were not tested on a Windows machine; the `tar` one works the same way as the macOS `tar`.)

**Converting WAV to FLAC** (about half the size, lossless):

macOS: install ffmpeg once with [Homebrew](https://brew.sh), then convert everything in the folder:

```sh
brew install ffmpeg
for f in *.wav; do ffmpeg -i "$f" -c:a flac "${f%.wav}.flac"; done
```

Windows: install ffmpeg once (`winget install ffmpeg`, then open a new PowerShell), then:

```powershell
Get-ChildItem *.wav | ForEach-Object { ffmpeg -i $_.Name -c:a flac ($_.BaseName + ".flac") }
```

Then pack the `.flac` files as above (`zip -0 -j "songname.merik" *.flac song.mid`).

The included script does the same in one go: `python3 scripts/make-merik.py pack "songname.merik" --folder stems/ --midi song.mid`.


### Setlists that stay out of your way
- Any number of setlists, reorderable by drag and drop.
- Drag songs to reorder them, to move them to another setlist, or hold <kbd>Command</kbd> (<kbd>Ctrl</kbd> on Windows and Linux) to copy them. Select several songs at once.
- The same song can appear more than once.
- Right-click menus to rename (your own title, the file name is untouched), transpose, duplicate, copy, move, delete, or show a file in the Finder.
- Song length, setlist total time, and tags that show which songs have **lyrics**, **chords**, **MIDI** and **audio**. Missing files are flagged in red.
- Everything is remembered between sessions.

### Lyrics and chords
- Reads lyrics from standard MIDI files, including karaoke (KAR) and Midisoft files, with word-by-word highlighting and correct handling of accented characters.
- Finds chords in the places MIDI files actually hide them: **Yamaha XF** chord data, **Midisoft** chord lines, markers and text events. Slash chords and bass notes (such as F/A or C/E) are shown properly.
- Chords transpose together with the music.

### Remote display and control in your browser
- Open Merik's address on any phone, tablet or laptop on the same network and get a full-screen **lyrics and chord display** with a progress bar for the current chord, a countdown before the first line, and an adjustable text size.
- By default the page is **view only**, so the audience and the band can't change anything by accident.
- Add your secret key to the address and the same page also shows **Play / Pause, Stop, Next** and the **family mixer**.

<p align="center">
  <img src="docs/images/web-interface.png" width="620" alt="Merik lyrics and chords in the browser">
</p>

### Hardware: MIDI out, MIDI in and LEDs
- **MIDI out** to hardware modules, interfaces and virtual ports, or to the built-in SoundFont player. Each message is scheduled on the audio clock and handed to the system with its own timestamp, so fast runs and chords stay tight.
- **MIDI control in** for controllers such as the **Korg nanoKONTROL2**: faders set the family volumes, the R buttons switch families on and off, and the transport buttons play, stop, skip and panic.
- **Controller LEDs** show the state: Next and Stop lit, Play lit while playing and blinking while paused, R buttons lit while the family is on. (Set the nanoKONTROL2 LED mode to *External* in the Korg Kontrol Editor.)
- Every CC number can be changed in Settings.

<p align="center">
  <img src="docs/images/settings-midi.png" width="400" alt="Merik MIDI settings">
</p>

### Settings
- **General:** remote-control key, the playback options, and the update check. Merik can tell you at startup when a newer version is available on GitHub, and you can switch that off or check by hand.
- **Soundfont:** the SoundFont, and the gain and reverb of the built-in sound.
- **Audio:** output device, sample rate and buffer size, the main output (a stereo pair or one channel), the click output (any other channel or pair), and an output per audio-stem family.
- **Midi:** input, output, controller LEDs and all CC assignments.

<p align="center">
  <img src="docs/images/settings-audio.png" width="400" alt="Merik audio settings">
</p>

---

## Getting started

1. Start Merik and choose a General MIDI SoundFont (`.sf2`) under **Settings → Audio**.
2. Add your MIDI files with the **+** in the Songs header, or drag them onto the window.
3. Double-click a song to load it, then press **Play**.
4. For the lyrics display, press **Lyrics** in Merik, or open Merik's address (port 8888) on another device. **Settings → General** shows the full address.

You will need to bring your own SoundFont. Merik works with any General MIDI SoundFont in the `.sf2` format. `MatrixSF_v2.1.5.sf2` is a great free GM SoundFont to start with, but many others exist.

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

Requirements: a C++20 compiler, CMake 3.22 or newer, FluidSynth 2.2 or newer (found through its CMake package or `pkg-config`) and an internet connection for the first configure (it fetches JUCE).

**Windows** (alpha):

1. Install [Git for Windows](https://git-scm.com/download/win) and [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community is free) with the workload **Desktop development with C++**.
2. Install [vcpkg](https://github.com/microsoft/vcpkg) and set `VCPKG_ROOT`:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```
3. Open a new terminal in the Merik folder and build. vcpkg fetches and builds FluidSynth automatically (the first time this takes a while):
   ```powershell
   cmake --preset windows
   cmake --build --preset windows-release
   ```
   The result is in `build-windows\Merik_artefacts\Release`.

The remote-control server uses Winsock on Windows. MIDI out falls back to JUCE's portable output there, so the timing is less tight than the CoreMIDI path on macOS.

### Releases

Releases are built on GitHub. Pushing a version tag starts the **Release** workflow, which builds the macOS DMGs (Apple silicon and Intel) and the Windows zip and creates a draft release with the three files:

```sh
git tag -a v0.5.1 -m "Merik 0.5.1"
git push origin v0.5.1
```

The release notes come from `docs/release-notes/<version>.md`. You can also start the workflow by hand (Actions, Release, Run workflow) to build the files without making a release.

On a Mac you can build the DMG yourself with `scripts/make-release.sh` (it builds for the processor of that Mac). It bundles FluidSynth and its libraries into the app, signs it ad hoc and packs a DMG in `dist/` with an arrow to the Applications folder. It needs `dylibbundler` (`brew install dylibbundler`).

```sh
scripts/make-release.sh 0.5.1
```

For a Developer ID signature and notarization, set `SIGN_IDENTITY` and `NOTARY_PROFILE` (see the comments in the script). A release runs on the macOS version it was built on or newer, because the bundled libraries are built for that version; the script prints it.

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

Merik uses [JUCE](https://juce.com), [FluidSynth](https://www.fluidsynth.org), [midifile](https://github.com/craigsapp/midifile) by Craig Stuart Sapp and the Noto Sans typeface. Their licenses apply to their respective parts, see [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## License

Merik is free software, licensed under the **GNU Affero General Public License v3.0**. See [LICENSE](LICENSE) and [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

Copyright © Erik Nie.
