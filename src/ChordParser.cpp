#include "ChordParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <regex>
#include <string_view>
#include <utility>

namespace
{
constexpr std::uint8_t META_TEXT = 0x01;
constexpr std::uint8_t META_TRACK_NAME = 0x03;
constexpr std::uint8_t META_LYRIC = 0x05;
constexpr std::uint8_t META_MARKER = 0x06;
constexpr std::uint8_t META_SEQUENCER_SPECIFIC = 0x7f;

struct Candidate
{
    double seconds = 0.0;
    int tick = 0;
    int track = 0;
    int sequence = 0;
    ChordSource source = ChordSource::Text;
    std::string label;
    std::string rawText;
};

// Yamaha XF chord type table. Values not in this map are deliberately ignored.
// The first entries are the forms most commonly found in XF MIDI files.
const std::array<const char*, 35> XF_TYPES =
{
    "",          //  0 Maj
    "6",         //  1 Maj6
    "maj7",      //  2 Maj7
    "maj7#11",   //  3 Maj7(#11)
    "add9",      //  4 Maj(9)
    "maj7(9)",   //  5 Maj7(9)
    "6(9)",      //  6 Maj6(9)
    "aug",       //  7 aug
    "m",         //  8 min
    "m6",        //  9 min6
    "m7",        // 10 min7
    "m7b5",      // 11 min7b5
    "m(9)",      // 12 min(9)
    "m7(9)",     // 13 min7(9)
    "m7(11)",    // 14 min7(11)
    "mMaj7",     // 15 minMaj7
    "mMaj7(9)",  // 16 minMaj7(9)
    "dim",       // 17 dim
    "dim7",      // 18 dim7
    "7",         // 19 7th
    "7sus4",     // 20 7sus4
    "7b5",       // 21 7b5
    "7(9)",      // 22 7(9)
    "7#11",      // 23 7(#11)
    "7(13)",     // 24 7(13)
    "7b9",       // 25 7(b9)
    "7b13",      // 26 7(b13)
    "7#9",       // 27 7(#9)
    "maj7aug",   // 28 Maj7aug
    "7aug",      // 29 7aug
    "1+8",       // 30
    "1+5",       // 31
    "sus4",      // 32
    "1+2+5",     // 33
    "cc"         // 34
};

int mod12(int value)
{
    value %= 12;
    return value < 0 ? value + 12 : value;
}
}

bool decodeXFRoot(
    std::uint8_t rootCode,
    std::string& rootName)
{
    if (rootCode == 0x7f)
    {
        return false;
    }

    const int accidentalCode =
        (rootCode >> 4) & 0x07;

    const int noteCode =
        rootCode & 0x0f;

    static const std::array<const char*, 8>
        noteNames =
    {
        "",   // 0 = reserved
        "C",  // 1
        "D",  // 2
        "E",  // 3
        "F",  // 4
        "G",  // 5
        "A",  // 6
        "B"   // 7
    };

    static const std::array<const char*, 7>
        accidentals =
    {
        "bbb", // 0
        "bb",  // 1
        "b",   // 2
        "",    // 3 = natural
        "#",   // 4
        "##",  // 5
        "###"  // 6
    };

    if (noteCode < 1 ||
        noteCode >=
            static_cast<int>(noteNames.size()))
    {
        return false;
    }

    if (accidentalCode < 0 ||
        accidentalCode >=
            static_cast<int>(accidentals.size()))
    {
        return false;
    }

    rootName =
        std::string(noteNames[noteCode]) +
        accidentals[accidentalCode];

    return true;
}


