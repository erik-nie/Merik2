#include "MidiOutSink.h"

#include <algorithm>
#include <atomic>

#if JUCE_MAC
 #include <CoreMIDI/CoreMIDI.h>
#endif

#if JUCE_MAC
struct MidiOutSink::Native
{
    MIDIClientRef client = 0;
    MIDIPortRef port = 0;
    MIDIEndpointRef destination = 0;

    // Berichten die "meteen" moeten krijgen toch elk een eigen, oplopend
    // tijdstempel (nu, of 1 µs na de vorige). Met tijdstempel 0 voegt
    // CoreMIDI een salvo (bijv. all-notes-off op 16 kanalen) samen tot één
    // pakket van tientallen berichten.
    std::atomic<std::uint64_t> lastImmediate { 0 };

    MIDITimeStamp immediateStamp()
    {
        const auto now = static_cast<std::uint64_t>(juce::Time::getHighResolutionTicks());
        const auto step = static_cast<std::uint64_t>(
            std::max<juce::int64>(1, juce::Time::secondsToHighResolutionTicks(0.000001)));

        auto last = lastImmediate.load();

        for (;;)
        {
            const auto next = std::max(now, last + step);

            if (lastImmediate.compare_exchange_weak(last, next))
                return next;
        }
    }

    ~Native()
    {
        if (port != 0)
            MIDIPortDispose(port);

        if (client != 0)
            MIDIClientDispose(client);
    }

    // De JUCE-identifier is de CoreMIDI unique ID van het apparaat of eindpunt.
    bool open(const juce::String& identifier)
    {
        MIDIObjectRef object = 0;
        MIDIObjectType type = kMIDIObjectType_Other;

        if (MIDIObjectFindByUniqueID(static_cast<MIDIUniqueID>(identifier.getIntValue()), &object, &type) != noErr
            || object == 0)
        {
            return false;
        }

        if (type == kMIDIObjectType_Destination || type == kMIDIObjectType_ExternalDestination)
        {
            destination = static_cast<MIDIEndpointRef>(object);
        }
        else if (type == kMIDIObjectType_Entity || type == kMIDIObjectType_ExternalEntity)
        {
            if (MIDIEntityGetNumberOfDestinations(static_cast<MIDIEntityRef>(object)) > 0)
                destination = MIDIEntityGetDestination(static_cast<MIDIEntityRef>(object), 0);
        }
        else if (type == kMIDIObjectType_Device || type == kMIDIObjectType_ExternalDevice)
        {
            const auto device = static_cast<MIDIDeviceRef>(object);

            for (ItemCount e = 0; e < MIDIDeviceGetNumberOfEntities(device) && destination == 0; ++e)
            {
                const auto entity = MIDIDeviceGetEntity(device, e);

                if (MIDIEntityGetNumberOfDestinations(entity) > 0)
                    destination = MIDIEntityGetDestination(entity, 0);
            }
        }

        if (destination == 0)
            return false;

        if (MIDIClientCreate(CFSTR("Merik"), nullptr, nullptr, &client) != noErr)
            return false;

        return MIDIOutputPortCreate(client, CFSTR("Merik out"), &port) == noErr;
    }
};
#else
struct MidiOutSink::Native {};
#endif

MidiOutSink::MidiOutSink()
    : juce::Thread("Merik MIDI out")
{
}

MidiOutSink::~MidiOutSink()
{
    close();
}

bool MidiOutSink::open(const juce::String& deviceIdentifier)
{
    close();

#if JUCE_MAC
    {
        auto candidate = std::make_unique<Native>();

        if (candidate->open(deviceIdentifier))
            native = std::move(candidate);
    }
#endif

    if (native == nullptr)
    {
        juceOutput = juce::MidiOutput::openDevice(deviceIdentifier);

        if (juceOutput == nullptr)
            return false;

        juceOutput->startBackgroundThread();
    }

    fifo.reset();
    haveBase = false;
    opened = true;
    startThread(juce::Thread::Priority::high);

    return true;
}

void MidiOutSink::close()
{
    if (opened.load())
        allNotesOff(-1);

    opened = false;

    signalThreadShouldExit();
    wake.signal();
    stopThread(1000);

    if (juceOutput != nullptr)
    {
        juceOutput->stopBackgroundThread();
        juceOutput.reset();
    }

    native.reset();
}

// --- audio-thread ----------------------------------------------------------

void MidiOutSink::beginBlock(std::int64_t sampleAtBlockStart, int numSamples, double rate)
{
    if (!opened.load() || rate <= 0.0)
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    sampleRate = rate;

    // Kloktijd waarop sample 0 zou liggen als deze callback precies op tijd was.
    const double candidate = nowMs - 1000.0 * static_cast<double>(sampleAtBlockStart) / rate;

    const bool discontinuity =
        !haveBase
        || sampleAtBlockStart != expectedNextSample    // seek / herstart
        || nowMs - lastBlockMs > 100.0;                // pauze

    if (discontinuity)
    {
        baseMs = candidate;
    }
    else
    {
        // Callbacks komen nooit te vroeg, alleen te laat: de vroegste
        // schatting is de beste. Een heel langzame opwaartse correctie
        // vangt het verschil tussen audioklok en systeemklok op.
        baseMs = std::min(baseMs + 0.001, candidate);
    }

    haveBase = true;
    expectedNextSample = sampleAtBlockStart + numSamples;
    lastBlockMs = nowMs;
}

