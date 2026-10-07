#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RawMidiEvent
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    std::int64_t samplePosition = 0;
    int sourceTrack = 0;
    std::vector<std::uint8_t> bytes;
};

struct LyricEvent
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    std::int64_t samplePosition = 0;
    int sourceTrack = 0;

    // Informatie uit LyricsParser.
    int lineIndex = 0;

    bool startsNewWord = false;
    bool startsNewLine = false;
    bool endsWord = false;
    bool endsLine = false;

    std::string text;
};

struct SongChordEvent
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    double endSeconds = 0.0;
    std::int64_t samplePosition = 0;
    int sourceTrack = 0;
    std::string label;
};

struct TempoPoint
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    double bpm = 120.0;
};

struct Song
{
    std::string sourceFile;
    std::string info;
    int ticksPerQuarterNote = 480;
    double sampleRate = 48000.0;

    std::vector<TempoPoint> tempoMap;

    std::vector<RawMidiEvent> playbackEvents;

    std::vector<LyricEvent> lyrics;
    std::vector<SongChordEvent> chords;
};