#include "MidiFileReader.h"
#include "LyricsParser.h"
#include "TempoMap.h"

#include "MidiFile.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace
{
std::string cleanLyric(std::string text)
{
    // Rudimentary only: preserve the original text but remove CR/LF separators.
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}
}

Song MidiFileReader::read(const std::string& filename, double sampleRate) const
{
    smf::MidiFile midi;
    if (!midi.read(filename))
        throw std::runtime_error("Cannot read MIDI file: " + filename);

    midi.absoluteTicks();
    midi.sortTracks();

    LyricsParser parser;
    LyricsData lyrics = parser.parse(midi);

    for (const auto& line : lyrics.lines)
    {
        std::cout
            << line.startSeconds
            << " : "
            << line.text
            << '\n';
    }

    std::cout << "\nSegments:\n";

    for (const auto& segment : lyrics.segments)
    {
        std::cout
            << segment.timeSeconds
            << " line=" << segment.lineIndex
            << " startsNewWord="
            << segment.startsNewWord
            << " startsNewLine="
            << segment.startsNewLine
            << " text=["
            << segment.text
            << "]\n";
    }



    Song song;
    song.sourceFile = filename;
    song.sampleRate = sampleRate;
    song.ticksPerQuarterNote = midi.getTicksPerQuarterNote();

    TempoMap tempoMap(song.ticksPerQuarterNote);

    // Pass 1: collect tempo events from every track.
    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        for (int index = 0; index < midi[track].getSize(); ++index)
        {
            const auto& event = midi[track][index];
            if (event.isTempo())
                tempoMap.addTempo(event.tick, event.getTempoBPM());
        }
    }
    tempoMap.finalise();
    song.tempoMap = tempoMap.points();

    // Pass 2: create immutable playback and lyric timelines.
    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        for (int index = 0; index < midi[track].getSize(); ++index)
        {
            const auto& event = midi[track][index];
            const auto seconds = tempoMap.tickToSeconds(event.tick);
            const auto sample = tempoMap.tickToSample(event.tick, sampleRate);

            RawMidiEvent output;
            output.tick = event.tick;
            output.seconds = seconds;
            output.samplePosition = sample;
            output.sourceTrack = track;
            output.bytes.assign(event.begin(), event.end());
            song.playbackEvents.push_back(std::move(output));

            // Rudimentary lyrics: only Standard MIDI Lyric meta-events (0x05).
            if (event.isLyricText())
            {
                LyricEvent lyric;
                lyric.tick = event.tick;
                lyric.seconds = seconds;
                lyric.samplePosition = sample;
                lyric.sourceTrack = track;
                lyric.text = cleanLyric(event.getMetaContent());
                song.lyrics.push_back(std::move(lyric));
            }
        }
    }

    const auto eventOrder = [](const auto& a, const auto& b)
    {
        if (a.samplePosition != b.samplePosition)
            return a.samplePosition < b.samplePosition;
        return a.sourceTrack < b.sourceTrack;
    };

    std::stable_sort(song.playbackEvents.begin(), song.playbackEvents.end(), eventOrder);
    std::stable_sort(song.lyrics.begin(), song.lyrics.end(), eventOrder);
    return song;
}
