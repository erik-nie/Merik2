#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MidiFile.h"

struct LyricSegment
{
    double timeSeconds = 0.0;

    int tick = 0;
    int track = 0;
    int lineIndex = 0;

    bool startsNewWord = false;
    bool startsNewLine = false;

    // TRUE wanneer de oorspronkelijke tekst eindigde
    // op een spatie of tab.
    bool endsWord = false;

    std::string text;
};

struct LyricLine
{
    int index = 0;
    double startSeconds = 0.0;

    std::string text;
    std::vector<LyricSegment> segments;
};

struct LyricsData
{
    std::vector<LyricSegment> segments;
    std::vector<LyricLine> lines;
};

class LyricsParser
{
public:
    LyricsData parse(smf::MidiFile& midi) const;

private:
    enum class ParsedTextType
    {
        Text,

        // Los event zoals "\r", "\n", CR of LF.
        LineBreak
    };

    struct ParsedText
    {
        ParsedTextType type =
            ParsedTextType::Text;

        bool startsNewWord = false;
        bool startsNewLine = false;

        bool endsWord = false;

        std::string text;
    };

    static bool isLyricMetaType(std::uint8_t type);
    static bool isTextMetaType(std::uint8_t type);

    static std::string decodeMetaText(
        const smf::MidiEvent& event);

    static std::string sanitizeRawText(
        std::string text);

    static bool isMetadataText(
        const std::string& text);

    static ParsedText parseKaraokeText(
        std::string rawText);

    static bool isStandaloneLineBreak(
        const std::string& text);

    static void appendSegmentToLine(
        LyricLine& line,
        const LyricSegment& segment);
};