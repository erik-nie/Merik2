#include "MidiFileReader.h"
#include "LyricsParser.h"
#include "TextEncoding.h"
#include "ChordParser.h"
#include "SectionParser.h"
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

    // ------------------------------------------------------------------------
    // Chords
    // ------------------------------------------------------------------------

    ChordParser chordParser;

    ChordData chordData =
        chordParser.parse(
            midi,
            getSongLengthSeconds(midi));

    // for (const auto& chord : chordData.chords)
    // {
    //     std::cout
    //         << chord.startSeconds
    //         << " "
    //         << chord.label
    //         << " source="
    //         << ChordParser::sourceName(chord.source)
    //         << " raw=["
    //         << chord.rawText
    //         << "]"
    //         << std::endl;
    // }

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
    // Maatsoort (meta-event FF 58 04 nn dd cc bb)
    // ------------------------------------------------------------------------

    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        for (int index = 0; index < midi[track].getSize(); ++index)
        {
            const auto& event = midi[track][index];

            if (event.size() < 5 || event[0] != 0xff || event[1] != 0x58)
                continue;

            const int numerator = static_cast<int>(event[3]);
            const int exponent = static_cast<int>(event[4]);

            if (numerator < 1 || numerator > 32 || exponent > 5)
                continue;

            TimeSignaturePoint point;
            point.tick = event.tick;
            point.seconds = tempoMap.tickToSeconds(event.tick);
            point.numerator = numerator;
            point.denominator = 1 << exponent;
            song.timeSignatures.push_back(point);
        }
    }

    std::stable_sort(song.timeSignatures.begin(), song.timeSignatures.end(),
        [](const TimeSignaturePoint& a, const TimeSignaturePoint& b) { return a.tick < b.tick; });

    // Altijd een maatsoort vanaf het begin; zonder meta-event is dat 4/4.
    if (song.timeSignatures.empty() || song.timeSignatures.front().tick > 0)
        song.timeSignatures.insert(song.timeSignatures.begin(), TimeSignaturePoint {});

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

    // Bepaal per nummer of de tekst al UTF-8 is. Zo niet (meestal
    // Windows-1252), dan zetten we alle lyrics om zodat tekens als
    // é, ë en ç goed worden weergegeven.
    std::string allLyricText;
    for (const auto& segment : lyrics.segments)
        allLyricText += segment.text;

    const bool lyricsAreUtf8 =
        TextEncoding::isValidUtf8(allLyricText);

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
            lyricsAreUtf8
                ? segment.text
                : TextEncoding::windows1252ToUtf8(segment.text);

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
    // Songonderdelen (markers: "Intro", "Verse 1", "15@Refrain:", ...)
    // ------------------------------------------------------------------------

    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        for (int index = 0; index < midi[track].getSize(); ++index)
        {
            const auto& event = midi[track][index];

            if (event.size() < 3 || event[0] != 0xff || event[1] != 0x06)
                continue;

            // FF 06 <lengte (VLQ)> <tekst>
            std::size_t position = 2;
            std::size_t length = 0;

            while (position < event.size())
            {
                const auto byte = static_cast<std::uint8_t>(event[position++]);
                length = (length << 7) | (byte & 0x7F);

                if ((byte & 0x80) == 0)
                    break;
            }

            std::string text;

            for (std::size_t i = 0; i < length && position + i < event.size(); ++i)
                text += static_cast<char>(event[position + i]);

            if (!TextEncoding::isValidUtf8(text))
                text = TextEncoding::windows1252ToUtf8(text);

            const auto name = SectionParser::parse(text);

            if (!name)
                continue;

            SongSection section;
            section.tick = event.tick;
            section.seconds = tempoMap.tickToSeconds(event.tick);
            section.name = *name;

            // Dezelfde naam op dezelfde plek (meerdere sporen) maar één keer.
            const bool duplicate = std::any_of(song.sections.begin(), song.sections.end(),
                [&section](const SongSection& other)
                {
                    return other.name == section.name && std::llabs(other.tick - section.tick) <= 1;
                });

            if (!duplicate)
                song.sections.push_back(std::move(section));
        }
    }

    std::stable_sort(song.sections.begin(), song.sections.end(),
        [](const SongSection& a, const SongSection& b) { return a.tick < b.tick; });

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