#include "LyricsParser.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace
{

constexpr std::uint8_t META_TEXT = 0x01;
constexpr std::uint8_t META_LYRIC = 0x05;

bool startsWith(
    const std::string& value,
    std::string_view prefix)
{
    return value.size() >= prefix.size() &&
           std::equal(
               prefix.begin(),
               prefix.end(),
               value.begin());
}

bool containsText(
    const std::string& value,
    std::string_view token)
{
    return value.find(token) != std::string::npos;
}

} // namespace

LyricsData LyricsParser::parse(
    smf::MidiFile& midi) const
{
    LyricsData result;

    // Zorg dat alle eventticks absoluut zijn.
    midi.makeAbsoluteTicks();

    // Berekent MidiEvent::seconds met inachtneming
    // van tempo-events.
    midi.doTimeAnalysis();

    struct Candidate
    {
        const smf::MidiEvent* event = nullptr;

        int track = 0;
        std::uint8_t type = 0;

        std::string rawText;
    };

    std::vector<Candidate> lyricEvents;
    std::vector<Candidate> textEvents;

    for (int track = 0;
         track < midi.getTrackCount();
         ++track)
    {
        for (int index = 0;
             index < midi.getEventCount(track);
             ++index)
        {
            const auto& event = midi[track][index];

            if (event.size() < 3)
            {
                continue;
            }

            if (event[0] != 0xff)
            {
                continue;
            }

            const auto type =
                static_cast<std::uint8_t>(event[1]);

            if (!isLyricMetaType(type) &&
                !isTextMetaType(type))
            {
                continue;
            }

            // Leading en trailing whitespace worden hier
            // bewust nog niet verwijderd.
            //
            // Leading whitespace:
            // " dis" betekent nieuw woord.
            //
            // Trailing whitespace:
            // "e " betekent dat het woord na dit segment eindigt.
            std::string rawText =
                sanitizeRawText(
                    decodeMetaText(event));

            if (rawText.empty())
            {
                continue;
            }

            if (isMetadataText(rawText))
            {
                continue;
            }

            Candidate candidate;

            candidate.event = &event;
            candidate.track = track;
            candidate.type = type;
            candidate.rawText = std::move(rawText);

            if (isLyricMetaType(type))
            {
                lyricEvents.push_back(
                    std::move(candidate));
            }
            else
            {
                textEvents.push_back(
                    std::move(candidate));
            }
        }
    }

    // Gebruik echte Lyric-events wanneer die aanwezig zijn.
    // Gebruik Text-events alleen als fallback.
    auto& selectedEvents =
        lyricEvents.empty()
            ? textEvents
            : lyricEvents;

    std::stable_sort(
        selectedEvents.begin(),
        selectedEvents.end(),
        [](const Candidate& left,
           const Candidate& right)
        {
            if (left.event->tick !=
                right.event->tick)
            {
                return left.event->tick <
                       right.event->tick;
            }

            if (left.track != right.track)
            {
                return left.track <
                       right.track;
            }

            return left.event->seq <
                   right.event->seq;
        });

    int lineIndex = -1;

    // Wordt gezet wanneer een los newline-event is gezien.
    //
    // Voorbeeld:
    // [he][ calls][no][ where][\r][Is]
    //
    // Het "\r"-event bevat zelf geen zichtbare tekst.
    // Het volgende segment krijgt startsNewLine=true.
    bool pendingNewLine = false;

    for (const auto& candidate : selectedEvents)
    {
        ParsedText parsed =
            parseKaraokeText(candidate.rawText);

        if (parsed.type ==
            ParsedTextType::LineBreak)
        {
            pendingNewLine = true;
            continue;
        }

        if (parsed.text.empty())
        {
            continue;
        }

        if (lineIndex < 0)
        {
            lineIndex = 0;

            parsed.startsNewLine = true;
            parsed.startsNewWord = true;

            pendingNewLine = false;
        }
        else if (pendingNewLine ||
                 parsed.startsNewLine)
        {
            ++lineIndex;

            parsed.startsNewLine = true;
            parsed.startsNewWord = true;

            pendingNewLine = false;
        }

        LyricSegment segment;

        segment.timeSeconds =
            candidate.event->seconds;

        segment.tick =
            candidate.event->tick;

        segment.track =
            candidate.track;

        segment.lineIndex =
            lineIndex;

        segment.startsNewWord =
            parsed.startsNewWord;

        segment.startsNewLine =
            parsed.startsNewLine;

        segment.endsWord =
            parsed.endsWord;

        segment.text =
            std::move(parsed.text);

        if (result.lines.empty() ||
            result.lines.back().index != lineIndex)
        {
            LyricLine line;

            line.index = lineIndex;
            line.startSeconds =
                segment.timeSeconds;

            result.lines.push_back(
                std::move(line));
        }

        appendSegmentToLine(
            result.lines.back(),
            segment);

        result.segments.push_back(
            std::move(segment));
    }

    return result;
}

