#include "MerikPackage.h"
#include "StemStreamer.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int stemFamilies = 8;   // 1..7 en 8 (de klik)

    struct StemInfo
    {
        int family = 0;       // 0-based; 7 = klik
        juce::String file;
        juce::String label;   // naam uit de bestandsnaam, max. 8 tekens
        float gain = 1.0f;
    };

    struct Manifest
    {
        bool tempoGiven = false;      // bpm in manifest.json of tempo.json
        double bpm = 120.0;
        int numerator = 4;
        int denominator = 4;
        double durationSeconds = 0.0;
        double audioOffsetSeconds = 0.0;
        juce::String midiName;        // leeg = zelf zoeken
        std::vector<StemInfo> stems;  // eerst de gevonden bestanden, dan de extra's uit het manifest
    };

    bool safeName(const juce::String& name)
    {
        return name.isNotEmpty() && !name.contains("..") && !name.startsWithChar('/')
            && !name.startsWithChar('\\') && !name.containsChar(':');
    }

    bool ignoredEntry(const juce::String& name)
    {
        const auto base = name.fromLastOccurrenceOf("/", false, false);

        return name.startsWith("__MACOSX/") || base == ".DS_Store" || base.startsWith("._");
    }

    // Finder en Verkenner pakken bij "Comprimeren" vaak de hele map in: als alle
    // bestanden in één map staan, die map negeren ("mp3/1-drums.mp3" -> "1-drums.mp3").
    juce::String wrapperPrefix(juce::ZipFile& zip)
    {
        juce::String prefix;

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry(i)->filename.replaceCharacter('\\', '/');

            if (ignoredEntry(name) || name.endsWithChar('/'))
                continue;

            if (!name.containsChar('/'))
                return {};

            const auto top = name.upToFirstOccurrenceOf("/", true, false);

            if (prefix.isEmpty())
                prefix = top;
            else if (prefix != top)
                return {};
        }

        return prefix;
    }

    // Naam in de zip, met / als scheidingsteken en zonder omhullende map.
    juce::String entryName(juce::ZipFile& zip, int index)
    {
        const auto name = zip.getEntry(index)->filename.replaceCharacter('\\', '/');

        if (ignoredEntry(name) || name.endsWithChar('/'))
            return "__ignored__/";

        const auto prefix = wrapperPrefix(zip);
        return prefix.isNotEmpty() && name.startsWith(prefix) ? name.substring(prefix.length()) : name;
    }

    // Een bestand in de root van het pakket (hoofdletterongevoelig).
    juce::String findRootEntry(juce::ZipFile& zip, const juce::String& lowerName)
    {
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = entryName(zip, i);

            if (name.toLowerCase() == lowerName)
                return name;
        }

        return {};
    }

    int entryIndex(juce::ZipFile& zip, const juce::String& name)
    {
        if (!safeName(name))
            return -1;

        int index = zip.getIndexOfFileName(name);

        if (index < 0)
        {
            // Andere hoofdletters, backslashes of een omhullende map in de zip.
            for (int i = 0; i < zip.getNumEntries() && index < 0; ++i)
                if (entryName(zip, i).equalsIgnoreCase(name))
                    index = i;
        }

        return index;
    }

    bool readEntry(juce::ZipFile& zip, const juce::String& name, juce::MemoryBlock& block)
    {
        const int index = entryIndex(zip, name);

        if (index < 0)
            return false;

        std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));

        if (stream == nullptr)
            return false;

        block.reset();
        stream->readIntoMemoryBlock(block);
        return block.getSize() > 0;
    }

    // Vaste bestandsnamen: "1-drums.flac", "drums.wav", "7-live vocals.wav",
    // "click.wav" (in de root of in stems/). Geeft 0..7 of -1.
    int familyFromFileName(const juce::String& path)
    {
        auto name = path.toLowerCase();

        if (name.startsWith("stems/"))
            name = name.substring(6);

        if (name.containsChar('/') || name.startsWithChar('.'))
            return -1;

        const auto extension = name.fromLastOccurrenceOf(".", false, false);

        if (extension != "wav" && extension != "flac" && extension != "mp3"
            && extension != "aif" && extension != "aiff")
            return -1;

        const auto stem = name.upToLastOccurrenceOf(".", false, false);

        const auto startsWithWord = [&stem](const juce::String& word)
        {
            return stem == word
                || (stem.startsWith(word) && !juce::CharacterFunctions::isLetter(stem[word.length()]));
        };

        if (stem.isNotEmpty() && stem[0] >= '1' && stem[0] <= '8'
            && (stem.length() == 1 || !juce::CharacterFunctions::isLetterOrDigit(stem[1])))
        {
            const int number = stem[0] - '0';

            if (number == 8)
                return 7;

            return number - 1;
        }

        static const char* words[] = { "drums", "bass", "guitars", "guitar", "keys", "strings", "winds", "others", "other" };
        static const int families[] = { 0, 1, 2, 2, 3, 4, 5, 6, 6 };

        for (int i = 0; i < 9; ++i)
            if (startsWithWord(words[i]))
                return families[i];

        if (startsWithWord("click"))
            return 7;

        return -1;
    }

    // "7-void.mp3" -> "void"; "drums.wav" (zonder nummer) -> geen eigen naam.
    juce::String labelFromFileName(const juce::String& path)
    {
        auto name = path.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false).trim();

        if (name.isEmpty() || name[0] < '1' || name[0] > '8')
            return {};

        name = name.substring(1).trimCharactersAtStart(" -_.").trim();
        return name.substring(0, 8).trim();
    }

    bool parseManifest(juce::ZipFile& zip, Manifest& manifest, juce::String& error)
    {
        // Het manifest is optioneel: zonder gelden de vaste bestandsnamen.
        juce::MemoryBlock block;

        if (readEntry(zip, "manifest.json", block))
        {
            const auto root = juce::JSON::parse(block.toString());

            if (!root.isObject())
            {
                error = "manifest.json is not valid JSON.";
                return false;
            }

            if (root.hasProperty("bpm"))
            {
                manifest.bpm = std::max(20.0, static_cast<double>(root.getProperty("bpm", 120.0)));
                manifest.tempoGiven = true;
            }

            manifest.durationSeconds = root.getProperty("durationSeconds", 0.0);

            if (const auto* signature = root.getProperty("timeSignature", {}).getArray())
                if (signature->size() >= 2)
                {
                    manifest.numerator = juce::jlimit(1, 32, static_cast<int>((*signature)[0]));
                    manifest.denominator = juce::jlimit(1, 32, static_cast<int>((*signature)[1]));
                }

            const auto timing = root.getProperty("timing", {});
            manifest.audioOffsetSeconds = timing.getProperty("audioOffsetSeconds", 0.0);
            manifest.midiName = timing.getProperty("midi", {}).toString();
        }

        // tempo.json: { "bpm": 96, "timeSignature": [4, 4] }
        if (const auto tempoName = findRootEntry(zip, "tempo.json"); tempoName.isNotEmpty())
        {
            juce::MemoryBlock tempoBlock;

            if (readEntry(zip, tempoName, tempoBlock))
            {
                const auto tempo = juce::JSON::parse(tempoBlock.toString());

                if (tempo.hasProperty("bpm"))
                {
                    manifest.bpm = std::max(20.0, static_cast<double>(tempo.getProperty("bpm", 120.0)));
                    manifest.tempoGiven = true;
                }

                if (const auto* signature = tempo.getProperty("timeSignature", {}).getArray())
                    if (signature->size() >= 2)
                    {
                        manifest.numerator = juce::jlimit(1, 32, static_cast<int>((*signature)[0]));
                        manifest.denominator = juce::jlimit(1, 32, static_cast<int>((*signature)[1]));
                    }
            }
        }

        // Gevonden stems.
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = entryName(zip, i);
            const int family = familyFromFileName(name);

            if (family >= 0 && !name.startsWith("__MACOSX"))
            {
                StemInfo stem;
                stem.family = family;
                stem.file = name;
                stem.label = labelFromFileName(name);
                manifest.stems.push_back(stem);
            }
        }

        // Optioneel: gain of extra bestanden via het manifest.
        if (block.getSize() > 0)
        {
            const auto root = juce::JSON::parse(block.toString());

            if (const auto* stems = root.getProperty("stems", {}).getArray())
            {
                for (const auto& item : *stems)
                {
                    const int family = item.getProperty("family", 0);

                    if (family < 1 || family > stemFamilies)
                    {
                        error = "A stem has family " + juce::String(family) + "; use 1 to 8.";
                        return false;
                    }

                    const auto file = item.getProperty("file", {}).toString();

                    if (!safeName(file))
                    {
                        error = "Invalid stem file name: " + file;
                        return false;
                    }

                    const float gain = std::pow(10.0f, static_cast<float>(
                        static_cast<double>(item.getProperty("gainDb", 0.0))) / 20.0f);

                    bool known = false;

                    for (auto& existing : manifest.stems)
                        if (existing.file.equalsIgnoreCase(file))
                        {
                            existing.family = family - 1;
                            existing.gain = gain;
                            known = true;
                        }

                    if (!known)
                    {
                        StemInfo stem;
                        stem.family = family - 1;
                        stem.file = file;
                        stem.gain = gain;
                        manifest.stems.push_back(stem);
                    }
                }
            }
        }

        return true;
    }

    // Het MIDI-bestand in het pakket: song.mid, anders het eerste .mid/.midi/.kar in de root.
    juce::String findMidiEntry(juce::ZipFile& zip, const Manifest& manifest)
    {
        if (manifest.midiName.isNotEmpty())
            return manifest.midiName;

        const auto standard = findRootEntry(zip, "song.mid");

        if (standard.isNotEmpty())
            return standard;

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = entryName(zip, i);

            if (!name.containsChar('/') && !name.startsWithChar('.')
                && (name.endsWithIgnoreCase(".mid") || name.endsWithIgnoreCase(".midi")
                    || name.endsWithIgnoreCase(".kar")))
                return name;
        }

        return {};
    }

    juce::File cacheFolder(const juce::File& package)
    {
        const auto key = package.getFullPathName() + "|"
                       + juce::String(package.getLastModificationTime().toMilliseconds()) + "|"
                       + juce::String(package.getSize());

        return juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("Merik2-cache")
            .getChildFile(juce::String::toHexString(key.hashCode64()));
    }

    std::unique_ptr<juce::AudioFormatReader> makeReader(juce::AudioFormatManager& formats,
                                                        const juce::MemoryBlock& block)
    {
        return std::unique_ptr<juce::AudioFormatReader>(
            formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(block, false)));
    }

    void appendVlq(std::vector<std::uint8_t>& out, std::uint32_t value)
    {
        std::uint8_t groups[5];
        int count = 0;

        do
        {
            groups[count++] = static_cast<std::uint8_t>(value & 0x7F);
            value >>= 7;
        } while (value != 0);

        while (count > 0)
        {
            --count;
            out.push_back(static_cast<std::uint8_t>(groups[count] | (count > 0 ? 0x80 : 0)));
        }
    }

    // Een minimaal MIDI-bestand met alleen tempo en maatsoort (en een
    // "all notes off" aan het eind), voor een .merik zonder song.mid.
    std::vector<std::uint8_t> makeMidi(const Manifest& manifest, double seconds)
    {
        constexpr int ppq = 480;
        std::vector<std::uint8_t> track;

        const auto tempo = static_cast<std::uint32_t>(std::llround(60000000.0 / manifest.bpm));
        appendVlq(track, 0);
        track.insert(track.end(), { 0xFF, 0x51, 0x03,
                                    static_cast<std::uint8_t>(tempo >> 16),
                                    static_cast<std::uint8_t>(tempo >> 8),
                                    static_cast<std::uint8_t>(tempo) });

        int power = 0;

        while ((1 << (power + 1)) <= manifest.denominator)
            ++power;

        appendVlq(track, 0);
        track.insert(track.end(), { 0xFF, 0x58, 0x04,
                                    static_cast<std::uint8_t>(manifest.numerator),
                                    static_cast<std::uint8_t>(power), 24, 8 });

        const auto endTicks = static_cast<std::uint32_t>(
            std::llround(std::max(1.0, seconds) * manifest.bpm / 60.0 * ppq));

        appendVlq(track, endTicks);
        track.insert(track.end(), { 0xB0, 0x7B, 0x00 });

        appendVlq(track, 0);
        track.insert(track.end(), { 0xFF, 0x2F, 0x00 });

        std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1,
                                         static_cast<std::uint8_t>(ppq >> 8),
                                         static_cast<std::uint8_t>(ppq & 0xFF),
                                         'M', 'T', 'r', 'k',
                                         static_cast<std::uint8_t>(track.size() >> 24),
                                         static_cast<std::uint8_t>(track.size() >> 16),
                                         static_cast<std::uint8_t>(track.size() >> 8),
                                         static_cast<std::uint8_t>(track.size()) };

        file.insert(file.end(), track.begin(), track.end());
        return file;
    }
}

