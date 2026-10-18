#include "RenderJob.h"

#include "MerikBuilder.h"
#include "MidiFileReader.h"
#include "RenderPlan.h"

#include <algorithm>
#include <cmath>

namespace
{
    const char* const names[7] = { "drums", "bass", "guitars", "keys", "strings", "winds", "others" };

    juce::String stemFileName(int family)
    {
        return juce::String(family + 1) + "-" + names[family] + ".wav";
    }

    bool writeWav(const juce::File& file, const std::vector<std::vector<float>>& data,
                  std::int64_t start, std::int64_t length, double rate, juce::String& error)
    {
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
        {
            error = "Cannot write " + file.getFullPathName();
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer = wav.createWriterFor(
            stream, juce::AudioFormatWriterOptions {}
                        .withSampleRate(rate)
                        .withNumChannels(static_cast<int>(data.size()))
                        .withBitsPerSample(24));

        if (writer == nullptr)
        {
            error = "Cannot create the WAV file " + file.getFileName();
            return false;
        }

        constexpr std::int64_t chunk = 1 << 16;

        for (std::int64_t position = 0; position < length; position += chunk)
        {
            const auto n = static_cast<int>(std::min<std::int64_t>(chunk, length - position));

            std::vector<const float*> pointers;

            for (const auto& channel : data)
                pointers.push_back(channel.data() + start + position);

            if (!writer->writeFromFloatArrays(pointers.data(), static_cast<int>(pointers.size()), n))
            {
                error = "Writing " + file.getFileName() + " failed (disk full?)";
                return false;
            }
        }

        return true;
    }
}

const char* RenderJob::familyName(int family)
{
    return names[std::clamp(family, 0, 6)];
}

juce::String RenderJob::outputBaseName(const juce::File& midiFile, bool demo)
{
    return midiFile.getFileNameWithoutExtension() + (demo ? " (demo 30s)" : "");
}

RenderEstimate RenderJob::estimate(const std::vector<juce::File>& files, bool demo)
{
    RenderEstimate result;

    for (const auto& file : files)
    {
        try
        {
            MidiFileReader reader;
            const auto song = reader.read(file.getFullPathName().toStdString(), 48000.0);
            auto plan = buildRenderPlan(song);

            if (demo)
                plan.limitTo(demoSeconds);

            int passes = 0;

            for (int family = 0; family < RenderPlan::passCount; ++family)
                if (plan.hasNotes(family))
                    ++passes;

            if (passes == 0)
            {
                result.notes.add(file.getFileName() + " has no notes and is skipped.");
                continue;
            }

            ++result.songs;
            result.passes += passes;
            result.seconds += passes * (prerollSeconds + plan.lengthSeconds + tailSeconds + 1.0);
        }
        catch (const std::exception&)
        {
            result.notes.add(file.getFileName() + " cannot be read.");
        }
    }

    return result;
}

RenderJob::RenderJob(RenderEngine& renderEngine, RenderOptions renderOptions)
    : juce::Thread("Merik render"),
      engine(renderEngine),
      options(std::move(renderOptions))
{
    current.running = true;
    startThread(juce::Thread::Priority::high);
}

RenderJob::~RenderJob()
{
    stopRequested = true;
    stopThread(30000);
}

RenderStatus RenderJob::status() const
{
    const juce::ScopedLock scoped(lock);
    return current;
}

void RenderJob::update(const std::function<void(RenderStatus&)>& change)
{
    const juce::ScopedLock scoped(lock);
    change(current);
}

void RenderJob::note(const juce::String& line)
{
    update([&](RenderStatus& status) { status.log.add(line); });
}

void RenderJob::run()
{
    // Eerst alles inlezen: aantallen en totale tijd voor de voortgang.
    struct Item { juce::File file; Song song; RenderPlan plan; };
    std::vector<Item> items;
    double total = 0.0;
    int totalPasses = 0;

    for (const auto& file : options.midiFiles)
    {
        try
        {
            MidiFileReader reader;
            Item item;
            item.file = file;
            item.song = reader.read(file.getFullPathName().toStdString(), 48000.0);
            item.plan = buildRenderPlan(item.song);

            if (options.demo)
                item.plan.limitTo(demoSeconds);

            int passes = 0;

            for (int family = 0; family < RenderPlan::passCount; ++family)
                if (item.plan.hasNotes(family))
                {
                    ++passes;
                    total += prerollSeconds + item.plan.lengthSeconds + tailSeconds + 1.0;
                }

            if (passes == 0)
            {
                note("Skipped " + file.getFileName() + ": no notes.");
                continue;
            }

            totalPasses += passes;
            items.push_back(std::move(item));
        }
        catch (const std::exception&)
        {
            note("Skipped " + file.getFileName() + ": cannot be read.");
        }
    }

    update([&](RenderStatus& status)
    {
        status.songs = static_cast<int>(items.size());
        status.totalSeconds = total;
    });

    int created = 0;

    for (std::size_t index = 0; index < items.size() && !stopRequested.load(); ++index)
    {
        auto& item = items[index];
        const auto name = outputBaseName(item.file, options.demo);

        int passCount = 0;

        for (int family = 0; family < RenderPlan::passCount; ++family)
            if (item.plan.hasNotes(family))
                ++passCount;

        update([&](RenderStatus& status)
        {
            status.song = static_cast<int>(index) + 1;
            status.songName = name;
            status.passes = passCount;
            status.pass = 0;
        });

        // De map met de WAV's: naast de .merik bewaard, of tijdelijk.
        const auto temporary = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("Merik2-render-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
        const auto folder = options.keepWav ? options.outputFolder.getChildFile(name + "_wav") : temporary;

        folder.createDirectory();

        // Een eerdere keer (met toestemming overschreven): de oude bestanden eerst weg.
        for (int family = 0; family < RenderPlan::passCount; ++family)
            folder.getChildFile(stemFileName(family)).deleteFile();

        folder.getChildFile("delay.json").deleteFile();
        folder.getChildFile("song.mid").deleteFile();
        item.file.copyFileTo(folder.getChildFile("song.mid"));

        bool ok = true;
        int passNumber = 0;

        for (int family = 0; family < RenderPlan::passCount && ok && !stopRequested.load(); ++family)
        {
            if (!item.plan.hasNotes(family))
                continue;

            ++passNumber;

            update([&](RenderStatus& status)
            {
                status.pass = passNumber;
                status.passName = juce::String(family + 1) + " " + names[family];
                status.step = "Recording";
            });

            juce::String problem;

            if (!renderPass(item.plan, family, folder.getChildFile(stemFileName(family)), problem))
            {
                ok = false;

                if (!stopRequested.load())
                {
                    update([&](RenderStatus& status) { status.error = problem; });
                    note("Stopped on " + name + ": " + problem);
                }
            }
        }

        if (!ok || stopRequested.load())
        {
            // Een half nummer bewaren we niet.
            folder.deleteRecursively();
            break;
        }

        // De vertraging voor het afspelen: de opname loopt zoveel ms achter op de MIDI.
        if (options.delayMs != 0.0)
        {
            folder.getChildFile("delay.json").replaceWithText(
                "{\n  \"audioDelayMs\": " + juce::String(options.delayMs, 2) + "\n}\n");
        }

        // Het .merik maken (zonder compressie).
        update([&](RenderStatus& status) { status.step = "Making the .merik"; });

        const auto output = options.outputFolder.getChildFile(name + ".merik");
        const auto plan = MerikBuilder::scan(folder);
        const auto result = MerikBuilder::build(plan, options.toFlac, output,
                                                [this](float, const juce::String&) { return !stopRequested.load(); });

        if (!options.keepWav)
            folder.deleteRecursively();

        if (!result.ok)
        {
            note("Could not make " + name + ".merik: " + (result.cancelled ? juce::String("stopped") : result.error));
            break;
        }

        ++created;
        note("Created " + output.getFileName() + " (" + juce::String(static_cast<double>(result.bytes) / (1024.0 * 1024.0), 1) + " MB)");
    }

    update([&](RenderStatus& status)
    {
        status.running = false;
        status.finished = true;
        status.stopped = stopRequested.load();
        status.step = stopRequested.load() ? "Stopped" : "Done";
        status.doneSeconds = status.stopped ? status.doneSeconds : status.totalSeconds;
    });

    juce::ignoreUnused(created, totalPasses);
}

bool RenderJob::renderPass(const RenderPlan& plan, int family, const juce::File& wavFile, juce::String& problem)
{
    const double rate = engine.sampleRate();
    const auto toSamples = [rate](double seconds) { return static_cast<std::int64_t>(std::llround(seconds * rate)); };

    const auto delaySamples = toSamples(options.delayMs * 0.001);
    const auto start = toSamples(prerollSeconds);
    const auto songSamples = toSamples(plan.lengthSeconds);
    const auto contentLength = songSamples + toSamples(tailSeconds) + std::max<std::int64_t>(0, delaySamples);
    const auto total = start + contentLength;

    auto& session = engine.session();
    session.prepare(rate, engine.channels(), total + 8192);

    // Het rooster van berichten op de tijdlijn van de opname.
    std::vector<CaptureSession::Message> schedule;

    const auto add = [&](std::int64_t sample, const std::uint8_t* bytes, int size)
    {
        CaptureSession::Message message;
        message.sample = std::max<std::int64_t>(0, sample);
        message.size = size;

        for (int i = 0; i < size; ++i)
            message.data[static_cast<std::size_t>(i)] = bytes[i];

        schedule.push_back(message);
    };

    if (options.gmReset)
    {
        static const std::uint8_t gmReset[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
        add(toSamples(0.05), gmReset, 6);
    }
    else
    {
        // Zonder reset: in elk geval alles stilzetten en de regelaars terug.
        for (int channel = 0; channel < 16; ++channel)
        {
            const std::uint8_t off[3] = { static_cast<std::uint8_t>(0xB0 | channel), 123, 0 };
            const std::uint8_t reset[3] = { static_cast<std::uint8_t>(0xB0 | channel), 121, 0 };
            add(toSamples(0.05) + channel * 4, off, 3);
            add(toSamples(0.06) + channel * 4, reset, 3);
        }
    }

    // Berichten voor de eerste noot (programma's, volumes): tijdens de pre-roll,
    // zodat de module klaar is als de eerste noot komt.
    double firstNote = plan.lengthSeconds + 1.0;

    for (const auto& message : plan.passes[static_cast<std::size_t>(family)])
        if ((message.data[0] & 0xF0) == 0x90 && message.size >= 3 && message.data[2] > 0)
        {
            firstNote = message.seconds;
            break;
        }

    // Programma's, volumes en dergelijke voor de eerste noot komen tijdens de pre-roll,
    // met ruimte ertussen (hoogstens 2 ms, bij veel berichten dichter op elkaar).
    int setupCount = 0;

    for (const auto& message : plan.passes[static_cast<std::size_t>(family)])
        if (message.seconds < firstNote && (message.data[0] & 0xF0) != 0x90 && (message.data[0] & 0xF0) != 0x80)
            ++setupCount;

    const auto spacing = setupCount > 0
        ? std::min<std::int64_t>(toSamples(0.002), std::max<std::int64_t>(1, toSamples(0.25) / setupCount))
        : toSamples(0.002);
    int setup = 0;

    for (const auto& message : plan.passes[static_cast<std::size_t>(family)])
    {
        if (message.seconds < firstNote && (message.data[0] & 0xF0) != 0x90 && (message.data[0] & 0xF0) != 0x80)
            add(toSamples(0.25) + setup++ * spacing, message.data.data(), message.size);
        else
            add(start + toSamples(message.seconds), message.data.data(), message.size);
    }

    // Aan het eind alles stilzetten.
    for (int channel = 0; channel < 16; ++channel)
    {
        const std::uint8_t off[3] = { static_cast<std::uint8_t>(0xB0 | channel), 123, 0 };
        add(start + songSamples + toSamples(0.3) + channel * 4, off, 3);
    }

    std::stable_sort(schedule.begin(), schedule.end(),
                     [](const CaptureSession::Message& a, const CaptureSession::Message& b) { return a.sample < b.sample; });

    session.arm(std::move(schedule), total);

    while (!session.isFinished())
    {
        if (stopRequested.load())
        {
            session.reset();
            engine.midi().allNotesOff(-1);
            return false;
        }

        update([&](RenderStatus& status)
        {
            status.doneSeconds = completedSeconds + static_cast<double>(session.position()) / rate;
        });

        juce::Thread::sleep(100);
    }

    completedSeconds += static_cast<double>(total) / rate + 1.0;
    engine.midi().allNotesOff(-1);

    // Opslaan: vanaf het tijdstip van de eerste (MIDI-)maat, zonder pre-roll.
    const auto& data = session.data();
    float peak = 0.0f;

    for (const auto& channel : data)
        for (std::int64_t i = start; i < start + contentLength; ++i)
            peak = std::max(peak, std::fabs(channel[static_cast<std::size_t>(i)]));

    if (!writeWav(wavFile, data, start, contentLength, rate, problem))
        return false;

    const auto label = juce::String(family + 1) + " " + names[family];

    if (peak < 0.002f)
        note("Warning: " + label + " is silent. Check the cables, the input and the level.");
    else if (peak >= 0.999f)
        note("Warning: " + label + " clipped (peak at 0 dBFS). Lower the level and render again.");

    // Even rust: de module moet uitgeklonken zijn voor de volgende pass.
    juce::Thread::sleep(700);
    return true;
}