void MidiOutSink::enqueue(const std::uint8_t* data, int size, double timeMs)
{
    if (!opened.load() || size < 1 || size > 8)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 < 1)
        return;   // wachtrij vol: liever een bericht missen dan de audio blokkeren

    auto& slot = queue[static_cast<std::size_t>(start1)];
    slot.timeMs = timeMs;
    slot.size = size;

    for (int i = 0; i < size; ++i)
        slot.data[static_cast<std::size_t>(i)] = data[i];

    fifo.finishedWrite(1);
    wake.signal();
}

void MidiOutSink::push(const std::uint8_t* data, int size, std::int64_t sampleTime)
{
    const double timeMs = haveBase
        ? baseMs + 1000.0 * static_cast<double>(sampleTime) / sampleRate + leadMs
        : juce::Time::getMillisecondCounterHiRes() + leadMs;

    enqueue(data, size, timeMs);
}

void MidiOutSink::pushNow(const std::uint8_t* data, int size)
{
    enqueue(data, size, 0.0);
}

// --- afleveren ---------------------------------------------------------------

void MidiOutSink::sendNowDirect(const std::uint8_t* data, int size)
{
#if JUCE_MAC
    if (native != nullptr)
    {
        Byte buffer[64];
        auto* list = reinterpret_cast<MIDIPacketList*>(buffer);
        auto* packet = MIDIPacketListInit(list);
        MIDIPacketListAdd(list, sizeof(buffer), packet, native->immediateStamp(),
                          static_cast<ByteCount>(size), data);
        MIDISend(native->port, native->destination, list);
        return;
    }
#endif

    if (juceOutput != nullptr)
        juceOutput->sendMessageNow(juce::MidiMessage(data, size));
}

void MidiOutSink::allNotesOff(int channel)
{
    if (!opened.load())
        return;

    const int first = channel < 0 ? 0 : channel;
    const int last = channel < 0 ? 15 : channel;

    for (int ch = first; ch <= last; ++ch)
    {
        const std::uint8_t status = static_cast<std::uint8_t>(0xB0 | ch);

        const std::uint8_t sustainOff[3] = { status, 64, 0 };
        const std::uint8_t notesOff[3]   = { status, 123, 0 };
        const std::uint8_t soundOff[3]   = { status, 120, 0 };

        sendNowDirect(sustainOff, 3);
        sendNowDirect(notesOff, 3);
        sendNowDirect(soundOff, 3);
    }
}

void MidiOutSink::run()
{
#if JUCE_MAC
    const auto toHostTicks = [](double ms)
    {
        return static_cast<MIDITimeStamp>(juce::Time::secondsToHighResolutionTicks(ms * 0.001));
    };
#endif

    juce::MidiBuffer buffer;

#if JUCE_MAC
    MIDITimeStamp lastScheduledStamp = 0;
#endif

    while (!threadShouldExit())
    {
        wake.wait(100);

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);

#if JUCE_MAC
        if (native != nullptr)
        {
            // Elk bericht gaat als eigen pakket weg, met een eigen (oplopend)
            // tijdstempel. CoreMIDI voegt berichten met hetzelfde tijdstempel
            // anders samen tot één pakket met meerdere berichten (bijv. een
            // akkoord of een nootwissel); lang niet elke ontvanger of
            // driver leest zo'n pakket helemaal uit, waardoor noten wegvallen,
            // vooral in drukke passages.
            const MIDITimeStamp oneMicrosecond =
                std::max<MIDITimeStamp>(1, toHostTicks(0.001) - toHostTicks(0.0));

            const auto deliver = [&](int start, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    const auto& message = queue[static_cast<std::size_t>(start + i)];

                    Byte listBuffer[32];
                    auto* list = reinterpret_cast<MIDIPacketList*>(listBuffer);
                    auto* packet = MIDIPacketListInit(list);

                    MIDITimeStamp stamp = 0;

                    if (message.timeMs > 0.0)
                    {
                        stamp = toHostTicks(message.timeMs);

                        if (lastScheduledStamp != 0 && stamp <= lastScheduledStamp)
                            stamp = lastScheduledStamp + oneMicrosecond;

                        lastScheduledStamp = stamp;
                    }
                    else
                    {
                        stamp = native->immediateStamp();
                    }

                    MIDIPacketListAdd(list, sizeof(listBuffer), packet, stamp,
                                      static_cast<ByteCount>(message.size), message.data.data());

                    MIDISend(native->port, native->destination, list);
                }
            };

            deliver(start1, size1);
            deliver(start2, size2);

            fifo.finishedRead(size1 + size2);
            continue;
        }
#endif

        const auto deliverJuce = [&](int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& message = queue[static_cast<std::size_t>(start + i)];

                if (juceOutput == nullptr)
                    continue;

                if (message.timeMs <= 0.0)
                {
                    juceOutput->sendMessageNow(juce::MidiMessage(message.data.data(), message.size));
                    continue;
                }

                buffer.clear();
                buffer.addEvent(juce::MidiMessage(message.data.data(), message.size), 0);
                juceOutput->sendBlockOfMessages(buffer, message.timeMs, 1000.0);
            }
        };

        deliverJuce(start1, size1);
        deliverJuce(start2, size2);

        fifo.finishedRead(size1 + size2);
    }
}
