#include "MerikBuilder.h"
#include "MerikPackage.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool hasExtension(const juce::File& file, std::initializer_list<const char*> extensions)
    {
        const auto extension = file.getFileExtension().toLowerCase().trimCharactersAtStart(".");

        for (const auto* candidate : extensions)
            if (extension == candidate)
                return true;

        return false;
    }

    juce::String megabytes(juce::int64 bytes)
    {
        return juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0), 1) + " MB";
    }

    const char* familyName(int family)
    {
        static const char* names[] = { "Drums", "Bass", "Guitars", "Keys", "Strings", "Winds", "Others", "Click" };
        return names[std::clamp(family, 0, 7)];
    }
}

namespace MerikBuilder
{
    Plan scan(const juce::File& folder)
    {
        Plan plan;
        plan.folder = folder;

        // De map zelf en een eventuele submap "stems".
        std::vector<juce::File> files;

        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
            files.push_back(entry.getFile());

        if (const auto stemsFolder = folder.getChildFile("stems"); stemsFolder.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator(stemsFolder, false, "*", juce::File::findFiles))
                files.push_back(entry.getFile());

        std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
                  { return a.getFileName().compareNatural(b.getFileName()) < 0; });

        juce::File midi, tempoJson;
        std::vector<juce::File> midiCandidates;
        std::vector<int> stemFamilies;
        std::vector<double> stemSeconds;
        juce::String firstRateNote;
        double firstRate = 0.0;

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        for (const auto& file : files)
        {
            const auto name = file.getFileName();
            const auto lower = name.toLowerCase();

            if (name.startsWithChar('.') || lower == "thumbs.db" || lower == "desktop.ini")
                continue;

            if (lower == "song.mid")
            {
                midi = file;
                continue;
            }

            if (hasExtension(file, { "mid", "midi", "kar" }))
            {
                midiCandidates.push_back(file);
                continue;
            }

            if (lower == "chords.json" || lower == "lyrics.json" || lower == "tempo.json" || lower == "manifest.json")
            {
                Item item;
                item.file = file;
                item.zipName = lower;
                item.description = lower + (lower == "chords.json" ? "   chords (replace those from the MIDI file)"
                                          : lower == "lyrics.json" ? "   lyrics (replace those from the MIDI file)"
                                          : lower == "tempo.json"  ? "   tempo and time signature"
                                                                   : "   optional settings");
                plan.items.push_back(item);

                if (lower == "tempo.json")
                    plan.hasTempo = true;

                continue;
            }

            const int family = MerikPackage::stemFamily(name);

            if (family >= 0)
            {
                Item item;
                item.file = file;
                item.zipName = name;
                item.isWav = hasExtension(file, { "wav", "aif", "aiff" });

                const auto label = MerikPackage::stemLabel(name);
                item.description = name + "   family " + juce::String(family + 1) + " " + familyName(family)
                                 + (label.isNotEmpty() ? " (shown as \"" + label + "\")" : "");

                // Lengte en samplefrequentie controleren: stems horen even lang te zijn.
                if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file)); reader != nullptr)
                {
                    stemSeconds.push_back(static_cast<double>(reader->lengthInSamples) / reader->sampleRate);

                    if (firstRate == 0.0)
                        firstRate = reader->sampleRate;
                    else if (std::abs(reader->sampleRate - firstRate) > 0.5)
                        plan.warnings.add(name + " has another sample rate (" + juce::String(reader->sampleRate, 0)
                                          + " Hz instead of " + juce::String(firstRate, 0) + " Hz). Merik converts it, but check the length.");
                }
                else
                {
                    plan.warnings.add(name + " cannot be read as audio.");
                }

                stemFamilies.push_back(family);
                plan.items.push_back(item);
                ++plan.stemCount;
                continue;
            }

            plan.ignored.add(name);
        }

        // MIDI: song.mid, anders het enige .mid-bestand.
        if (midi == juce::File() && !midiCandidates.empty())
        {
            midi = midiCandidates.front();

            if (midiCandidates.size() > 1)
                plan.warnings.add("There are " + juce::String(midiCandidates.size()) + " MIDI files; "
                                  + midi.getFileName() + " is used. Name the one you want song.mid.");
        }
        else
        {
            for (const auto& candidate : midiCandidates)
                plan.ignored.add(candidate.getFileName());
        }

        if (midi != juce::File())
        {
            Item item;
            item.file = midi;
            item.zipName = "song.mid";
            item.description = midi.getFileName() + (midi.getFileName().equalsIgnoreCase("song.mid") ? "" : "   saved as song.mid")
                             + "   tempo, lyrics, chords, MIDI parts";
            plan.items.insert(plan.items.begin(), item);
            plan.hasMidi = true;
        }

        // Dingen om op te letten.
        if (plan.stemCount == 0)
            plan.warnings.add("No audio stems found. Name them like 1-drums.wav, 2-bass.wav, ... (number 1-8 or a family name).");

        if (!plan.hasMidi && !plan.hasTempo && plan.stemCount > 0)
            plan.warnings.add("No MIDI file and no tempo.json: Merik will not play its own click and shows no beat display "
                              "(a click stem named 8-click works).");

        if (stemSeconds.size() > 1)
        {
            const auto [shortest, longest] = std::minmax_element(stemSeconds.begin(), stemSeconds.end());

            if (*longest - *shortest > 0.5)
                plan.warnings.add("The stems differ in length (" + juce::String(*shortest, 1) + " s to "
                                  + juce::String(*longest, 1) + " s). They should all start at the same point and have the same length.");
        }

        for (std::size_t i = 0; i < stemFamilies.size(); ++i)
            for (std::size_t j = i + 1; j < stemFamilies.size(); ++j)
                if (stemFamilies[i] == stemFamilies[j] && stemFamilies[i] != 7)
                {
                    plan.warnings.add("More than one stem in family " + juce::String(stemFamilies[i] + 1)
                                      + ": they are mixed together.");
                    i = stemFamilies.size();
                    break;
                }

        return plan;
    }

    namespace
    {
        // WAV/AIFF -> FLAC, zelfde bitdiepte (float wordt 24 bit), met voortgang.
        bool convertToFlac(const juce::File& source, const juce::File& target,
                           const std::function<bool(float)>& progress, juce::String& error)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));

            if (reader == nullptr)
            {
                error = "Cannot read " + source.getFileName();
                return false;
            }

            const int channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 8));
            const bool floatSource = reader->usesFloatingPointData;
            const int bits = floatSource ? 24 : (reader->bitsPerSample > 16 ? 24 : 16);

            std::unique_ptr<juce::OutputStream> stream = target.createOutputStream();

            if (stream == nullptr)
            {
                error = "Cannot write to the temporary folder.";
                return false;
            }

            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::AudioFormatWriter> writer = flac.createWriterFor(
                stream, juce::AudioFormatWriterOptions {}
                            .withSampleRate(reader->sampleRate)
                            .withNumChannels(channels)
                            .withBitsPerSample(bits)
                            .withQualityOptionIndex(5));

            if (writer == nullptr)
            {
                error = "Cannot create a FLAC file for " + source.getFileName();
                return false;
            }

            constexpr int chunk = 1 << 16;
            const auto total = reader->lengthInSamples;
            juce::AudioBuffer<float> floatBuffer(channels, chunk);
            juce::HeapBlock<int> ints(static_cast<size_t>(channels) * chunk);

            for (juce::int64 position = 0; position < total; position += chunk)
            {
                const int n = static_cast<int>(std::min<juce::int64>(chunk, total - position));
                bool ok;

                if (floatSource)
                {
                    reader->read(&floatBuffer, 0, n, position, true, channels > 1);
                    ok = writer->writeFromAudioSampleBuffer(floatBuffer, 0, n);
                }
                else
                {
                    // Gehele getallen (links uitgelijnd in 32 bit): zonder afronding, dus verliesvrij.
                    std::vector<int*> destination(static_cast<size_t>(channels) + 1, nullptr);

                    for (int c = 0; c < channels; ++c)
                        destination[static_cast<size_t>(c)] = ints.get() + static_cast<size_t>(c) * chunk;

                    reader->readSamples(destination.data(), channels, 0, position, n);
                    ok = writer->write(const_cast<const int**>(destination.data()), n);
                }

                if (!ok)
                {
                    error = "Writing FLAC failed for " + source.getFileName();
                    return false;
                }

                if (!progress(static_cast<float>(position + n) / static_cast<float>(std::max<juce::int64>(1, total))))
                    return false;
            }

            writer.reset();   // sluit en schrijft de laatste blokken
            return true;
        }
    }

    Result build(const Plan& plan, bool wavToFlac, const juce::File& output,
                 const std::function<bool(float, const juce::String&)>& progress)
    {
        Result result;

        if (!plan.usable())
        {
            result.error = "Nothing to put in the file.";
            return result;
        }

        const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("Merik2-maker-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
        temp.createDirectory();

        const auto cleanup = [&temp] { temp.deleteRecursively(); };

        // Hoeveel werk: de te converteren wav's tellen mee naar hun grootte.
        juce::int64 convertBytes = 0;

        for (const auto& item : plan.items)
            if (wavToFlac && item.isWav)
                convertBytes += item.file.getSize();

        juce::ZipFile::Builder builder;
        juce::int64 done = 0;
        bool stopped = false;

        for (const auto& item : plan.items)
        {
            if (wavToFlac && item.isWav)
            {
                const auto flacName = item.file.getFileNameWithoutExtension() + ".flac";
                const auto target = temp.getChildFile(flacName);
                const auto size = item.file.getSize();

                juce::String error;

                const bool ok = convertToFlac(item.file, target, [&](float p)
                {
                    const float overall = 0.9f * static_cast<float>(done + static_cast<juce::int64>(p * static_cast<float>(size)))
                                        / static_cast<float>(std::max<juce::int64>(1, convertBytes));

                    if (!progress(overall, "Converting " + item.file.getFileName() + " to FLAC"))
                    {
                        stopped = true;
                        return false;
                    }

                    return true;
                }, error);

                if (!ok)
                {
                    cleanup();
                    result.cancelled = stopped;
                    result.error = error;
                    return result;
                }

                done += size;
                builder.addFile(target, 0, item.zipName.upToLastOccurrenceOf(".", false, false) + ".flac");
            }
            else
            {
                builder.addFile(item.file, 0, item.zipName);   // 0 = opslaan zonder compressie
            }
        }

        if (!progress(0.92f, "Writing the .merik file (no compression)"))
        {
            cleanup();
            result.cancelled = true;
            return result;
        }

        const auto partial = output.getSiblingFile(output.getFileName() + ".part");
        partial.deleteFile();

        {
            juce::FileOutputStream stream(partial);

            if (stream.failedToOpen())
            {
                cleanup();
                result.error = "Cannot write " + output.getFullPathName() + " (" + stream.getStatus().getErrorMessage() + ")";
                return result;
            }

            double zipProgress = 0.0;

            if (!builder.writeToStream(stream, &zipProgress))
            {
                stream.flush();
                partial.deleteFile();
                cleanup();
                result.error = "Writing the zip file failed.";
                return result;
            }

            stream.flush();
        }

        cleanup();

        // De ZIP-indeling van JUCE kent geen "zip64": grotere pakketten zijn onbetrouwbaar.
        if (partial.getSize() >= 0xFFFFFFFFLL)
        {
            partial.deleteFile();
            result.error = "The package would be larger than 4 GB, which the zip format used here cannot hold. "
                           "Use FLAC or MP3, or split the song.";
            return result;
        }

        output.deleteFile();

        if (!partial.moveFileTo(output))
        {
            partial.deleteFile();
            result.error = "Could not save " + output.getFullPathName();
            return result;
        }

        progress(1.0f, "Done");
        result.ok = true;
        result.bytes = output.getSize();
        return result;
    }
}
