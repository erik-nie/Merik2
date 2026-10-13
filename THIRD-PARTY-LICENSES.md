# Third-party software

Merik is licensed under the GNU Affero General Public License v3.0 (see `LICENSE`).
It uses and, in the macOS download, includes the following components. Each
keeps its own license.

| Component | License | Notes |
|---|---|---|
| [JUCE](https://juce.com) | AGPL-3.0 (or a commercial JUCE license) | User interface, audio and MIDI I/O. Used under the AGPL. |
| [FluidSynth](https://www.fluidsynth.org) | LGPL-2.1-or-later | Built-in SoundFont synthesizer. Included as a separate dynamic library in `Merik.app/Contents/Frameworks` so it can be replaced. |
| FluidSynth's own dependencies (GLib, libsndfile, PortAudio and others) | LGPL, BSD and similar | Included in `Merik.app/Contents/Frameworks`. |
| [midifile](https://github.com/craigsapp/midifile) by Craig Stuart Sapp | BSD-2-Clause | MIDI file reading, in `src/midifile`. |
| [Noto Sans](https://fonts.google.com/noto) | SIL Open Font License 1.1 | Interface typeface, in `assets/fonts`. |

Source code for Merik: <https://github.com/erik-nie/Merik2>

The LGPL libraries come from the Homebrew distribution; their source code is
available from the respective projects and from Homebrew.