ChordData ChordParser::parse(smf::MidiFile& midi, double songEndSeconds) const
{
    ChordData result;
    midi.makeAbsoluteTicks();
    midi.doTimeAnalysis();

    std::vector<Candidate> xf;
    std::vector<Candidate> markers;
    std::vector<Candidate> text;
    std::vector<Candidate> lyrics;

    for (int track = 0; track < midi.getTrackCount(); ++track)
    {
        std::string trackName;
        bool chordTrack = false;

        // First pass: determine whether this is explicitly a chord track.
        for (int i = 0; i < midi.getEventCount(track); ++i)
        {
            const auto& event = midi[track][i];
            if (event.size() >= 3 && event[0] == 0xff && event[1] == META_TRACK_NAME)
            {
                trackName = trim(decodeMetaPayload(event));
                const std::string nameLower = lower(trackName);
                chordTrack = nameLower.find("chord") != std::string::npos ||
                             nameLower.find("akkoord") != std::string::npos ||
                             nameLower == "harmony";
                break;
            }
        }

        for (int i = 0; i < midi.getEventCount(track); ++i)
        {
            const auto& event = midi[track][i];
            if (event.size() < 3 || event[0] != 0xff)
                continue;

            const auto type = static_cast<std::uint8_t>(event[1]);
            std::string label;
            std::string raw;
            ChordSource source = ChordSource::Text;
            std::vector<Candidate>* destination = nullptr;

            if (type == META_SEQUENCER_SPECIFIC && decodeYamahaXFChord(event, label))
            {
                source = ChordSource::YamahaXF;
                destination = &xf;
                raw = "Yamaha XF";
            }
            else if (type == META_MARKER)
            {
                raw = decodeMetaPayload(event);
                if (extractTextChord(raw, true, label))
                {
                    source = ChordSource::Marker;
                    destination = &markers;
                }
            }
            else if (type == META_TEXT)
            {
                raw = decodeMetaPayload(event);
                if (extractTextChord(raw, chordTrack, label))
                {
                    source = chordTrack ? ChordSource::TrackName : ChordSource::Text;
                    destination = &text;
                }
            }
            else if (type == META_LYRIC)
            {
                raw = decodeMetaPayload(event);
                // Lyrics are only accepted when decorated, for example [Am] or Chord: Am.
                if (extractTextChord(raw, false, label))
                {
                    source = ChordSource::Lyric;
                    destination = &lyrics;
                }
            }

            if (destination == nullptr || label.empty())
                continue;

            destination->push_back(Candidate{
                event.seconds,
                event.tick,
                track,
                event.seq,
                source,
                normalizeLabel(label),
                raw
            });
        }
    }

    // Priority prevents ordinary lyrics/text from being mixed with an authoritative XF stream.
    std::vector<Candidate>* selected = nullptr;
    if (!xf.empty()) selected = &xf;
    else if (!markers.empty()) selected = &markers;
    else if (!text.empty()) selected = &text;
    else if (!lyrics.empty()) selected = &lyrics;

    if (selected == nullptr)
        return result;

    std::stable_sort(selected->begin(), selected->end(),
        [](const Candidate& left, const Candidate& right)
        {
            if (left.tick != right.tick) return left.tick < right.tick;
            if (left.track != right.track) return left.track < right.track;
            return left.sequence < right.sequence;
        });

    // Remove exact duplicates and collapse repeated equal chords.
    std::vector<Candidate> clean;
    for (const auto& candidate : *selected)
    {
        if (!clean.empty() &&
            clean.back().tick == candidate.tick &&
            clean.back().label == candidate.label)
            continue;

        if (!clean.empty() && clean.back().label == candidate.label)
            continue;

        clean.push_back(candidate);
    }

    for (std::size_t i = 0; i < clean.size(); ++i)
    {
        ChordEvent chord;
        chord.startSeconds = clean[i].seconds;
        chord.tick = clean[i].tick;
        chord.track = clean[i].track;
        chord.source = clean[i].source;
        chord.label = clean[i].label;
        chord.rawText = clean[i].rawText;

        if (i + 1 < clean.size())
            chord.endSeconds = std::max(chord.startSeconds + 0.01, clean[i + 1].seconds);
        else if (songEndSeconds > chord.startSeconds)
            chord.endSeconds = songEndSeconds;
        else
            chord.endSeconds = chord.startSeconds + 0.01;

        result.chords.push_back(std::move(chord));
    }

    return result;
}

std::string ChordParser::sourceName(ChordSource source)
{
    switch (source)
    {
        case ChordSource::YamahaXF: return "YamahaXF";
        case ChordSource::Marker: return "Marker";
        case ChordSource::Text: return "Text";
        case ChordSource::Lyric: return "Lyric";
        case ChordSource::TrackName: return "ChordTrack";
    }
    return "Unknown";
}

std::string ChordParser::decodeMetaPayload(const smf::MidiEvent& event)
{
    if (event.size() < 3 || event[0] != 0xff)
        return {};

    std::size_t position = 2;
    std::size_t length = 0;
    int count = 0;
    bool complete = false;

    while (position < event.size())
    {
        const auto byte = static_cast<std::uint8_t>(event[position++]);
        length = (length << 7) | (byte & 0x7f);
        ++count;
        if ((byte & 0x80) == 0)
        {
            complete = true;
            break;
        }
        if (count >= 4) return {};
    }

    if (!complete || position > event.size()) return {};
    length = std::min(length, event.size() - position);
    return std::string(
        event.begin() + static_cast<std::ptrdiff_t>(position),
        event.begin() + static_cast<std::ptrdiff_t>(position + length));
}