bool LyricsParser::isLyricMetaType(
    std::uint8_t type)
{
    return type == META_LYRIC;
}

bool LyricsParser::isTextMetaType(
    std::uint8_t type)
{
    return type == META_TEXT;
}

std::string LyricsParser::decodeMetaText(
    const smf::MidiEvent& event)
{
    if (event.size() < 3 ||
        event[0] != 0xff)
    {
        return {};
    }

    // Eventformaat:
    //
    // FF <type> <VLV length> <payload>
    //
    // De midifile-library bewaart de VLV-lengtebytes
    // in het event. Daarom lezen we deze eerst uit.

    std::size_t position = 2;
    std::size_t length = 0;

    int vlvByteCount = 0;
    bool vlvComplete = false;

    while (position < event.size())
    {
        const auto byte =
            static_cast<std::uint8_t>(
                event[position++]);

        length =
            (length << 7) |
            (byte & 0x7f);

        ++vlvByteCount;

        if ((byte & 0x80) == 0)
        {
            vlvComplete = true;
            break;
        }

        if (vlvByteCount >= 4)
        {
            return {};
        }
    }

    if (!vlvComplete)
    {
        return {};
    }

    if (position > event.size())
    {
        return {};
    }

    // Defensief begrenzen als een beschadigd bestand
    // een te grote lengte opgeeft.
    length = std::min(
        length,
        event.size() - position);

    return std::string(
        event.begin() +
            static_cast<std::ptrdiff_t>(position),

        event.begin() +
            static_cast<std::ptrdiff_t>(
                position + length));
}

std::string LyricsParser::sanitizeRawText(
    std::string text)
{
    // NUL-bytes verwijderen.
    text.erase(
        std::remove(
            text.begin(),
            text.end(),
            '\0'),
        text.end());

    // Niet-afdrukbare tekens vervangen, behalve:
    //
    // CR/LF: newline-informatie
    // TAB: mogelijke woordgrens
    for (char& character : text)
    {
        const auto value =
            static_cast<unsigned char>(
                character);

        if (value < 0x20 &&
            character != '\r' &&
            character != '\n' &&
            character != '\t')
        {
            character = ' ';
        }
    }

    // Geen trim uitvoeren.
    //
    // Zowel leading als trailing whitespace heeft
    // betekenis in karaoke-MIDI-bestanden.
    return text;
}

bool LyricsParser::isMetadataText(
    const std::string& text)
{
    // Voor metadataherkenning mogen karaoke-prefixes
    // tijdelijk worden overgeslagen.
    const auto contentPosition =
        text.find_first_not_of(
            " \t/\\\r\n");

    if (contentPosition ==
        std::string::npos)
    {
        // Standalone newline-event is geen metadata.
        return false;
    }

    const std::string logicalText =
        text.substr(contentPosition);

    // Bekende KAR-headerrecords.
    if (startsWith(logicalText, "@KMIDI") ||
        startsWith(logicalText, "@K") ||
        startsWith(logicalText, "@L") ||
        startsWith(logicalText, "@T") ||
        startsWith(logicalText, "@V"))
    {
        return true;
    }

    if (startsWith(logicalText, "(C)") ||
        startsWith(logicalText, "(c)") ||
        containsText(logicalText, "http://") ||
        containsText(logicalText, "https://") ||
        containsText(logicalText, "www."))
    {
        return true;
    }

    return false;
}

