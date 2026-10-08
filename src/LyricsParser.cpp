#include "LyricsParser.h"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace
{

constexpr std::uint8_t META_TEXT = 0x01;
constexpr std::uint8_t META_LYRIC = 0x05;
constexpr std::uint8_t SYSEX_START = 0xF0;
constexpr std::uint8_t SYSEX_END   = 0xF7;

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

bool isMidisoftSysEx(const smf::MidiEvent& event)
{
    if (event.size() < 8)
        return false;

    if (event[0] != SYSEX_START)
        return false;

    // Midisoft signature:
    //
    // F0 xx 00 20 24 00 <line>
    //
    // xx is de SysEx length byte in de representatie
    // die Midifile hier gebruikt.
    //
    // Zoek bewust naar de signature zodat we niet
    // afhankelijk zijn van de precieze lengtecodering.

    for (std::size_t i = 1; i + 4 < event.size(); ++i)
    {
        if (event[i]     == 0x00 &&
            event[i + 1] == 0x20 &&
            event[i + 2] == 0x24 &&
            event[i + 3] == 0x00)
        {
            return true;
        }
    }

    return false;
}

std::string decodeMidisoftSysEx(
    const smf::MidiEvent& event)
{
    if (!isMidisoftSysEx(event))
        return {};

    std::size_t signaturePosition = 0;

    for (std::size_t i = 1; i + 4 < event.size(); ++i)
    {
        if (event[i]     == 0x00 &&
            event[i + 1] == 0x20 &&
            event[i + 2] == 0x24 &&
            event[i + 3] == 0x00)
        {
            signaturePosition = i;
            break;
        }
    }

    if (signaturePosition == 0)
        return {};

    // Na de signature:
    //
    // 00 20 24 00 <line> <tekst> 0A F7
    //
    const std::size_t textStart =
        signaturePosition + 4 + 1;

    if (textStart >= event.size())
        return {};

    std::size_t textEnd = event.size();

    for (std::size_t i = textStart;
         i < event.size();
         ++i)
    {
        if (event[i] == 0x0A ||
            event[i] == SYSEX_END)
        {
            textEnd = i;
            break;
        }
    }

    if (textEnd <= textStart)
        return {};

    std::string text;

    for (std::size_t i = textStart;
         i < textEnd;
         ++i)
    {
        const auto value =
            static_cast<unsigned char>(event[i]);

        if (value >= 0x20 &&
            value <= 0x7E)
        {
            text.push_back(
                static_cast<char>(value));
        }
    }

    std::cout << "Midisoft lyric: " << text << '\n';

    return text;
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

        bool isMidisoft = false;
    };

    std::vector<Candidate> lyricEvents;
    std::vector<Candidate> textEvents;
    std::vector<Candidate> midisoftEvents;

    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        auto& trackData = midi[track];

        for (int eventIndex = 0; eventIndex < trackData.size(); ++eventIndex)
        {
            auto& event = trackData[eventIndex];

            if (event.size() < 3)
                continue;

            Candidate candidate;
            candidate.event = &event;
            candidate.track = track;

            // ------------------------------------------------------------
            // Standard MIDI meta events
            // FF 01 = Text
            // FF 05 = Lyric
            // ------------------------------------------------------------
            if (event[0] == 0xff)
            {
                const auto type =
                    static_cast<std::uint8_t>(event[1]);

                if (!isLyricMetaType(type) &&
                    !isTextMetaType(type))
                {
                    continue;
                }

                std::string rawText =
                    sanitizeRawText(decodeMetaText(event));

                if (rawText.empty())
                    continue;

                if (isMetadataText(rawText))
                    continue;

                candidate.type = type;
                candidate.rawText = std::move(rawText);

                if (isLyricMetaType(type))
                    lyricEvents.push_back(std::move(candidate));
                else
                    textEvents.push_back(std::move(candidate));

                continue;
            }

            // ------------------------------------------------------------
            // Midisoft SysEx lyrics
            // F0 ... 00 20 24 00 <line> <text> 0A F7
            // ------------------------------------------------------------
            if (event[0] == SYSEX_START &&
                isMidisoftSysEx(event))
            {
                auto rawText = decodeMidisoftSysEx(event);

                if (rawText.empty())
                    continue;

                candidate.type = SYSEX_START;
                candidate.rawText = std::move(rawText);
                candidate.isMidisoft = true;

                midisoftEvents.push_back(std::move(candidate));
            }
        }
    }

    // Gebruik echte Lyric-events wanneer die aanwezig zijn.
    // Gebruik Text-events alleen als fallback.
    auto& selectedEvents =
        !lyricEvents.empty()
            ? lyricEvents
            : (!midisoftEvents.empty()
                ? midisoftEvents
                : textEvents);

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

        // Een Midisoft SysEx-record representeert
        // altijd één nieuwe lyricregel.
        //
        // De volgende SysEx moet dus op een nieuwe
        // regel beginnen, ook als de tekst zelf geen
        // CR/LF bevat.
        if (candidate.isMidisoft)
        {
            parsed.startsNewLine = true;
            parsed.startsNewWord = true;
        }

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

        segment.endsLine =
            parsed.endsLine;

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

        // Bewaar dit voordat segment wordt verplaatst.
        const bool segmentEndsLine =
            segment.endsLine;

        result.segments.push_back(
            std::move(segment));

        // Een newline achter de tekst geldt voor het
        // eerstvolgende zichtbare lyricsegment.
        if (segmentEndsLine)
        {
            pendingNewLine = true;
        }
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

    // Een volledig los line-break-event.
    if (isStandaloneLineBreak(rawText))
    {
        result.type =
            ParsedTextType::LineBreak;

        result.startsNewLine = true;
        result.startsNewWord = true;
        result.endsLine = true;

        return result;
    }

    result.type =
        ParsedTextType::Text;

    // ---------------------------------------------------------
    // Line-break aan het begin
    // ---------------------------------------------------------
    //
    // Ondersteunt:
    //
    // "\In"
    // "/Zat"
    // CR + "Is"
    // LF + "Is"
    //
    while (!rawText.empty())
    {
        const char first =
            rawText.front();

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

    // Ook letterlijke escaped prefixen ondersteunen:
    //
    // "\rIs"
    // "\nIs"
    while (rawText.size() >= 2 &&
           rawText[0] == '\\' &&
           (rawText[1] == 'r' ||
            rawText[1] == 'n'))
    {
        result.startsNewLine = true;
        result.startsNewWord = true;

        rawText.erase(0, 2);
    }

    // ---------------------------------------------------------
    // Word boundary aan het begin
    // ---------------------------------------------------------
    //
    // Leading whitespace:
    //
    // [ dis][co][theek]
    //
    // => "dis" begint nieuw woord
    // => "co" en "theek" vervolgen dat woord
    //
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

    // ---------------------------------------------------------
    // Line-break aan het einde
    // ---------------------------------------------------------
    //
    // Dit moet vóór trailing-space-detectie gebeuren.
    //
    // Ondersteunt:
    //
    // "men\r"
    // "vloog\n"
    // "tekst\r\n"
    //
    while (!rawText.empty() &&
           (rawText.back() == '\r' ||
            rawText.back() == '\n'))
    {
        result.endsLine = true;
        rawText.pop_back();
    }

    // Letterlijk opgeslagen escaped suffixen:
    //
    // "tekst\\r"
    // "tekst\\n"
    // "tekst\\r\\n"
    //
    bool removedEscapedLineBreak = true;

    while (removedEscapedLineBreak)
    {
        removedEscapedLineBreak = false;

        if (rawText.size() >= 2)
        {
            const std::size_t size =
                rawText.size();

            if (rawText[size - 2] == '\\' &&
                (rawText[size - 1] == 'r' ||
                 rawText[size - 1] == 'n'))
            {
                result.endsLine = true;
                rawText.erase(size - 2);
                removedEscapedLineBreak = true;
            }
        }
    }

    // ---------------------------------------------------------
    // Word boundary aan het einde
    // ---------------------------------------------------------
    //
    // Trailing whitespace:
    //
    // [jij ][bent ][een ][mooi][e ][vrouw ]
    //
    // => "mooi" + "e" = "mooie"
    // => na "e " begint het volgende woord
    //
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

    // Een lege tekst na het verwijderen van markers
    // functioneert alleen als line break.
    if (rawText.empty() &&
        result.endsLine)
    {
        result.type =
            ParsedTextType::LineBreak;

        return result;
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