bool ChordParser::decodeYamahaXFChord(
    const smf::MidiEvent& event,
    std::string& label)
{
    const std::string payloadString =
        decodeMetaPayload(event);

    if (payloadString.size() < 7)
    {
        return false;
    }

    const auto* data =
        reinterpret_cast<const unsigned char*>(
            payloadString.data());

    const std::size_t size =
        payloadString.size();

    // Yamaha XF Chord Name:
    //
    // 43 7B 01 cr ct bn bt
    std::size_t position =
        std::string::npos;

    for (std::size_t index = 0;
         index + 6 < size;
         ++index)
    {
        if (data[index] == 0x43 &&
            data[index + 1] == 0x7b &&
            data[index + 2] == 0x01)
        {
            position = index + 3;
            break;
        }
    }

    if (position == std::string::npos ||
        position + 3 >= size)
    {
        return false;
    }

    const auto rootCode =
        static_cast<std::uint8_t>(
            data[position]);

    const auto typeCode =
        static_cast<std::uint8_t>(
            data[position + 1]);

    const auto bassRootCode =
        static_cast<std::uint8_t>(
            data[position + 2]);

    const auto bassTypeCode =
        static_cast<std::uint8_t>(
            data[position + 3]);

    if (rootCode == 0x7f ||
        typeCode == 0x7f)
    {
        return false;
    }

    if (typeCode >= XF_TYPES.size())
    {
        return false;
    }

    std::string rootName;

    if (!decodeXFRoot(
            rootCode,
            rootName))
    {
        return false;
    }

    label =
        rootName +
        XF_TYPES[typeCode];

    // Geen basakkoord.
    if (bassRootCode == 0x7f ||
        bassTypeCode == 0x7f)
    {
        return true;
    }

    if (bassTypeCode >= XF_TYPES.size())
    {
        return true;
    }

    std::string bassRootName;

    if (!decodeXFRoot(
            bassRootCode,
            bassRootName))
    {
        return true;
    }

    // XF bevat feitelijk een volledig bass chord:
    //
    // root type / bassRoot bassType
    //
    // Voor gewone slash-chords is alleen de basgrondtoon
    // meestal voldoende.
    label += "/";
    label += bassRootName;

    // Alleen toevoegen wanneer de bass chord quality
    // betekenisvol afwijkt van een standaard majeurvorm.
    if (bassTypeCode != 0)
    {
        label += XF_TYPES[bassTypeCode];
    }

    return true;
}

bool ChordParser::extractTextChord(
    const std::string& raw,
    bool allowBareChord,
    std::string& label)
{
    std::string text = trim(raw);
    if (text.empty()) return false;

    std::smatch match;
    static const std::regex bracket(R"(^\s*[\[\(\{]\s*([^\]\)\}]+)\s*[\]\)\}]\s*$)");
    static const std::regex prefix(R"(^\s*(?:chord|akkoord|cho|ch)\s*[:=]\s*(.+?)\s*$)", std::regex::icase);

    if (std::regex_match(text, match, bracket))
        text = trim(match[1].str());
    else if (std::regex_match(text, match, prefix))
        text = trim(match[1].str());
    else if (!allowBareChord)
        return false;

    if (!looksLikeChord(text)) return false;
    label = text;
    return true;
}

bool ChordParser::looksLikeChord(const std::string& text)
{
    // Supports C, Cm, C#7, Bbmaj7, F#m7b5, Gsus4, C/E, N.C.
    static const std::regex chord(
        R"(^(?:N\.?C\.?|[A-Ga-g](?:#{1,3}|b{1,3}|c{1,3})?(?:(?:maj|min|dim|aug|sus|add|m|M)?[0-9+#b()\-]*)?(?:/[A-Ga-g](?:#{1,3}|b{1,3}|c{1,3})?)?)$)",
        std::regex::icase);
    return std::regex_match(trim(text), chord);
}

std::string ChordParser::trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n\0");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n\0");
    return text.substr(first, last - first + 1);
}

std::string ChordParser::lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

int ChordParser::noteToPitchClass(char note, const std::string& accidental)
{
    int pc = 0;
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(note))))
    {
        case 'C': pc = 0; break;
        case 'D': pc = 2; break;
        case 'E': pc = 4; break;
        case 'F': pc = 5; break;
        case 'G': pc = 7; break;
        case 'A': pc = 9; break;
        case 'B': pc = 11; break;
        default: return -1;
    }
    for (char c : accidental)
    {
        if (c == '#') ++pc;
        else if (c == 'b' || c == 'c') --pc;
    }
    return mod12(pc);
}

std::string ChordParser::pitchClassName(int pitchClass, bool preferSharps)
{
    static const std::array<const char*, 12> sharps = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static const std::array<const char*, 12> flats = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    return (preferSharps ? sharps : flats)[mod12(pitchClass)];
}

std::string ChordParser::normalizeLabel(const std::string& label)
{
    return transposeLabel(label, 0);
}

std::string ChordParser::transposeLabel(const std::string& label, int semitones)
{
    std::string value = trim(label);
    if (value.empty() || lower(value) == "n.c." || lower(value) == "nc") return value;

    static const std::regex parts(R"(^([A-Ga-g])([#bc]{0,5})(.*?)(?:/([A-Ga-g])([#bc]{0,5}))?$)");
    std::smatch match;
    if (!std::regex_match(value, match, parts)) return value;

    const std::string rootAccidental = match[2].str();
    const bool preferSharps = rootAccidental.find('#') != std::string::npos;
    const int rootPc = noteToPitchClass(match[1].str()[0], rootAccidental);
    if (rootPc < 0) return value;

    std::string result = pitchClassName(rootPc + semitones, preferSharps);
    result += match[3].str();

    if (match[4].matched)
    {
        const int bassPc = noteToPitchClass(match[4].str()[0], match[5].str());
        if (bassPc >= 0)
            result += "/" + pitchClassName(bassPc + semitones, preferSharps);
    }
    return result;
}
