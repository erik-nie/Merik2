#include "MerikPackage.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int familyCountInStems = 7;   // 1..7; familie 8 is de klik

    struct StemInfo
    {
        int family = 0;       // 0-based
        juce::String file;
        float gain = 1.0f;
    };

    struct Manifest
    {
        double bpm = 120.0;
        int numerator = 4;
        int denominator = 4;
        double durationSeconds = 0.0;
        double audioOffsetSeconds = 0.0;
        juce::String midiName;
        std::vector<StemInfo> stems;
    };

    bool safeName(const juce::String& name)
    {
        return name.isNotEmpty() && !name.contains("..") && !name.startsWithChar('/')
            && !name.startsWithChar('\\') && !name.containsChar(':');
    }

    bool readEntry(juce::ZipFile& zip, const juce::String& name, juce::MemoryBlock& block)
    {
        if (!safeName(name))
            return false;

        const int index = zip.getIndexOfFileName(name);

        if (index < 0)
            return false;

        std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));

        if (stream == nullptr)
            return false;

        block.reset();
        stream->readIntoMemoryBlock(block);
        return block.getSize() > 0;
    }

    bool parseManifest(juce::ZipFile& zip, Manifest& manifest, juce::String& error)
    {
        juce::MemoryBlock block;

        if (!readEntry(zip, "manifest.json", block))
        {
            error = "manifest.json is missing in this .merik file.";
            return false;
        }

        const auto root = juce::JSON::parse(block.toString());

        if (!root.isObject())
        {
            error = "manifest.json is not valid JSON.";
            return false;
        }

        manifest.bpm = std::max(20.0, static_cast<double>(root.getProperty("bpm", 120.0)));
        manifest.durationSeconds = root.getProperty("durationSeconds", 0.0);

        if (const auto* signature = root.getProperty("timeSignature", {}).getArray())
            if (signature->size() >= 2)
            {
                manifest.numerator = juce::jlimit(1, 32, static_cast<int>((*signature)[0]));
                manifest.denominator = juce::jlimit(1, 32, static_cast<int>((*signature)[1]));
            }

        const auto timing = root.getProperty("timing", {});
        manifest.audioOffsetSeconds = timing.getProperty("audioOffsetSeconds", 0.0);
        manifest.midiName = timing.getProperty("midi", "song.mid").toString();

        if (const auto* stems = root.getProperty("stems", {}).getArray())
        {
            for (const auto& item : *stems)
            {
                StemInfo stem;
                const int family = item.getProperty("family", 0);

                if (family < 1 || family > familyCountInStems)
                {
                    error = "A stem has family " + juce::String(family)
                          + "; use 1 to 7 (8 is the click).";
                    return false;
                }

                stem.family = family - 1;
                stem.file = item.getProperty("file", {}).toString();
                stem.gain = std::pow(10.0f, static_cast<float>(
                    static_cast<double>(item.getProperty("gainDb", 0.0))) / 20.0f);

                if (!safeName(stem.file))
                {
                    error = "Invalid stem file name: " + stem.file;
                    return false;
                }

                manifest.stems.push_back(stem);
            }
        }

        return true;
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

        if (readEntry(zip, manifest.midiName, block))
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
            error = "This .merik file has no song.mid, no durationSeconds and no readable stems.";
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

    Contents open(const juce::File& package, double deviceRate)
    {
        Contents contents;

        contents.midiFile = midiFor(package, contents.error);

        if (contents.error.isNotEmpty())
            return contents;

        juce::ZipFile zip(package);
        Manifest manifest;

        if (!parseManifest(zip, manifest, contents.error))
            return contents;

        if (manifest.stems.empty())
            return contents;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        auto set = std::make_shared<StemSet>();
        const auto offset = static_cast<std::int64_t>(std::llround(manifest.audioOffsetSeconds * deviceRate));

        for (const auto& info : manifest.stems)
        {
            juce::MemoryBlock block;

            if (!readEntry(zip, info.file, block))
            {
                contents.error = "Stem not found in the .merik file: " + info.file;
                return contents;
            }

            auto reader = makeReader(formats, block);

            if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            {
                contents.error = "Cannot read the stem " + info.file + " (use wav, flac or mp3).";
                return contents;
            }

            const auto length = static_cast<int>(reader->lengthInSamples);
            juce::AudioBuffer<float> buffer(2, length);
            reader->read(&buffer, 0, length, 0, true, true);

            if (reader->numChannels < 2)
                buffer.copyFrom(1, 0, buffer, 0, 0, length);

            StemTrack track;
            track.family = info.family;
            track.gain = info.gain;
            track.offset = offset;

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

            set->lengthSamples = std::max(set->lengthSamples,
                                          offset + static_cast<std::int64_t>(track.left.size()));
            set->tracks.push_back(std::move(track));
        }

        contents.stems = std::move(set);
        return contents;
    }
}