LyricsParser::ParsedText
LyricsParser::parseKaraokeText(
    std::string rawText)
{
    ParsedText result;

    // Eerst standalone newline-events herkennen.
    //
    // Dit kan een echte CR/LF-byte zijn of een
    // letterlijk opgeslagen tekst "\r" of "\n".
    if (isStandaloneLineBreak(rawText))
    {
        result.type =
            ParsedTextType::LineBreak;

        result.startsNewLine = true;
        result.startsNewWord = true;

        return result;
    }

    result.type =
        ParsedTextType::Text;

    // Regelprefixen:
    //
    // "\In"  -> nieuwe regel
    // "/Zat" -> nieuwe regel
    //
    // Echte CR/LF aan het begin wordt ook ondersteund.
    while (!rawText.empty())
    {
        const char first = rawText.front();

        if (first == '/' ||
            first == '\\' ||
            first == '\r' ||
            first == '\n')
        {
            result.startsNewLine = true;
            result.startsNewWord = true;

            rawText.erase(
                rawText.begin());

            continue;
        }

        break;
    }

    // Leading whitespace betekent dat dit segment
    // een nieuw woord begint.
    //
    // Voorbeeld:
    //
    // [ dis][co][theek]
    //
    // "dis" begint een woord.
    // "co" en "theek" vervolgen dat woord.
    if (!rawText.empty() &&
        (rawText.front() == ' ' ||
         rawText.front() == '\t'))
    {
        result.startsNewWord = true;

        while (!rawText.empty() &&
               (rawText.front() == ' ' ||
                rawText.front() == '\t'))
        {
            rawText.erase(
                rawText.begin());
        }
    }

    // Trailing whitespace betekent dat het woord
    // na dit segment eindigt.
    //
    // Voorbeeld:
    //
    // [mooi][e ][vrouw ]
    //
    // "mooi" en "e" horen bij hetzelfde woord.
    // Na "e " begint een nieuw woord.
    if (!rawText.empty() &&
        (rawText.back() == ' ' ||
         rawText.back() == '\t'))
    {
        result.endsWord = true;

        while (!rawText.empty() &&
               (rawText.back() == ' ' ||
                rawText.back() == '\t'))
        {
            rawText.pop_back();
        }
    }

    result.text =
        std::move(rawText);

    return result;
}

bool LyricsParser::isStandaloneLineBreak(
    const std::string& text)
{
    // Werkelijke control characters.
    if (text == "\r" ||
        text == "\n" ||
        text == "\r\n")
    {
        return true;
    }

    // Letterlijk opgeslagen escaped tekst.
    if (text == "\\r" ||
        text == "\\n" ||
        text == "\\r\\n")
    {
        return true;
    }

    return false;
}

void LyricsParser::appendSegmentToLine(
    LyricLine& line,
    const LyricSegment& segment)
{
    bool addSpace = false;

    if (!line.text.empty())
    {
        // Variant 1: dit segment heeft leading whitespace.
        //
        // [ dis][co][theek]
        //
        // Voor "dis" moet een spatie komen.
        if (segment.startsNewWord)
        {
            addSpace = true;
        }

        // Variant 2: het vorige segment had trailing whitespace.
        //
        // [jij ][bent ][een ][mooi][e ][vrouw ]
        //
        // Na "jij " moet vóór "bent" een spatie komen.
        if (!line.segments.empty() &&
            line.segments.back().endsWord)
        {
            addSpace = true;
        }
    }

    if (addSpace &&
        !line.text.empty() &&
        line.text.back() != ' ')
    {
        line.text += ' ';
    }

    line.text += segment.text;

    line.segments.push_back(segment);
}