namespace MerikPackage
{
    bool isPackage(const juce::File& file)
    {
        return file.hasFileExtension("merik");
    }

    int stemCount(const juce::File& package)
    {
        juce::ZipFile zip(package);
        Manifest manifest;
        juce::String error;

        if (zip.getNumEntries() <= 0 || !parseManifest(zip, manifest, error))
            return 0;

        return static_cast<int>(manifest.stems.size());
    }

    int stemFamily(const juce::String& fileName)
    {
        return familyFromFileName(fileName);
    }

    juce::String stemLabel(const juce::String& fileName)
    {
        return labelFromFileName(fileName);
    }

    juce::File midiFor(const juce::File& package, juce::String& error)
    {
        juce::ZipFile zip(package);

        if (zip.getNumEntries() <= 0)
        {
            error = "This .merik file is not a valid zip file.";
            return {};
        }

        Manifest manifest;

        if (!parseManifest(zip, manifest, error))
            return {};

        const auto folder = cacheFolder(package);
        const auto target = folder.getChildFile("song.mid");

        if (target.existsAsFile())
            return target;

        folder.createDirectory();

        juce::MemoryBlock block;

        if (const auto midiName = findMidiEntry(zip, manifest); midiName.isNotEmpty() && readEntry(zip, midiName, block))
        {
            if (!target.replaceWithData(block.getData(), block.getSize()))
            {
                error = "Could not write to the temporary folder.";
                return {};
            }

            return target;
        }

        // Geen MIDI: lengte uit het manifest of uit de langste stem.
        double seconds = manifest.durationSeconds;

        if (seconds <= 0.0)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            for (const auto& stem : manifest.stems)
                if (readEntry(zip, stem.file, block))
                    if (auto reader = makeReader(formats, block); reader != nullptr && reader->sampleRate > 0.0)
                        seconds = std::max(seconds, static_cast<double>(reader->lengthInSamples) / reader->sampleRate);
        }

