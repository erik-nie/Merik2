#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MidiFile.h"

enum class ChordSource
{
    YamahaXF,
    Marker,
    Text,
    Lyric,
    TrackName
};

struct ChordEvent
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int tick = 0;
    int track = 0;
    ChordSource source = ChordSource::Text;
    std::string label;
    std::string rawText;
};

struct ChordData
{
    std::vector<ChordEvent> chords;
};

class ChordParser
{
public:
    // songEndSeconds <= 0: laatste akkoord krijgt minimaal 0.01 seconde.
    ChordData parse(smf::MidiFile& midi, double songEndSeconds = 0.0) const;

    static std::string sourceName(ChordSource source);
    static std::string normalizeLabel(const std::string& label);
    static std::string transposeLabel(const std::string& label, int semitones);

private:
    static std::string decodeMetaPayload(const smf::MidiEvent& event);
    static bool decodeYamahaXFChord(
        const smf::MidiEvent& event,
        std::string& label);
    static bool extractTextChord(
        const std::string& raw,
        bool allowBareChord,
        std::string& label);
    static bool looksLikeChord(const std::string& text);
    static std::string trim(std::string text);
    static std::string lower(std::string text);
    static int noteToPitchClass(char note, const std::string& accidental);
    static std::string pitchClassName(int pitchClass, bool preferSharps);
};
