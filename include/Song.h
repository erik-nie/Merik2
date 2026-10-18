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

struct TimeSignaturePoint
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    int numerator = 4;
    int denominator = 4;
};

// Songonderdeel uit een marker van het MIDI-bestand ("Intro", "Verse 1", "Refrain", ...).
struct SongSection
{
    std::int64_t tick = 0;
    double seconds = 0.0;
    std::string name;
};

struct Song
{
    std::string sourceFile;
    std::string info;
    int ticksPerQuarterNote = 480;
    double sampleRate = 48000.0;

    // false: geen MIDI of tempo-informatie (een .merik met alleen stems): de
    // tempo en maatsoort hieronder zijn dan een aanname; geen eigen klik en geen beat-ledjes.
    bool tempoKnown = true;

    std::vector<TempoPoint> tempoMap;

    // Maatsoort(en); begint altijd op tick 0 (standaard 4/4).
    std::vector<TimeSignaturePoint> timeSignatures;

    std::vector<RawMidiEvent> playbackEvents;

    std::vector<LyricEvent> lyrics;
    std::vector<SongChordEvent> chords;
    std::vector<SongSection> sections;
};