        if (seconds <= 0.0)
        {
            error = "This .merik file has no MIDI file and no readable stems (name stems like 1-drums.flac).";
            return {};
        }

        const auto midi = makeMidi(manifest, seconds + manifest.audioOffsetSeconds);

        if (!target.replaceWithData(midi.data(), midi.size()))
        {
            error = "Could not write to the temporary folder.";
            return {};
        }

        return target;
    }

    namespace
    {
        std::int64_t secondsToTick(const Song& song, double seconds)
        {
            const double ppq = static_cast<double>(std::max(1, song.ticksPerQuarterNote));

            if (song.tempoMap.empty())
                return static_cast<std::int64_t>(std::llround(seconds * 2.0 * ppq));

            std::size_t i = 0;

            while (i + 1 < song.tempoMap.size() && song.tempoMap[i + 1].seconds <= seconds)
                ++i;

            const auto& point = song.tempoMap[i];
            return point.tick + static_cast<std::int64_t>(
                std::llround((seconds - point.seconds) * point.bpm * ppq / 60.0));
        }

        const juce::Array<juce::var>* listFrom(const juce::var& root, const char* key)
        {
            if (const auto* array = root.getArray())
                return array;

            return root.getProperty(key, {}).getArray();
        }
    }

    void applyScore(const juce::File& package, Song& song)
    {
        juce::ZipFile zip(package);

        if (zip.getNumEntries() <= 0)
            return;

        juce::MemoryBlock block;

        // chords.json: [ { "time": 12.5, "chord": "Am" }, ... ]  (seconden)
        if (const auto name = findRootEntry(zip, "chords.json"); name.isNotEmpty() && readEntry(zip, name, block))
        {
            const auto root = juce::JSON::parse(block.toString());

            if (const auto* list = listFrom(root, "chords"))
            {
                std::vector<SongChordEvent> chords;

                for (const auto& item : *list)
                {
                    SongChordEvent chord;
                    chord.seconds = item.getProperty("time", 0.0);
                    chord.label = item.getProperty("chord", item.getProperty("label", {})).toString().toStdString();

                    if (chord.label.empty() || chord.seconds < 0.0)
                        continue;

                    chord.tick = secondsToTick(song, chord.seconds);
                    chord.samplePosition = static_cast<std::int64_t>(std::llround(chord.seconds * song.sampleRate));
                    chords.push_back(std::move(chord));
                }

                std::stable_sort(chords.begin(), chords.end(),
                                 [](const SongChordEvent& a, const SongChordEvent& b) { return a.seconds < b.seconds; });

                const double songEnd = song.playbackEvents.empty() ? 0.0 : song.playbackEvents.back().seconds;

                for (std::size_t i = 0; i < chords.size(); ++i)
                    chords[i].endSeconds = i + 1 < chords.size()
                        ? chords[i + 1].seconds
                        : std::max(songEnd, chords[i].seconds + 4.0);

                if (!chords.empty())
                    song.chords = std::move(chords);
            }
        }

        // lyrics.json: [ { "time": 10.0, "text": "A line", "words": [ { "time": 10.0, "text": "A" }, ... ] }, ... ]
        if (const auto name = findRootEntry(zip, "lyrics.json"); name.isNotEmpty() && readEntry(zip, name, block))
        {
            const auto root = juce::JSON::parse(block.toString());

            if (const auto* list = listFrom(root, "lyrics"))
            {
                std::vector<LyricEvent> lyrics;
                int lineIndex = 0;

                const auto add = [&](double seconds, const juce::String& text, bool newLine, bool endsLine)
                {
                    LyricEvent event;
                    event.seconds = seconds;
                    event.tick = secondsToTick(song, seconds);
                    event.samplePosition = static_cast<std::int64_t>(std::llround(seconds * song.sampleRate));
                    event.lineIndex = lineIndex;
                    event.startsNewWord = true;
                    event.endsWord = true;
                    event.startsNewLine = newLine;
                    event.endsLine = endsLine;
                    event.text = text.toStdString();
                    lyrics.push_back(std::move(event));
                };

                for (const auto& line : *list)
                {
                    const double time = line.getProperty("time", 0.0);
                    const auto text = line.getProperty("text", {}).toString();

                    if (const auto* words = line.getProperty("words", {}).getArray(); words != nullptr && !words->isEmpty())
                    {
                        for (int w = 0; w < words->size(); ++w)
                            add((*words)[w].getProperty("time", time),
                                (*words)[w].getProperty("text", {}).toString(),
                                w == 0, w + 1 == words->size());
                    }
                    else if (text.isNotEmpty())
                    {
                        add(time, text, true, true);
                    }
                    else
                    {
                        continue;
                    }

                    ++lineIndex;
                }

                if (!lyrics.empty())
                    song.lyrics = std::move(lyrics);
            }
        }
    }

    Contents open(const juce::File& package, double deviceRate, Progress* progress)
    {
        Contents contents;

        const auto cancelled = [progress] { return progress != nullptr && progress->cancel.load(); };

        contents.midiFile = midiFor(package, contents.error);

        if (contents.error.isNotEmpty())
            return contents;

        auto zipShared = std::make_shared<juce::ZipFile>(package);
        auto& zip = *zipShared;
        Manifest manifest;

        if (!parseManifest(zip, manifest, contents.error))
            return contents;

        contents.tempoKnown = manifest.tempoGiven || findMidiEntry(zip, manifest).isNotEmpty();

        if (manifest.stems.empty())
            return contents;

        const auto count = manifest.stems.size();

        if (progress != nullptr)
        {
            progress->stemsTotal = static_cast<int>(count);
            progress->stemsDone = 0;
            progress->fraction = 0.02f;
        }

        std::vector<std::atomic<float>> stemProgress(count);

        for (auto& value : stemProgress)
            value = 0.0f;

        const auto publish = [&]
        {
            if (progress == nullptr)
                return;

            float sum = 0.0f;

            for (const auto& value : stemProgress)
                sum += value.load();

            progress->fraction = std::max(progress->fraction.load(), 0.02f + 0.98f * sum / static_cast<float>(count));
        };

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        // 1. Per stem: staat hij zonder compressie in de zip (en is het wav/flac/mp3), dan
        //    streamen we hem van schijf; anders lezen we hem helemaal in en decoderen we hem.
        std::vector<std::shared_ptr<StemStreamer>> streamers(count);
        std::vector<juce::MemoryBlock> blocks(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            if (cancelled())
            {
                contents.cancelled = true;
                return contents;
            }

            const int index = entryIndex(zip, manifest.stems[i].file);

            if (index < 0)
            {
                contents.error = "Stem not found in the .merik file: " + manifest.stems[i].file;
                return contents;
            }

            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));

            if (stream != nullptr && stream->getTotalLength() == zip.getEntry(index)->uncompressedSize)
            {
                // Opgeslagen zonder compressie: een buffer maakt de vele kleine leesacties van de decoder snel.
                auto buffered = std::make_unique<juce::BufferedInputStream>(stream.release(), 1 << 17, true);

                if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(buffered)));
                    reader != nullptr && reader->lengthInSamples > 0 && reader->sampleRate > 0.0)
                {
                    streamers[i] = std::make_shared<StemStreamer>(zipShared, std::move(reader), deviceRate);
                    stemProgress[i] = 0.1f;
                    publish();
                    continue;
                }
            }

            stream.reset();

            if (!readEntry(zip, manifest.stems[i].file, blocks[i]))
            {
                contents.error = "Stem not found in the .merik file: " + manifest.stems[i].file;
                return contents;
            }

            stemProgress[i] = 0.1f;
            publish();
        }

        // 2. Het begin van de streams laden en de rest decoderen, parallel.
        auto set = std::make_shared<StemSet>();
        set->tracks.resize(count);

        const auto offset = static_cast<std::int64_t>(std::llround(manifest.audioOffsetSeconds * deviceRate));
        std::vector<juce::String> errors(count);
        std::atomic<std::size_t> remaining { count };

        {
            juce::ThreadPool pool(juce::jlimit(2, 8, juce::SystemStats::getNumCpus()));

            for (std::size_t i = 0; i < count; ++i)
            {
                pool.addJob([&, i]
                {
                    const auto finish = [&]
                    {
                        stemProgress[i] = 1.0f;

                        if (progress != nullptr)
                            ++progress->stemsDone;

                        publish();
                        --remaining;
                    };

                    if (cancelled())
                    {
                        --remaining;
                        return;
                    }

                    const auto& info = manifest.stems[i];
                    auto& track = set->tracks[i];
                    track.family = info.family;
                    track.gain = info.gain;
                    track.offset = offset;

                    if (streamers[i] != nullptr)
                    {
                        streamers[i]->preloadHead(progress != nullptr ? &progress->cancel : nullptr,
                                                  [&](float p) { stemProgress[i] = p; publish(); });
                        track.streamer = streamers[i];
                        finish();
                        return;
                    }

                    auto reader = makeReader(formats, blocks[i]);

                    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
                    {
                        errors[i] = "Cannot read the stem " + info.file + " (use wav, flac or mp3).";
                        finish();
                        return;
                    }

                    const auto length = static_cast<int>(reader->lengthInSamples);
                    juce::AudioBuffer<float> buffer(2, length);

                    constexpr int chunk = 1 << 18;

                    for (int position = 0; position < length; position += chunk)
                    {
                        if (cancelled())
                        {
                            --remaining;
                            return;
                        }

                        const int n = std::min(chunk, length - position);
                        reader->read(&buffer, position, n, position, true, true);

                        stemProgress[i] = 0.1f + 0.7f * static_cast<float>(position + n) / static_cast<float>(length);
                        publish();
                    }

                    if (reader->numChannels < 2)
                        buffer.copyFrom(1, 0, buffer, 0, 0, length);

                    if (std::abs(reader->sampleRate - deviceRate) > 0.5)
                    {
                        const double ratio = reader->sampleRate / deviceRate;
                        const auto outLength = static_cast<int>(std::llround(static_cast<double>(length) / ratio));

                        track.left.resize(static_cast<std::size_t>(outLength));
                        track.right.resize(static_cast<std::size_t>(outLength));

                        juce::LagrangeInterpolator leftResampler, rightResampler;
                        leftResampler.process(ratio, buffer.getReadPointer(0), track.left.data(), outLength);
                        rightResampler.process(ratio, buffer.getReadPointer(1), track.right.data(), outLength);
                    }
                    else
                    {
                        track.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + length);
                        track.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + length);
                    }

                    blocks[i] = juce::MemoryBlock();   // het gecomprimeerde origineel is niet meer nodig
                    finish();
                });
            }

            while (remaining.load() > 0)
                juce::Thread::sleep(10);
        }

        if (cancelled())
        {
            contents.cancelled = true;
            return contents;
        }

        for (const auto& error : errors)
            if (error.isNotEmpty())
            {
                contents.error = error;
                return contents;
            }

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& info = manifest.stems[i];
            const auto& track = set->tracks[i];

            if (set->names[static_cast<std::size_t>(info.family)].empty() && info.label.isNotEmpty())
                set->names[static_cast<std::size_t>(info.family)] = info.label.toStdString();

            const auto length = track.streamer != nullptr ? track.streamer->lengthSamples()
                                                          : static_cast<std::int64_t>(track.left.size());

            set->lengthSamples = std::max(set->lengthSamples, offset + length);
        }

        // Pas nu de achtergrondthreads starten die de rest van elke stem vooruit laden.
        for (auto& track : set->tracks)
            if (track.streamer != nullptr)
                track.streamer->startStreaming();

        if (progress != nullptr)
            progress->fraction = 1.0f;

        contents.stems = std::move(set);
        return contents;
    }
}
