#include "MidiFileReader.h"
#include "LyricsParser.h"
#include "ChordParser.h"
#include "TempoMap.h"

#include "MidiFile.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

double getSongLengthSeconds(smf::MidiFile& midi)
{
    double result = 0.0;

    for (int track = 0;
         track < midi.getTrackCount();
         ++track)
    {
        for (int i = 0;
             i < midi.getEventCount(track);
             ++i)
        {
            result = std::max(
                result,
                midi[track][i].seconds);
        }
    }

    return result;
}

Song MidiFileReader::read(
    const std::string& filename,
    double sampleRate) const
{
    smf::MidiFile midi;

    if (!midi.read(filename))
        throw std::runtime_error(
            "Cannot read MIDI file: " + filename);

    midi.absoluteTicks();
    midi.sortTracks();

    // ------------------------------------------------------------------------
    // Lyrics
    //
    // LyricsParser bevat de volledige karaoke-informatie:
    //
    // - regelnummer
    // - woordgrenzen
    // - lettergrepen
    // - regelovergangen
    // ------------------------------------------------------------------------

    LyricsParser parser;

    LyricsData lyrics =
        parser.parse(midi);

    // Debug: regels
    for (const auto& line : lyrics.lines)
    {
        std::cout
            << line.startSeconds
            << " : "
            << line.text
            << '\n';
    }

    // Debug: eerste segmenten
    std::cout << "\nSegments:\n";

    int debugCount = 20;

    for (const auto& segment : lyrics.segments)
    {
        std::cout
            << "line="
            << segment.lineIndex

            << " startsNewWord="
            << segment.startsNewWord

            << " startsNewLine="
            << segment.startsNewLine

            << " endsWord="
            << segment.endsWord

            << " endsLine="
            << segment.endsLine

            << " text=["
            << segment.text
            << "]\n";

        if (debugCount-- == 0)
            break;
    }

    // ------------------------------------------------------------------------
    // Chords
    // ------------------------------------------------------------------------

    ChordParser chordParser;

    ChordData chordData =
        chordParser.parse(
            midi,
            getSongLengthSeconds(midi));

    for (const auto& chord : chordData.chords)
    {
        std::cout
            << chord.startSeconds
            << " "
            << chord.label
            << " source="
            << ChordParser::sourceName(chord.source)
            << " raw=["
            << chord.rawText
            << "]"
            << std::endl;
    }

    // ------------------------------------------------------------------------
    // Song
    // ------------------------------------------------------------------------

    Song song;

    song.sourceFile = filename;
    song.sampleRate = sampleRate;
    song.ticksPerQuarterNote =
        midi.getTicksPerQuarterNote();

    // ------------------------------------------------------------------------
    // Tempo map
    // ------------------------------------------------------------------------

    TempoMap tempoMap(
        song.ticksPerQuarterNote);

    for (int track = 0;
         track < midi.getTrackCount();
         ++track)
    {
        for (int index = 0;
             index < midi[track].getSize();
             ++index)
        {
            const auto& event =
                midi[track][index];

            if (event.isTempo())
            {
                tempoMap.addTempo(
                    event.tick,
                    event.getTempoBPM());
            }
        }
    }

    tempoMap.finalise();

    song.tempoMap =
        tempoMap.points();

    // ------------------------------------------------------------------------
    // Playback events
    // ------------------------------------------------------------------------

    for (int track = 0;
         track < midi.getTrackCount();
         ++track)
    {
        for (int index = 0;
             index < midi[track].getSize();
             ++index)
        {
            const auto& event =
                midi[track][index];

            const auto seconds =
                tempoMap.tickToSeconds(
                    event.tick);

            const auto sample =
                tempoMap.tickToSample(
                    event.tick,
                    sampleRate);

            RawMidiEvent output;

            output.tick =
                event.tick;

            output.seconds =
                seconds;

            output.samplePosition =
                sample;

            output.sourceTrack =
                track;

            output.bytes.assign(
                event.begin(),
                event.end());

            song.playbackEvents.push_back(
                std::move(output));
        }
    }

    // ------------------------------------------------------------------------
    // Lyrics
    //
    // BELANGRIJK:
    //
    // Gebruik hier de resultaten van LyricsParser.
    //
    // Niet opnieuw event.getMetaContent() uitlezen.
    // Anders verliezen we lineIndex / word boundaries.
    // ------------------------------------------------------------------------

    for (const auto& segment : lyrics.segments)
    {
        LyricEvent lyric;

        lyric.tick =
            segment.tick;

        lyric.seconds =
            segment.timeSeconds;

        lyric.samplePosition =
            tempoMap.tickToSample(
                segment.tick,
                sampleRate);

        lyric.sourceTrack =
            segment.track;

        lyric.lineIndex =
            segment.lineIndex;

        lyric.startsNewWord =
            segment.startsNewWord;

        lyric.startsNewLine =
            segment.startsNewLine;

        lyric.endsWord =
            segment.endsWord;

        lyric.endsLine =
            segment.endsLine;

        lyric.text =
            segment.text;

        song.lyrics.push_back(
            std::move(lyric));
    }

    // ------------------------------------------------------------------------
    // Chords
    //
    // Kopieer de uniforme ChordParser-resultaten naar Song.
    // ------------------------------------------------------------------------

    for (const auto& parsedChord : chordData.chords)
    {
        SongChordEvent chord;

        chord.tick =
            parsedChord.tick;

        chord.seconds =
            parsedChord.startSeconds;

        chord.endSeconds =
            parsedChord.endSeconds;

        chord.samplePosition =
            tempoMap.tickToSample(
                parsedChord.tick,
                sampleRate);

        chord.sourceTrack =
            parsedChord.track;

        chord.label =
            parsedChord.label;

        song.chords.push_back(
            std::move(chord));
    }
    // ------------------------------------------------------------------------
    // Sorteren
    // ------------------------------------------------------------------------

    const auto eventOrder =
        [](const auto& a, const auto& b)
        {
            if (a.samplePosition !=
                b.samplePosition)
            {
                return a.samplePosition <
                       b.samplePosition;
            }

            return a.sourceTrack <
                   b.sourceTrack;
        };

    std::stable_sort(
        song.playbackEvents.begin(),
        song.playbackEvents.end(),
        eventOrder);

    std::stable_sort(
        song.lyrics.begin(),
        song.lyrics.end(),
        eventOrder);

    std::stable_sort(
        song.chords.begin(),
        song.chords.end(),
        eventOrder);

    return song;
}