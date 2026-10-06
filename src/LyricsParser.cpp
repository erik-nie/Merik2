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

            // Belangrijk:
            // Leading spaces worden hier bewust niet verwijderd.
            //
            // " calls" betekent: nieuw woord.
            // "no" betekent: vervolg van het huidige woord.
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
        [](const Candidate& left, const Candidate& right)
        {
            if (left.event->tick != right.event->tick)
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
    // Het "\r"-event bevat zelf geen tekst.
    // Het daaropvolgende "Is"-segment moet wel
    // startsNewLine=true krijgen.
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
    // De midifile-library bewaart ook de VLV-lengtebytes
    // in het event. Daarom moeten we die hier eerst lezen.

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

    // Defensief begrenzen wanneer een beschadigd bestand
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
    // NUL-bytes horen niet in zichtbare lyrics.
    text.erase(
        std::remove(
            text.begin(),
            text.end(),
            '\0'),
        text.end());

    // Andere niet-afdrukbare tekens vervangen.
    //
    // CR en LF blijven bewust behouden. Zij kunnen zelf
    // een newline-marker zijn.
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

    // Alleen trailing spaces/tabs verwijderen.
    //
    // Leading spaces zijn semantisch belangrijk:
    //
    // " calls" = nieuw woord
    // "no"     = vervolg van huidig woord
    const auto lastNonWhitespace =
        text.find_last_not_of(" \t");

    if (lastNonWhitespace ==
        std::string::npos)
    {
        return {};
    }

    text.erase(lastNonWhitespace + 1);

    return text;
}

bool LyricsParser::isMetadataText(
    const std::string& text)
{
    // Voor metadataherkenning mogen we de karaoke-prefixen
    // tijdelijk overslaan zonder de originele tekst te veranderen.
    const auto contentPosition =
        text.find_first_not_of(
            " \t/\\\r\n");

    if (contentPosition ==
        std::string::npos)
    {
        // Een standalone newline-event is geen metadata.
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

    // Ondersteunde line-prefixen:
    //
    // "\\In" -> nieuwe regel
    // "/Zat" -> nieuwe regel
    //
    // Ook echte CR/LF aan het begin worden ondersteund.
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

    // Een voorloopspatie betekent een nieuw woord.
    //
    // " calls" -> calls begint een nieuw woord
    // "no"     -> no vervolgt het vorige woord
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

    result.text = std::move(rawText);

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

    // Letterlijke escaped tekst zoals Sekaiju deze mogelijk toont.
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
    if (!line.text.empty() &&
        segment.startsNewWord)
    {
        line.text += ' ';
    }

    line.text += segment.text;
    line.segments.push_back(segment);
}